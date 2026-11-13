#include "conffiles.h"

#include <cstdio>

#include "textutil.h"

namespace {

enum LineKind { L_SKIP, L_SECTION, L_KEY };

struct Line {
  LineKind kind;
  std::string name;   // seccion, o clave en mayusculas
  std::string value;  // valor de la clave
};

// Valor de una clave: si viene entre comillas se toma lo que hay entre la
// primera y la ultima (asi una clave puede contener comillas, # o ;).
std::string parseValue(const std::string& raw) {
  const std::string t = txt::trim(raw);
  if (t.size() >= 1 && (t[0] == '"' || t[0] == '\'')) {
    const size_t last = t.rfind(t[0]);
    if (last > 0) return t.substr(1, last - 1);
    return t.substr(1);
  }
  return t;
}

Line parseLine(const std::string& rawLine) {
  Line out;
  out.kind = L_SKIP;
  const std::string t = txt::trim(rawLine);
  if (t.empty() || t[0] == '#' || t[0] == ';') return out;
  if (t[0] == '[') {
    const size_t close = t.rfind(']');
    if (close == std::string::npos || close == 0) return out;
    out.kind = L_SECTION;
    out.name = txt::trim(t.substr(1, close - 1));
    return out;
  }
  const size_t eq = t.find('=');
  if (eq == std::string::npos || eq == 0) return out;
  out.kind = L_KEY;
  out.name = txt::trim(t.substr(0, eq));
  for (size_t i = 0; i < out.name.size(); i++) {
    if (out.name[i] >= 'a' && out.name[i] <= 'z') out.name[i] = (char)(out.name[i] - 'a' + 'A');
  }
  out.value = parseValue(t.substr(eq + 1));
  return out;
}

// Recorre el contenido linea por linea (acepta \n, \r\n y BOM inicial).
template <typename Fn>
void forEachLine(const std::string& content, Fn fn) {
  size_t pos = 0;
  if (content.size() >= 3 && (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB &&
      (unsigned char)content[2] == 0xBF) {
    pos = 3;
  }
  while (pos <= content.size()) {
    size_t end = content.find('\n', pos);
    if (end == std::string::npos) end = content.size();
    fn(parseLine(content.substr(pos, end - pos)));
    pos = end + 1;
  }
}

struct WifiCollector {
  std::vector<WifiNet>* nets;
  WifiNet cur;

  void flush() {
    if (!cur.ssid.empty() && nets->size() < kMaxWifiNets) {
      bool dup = false;
      for (size_t i = 0; i < nets->size(); i++) {
        if ((*nets)[i].ssid == cur.ssid) {
          (*nets)[i].pass = cur.pass;  // repetida: vale la ultima clave
          dup = true;
          break;
        }
      }
      if (!dup) nets->push_back(cur);
    }
    cur = WifiNet();
  }

  void operator()(const Line& l) {
    if (l.kind == L_SECTION) {
      flush();
    } else if (l.kind == L_KEY) {
      if (l.name == "SSID") {
        if (!cur.ssid.empty()) flush();
        cur.ssid = l.value;
      } else if (l.name == "PASS" || l.name == "PASSWORD" || l.name == "PSK" || l.name == "CLAVE") {
        cur.pass = l.value;
      }
    }
  }
};

std::string hostOf(const std::string& url) {
  size_t a = url.find("://");
  a = (a == std::string::npos) ? 0 : a + 3;
  size_t b = url.find_first_of("/?#", a);
  if (b == std::string::npos) b = url.size();
  return url.substr(a, b - a);
}

struct FeedCollector {
  std::vector<FeedDef>* feeds;
  std::string* tz;
  FeedDef cur;

  void flush() {
    if (!cur.url.empty() && feeds->size() < kMaxFeeds) {
      if (cur.name.empty()) cur.name = hostOf(cur.url);
      txt::utf8Truncate(cur.name, 60);
      bool dup = false;
      for (size_t i = 0; i < feeds->size(); i++) {
        if ((*feeds)[i].url == cur.url) dup = true;
      }
      if (!dup) feeds->push_back(cur);
    }
    cur = FeedDef();
  }

  void operator()(const Line& l) {
    if (l.kind == L_SECTION) {
      flush();
      cur.name = txt::cleanLine(l.name, 120);
    } else if (l.kind == L_KEY) {
      if (l.name == "TZ") {
        *tz = l.value;
      } else if (l.name == "URL" || l.name == "FEED" || l.name == "LINK") {
        const std::string v = txt::cleanLine(l.value, 500);
        if (txt::startsWithNoCase(v, "http://") || txt::startsWithNoCase(v, "https://")) {
          if (!cur.url.empty()) flush();  // segunda URL sin encabezado propio
          cur.url = v;
        }
      }
    }
  }
};

}  // namespace

void parseWifiConfig(const std::string& content, std::vector<WifiNet>& nets) {
  nets.clear();
  WifiCollector c;
  c.nets = &nets;
  forEachLine(content, [&c](const Line& l) { c(l); });
  c.flush();
}

std::string buildWifiConfig(const std::vector<WifiNet>& nets) {
  std::string out;
  out += "# TenoRSS - redes WiFi conocidas\n";
  out += "#\n";
  out += "# Se puede editar a mano. Un bloque por red:\n";
  out += "#   [wifi 1]\n";
  out += "#   SSID=\"nombre de la red\"\n";
  out += "#   PASS=\"clave\"          (PASS=\"\" si la red es abierta)\n";
  out += "#\n";
  out += "# Las lineas que empiezan con # se ignoran.\n";
  for (size_t i = 0; i < nets.size(); i++) {
    char head[24];
    snprintf(head, sizeof(head), "\n[wifi %u]\n", (unsigned)(i + 1));
    out += head;
    out += "SSID=\"" + nets[i].ssid + "\"\n";
    out += "PASS=\"" + nets[i].pass + "\"\n";
  }
  return out;
}

void parseFeedsConfig(const std::string& content, std::vector<FeedDef>& feeds, std::string& tz) {
  feeds.clear();
  tz.clear();
  FeedCollector c;
  c.feeds = &feeds;
  c.tz = &tz;
  forEachLine(content, [&c](const Line& l) { c(l); });
  c.flush();
}

std::string defaultFeedsConfig() {
  return "# TenoRSS - fuentes RSS\n"
         "#\n"
         "# Se puede editar a mano. Un bloque por fuente:\n"
         "#   [Nombre que se muestra]\n"
         "#   URL=\"https://direccion/del/feed\"\n"
         "#\n"
         "# Zona horaria del reloj:\n"
         "#   TZ=\"auto\"   la detecta por IP al conectarse (consulta ip-api.com)\n"
         "#   TZ=\"-3\"     corrimiento fijo respecto de UTC (ej: \"-3\", \"+1\", \"+5:30\")\n"
         "#\n"
         "# Las lineas que empiezan con # se ignoran.\n"
         "\n"
         "TZ=\"auto\"\n"
         "\n"
         "[Microsoft Admin Center Status]\n"
         "URL=\"https://status.cloud.microsoft/api/feed/mac\"\n"
         "\n"
         "[Hackaday]\n"
         "URL=\"https://hackaday.com/blog/feed/\"\n"
         "\n"
         "[Meshtastic]\n"
         "URL=\"https://meshtastic.org/blog/rss.xml\"\n"
         "\n"
         "[Segu-Info]\n"
         "URL=\"https://blog.segu-info.com.ar/feeds/posts/default\"\n"
         "\n"
         "[Una al Dia]\n"
         "URL=\"https://unaaldia.hispasec.com/feed\"\n"
         "\n"
         "[BBC Mundo]\n"
         "URL=\"https://feeds.bbci.co.uk/mundo/rss.xml\"\n"
         "\n"
         "[Hacker News]\n"
         "URL=\"https://news.ycombinator.com/rss\"\n";
}
