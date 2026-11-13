// Pruebas de PC para el nucleo portable de TenoRSS.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/core/cache.h"
#include "../../src/core/conffiles.h"
#include "../../src/core/datetime.h"
#include "../../src/core/feedparser.h"
#include "../../src/core/json.h"
#include "../../src/core/textutil.h"
#include "../../src/core/wrap.h"

static int g_fail = 0, g_total = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    g_total++;                                                          \
    if (!(cond)) {                                                      \
      g_fail++;                                                         \
      printf("FALLA %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                                         \
  do {                                                                                         \
    g_total++;                                                                                 \
    const std::string _a = (a), _b = (b);                                                      \
    if (_a != _b) {                                                                            \
      g_fail++;                                                                                \
      printf("FALLA %s:%d:\n   obtuve : [%s]\n   esperaba: [%s]\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); \
    }                                                                                          \
  } while (0)

static std::string html(const char* in, size_t maxChars = 1000, bool single = false) {
  std::string out;
  txt::HtmlToText h;
  h.begin(&out, maxChars, single);
  h.write(in);
  h.finish();
  return out;
}

struct StrSource : ByteSource {
  const std::string* s;
  size_t pos;
  StrSource(const std::string& str, size_t start = 0) : s(&str), pos(start) {}
  int readByte() override { return pos < s->size() ? (unsigned char)(*s)[pos++] : -1; }
};

struct StrSink : ByteSink {
  std::string data;
  bool write(const char* d, size_t n) override {
    data.append(d, n);
    return true;
  }
};

static void testHtml() {
  CHECK_EQ(html("<p>Hola <b>mundo</b></p><p>Chau</p>"), "Hola mundo\nChau");
  CHECK_EQ(html("a&nbsp;&amp;&nbsp;b &lt;tag&gt; &quot;x&quot; &#233; &#xF1; &eacute;&ntilde;"),
           "a & b <tag> \"x\" \xC3\xA9 \xC3\xB1 \xC3\xA9\xC3\xB1");
  CHECK_EQ(html("AT&T y Q&A; fin &"), "AT&T y Q&A; fin &");
  CHECK_EQ(html("uno<br>dos<br/>tres"), "uno\ndos\ntres");
  CHECK_EQ(html("  mucho   espacio \n\t  junto  "), "mucho espacio junto");
  CHECK_EQ(html("x<script>var a = '<p>no</p>';</script>y<style>p{color:red}</style>z"), "xyz");
  CHECK_EQ(html("a<!-- comentario <b>no</b> -->b"), "ab");
  CHECK_EQ(html("<ul><li>uno</li><li>dos</li></ul>fin"), "\xE2\x80\xA2 uno\n\xE2\x80\xA2 dos\nfin");
  CHECK_EQ(html("<a href=\"http://x/?a=1&b=2\" title='a > b'>link</a> ok"), "link ok");
  CHECK_EQ(html("3 < 5 y 7 > 2"), "3 < 5 y 7 > 2");
  CHECK_EQ(html("<img src=x alt=\"don't\">despues"), "despues");
  CHECK_EQ(html("comilla &#146;rara&#148; &rsquo;ok&rdquo;"),
           "comilla \xE2\x80\x99rara\xE2\x80\x9D \xE2\x80\x99ok\xE2\x80\x9D");
  CHECK_EQ(html("emoji \xF0\x9F\x9A\x80 fuera"), "emoji fuera");
  CHECK_EQ(html("cero\xE2\x80\x8B" "ancho y soft\xC2\xADhyphen"), "ceroancho y softhyphen");
  CHECK_EQ(html("<p>Titulo</p>\n<p>en <em>una</em> linea</p>", 100, true), "Titulo en una linea");
  CHECK_EQ(html("<h1>T</h1><div><div>anidado</div></div>"), "T\nanidado");
  CHECK_EQ(html("bytes invalidos \xFF\xFE ok \xC3"), "bytes invalidos ok");
  // recorte: no parte palabras y agrega puntos suspensivos
  CHECK_EQ(html("uno dos tres cuatro cinco seis", 12), "uno dos\xE2\x80\xA6");
  CHECK_EQ(html("justo", 5), "justo");
  CHECK_EQ(html("justo!", 5), "justo\xE2\x80\xA6");
  CHECK(txt::utf8Count(html("\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xB1 palabra", 4)) <= 5);
  {
    // tope en bytes: nunca se pasa ni deja un caracter partido
    std::string out;
    txt::HtmlToText h;
    h.begin(&out, 1000, false, 40);
    for (int i = 0; i < 100; i++) h.write("\xE6\x97\xA5");
    h.finish();
    CHECK(out.size() <= 40 + 3);
    uint32_t cp = 0;
    size_t i = 0, bad = 0;
    while (i < out.size()) {
      const size_t n = txt::utf8Decode(out.data() + i, out.size() - i, cp);
      if (cp == 0xFFFD) bad++;
      i += n ? n : 1;
    }
    CHECK(bad == 0);
  }
  // el texto escapado dos veces (tipico de RSS) queda bien
  CHECK_EQ(html("Caf&amp;eacute; &amp;amp; t&amp;#233;"), "Caf&eacute; &amp; t&#233;");
}

static void testDates() {
  CHECK(dt::parseDate("Thu, 01 Jan 1970 00:00:00 GMT") == 0);  // el epoch se informa como "sin fecha"
  CHECK(dt::parseDate("Fri, 13 Nov 2026 12:00:37 GMT") == 1794571237);
  CHECK(dt::parseDate("Fri, 13 Nov 2026 09:00:37 -0300") == 1794571237);
  CHECK(dt::parseDate("13 Nov 2026 12:00:37 +0000") == 1794571237);
  CHECK(dt::parseDate("Fri, 13 Nov 2026 13:00:37 +01:00") == 1794571237);
  CHECK(dt::parseDate("Fri, 13 Nov 2026 07:00:37 EST") == 1794571237);
  CHECK(dt::parseDate("Fri, 13 Nov 26 12:00 UT") == 1794571200);
  CHECK(dt::parseDate("2026-11-13T12:00:37Z") == 1794571237);
  CHECK(dt::parseDate("2026-11-13T12:00:37.123456Z") == 1794571237);
  CHECK(dt::parseDate("2026-11-13T09:00:37-03:00") == 1794571237);
  CHECK(dt::parseDate("2026-11-13T17:30:37+0530") == 1794571237);
  CHECK(dt::parseDate("2026-11-13 12:00:37") == 1794571237);
  CHECK(dt::parseDate("2026-11-13") == 1794528000);
  CHECK(dt::parseDate("  Sat, 29 Feb 2020 23:59:59 GMT") == 1583020799);
  CHECK(dt::parseDate("vie, 13 dic 2024 10:00:00 -0300") == 1734094800);
  CHECK(dt::parseDate("") == 0);
  CHECK(dt::parseDate("ayer") == 0);
  CHECK(dt::parseDate("32 Foo 2026") == 0);
  CHECK_EQ(dt::formatAge(1000000, 1000000 - 30), "ahora");
  CHECK_EQ(dt::formatAge(1000000, 1000000 - 300), "5m");
  CHECK_EQ(dt::formatAge(1000000, 1000000 - 7300), "2h");
  CHECK_EQ(dt::formatAge(1000000, 1000000 - 3 * 86400), "3d");
  CHECK_EQ(dt::formatAge(10000000, 10000000 - 21 * 86400), "3sem");
  CHECK_EQ(dt::formatAge(1000000, 0), "");
  CHECK_EQ(dt::formatAge(1000000, 1000500), "ahora");

  CHECK(dt::parseTz("auto").kind == dt::TzSpec::AUTO);
  CHECK(dt::parseTz("").kind == dt::TzSpec::AUTO);
  CHECK(dt::parseTz(" AUTO ").kind == dt::TzSpec::AUTO);
  CHECK(dt::parseTz("-3").kind == dt::TzSpec::OFFSET && dt::parseTz("-3").offsetSec == -10800);
  CHECK(dt::parseTz("+1").offsetSec == 3600);
  CHECK(dt::parseTz("2").offsetSec == 7200);
  CHECK(dt::parseTz("-03:00").offsetSec == -10800);
  CHECK(dt::parseTz("-0300").offsetSec == -10800);
  CHECK(dt::parseTz("+5:30").offsetSec == 19800);
  CHECK(dt::parseTz("UTC-3").kind == dt::TzSpec::OFFSET && dt::parseTz("UTC-3").offsetSec == -10800);
  CHECK(dt::parseTz("GMT+01:00").offsetSec == 3600);
  CHECK(dt::parseTz("UTC").kind == dt::TzSpec::OFFSET && dt::parseTz("UTC").offsetSec == 0);
  CHECK(dt::parseTz("CET-1CEST,M3.5.0,M10.5.0/3").kind == dt::TzSpec::POSIX);
  CHECK_EQ(dt::parseTz("CET-1CEST,M3.5.0,M10.5.0/3").posix, "CET-1CEST,M3.5.0,M10.5.0/3");
  CHECK(dt::parseTz("???").kind == dt::TzSpec::AUTO);
  CHECK_EQ(dt::posixFromOffset(-10800), "<-03>3");
  CHECK_EQ(dt::posixFromOffset(3600), "<+01>-1");
  CHECK_EQ(dt::posixFromOffset(19800), "<+0530>-5:30");
  CHECK_EQ(dt::posixFromOffset(-12600), "<-0330>3:30");
  CHECK_EQ(dt::posixFromOffset(0), "UTC0");
}

static void testConfig() {
  std::vector<WifiNet> nets;
  parseWifiConfig("\xEF\xBB\xBF# comentario\r\n[wifi 1]\r\nSSID=\"Mi Red\"\r\nPASS=\"cl#ve;con \"comillas\"\"\r\n\r\n"
                  "[wifi 2]\n ssid = abierta \npass=\"\"\n[wifi 3]\nPASS=\"sin ssid\"\n[x]\nSSID='simple'\nPASSWORD=' con espacios '\n",
                  nets);
  CHECK(nets.size() == 3);
  if (nets.size() == 3) {
    CHECK_EQ(nets[0].ssid, "Mi Red");
    CHECK_EQ(nets[0].pass, "cl#ve;con \"comillas\"");
    CHECK_EQ(nets[1].ssid, "abierta");
    CHECK_EQ(nets[1].pass, "");
    CHECK_EQ(nets[2].ssid, "simple");
    CHECK_EQ(nets[2].pass, " con espacios ");
  }
  // ida y vuelta
  std::vector<WifiNet> again;
  parseWifiConfig(buildWifiConfig(nets), again);
  CHECK(again.size() == nets.size());
  for (size_t i = 0; i < again.size() && i < nets.size(); i++) {
    CHECK_EQ(again[i].ssid, nets[i].ssid);
    CHECK_EQ(again[i].pass, nets[i].pass);
  }
  // repetidas: queda una sola, con la ultima clave
  parseWifiConfig("[a]\nSSID=\"x\"\nPASS=\"1\"\n[b]\nSSID=\"x\"\nPASS=\"2\"\n", nets);
  CHECK(nets.size() == 1 && nets[0].pass == "2");
  parseWifiConfig("", nets);
  CHECK(nets.empty());

  std::vector<FeedDef> feeds;
  std::string tz;
  parseFeedsConfig(defaultFeedsConfig(), feeds, tz);
  CHECK(feeds.size() >= 5);
  CHECK_EQ(tz, "auto");
  if (!feeds.empty()) {
    CHECK_EQ(feeds[0].name, "Microsoft Admin Center Status");
    CHECK_EQ(feeds[0].url, "https://status.cloud.microsoft/api/feed/mac");
  }
  parseFeedsConfig("TZ=-3\n[Uno]\nURL=https://a/b\n[Sin url]\n[Dos [ES]]\nurl = \"http://c/d?x=1#y\"\n"
                   "[Mala]\nURL=\"ftp://no\"\nURL=\"https://suelta.example/feed\"\n[Uno otra vez]\nURL=https://a/b\n",
                   feeds, tz);
  CHECK_EQ(tz, "-3");
  CHECK(feeds.size() == 3);
  if (feeds.size() == 3) {
    CHECK_EQ(feeds[0].name, "Uno");
    CHECK_EQ(feeds[1].name, "Dos [ES]");
    CHECK_EQ(feeds[1].url, "http://c/d?x=1#y");
    CHECK_EQ(feeds[2].name, "Mala");
    CHECK_EQ(feeds[2].url, "https://suelta.example/feed");
  }
}

static FeedItem mk(uint32_t id, const char* title, int64_t date, bool read = false) {
  FeedItem it;
  it.id = id;
  it.title = title;
  it.date = date;
  it.read = read;
  it.link = std::string("https://example.com/") + title;
  it.text = std::string("Texto de ") + title;
  return it;
}

static void testCache(const char* dumpPath) {
  CacheHeader hdr;
  hdr.hasTz = true;
  hdr.tzOffset = -10800;
  hdr.saved = 1794571237;
  std::vector<FeedItem> a, b, none;
  a.push_back(mk(0x11111111, "Uno \"con comillas\" y \\barra", 300));
  a.push_back(mk(0x22222222, "Dos con\nsalto\ty tab \x01 control", 200, true));
  a.push_back(mk(0x0000000a, "Tres: \xC3\xB1" "and\xC3\xBA \xE2\x80\x9Ctipogr\xC3\xA1" "fico\xE2\x80\x9D \xE6\x97\xA5\xE6\x9C\xAC", 0));
  a[2].text = "items\":[ {\"read\":1} ]} texto que parece JSON";
  b.push_back(mk(0xdeadbeef, "Solo", 100));

  StrSink sink;
  CacheWriter w(sink);
  w.begin(hdr);
  CacheFeed fa, fb, fc;
  fa.name = "Feed A";
  fa.url = "https://a.example/feed";
  fa.updated = 1794571000;
  fa.status = "ok";
  fa.etag = "W/\"abc\"";
  fb.name = "Feed B \xC3\xB1";
  fb.url = "https://b.example/rss";
  fb.status = "HTTP 404";
  fc.name = "Vacio";
  fc.url = "https://c.example/";
  fc.status = "timeout";
  const size_t offA = w.writeFeed(fa, a);
  const size_t offC = w.writeFeed(fc, none);
  const size_t offB = w.writeFeed(fb, b);
  w.end();
  CHECK(w.ok());
  CHECK(w.bytes() == sink.data.size());
  if (dumpPath) {
    FILE* f = fopen(dumpPath, "wb");
    if (f) {
      fwrite(sink.data.data(), 1, sink.data.size(), f);
      fclose(f);
    }
  }

  // indice
  CacheHeader h2;
  std::vector<CacheFeed> idx;
  StrSource src(sink.data);
  CHECK(cacheReadIndex(src, h2, idx));
  CHECK(h2.hasTz && h2.tzOffset == -10800 && h2.saved == 1794571237);
  CHECK(idx.size() == 3);
  if (idx.size() == 3) {
    CHECK_EQ(idx[0].name, "Feed A");
    CHECK_EQ(idx[0].etag, "W/\"abc\"");
    CHECK(idx[0].count == 3 && idx[0].unread == 2 && idx[0].offset == offA);
    CHECK(idx[1].count == 0 && idx[1].unread == 0 && idx[1].offset == offC);
    CHECK_EQ(idx[1].status, "timeout");
    CHECK(idx[2].count == 1 && idx[2].unread == 1 && idx[2].offset == offB);
    CHECK_EQ(idx[2].name, "Feed B \xC3\xB1");
    CHECK(sink.data[idx[0].offset] == '{' && sink.data[idx[2].offset] == '{');

    // lectura de un feed por offset, con posiciones de "read"
    CacheFeed fr;
    std::vector<FeedItem> items;
    std::vector<size_t> offs;
    StrSource s2(sink.data, idx[0].offset);
    CHECK(cacheReadFeed(s2, idx[0].offset, fr, &items, &offs, true));
    CHECK(items.size() == 3 && offs.size() == 3);
    if (items.size() == 3) {
      for (size_t i = 0; i < 3; i++) {
        CHECK(items[i].id == a[i].id);
        CHECK_EQ(items[i].title, a[i].title);
        CHECK_EQ(items[i].link, a[i].link);
        CHECK_EQ(items[i].text, a[i].text);
        CHECK(items[i].date == a[i].date && items[i].read == a[i].read);
        CHECK(offs[i] != kNoOffset && sink.data[offs[i]] == (a[i].read ? '1' : '0'));
      }
      // marcar leido el primero cambiando un byte, y releer
      std::string patched = sink.data;
      patched[offs[0]] = '1';
      CacheHeader h3;
      std::vector<CacheFeed> idx3;
      StrSource s3(patched);
      CHECK(cacheReadIndex(s3, h3, idx3));
      CHECK(idx3.size() == 3 && idx3[0].unread == 1);
    }
    // carga selectiva: solo los ids pedidos ocupan memoria
    {
      std::vector<uint32_t> only;
      only.push_back(0x22222222);
      only.push_back(0x99999999);
      std::vector<FeedItem> some;
      CacheFeed fsel;
      StrSource ssel(sink.data, idx[0].offset);
      CHECK(cacheReadFeed(ssel, idx[0].offset, fsel, &some, nullptr, true, &only));
      CHECK(fsel.count == 3 && fsel.unread == 2);
      CHECK(some.size() == 1 && some[0].id == 0x22222222 && some[0].text == a[1].text);
    }
    // sin texto
    items.clear();
    StrSource s4(sink.data, idx[2].offset);
    CHECK(cacheReadFeed(s4, idx[2].offset, fr, &items, nullptr, false));
    CHECK(items.size() == 1 && items[0].text.empty() && items[0].title == "Solo");
  }

  // JSON reformateado a mano (otro orden, espacios, true/false, claves extra)
  const std::string hand =
      "{\n  \"feeds\" : [\n    { \"items\" : [ { \"title\" : \"T\\u00e9 \\ud83d\\ude80 x\", \"extra\" : {\"a\":[1,2,\"]}\"]},\n"
      "        \"read\" : true , \"id\" : \"00ff\", \"date\": 12.5e0 } , {\"read\":false,\"id\":7} ],\n"
      "      \"url\" : \"u\", \"name\" : \"N\", \"updated\" : null }\n  ],\n  \"otro\" : [ ]\n}\n";
  StrSource s5(hand);
  CHECK(cacheReadIndex(s5, h2, idx));
  CHECK(idx.size() == 1);
  if (idx.size() == 1) {
    CHECK(idx[0].count == 2 && idx[0].unread == 1);
    CHECK_EQ(idx[0].name, "N");
    CacheFeed fr;
    std::vector<FeedItem> items;
    std::vector<size_t> offs;
    StrSource s6(hand, idx[0].offset);
    CHECK(cacheReadFeed(s6, idx[0].offset, fr, &items, &offs, true));
    CHECK(items.size() == 2);
    if (items.size() == 2) {
      CHECK_EQ(items[0].title, "T\xC3\xA9 \xF0\x9F\x9A\x80 x");
      CHECK(items[0].id == 0xff && items[0].read && items[0].date == 12);
      CHECK(offs[0] == kNoOffset && offs[1] == kNoOffset);  // true/false no se pueden parchear
      CHECK(items[1].id == 7 && !items[1].read);
    }
  }
  // archivo cortado: no explota y conserva lo que alcanzo a leer
  const std::string cut = sink.data.substr(0, offB + 20);
  StrSource s7(cut);
  CHECK(!cacheReadIndex(s7, h2, idx));
  CHECK(idx.size() == 2);
  const std::string junk = "basura";
  StrSource s8(junk);
  CHECK(!cacheReadIndex(s8, h2, idx));
  CHECK(idx.empty());
}

static void testMerge() {
  std::vector<FeedItem> fresh, old;
  old.push_back(mk(1, "viejo1", 100, true));
  old.push_back(mk(2, "viejo2", 90, false));
  old.push_back(mk(3, "viejo3", 80, true));
  fresh.push_back(mk(9, "nuevo", 120));
  fresh.push_back(mk(1, "viejo1 editado", 100));
  fresh.push_back(mk(9, "nuevo repetido", 120));
  const int added = mergeItems(fresh, old, 20);
  CHECK(added == 1);
  CHECK(fresh.size() == 4);
  if (fresh.size() == 4) {
    CHECK(fresh[0].id == 9 && !fresh[0].read);
    CHECK(fresh[1].id == 1 && fresh[1].read);
    CHECK_EQ(fresh[1].title, "viejo1 editado");
    CHECK(fresh[2].id == 2 && !fresh[2].read);
    CHECK(fresh[3].id == 3 && fresh[3].read);
  }
  // en dos pasos: primero estado leido + ids a conservar, despues se agregan
  {
    std::vector<FeedItem> fr, kept;
    std::vector<uint32_t> keep;
    fr.push_back(mk(9, "nuevo", 120));
    fr.push_back(mk(1, "viejo1", 100));
    CHECK(mergeReadState(fr, old, 3, keep) == 1);
    CHECK(fr.size() == 2 && fr[1].read);
    CHECK(keep.size() == 1 && keep[0] == 2);  // solo queda lugar para uno
    kept.push_back(old[1]);
    mergeAppend(fr, kept, 3);
    CHECK(fr.size() == 3 && fr[2].id == 2 && kept.empty());
  }
  // tope de items
  std::vector<FeedItem> f2;
  f2.push_back(mk(9, "nuevo", 120));
  CHECK(mergeItems(f2, old, 2) == 1);
  CHECK(f2.size() == 2 && f2[1].id == 1);
  // sin fechas no se reordena
  std::vector<FeedItem> f3, o3;
  f3.push_back(mk(5, "a", 0));
  f3.push_back(mk(6, "b", 500));
  o3.push_back(mk(7, "c", 900));
  mergeItems(f3, o3, 20);
  CHECK(f3.size() == 3 && f3[0].id == 5 && f3[1].id == 6 && f3[2].id == 7);
  // con fechas: mas nuevo primero
  std::vector<FeedItem> f4, o4;
  f4.push_back(mk(5, "a", 100));
  f4.push_back(mk(6, "b", 500));
  o4.push_back(mk(7, "c", 900));
  mergeItems(f4, o4, 20);
  CHECK(f4.size() == 3 && f4[0].id == 7 && f4[1].id == 6 && f4[2].id == 5);
}

struct Collect {
  std::vector<FeedItem> items;
};
static bool collect(void* c, FeedItem& it) {
  ((Collect*)c)->items.push_back(it);
  return true;
}

static void testParser() {
  const char* rss =
      "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss version=\"2.0\" xmlns:content=\"x\" xmlns:atom=\"y\">"
      "<channel><title>Canal</title><link>https://canal/</link><atom:link href=\"https://canal/feed\" rel=\"self\"/>"
      "<item><title>Uno &amp;amp; <![CDATA[<b>dos</b>]]></title><link> https://x/1?a=1&amp;b=2 </link>"
      "<guid isPermaLink=\"false\">g-1</guid><pubDate>Fri, 13 Nov 2026 12:00:37 GMT</pubDate>"
      "<description>&lt;p&gt;Resumen&lt;/p&gt;</description>"
      "<content:encoded><![CDATA[<p>Texto <a href=\"#\">completo</a> con ]] corchetes ]]]></content:encoded></item>"
      "<item><description>Sin titulo pero con texto largo que sirve de titulo para el listado, bastante mas alla del limite</description>"
      "<guid>https://x/2</guid></item>"
      "<item><title/><link/></item>"
      "</channel></rss>";
  Collect c;
  FeedParser p(collect, &c, 1500);
  p.feed(rss, strlen(rss));
  p.finish();
  CHECK(p.rootKind() == 1);
  CHECK(c.items.size() == 3);
  if (c.items.size() == 3) {
    CHECK_EQ(c.items[0].title, "Uno & dos");
    CHECK_EQ(c.items[0].link, "https://x/1?a=1&b=2");
    CHECK_EQ(c.items[0].text, "Texto completo con ]] corchetes ]");
    CHECK(c.items[0].date == 1794571237);
    CHECK(c.items[0].id == txt::fnv1a("g-1", 3));
    CHECK_EQ(c.items[1].link, "https://x/2");
    CHECK(c.items[1].title.find("Sin titulo pero con texto") == 0);
    CHECK(txt::utf8Count(c.items[1].title) == 81);
    CHECK_EQ(c.items[2].title, "(sin t\xC3\xADtulo)");
  }

  const char* atom =
      "<?xml version='1.0' encoding='utf-8'?><feed xmlns=\"http://www.w3.org/2005/Atom\"><title>F</title>"
      "<entry><title type=\"html\">A &lt;i&gt;t&lt;/i&gt; &amp;amp; B</title>"
      "<link rel=\"replies\" href=\"https://x/c\"/><link rel='alternate' type='text/html' href='https://x/a?p=1&amp;q=2'/>"
      "<link rel=\"self\" href=\"https://x/self\"/><id>tag:x,2026:1</id><updated>2026-11-13T12:00:37Z</updated>"
      "<published>2026-11-12T12:00:37Z</published><source><title>NO</title><id>NO</id></source>"
      "<author><name>Autor</name></author>"
      "<content type=\"xhtml\"><div xmlns=\"http://www.w3.org/1999/xhtml\"><p>Uno</p><p>Dos <b>negrita</b></p><br/>Tres</div></content>"
      "<summary>resumen</summary></entry>"
      "<entry><title>B</title><link href=\"https://x/b\"/><summary type=\"html\">&lt;p&gt;S&amp;oacute;lo resumen&lt;/p&gt;</summary>"
      "<updated>2026-11-13T12:00:37Z</updated></entry></feed>";
  Collect c2;
  FeedParser p2(collect, &c2, 1500);
  for (size_t i = 0; i < strlen(atom); i++) p2.feed(atom + i, 1);
  p2.finish();
  CHECK(p2.rootKind() == 2);
  CHECK(c2.items.size() == 2);
  if (c2.items.size() == 2) {
    CHECK_EQ(c2.items[0].title, "A t & B");
    CHECK_EQ(c2.items[0].link, "https://x/a?p=1&q=2");
    CHECK_EQ(c2.items[0].text, "Uno\nDos negrita\nTres");
    CHECK(c2.items[0].date == 1794571237 - 86400);
    CHECK(c2.items[0].id == txt::fnv1a("tag:x,2026:1", 12));
    CHECK_EQ(c2.items[1].text, "S\xC3\xB3lo resumen");
    CHECK(c2.items[1].date == 1794571237);
  }

  const char* latin = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><rdf:RDF><item><title>A\xF1o \x93nuevo\x94</title>"
                      "<link>http://x</link><dc:date>2026-11-13T12:00:37+00:00</dc:date></item></rdf:RDF>";
  Collect c3;
  FeedParser p3(collect, &c3, 1500);
  p3.feed(latin, strlen(latin));
  CHECK(p3.rootKind() == 3 && c3.items.size() == 1);
  if (c3.items.size() == 1) {
    CHECK_EQ(c3.items[0].title, "A\xC3\xB1o \xE2\x80\x9Cnuevo\xE2\x80\x9D");
    CHECK(c3.items[0].date == 1794571237);
  }

  const char* page = "<!DOCTYPE html><html><head><title>404</title></head><body><item>x</item></body></html>";
  Collect c4;
  FeedParser p4(collect, &c4, 1500);
  p4.feed(page, strlen(page));
  CHECK(p4.rootKind() == 4 && p4.stopped() && c4.items.empty());

  const char utf16[] = "\xFF\xFE<\0r\0s\0s\0";
  Collect c5;
  FeedParser p5(collect, &c5, 1500);
  p5.feed(utf16, sizeof(utf16) - 1);
  CHECK(p5.unsupportedEncoding());
}

static int fixedWidth(void*, uint32_t cp) { return cp >= 0x2E80 ? 14 : 7; }

static std::string joinLines(const std::string& text, int width) {
  std::vector<WrapLine> lines;
  wrapText(text, width, fixedWidth, nullptr, lines, 1000);
  std::string out;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i) out += "|";
    out += text.substr(lines[i].start, lines[i].len);
    if (lines[i].paragraphEnd) out += "$";
  }
  return out;
}

static void testWrap() {
  CHECK_EQ(joinLines("uno dos tres", 7 * 7), "uno dos|tres$");
  CHECK_EQ(joinLines("uno dos tres", 7 * 8), "uno dos|tres$");
  CHECK_EQ(joinLines("uno dos tres", 7 * 12), "uno dos tres$");
  CHECK_EQ(joinLines("a\nb\n\nc", 70), "a$|b$|$|c$");
  CHECK_EQ(joinLines("palabralarguisima corta", 7 * 6), "palabr|alargu|isima|corta$");
  CHECK_EQ(joinLines("", 70), "");
  CHECK_EQ(joinLines("\xC3\xB1\xC3\xB1\xC3\xB1 \xC3\xB1\xC3\xB1\xC3\xB1", 7 * 4), "\xC3\xB1\xC3\xB1\xC3\xB1|\xC3\xB1\xC3\xB1\xC3\xB1$");
  CHECK_EQ(joinLines("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC", 14 * 2),
           "\xE6\x97\xA5\xE6\x9C\xAC|\xE8\xAA\x9E\xE6\x97\xA5|\xE6\x9C\xAC$");
  CHECK_EQ(joinLines("fin con espacio  ", 7 * 20), "fin con espacio$");
  std::vector<WrapLine> lines;
  wrapText("a b c d e f g h", 7, fixedWidth, nullptr, lines, 3);
  CHECK(lines.size() == 3);
}

int main(int argc, char** argv) {
  testHtml();
  testDates();
  testConfig();
  testCache(argc > 1 ? argv[1] : nullptr);
  testMerge();
  testParser();
  testWrap();
  printf("%d pruebas, %d fallas\n", g_total, g_fail);
  return g_fail ? 1 : 0;
}
