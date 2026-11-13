#include "net.h"

#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <algorithm>

#include "app.h"
#include "core/feedparser.h"
#include "core/textutil.h"
#include "storage.h"

namespace net {

namespace {

// ---------------------------------------------------------------------------
// Estado compartido con la interfaz
// ---------------------------------------------------------------------------

SemaphoreHandle_t g_mux = nullptr;
QueueHandle_t g_queue = nullptr;
TaskHandle_t g_task = nullptr;

Status g_st;
std::vector<WifiNet> g_known;
std::vector<FeedDef> g_feeds;
std::vector<CacheFeed> g_index;
std::vector<FeedResult> g_results;
std::vector<ScanNet> g_scan;

struct Lock {
  Lock() { xSemaphoreTake(g_mux, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mux); }
};

enum CmdType : uint8_t { CMD_BOOT, CMD_UPDATE, CMD_SCAN, CMD_CONNECT };

struct Cmd {
  CmdType type;
  char ssid[33];
  char pass[65];
};

// Solo los toca la tarea de red.
dt::TzSpec g_tz;
bool g_tzDetected = false;   // hay un corrimiento autodetectado (de esta sesion o del cache)
int32_t g_tzOffset = 0;
bool g_tzPending = false;    // falta detectar la zona en esta sesion
uint8_t g_tzTries = 0;
bool g_ntpStarted = false;
bool g_wasUp = false;
bool g_connecting = false;
volatile uint8_t g_discReason = 0;
uint8_t g_buf[1460];

void copyStr(char* dst, size_t cap, const char* src) {
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = 0;
}

int rssiBars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  return 1;
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) g_discReason = info.wifi_sta_disconnected.reason;
}

void applyTz(const std::string& posix) {
  setenv("TZ", posix.c_str(), 1);
  tzset();
}

std::string currentTzString() {
  if (g_tz.kind == dt::TzSpec::POSIX) return g_tz.posix;
  if (g_tz.kind == dt::TzSpec::OFFSET) return dt::posixFromOffset(g_tz.offsetSec);
  return dt::posixFromOffset(g_tzDetected ? g_tzOffset : cfg::kDefaultTzOffset);
}

void publishWifi() {
  const bool up = WiFi.status() == WL_CONNECTED;
  const int rssi = up ? WiFi.RSSI() : 0;
  const String ssid = up ? WiFi.SSID() : String();
  const WifiState ws = up ? W_UP : (g_connecting ? W_CONNECTING : W_DOWN);
  Lock l;
  if (ws != g_st.wifi || strcmp(ssid.c_str(), g_st.ssid) != 0 || rssiBars(rssi) != rssiBars(g_st.rssi)) {
    g_st.wifi = ws;
    copyStr(g_st.ssid, sizeof(g_st.ssid), ssid.c_str());
    g_st.rssi = (int8_t)rssi;
    g_st.seq++;
  }
}

bool tryConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs) {
  logf("[wifi] conectando a \"%s\"", ssid.c_str());
  g_discReason = 0;
  WiFi.disconnect(false, false);
  delay(120);
  if (pass.empty()) WiFi.begin(ssid.c_str());
  else WiFi.begin(ssid.c_str(), pass.c_str());
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      logf("[wifi] conectado, ip %s, rssi %d", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
      return true;
    }
    // red inexistente o clave rechazada: no tiene sentido esperar todo el plazo
    if (millis() - t0 > 3000 && (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED)) break;
    delay(100);
  }
  logf("[wifi] no se pudo conectar a \"%s\" (motivo %u)", ssid.c_str(), (unsigned)g_discReason);
  WiFi.disconnect(false, false);
  delay(100);
  return false;
}

int scanNetworks() {
  int n = WiFi.scanNetworks(false, false, false, 120);
  if (n < 0) {  // suele fallar si habia un intento de conexion en curso
    WiFi.disconnect(false, false);
    delay(200);
    n = WiFi.scanNetworks(false, false, false, 120);
  }
  return n;
}

// Conecta a la red conocida que se vea con mejor senal.
bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  std::vector<WifiNet> known;
  {
    Lock l;
    known = g_known;
  }
  if (known.empty()) {
    logf("[wifi] no hay redes guardadas");
    return false;
  }
  g_connecting = true;
  publishWifi();
  const int n = scanNetworks();
  struct Cand {
    size_t idx;
    int rssi;
  };
  std::vector<Cand> cands;
  for (size_t k = 0; k < known.size(); k++) {
    int best = -1000;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == known[k].ssid.c_str() && WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
    }
    if (best > -1000) {
      Cand c = {k, best};
      cands.push_back(c);
    }
  }
  WiFi.scanDelete();
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.rssi > b.rssi; });
  logf("[wifi] %d redes a la vista, %u conocidas", n, (unsigned)cands.size());
  bool ok = false;
  for (size_t i = 0; i < cands.size() && !ok; i++) {
    ok = tryConnect(known[cands[i].idx].ssid, known[cands[i].idx].pass, cfg::kWifiConnectMs);
  }
  g_connecting = false;
  publishWifi();
  return ok;
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

// Lee el cuerpo de la respuesta con control de tiempo. HTTPClient::writeToStream
// no sirve: si el servidor se queda mudo con la conexion abierta, espera para siempre.
class BodyReader {
 public:
  BodyReader(WiFiClient* c, bool chunked, int contentLen)
      : c_(c), chunked_(chunked), remaining_(contentLen), chunkLeft_(0), needCrlf_(false), done_(false) {}

  // > 0 bytes leidos, 0 fin normal, -1 timeout, -2 conexion cortada
  int read(uint8_t* buf, size_t max) {
    if (!chunked_) {
      if (remaining_ == 0) return 0;
      const size_t want = (remaining_ > 0 && (size_t)remaining_ < max) ? (size_t)remaining_ : max;
      const int n = readRaw(buf, want);
      if (n > 0 && remaining_ > 0) remaining_ -= n;
      if (n == 0 && remaining_ > 0) return -2;  // faltaban datos
      return n;
    }
    if (done_) return 0;
    char line[40];
    if (needCrlf_) {
      const int r = readLine(line, sizeof(line));
      if (r < 0) return r;
      needCrlf_ = false;
    }
    if (chunkLeft_ == 0) {
      const int r = readLine(line, sizeof(line));
      if (r < 0) return r;
      chunkLeft_ = strtol(line, nullptr, 16);
      if (chunkLeft_ <= 0) {
        done_ = true;
        return 0;
      }
    }
    const size_t want = ((size_t)chunkLeft_ < max) ? (size_t)chunkLeft_ : max;
    const int n = readRaw(buf, want);
    if (n == 0) return -2;
    if (n < 0) return n;
    chunkLeft_ -= n;
    if (chunkLeft_ == 0) needCrlf_ = true;
    return n;
  }

 private:
  // > 0 bytes, 0 conexion cerrada, -1 timeout
  int readRaw(uint8_t* buf, size_t max) {
    const uint32_t t0 = millis();
    for (;;) {
      const int avail = c_->available();
      if (avail > 0) {
        const int n = c_->read(buf, (size_t)avail < max ? (size_t)avail : max);
        if (n > 0) return n;
      }
      if (avail < 0 || !c_->connected()) {
        if (c_->available() > 0) continue;
        return 0;
      }
      if (millis() - t0 > cfg::kHttpStallMs) return -1;
      delay(4);
    }
  }

  // 0 ok, -1 timeout, -2 cortada
  int readLine(char* line, size_t cap) {
    size_t n = 0;
    for (;;) {
      uint8_t b;
      const int r = readRaw(&b, 1);
      if (r == 0) return -2;
      if (r < 0) return -1;
      if (b == '\n') break;
      if (b != '\r' && n < cap - 1) line[n++] = (char)b;
    }
    line[n] = 0;
    return 0;
  }

  WiFiClient* c_;
  bool chunked_;
  int remaining_;
  long chunkLeft_;
  bool needCrlf_;
  bool done_;
};

struct HttpResp {
  int code;           // codigo HTTP, o < 0 si fallo la conexion
  bool truncated;     // el cuerpo llego incompleto
  std::string error;  // texto corto para mostrar
  std::string etag;
  std::string modified;
  std::string charset;

  HttpResp() : code(0), truncated(false) {}
};

// Devuelve false para dejar de recibir.
typedef bool (*BodyFn)(void* ctx, const uint8_t* data, size_t n);

std::string httpErrorText(int code) {
  char b[32];
  if (code > 0) {
    snprintf(b, sizeof(b), "HTTP %d", code);
    return b;
  }
  switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED:
      return "sin conexión";  // DNS, TCP o TLS
    case HTTPC_ERROR_READ_TIMEOUT:
      return "timeout";
    case HTTPC_ERROR_CONNECTION_LOST:
    case HTTPC_ERROR_NOT_CONNECTED:
    case HTTPC_ERROR_SEND_HEADER_FAILED:
      return "conexión cortada";
    case HTTPC_ERROR_NO_HTTP_SERVER:
    case HTTPC_ERROR_ENCODING:
      return "respuesta inválida";
    default:
      snprintf(b, sizeof(b), "error de red %d", code);
      return b;
  }
}

std::string resolveUrl(const std::string& base, const std::string& loc) {
  if (txt::startsWithNoCase(loc, "http://") || txt::startsWithNoCase(loc, "https://")) return loc;
  const size_t schemeEnd = base.find("://");
  if (schemeEnd == std::string::npos) return loc;
  if (loc.compare(0, 2, "//") == 0) return base.substr(0, schemeEnd + 1) + loc;
  const size_t hostEnd = base.find('/', schemeEnd + 3);
  const std::string origin = hostEnd == std::string::npos ? base : base.substr(0, hostEnd);
  if (!loc.empty() && loc[0] == '/') return origin + loc;
  std::string path = hostEnd == std::string::npos ? std::string("/") : base.substr(hostEnd);
  const size_t q = path.find_first_of("?#");
  if (q != std::string::npos) path.resize(q);
  return origin + path.substr(0, path.rfind('/') + 1) + loc;
}

std::string charsetOf(const String& contentType) {
  String low = contentType;
  low.toLowerCase();
  const int k = low.indexOf("charset=");
  if (k < 0) return std::string();
  std::string v = low.c_str() + k + 8;
  const size_t end = v.find_first_of("; \t");
  if (end != std::string::npos) v.resize(end);
  std::string out;
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i] != '"' && v[i] != '\'') out.push_back(v[i]);
  }
  return out;
}

// GET con redirecciones manuales (HTTPClient no sigue las que cambian de
// http a https). Devuelve true si hubo respuesta 200 y se entrego el cuerpo.
bool httpGet(const std::string& url0, const char* accept, const std::string& etag, const std::string& modified,
             HttpResp& resp, BodyFn fn, void* ctx, uint32_t deadlineMs, size_t maxBytes) {
  std::string url = url0;
  const size_t frag = url.find('#');
  if (frag != std::string::npos) url.resize(frag);

  for (int hop = 0; hop < 6; hop++) {
    const bool https = txt::startsWithNoCase(url, "https://");
    WiFiClient* client;
    if (https) {
      WiFiClientSecure* tls = new WiFiClientSecure();
      // Sin validar certificados: los feeds son contenido publico y no se envia
      // nada sensible; validar exigiria embarcar y mantener un paquete de CAs.
      tls->setInsecure();
      tls->setHandshakeTimeout(10);
      client = tls;
    } else {
      client = new WiFiClient();
    }
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout((int32_t)cfg::kHttpConnectMs);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(cfg::kUserAgent);
    if (!http.begin(*client, url.c_str())) {
      delete client;
      resp.code = -100;
      resp.error = "URL inválida";
      return false;
    }
    if (accept) http.addHeader("Accept", accept);
    if (!etag.empty()) http.addHeader("If-None-Match", etag.c_str());
    if (!modified.empty()) http.addHeader("If-Modified-Since", modified.c_str());
    static const char* kKeys[] = {"Location", "Content-Type", "Content-Encoding", "Transfer-Encoding", "ETag",
                                  "Last-Modified"};
    http.collectHeaders(kKeys, sizeof(kKeys) / sizeof(kKeys[0]));

    const int code = http.GET();
    resp.code = code;

    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      const String loc = http.header("Location");
      http.end();
      delete client;
      if (loc.length() == 0) {
        resp.error = "redirección inválida";
        return false;
      }
      url = resolveUrl(url, loc.c_str());
      continue;
    }
    if (code != 200) {
      http.end();
      delete client;
      resp.error = httpErrorText(code);
      return false;
    }

    String enc = http.header("Content-Encoding");
    enc.toLowerCase();
    if (enc.length() && enc != "identity") {
      http.end();
      delete client;
      resp.error = "comprimido (" + std::string(enc.c_str()) + ")";
      return false;
    }
    String te = http.header("Transfer-Encoding");
    te.toLowerCase();
    resp.etag = http.header("ETag").c_str();
    resp.modified = http.header("Last-Modified").c_str();
    resp.charset = charsetOf(http.header("Content-Type"));

    BodyReader body(http.getStreamPtr(), te.indexOf("chunked") >= 0, http.getSize());
    const uint32_t t0 = millis();
    size_t total = 0;
    for (;;) {
      const int n = body.read(g_buf, sizeof(g_buf));
      if (n > 0) {
        total += (size_t)n;
        if (!fn(ctx, g_buf, (size_t)n)) break;  // el receptor ya tiene lo que queria
        if (total >= maxBytes) break;
        if (millis() - t0 > deadlineMs) {
          resp.truncated = true;
          resp.error = "timeout";
          break;
        }
      } else {
        if (n < 0) {
          resp.truncated = true;
          resp.error = (n == -1) ? "timeout" : "conexión cortada";
        }
        break;
      }
    }
    http.end();
    delete client;
    return true;
  }
  resp.code = -101;
  resp.error = "demasiadas redirecciones";
  return false;
}

// ---------------------------------------------------------------------------
// Hora y zona horaria
// ---------------------------------------------------------------------------

struct SmallBody {
  std::string data;
};

bool onSmallBody(void* ctx, const uint8_t* d, size_t n) {
  SmallBody* b = (SmallBody*)ctx;
  b->data.append((const char*)d, n);
  return b->data.size() < 400;
}

// Corrimiento UTC segun la IP publica: {"status":"success","offset":-10800}
bool detectTz(int32_t& offset) {
  HttpResp resp;
  SmallBody body;
  if (!httpGet(cfg::kTzUrl, nullptr, "", "", resp, onSmallBody, &body, 5000, 400)) return false;
  if (body.data.find("\"success\"") == std::string::npos) return false;
  const size_t k = body.data.find("\"offset\":");
  if (k == std::string::npos) return false;
  const long v = strtol(body.data.c_str() + k + 9, nullptr, 10);
  if (v < -14 * 3600 || v > 14 * 3600) return false;
  offset = (int32_t)v;
  return true;
}

void startTimeServices() {
  if (!g_ntpStarted) {
    configTzTime(currentTzString().c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    g_ntpStarted = true;
    logf("[hora] NTP iniciado, TZ=%s", currentTzString().c_str());
  }
  if (g_tzPending) {
    int32_t off = 0;
    if (detectTz(off)) {
      g_tzPending = false;
      g_tzDetected = true;
      g_tzOffset = off;
      applyTz(currentTzString());
      logf("[hora] zona detectada: UTC%+.2f (TZ=%s)", off / 3600.0, currentTzString().c_str());
      Lock l;
      g_st.seq++;
    } else {
      if (++g_tzTries >= 3) g_tzPending = false;  // no insistir toda la sesion
      logf("[hora] no se pudo detectar la zona horaria; sigue TZ=%s", currentTzString().c_str());
    }
  }
}

void waitForTime(uint32_t maxMs) {
  const uint32_t t0 = millis();
  while (!timeValid() && millis() - t0 < maxMs) delay(100);
}

// ---------------------------------------------------------------------------
// Actualizacion de feeds
// ---------------------------------------------------------------------------

struct Collector {
  std::vector<FeedItem>* items;
  size_t max;
};

bool onFeedItem(void* c, FeedItem& it) {
  Collector* col = (Collector*)c;
  col->items->push_back(std::move(it));
  return col->items->size() < col->max;
}

struct FeedBody {
  FeedParser* parser;
  const HttpResp* resp;
  bool hinted;
};

bool onFeedBody(void* c, const uint8_t* d, size_t n) {
  FeedBody* b = (FeedBody*)c;
  if (!b->hinted) {  // charset del encabezado HTTP, por si el XML no lo declara
    b->hinted = true;
    b->parser->setCharsetHint(b->resp->charset.c_str());
  }
  b->parser->feed((const char*)d, n);
  return !b->parser->stopped() && !b->parser->unsupportedEncoding();
}

struct FetchResult {
  bool ok;
  bool notModified;
  bool connError;  // ni siquiera se pudo conectar
  std::string error;
  std::string etag;
  std::string modified;
  std::vector<FeedItem> items;

  FetchResult() : ok(false), notModified(false), connError(false) {}
};

void fetchFeed(const std::string& url, const std::string& etag, const std::string& modified, FetchResult& out) {
  Collector col = {&out.items, cfg::kMaxItems};
  FeedParser parser(onFeedItem, &col, cfg::kMaxTextChars);
  HttpResp resp;
  FeedBody body = {&parser, &resp, false};
  const bool got = httpGet(url, "application/rss+xml, application/atom+xml, application/xml;q=0.9, text/xml;q=0.8, */*;q=0.5",
                           etag, modified, resp, onFeedBody, &body, cfg::kFeedDeadlineMs, cfg::kFeedMaxBytes);
  if (!got) {
    if (resp.code == 304) {
      out.notModified = true;
      return;
    }
    out.connError = resp.code == HTTPC_ERROR_CONNECTION_REFUSED;
    out.error = resp.error;
    return;
  }
  parser.finish();
  if (parser.unsupportedEncoding()) {
    out.error = "codificación no soportada";
  } else if (parser.rootKind() == 4) {
    out.error = "no es un feed (HTML)";
  } else if (out.items.empty() && (parser.rootKind() == 0 || resp.truncated)) {
    out.error = resp.truncated ? resp.error : std::string("no es RSS/Atom");
  } else {
    out.ok = true;
    // si la descarga quedo a medias no se guarda el ETag: la proxima va completa
    if (!resp.truncated || parser.stopped()) {
      out.etag = resp.etag;
      out.modified = resp.modified;
    }
  }
}

const CacheFeed* findByUrl(const std::vector<CacheFeed>& idx, const std::string& url) {
  for (size_t i = 0; i < idx.size(); i++) {
    if (idx[i].url == url) return &idx[i];
  }
  return nullptr;
}

void setProgress(Phase phase, uint16_t idx, uint16_t total, const char* name) {
  Lock l;
  g_st.phase = phase;
  g_st.feedIdx = idx;
  g_st.feedTotal = total;
  copyStr(g_st.feedName, sizeof(g_st.feedName), name);
  g_st.seq++;
}

UpdateOutcome updateFeeds() {
  std::vector<FeedDef> feeds;
  {
    Lock l;
    feeds = g_feeds;
    g_results.clear();
    g_st.okCount = g_st.failCount = g_st.newItems = 0;
  }
  if (feeds.empty()) {
    logf("[feeds] RSS.txt no tiene fuentes");
    return UP_NO_FEEDS;
  }

  CacheHeader oldHdr;
  std::vector<CacheFeed> oldIdx;
  store::readCacheIndex(oldHdr, oldIdx);  // si esta danado se usa lo que se haya podido leer

  fs::File tmp;
  {
    store::Guard g;
    tmp = SD.open(cfg::kCacheTmp, FILE_WRITE);
  }
  if (!tmp) {
    logf("[feeds] no se pudo crear %s", cfg::kCacheTmp);
    return UP_SD_ERROR;
  }
  store::FileSink sink(tmp);
  CacheWriter writer(sink);
  CacheHeader hdr;
  hdr.saved = timeValid() ? (int64_t)time(nullptr) : 0;
  hdr.hasTz = g_tzDetected;
  hdr.tzOffset = g_tzOffset;
  {
    store::Guard g;
    writer.begin(hdr);
  }

  std::vector<CacheFeed> newIdx;
  uint16_t okCount = 0, failCount = 0, newItems = 0;
  int connFails = 0;
  bool offline = false;

  for (size_t i = 0; i < feeds.size(); i++) {
    const FeedDef& def = feeds[i];
    setProgress(PH_FEEDS, (uint16_t)(i + 1), (uint16_t)feeds.size(), def.name.c_str());
    const CacheFeed* old = findByUrl(oldIdx, def.url);

    FetchResult fr;
    const uint32_t t0 = millis();
    if (offline) {
      fr.error = "sin conexión";
    } else {
      fetchFeed(def.url, old ? old->etag : std::string(), old ? old->modified : std::string(), fr);
    }

    CacheFeed cf;
    cf.name = def.name;
    cf.url = def.url;
    std::vector<FeedItem> oldItems;
    std::vector<FeedItem> finalItems;
    FeedResult result;
    result.name = def.name;
    result.ok = false;
    result.added = 0;

    if (fr.ok) {
      // Para no tener dos juegos de textos en memoria: primero se lee del cache
      // viejo solo lo liviano (ids y leido/no leido) y despues, con texto,
      // unicamente los items que se conservan porque ya no vienen en el feed.
      std::vector<uint32_t> keepIds;
      {
        std::vector<FeedItem> oldLight;
        if (old && old->count > 0) {
          CacheFeed scratch;
          store::readCacheFeed(old->offset, scratch, &oldLight, nullptr, false);
        }
        result.added = mergeReadState(fr.items, oldLight, cfg::kMaxItems, keepIds);
      }
      if (old && !keepIds.empty()) {
        CacheFeed scratch;
        store::readCacheFeed(old->offset, scratch, &oldItems, nullptr, true, &keepIds);
      }
      mergeAppend(fr.items, oldItems, cfg::kMaxItems);
      finalItems.swap(fr.items);
      cf.updated = hdr.saved;
      cf.status = "ok";
      cf.etag = fr.etag;
      cf.modified = fr.modified;
      result.ok = true;
      okCount++;
      newItems = (uint16_t)(newItems + result.added);
      connFails = 0;
    } else {
      if (old && old->count > 0) {
        CacheFeed scratch;
        store::readCacheFeed(old->offset, scratch, &oldItems, nullptr, true);
      }
      finalItems.swap(oldItems);
      if (old) {
        cf.etag = old->etag;
        cf.modified = old->modified;
      }
      if (fr.notModified) {  // el servidor dice que no cambio: vale lo que hay
        cf.updated = hdr.saved ? hdr.saved : (old ? old->updated : 0);
        cf.status = "ok";
        result.ok = true;
        okCount++;
        connFails = 0;
      } else {
        cf.updated = old ? old->updated : 0;
        cf.status = fr.error.empty() ? std::string("error") : fr.error;
        result.msg = cf.status;
        failCount++;
        // dos fallas de conexion seguidas sin ningun exito: no hay Internet
        if (fr.connError && okCount == 0 && ++connFails >= 2) offline = true;
      }
    }

    cf.count = (int)finalItems.size();
    cf.unread = 0;
    for (size_t k = 0; k < finalItems.size(); k++) {
      if (!finalItems[k].read) cf.unread++;
    }
    {
      store::Guard g;
      cf.offset = writer.writeFeed(cf, finalItems);
    }
    newIdx.push_back(cf);
    logf("[feeds] %u/%u %s: %s, %d items (%d nuevos) en %lu ms, heap %u", (unsigned)(i + 1), (unsigned)feeds.size(),
         def.name.c_str(), result.ok ? "ok" : cf.status.c_str(), cf.count, result.added,
         (unsigned long)(millis() - t0), (unsigned)ESP.getFreeHeap());
    {
      Lock l;
      g_results.push_back(result);
      g_st.okCount = okCount;
      g_st.failCount = failCount;
      g_st.newItems = newItems;
      g_st.seq++;
    }
  }

  setProgress(PH_SAVE, (uint16_t)feeds.size(), (uint16_t)feeds.size(), "");
  bool saved;
  {
    store::Guard g;
    writer.end();
    sink.flush();
    tmp.close();
    const bool written = writer.ok() && sink.ok();
    if (!written) logf("[feeds] fallo la escritura de %s a los %u bytes", cfg::kCacheTmp, (unsigned)writer.bytes());
    saved = written && store::commitCache();
    if (!saved) SD.remove(cfg::kCacheTmp);
  }
  if (!saved) {
    logf("[feeds] error al escribir cache.json");
    return UP_SD_ERROR;
  }
  {
    Lock l;
    g_index.swap(newIdx);
    if (okCount > 0 && hdr.saved) g_st.lastSaved = hdr.saved;
    g_st.seq++;
  }
  logf("[feeds] listo: %u ok, %u con error, %u items nuevos, cache %u bytes", (unsigned)okCount,
       (unsigned)failCount, (unsigned)newItems, (unsigned)writer.bytes());
  return UP_DONE;
}

void finishBusy(UpdateOutcome outcome) {
  Lock l;
  g_st.busy = false;
  g_st.phase = PH_IDLE;
  g_st.outcome = outcome;
  g_st.feedName[0] = 0;
  g_st.seq++;
}

// Secuencia completa: WiFi -> hora -> feeds. Es la de arranque y la de "Update".
void runUpdate() {
  setProgress(PH_WIFI, 0, 0, "");
  if (!ensureWifi()) {
    finishBusy(UP_NO_WIFI);
    return;
  }
  g_wasUp = true;
  setProgress(PH_TIME, 0, 0, "");
  startTimeServices();
  waitForTime(4000);
  finishBusy(updateFeeds());
}

// ---------------------------------------------------------------------------
// WiFi Setup
// ---------------------------------------------------------------------------

void runScan() {
  const int n = scanNetworks();
  std::vector<ScanNet> found;
  for (int i = 0; i < n; i++) {
    const String s = WiFi.SSID(i);
    if (s.length() == 0 || s.length() > 32) continue;  // redes ocultas
    bool printable = true;
    for (size_t k = 0; k < s.length(); k++) {
      if ((uint8_t)s[k] < 0x20 || s[k] == 0x7F) printable = false;
    }
    if (!printable) continue;
    const int rssi = WiFi.RSSI(i);
    bool dup = false;
    for (size_t k = 0; k < found.size(); k++) {
      if (found[k].ssid == s.c_str()) {  // varios puntos de acceso con el mismo nombre
        if (rssi > found[k].rssi) found[k].rssi = (int8_t)rssi;
        dup = true;
        break;
      }
    }
    if (dup) continue;
    ScanNet sn;
    sn.ssid = s.c_str();
    sn.rssi = (int8_t)rssi;
    sn.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    found.push_back(sn);
  }
  WiFi.scanDelete();
  std::sort(found.begin(), found.end(), [](const ScanNet& a, const ScanNet& b) { return a.rssi > b.rssi; });
  if (found.size() > 40) found.resize(40);
  logf("[wifi] escaneo: %d puntos de acceso, %u redes", n, (unsigned)found.size());
  Lock l;
  g_scan.swap(found);
  g_st.scanning = false;
  g_st.scanSeq++;
  g_st.seq++;
}

void runConnect(const char* ssid, const char* pass) {
  g_connecting = true;
  publishWifi();
  const bool ok = tryConnect(ssid, pass, 15000);
  g_connecting = false;
  const char* msg;
  if (ok) {
    msg = "Conectado";
  } else {
    const uint8_t r = g_discReason;
    if (r == WIFI_REASON_NO_AP_FOUND) msg = "No se encontró la red";
    else if (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
             r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_EXPIRE)
      msg = "Clave incorrecta";
    else msg = "No se pudo conectar";
  }
  {
    Lock l;
    g_st.conn = ok ? CONN_OK : CONN_FAIL;
    copyStr(g_st.connMsg, sizeof(g_st.connMsg), msg);
    g_st.seq++;
  }
  if (!ok) ensureWifi();  // volver a la red que estaba funcionando, si hay
  publishWifi();
}

void workerTask(void*) {
  for (;;) {
    Cmd cmd;
    if (xQueueReceive(g_queue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
      switch (cmd.type) {
        case CMD_BOOT:
        case CMD_UPDATE:
          runUpdate();
          break;
        case CMD_SCAN:
          runScan();
          break;
        case CMD_CONNECT:
          runConnect(cmd.ssid, cmd.pass);
          break;
      }
    }
    publishWifi();
    // conexion establecida fuera de un Update (WiFi Setup o reconexion): hora y zona
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && (!g_wasUp || !g_ntpStarted || g_tzPending)) {
      static uint32_t lastTry = 0;
      if (!g_wasUp || millis() - lastTry > 60000) {
        lastTry = millis();
        startTimeServices();
      }
    }
    g_wasUp = up;
  }
}

void send(const Cmd& cmd) {
  if (g_queue) xQueueSend(g_queue, &cmd, 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// API publica (la llama la interfaz)
// ---------------------------------------------------------------------------

void begin(const std::vector<WifiNet>& known, const std::vector<FeedDef>& feeds, const dt::TzSpec& tz,
           const CacheHeader& hdr, const std::vector<CacheFeed>& index) {
  if (g_task) return;
  g_mux = xSemaphoreCreateMutex();
  g_queue = xQueueCreate(4, sizeof(Cmd));
  memset(&g_st, 0, sizeof(g_st));
  g_known = known;
  g_feeds = feeds;
  g_index = index;
  g_tz = tz;
  g_tzDetected = hdr.hasTz;
  g_tzOffset = hdr.tzOffset;
  g_tzPending = tz.kind == dt::TzSpec::AUTO;
  g_st.lastSaved = hdr.saved;
  g_st.busy = true;  // desde el primer instante: la secuencia de arranque esta en curso
  g_st.phase = PH_WIFI;
  applyTz(currentTzString());

  WiFi.persistent(false);  // las claves viven solo en wifi-config.txt, no en la flash
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWifiEvent);

  xTaskCreatePinnedToCore(workerTask, "net", 20480, nullptr, 1, &g_task, 0);
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_BOOT;
  send(cmd);
}

bool started() { return g_task != nullptr; }

void getStatus(Status& out) {
  if (!g_mux) {
    memset(&out, 0, sizeof(out));
    return;
  }
  Lock l;
  out = g_st;
}

uint32_t seq() { return g_st.seq; }

void requestUpdate() {
  if (!g_mux) return;
  {
    Lock l;
    if (g_st.busy) return;
    g_st.busy = true;
    g_st.phase = PH_WIFI;
    g_st.feedIdx = g_st.feedTotal = 0;
    g_st.feedName[0] = 0;
    g_st.seq++;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_UPDATE;
  send(cmd);
}

void requestScan() {
  if (!g_mux) return;
  {
    Lock l;
    if (g_st.scanning) return;
    g_st.scanning = true;
    g_st.seq++;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_SCAN;
  send(cmd);
}

void requestConnect(const std::string& ssid, const std::string& pass) {
  if (!g_mux) return;
  {
    Lock l;
    g_st.conn = CONN_BUSY;
    g_st.connMsg[0] = 0;
    g_st.seq++;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_CONNECT;
  copyStr(cmd.ssid, sizeof(cmd.ssid), ssid.c_str());
  copyStr(cmd.pass, sizeof(cmd.pass), pass.c_str());
  send(cmd);
}

void clearConnResult() {
  if (!g_mux) return;
  Lock l;
  g_st.conn = CONN_IDLE;
  g_st.connMsg[0] = 0;
}

void setKnown(const std::vector<WifiNet>& known) {
  if (!g_mux) return;
  Lock l;
  g_known = known;
}

void getScan(std::vector<ScanNet>& out) {
  out.clear();
  if (!g_mux) return;
  Lock l;
  out = g_scan;
}

void getResults(std::vector<FeedResult>& out) {
  out.clear();
  if (!g_mux) return;
  Lock l;
  out = g_results;
}

void getIndex(std::vector<CacheFeed>& out) {
  out.clear();
  if (!g_mux) return;
  Lock l;
  out = g_index;
}

void noteRead(const std::string& url) {
  if (!g_mux) return;
  Lock l;
  for (size_t i = 0; i < g_index.size(); i++) {
    if (g_index[i].url == url) {
      if (g_index[i].unread > 0) g_index[i].unread--;
      g_st.seq++;
      break;
    }
  }
}

uint32_t workerStackFree() { return g_task ? (uint32_t)uxTaskGetStackHighWaterMark(g_task) : 0; }

}  // namespace net
