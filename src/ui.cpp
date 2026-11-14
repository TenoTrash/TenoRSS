#include "ui.h"

#include <M5Cardputer.h>
#include <lgfx/utility/lgfx_qrcode.h>
#include <time.h>

#include <string>
#include <vector>

#include "app.h"
#include "core/datetime.h"
#include "core/textutil.h"
#include "core/wrap.h"
#include "input.h"
#include "net.h"
#include "storage.h"

namespace ui {

namespace {

// ---------------------------------------------------------------------------
// Lienzo, colores y fuentes
// ---------------------------------------------------------------------------

// Toda la pantalla se dibuja en un sprite de 8 bits (32 KB) y se envia de una
// vez: sin parpadeo y sin gastar los 65 KB que pediria uno de 16 bits.
M5Canvas canvas(&M5Cardputer.Display);

const int W = 240;
const int H = 135;
const int VIEW_Y = 18;                       // debajo de la barra de estado
const int BOTTOM_Y = 122;                    // barra inferior de ayuda
const int VIEW_H = BOTTOM_Y - VIEW_Y - 1;    // 103 px de contenido
const int ROW_H = 17;
const int ROWS = VIEW_H / ROW_H;             // 6 filas

// Verdes "Matrix" (todos representables exactos en RGB332).
const uint32_t C_BLACK = 0x000000U;
const uint32_t C_GREEN = 0x00FF41U;  // verde principal
const uint32_t C_SOFT = 0x00DB00U;   // texto de lectura
const uint32_t C_MID = 0x00B600U;    // texto secundario
const uint32_t C_READ = 0x009200U;   // items ya leidos
const uint32_t C_DIM = 0x006D00U;    // ayudas
const uint32_t C_DARK = 0x004900U;   // lineas y fondos apagados
const uint32_t C_RED = 0xFF2400U;
const uint32_t C_DKRED = 0x6D0000U;
const uint32_t C_AMBER = 0xFFDB00U;
const uint32_t C_WHITE = 0xFFFFFFU;

enum FontId : uint8_t { F_SMALL = 0, F_NORMAL, F_BOLD, F_BIG, F_READ, F_COUNT };

const lgfx::IFont* fontOf(FontId f) {
  switch (f) {
    case F_SMALL:
      return &fonts::efontCN_12;
    case F_NORMAL:
      return &fonts::efontCN_14;
    case F_BOLD:
      return &fonts::efontCN_14_b;
    case F_READ:
      return &fonts::efontCN_16;  // texto de lectura
    default:
      return &fonts::efontCN_16_b;
  }
}

uint8_t g_asciiW[F_COUNT][96];
bool g_asciiReady[F_COUNT];

int cpWidth(FontId f, uint32_t cp) {
  if (cp >= 0x20 && cp < 0x80) {
    if (!g_asciiReady[f]) {
      for (uint32_t c = 0x20; c < 0x80; c++) {
        lgfx::FontMetrics m;
        fontOf(f)->updateFontMetric(&m, (uint16_t)c);
        g_asciiW[f][c - 0x20] = (uint8_t)m.x_advance;
      }
      g_asciiReady[f] = true;
    }
    return g_asciiW[f][cp - 0x20];
  }
  lgfx::FontMetrics m;
  fontOf(f)->updateFontMetric(&m, (uint16_t)(cp > 0xFFFF ? '?' : cp));
  return m.x_advance;
}

int wrapWidthCb(void* ctx, uint32_t cp) { return cpWidth((FontId)(intptr_t)ctx, cp); }

int strWidth(const std::string& s, FontId f) {
  int w = 0;
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp;
    size_t n = txt::utf8Decode(s.data() + i, s.size() - i, cp);
    if (!n) n = 1;
    w += cpWidth(f, cp);
    i += n;
  }
  return w;
}

// Recorta a maxW pixeles, con puntos suspensivos si no entra.
std::string fit(const std::string& s, int maxW, FontId f) {
  const int ell = cpWidth(f, 0x2026);
  int w = 0;
  size_t i = 0;
  size_t lastFit = 0;
  while (i < s.size()) {
    uint32_t cp;
    size_t n = txt::utf8Decode(s.data() + i, s.size() - i, cp);
    if (!n) n = 1;
    const int cw = cpWidth(f, cp);
    if (w + cw > maxW) {
      std::string out = s.substr(0, lastFit);
      while (!out.empty() && out[out.size() - 1] == ' ') out.resize(out.size() - 1);
      txt::utf8Append(out, 0x2026);
      return out;
    }
    w += cw;
    i += n;
    if (w + ell <= maxW) lastFit = i;
  }
  return s;
}

void text(int x, int y, const std::string& s, FontId f, uint32_t color, int scale = 1) {
  canvas.setFont(fontOf(f));
  canvas.setTextSize(scale);
  canvas.setTextColor(color);
  canvas.drawString(s.c_str(), x, y);
}

void textRight(int xRight, int y, const std::string& s, FontId f, uint32_t color, int scale = 1) {
  text(xRight - strWidth(s, f) * scale, y, s, f, color, scale);
}

void textCenter(int y, const std::string& s, FontId f, uint32_t color, int scale = 1) {
  text((W - strWidth(s, f) * scale) / 2, y, s, f, color, scale);
}

// ---------------------------------------------------------------------------
// Estado de la interfaz
// ---------------------------------------------------------------------------

enum Screen : uint8_t {
  SC_SPLASH,
  SC_NOSD,
  SC_MENU,
  SC_WAIT,
  SC_FEEDS,
  SC_ITEMS,
  SC_READER,
  SC_QR,
  SC_UPDATE,
  SC_WIFI,
  SC_PASS,
  SC_CONNECT,
  SC_CONFIRM,
  SC_MSG
};

Screen g_scr = SC_SPLASH;
Screen g_afterSplash = SC_MENU;  // a donde se va al pulsar una tecla en el inicio
bool g_started = false;          // la SD y la red ya estan en marcha
bool g_dirty = true;
net::Status g_st;
uint32_t g_lastSeq = 0xFFFFFFFFu;
uint32_t g_lastTick = 0;
uint32_t g_anim = 0;
char g_clock[96] = "";
int g_battery = -1;
uint32_t g_batteryAt = 0;

// menu
int g_menuSel = 0;
// "Actualizando..." antes de entrar a RSS o WiFi Setup
Screen g_waitTarget = SC_MENU;
// fuentes
std::vector<CacheFeed> g_idx;
int g_feedSel = 0;
int g_feedTop = 0;
// items del feed abierto
std::vector<FeedItem> g_items;
std::vector<size_t> g_readOffs;
std::string g_curName;
std::string g_curUrl;
int g_itemSel = 0;
int g_itemTop = 0;
// lectura
enum DocSrc : uint8_t { SRC_TITLE, SRC_META, SRC_BODY, SRC_TAIL };
struct DocLine {
  uint8_t src;
  uint8_t h;  // alto de la linea
  uint32_t start;
  uint16_t len;
  int16_t y;
};
std::vector<DocLine> g_doc;
std::string g_docMeta;
std::string g_docTail;
int g_docHeight = 0;
int g_scrollY = 0;
// resultado del Update
std::vector<net::FeedResult> g_results;
int g_resTop = 0;
// WiFi Setup
struct WifiRow {
  std::string ssid;
  bool saved;
  bool inRange;
  bool open;
  int8_t rssi;
};
std::vector<WifiRow> g_wifiRows;
int g_wifiSel = 0;
int g_wifiTop = 0;
uint32_t g_scanSeqSeen = 0;
std::string g_passSsid;
std::string g_passInput;
std::string g_passError;
std::string g_connSsid;
std::string g_connPass;
bool g_connSecured = false;
bool g_connHandled = false;
std::string g_confirmSsid;
// mensaje simple
std::string g_msgTitle;
std::string g_msgBody;
Screen g_msgNext = SC_MENU;

const int kTitleH = 18;
const int kMetaH = 14;
const int kBodyH = 17;
const int kParaGap = 5;
const int kTextW = 228;  // ancho de texto en la vista de lectura

void go(Screen s) {
  g_scr = s;
  g_dirty = true;
  // en las pantallas de WiFi Setup la radio tiene que estar encendida
  net::holdWifi(s == SC_WIFI || s == SC_PASS || s == SC_CONNECT || s == SC_CONFIRM);
}

void message(const char* title, const char* body, Screen next) {
  g_msgTitle = title;
  g_msgBody = body;
  g_msgNext = next;
  go(SC_MSG);
}

const CacheFeed* infoFor(const std::string& url) {
  for (size_t i = 0; i < g_idx.size(); i++) {
    if (g_idx[i].url == url) return &g_idx[i];
  }
  return nullptr;
}

int totalUnread() {
  int n = 0;
  const std::vector<FeedDef>& defs = app::feeds();
  for (size_t i = 0; i < defs.size(); i++) {
    const CacheFeed* f = infoFor(defs[i].url);
    if (f) n += f->unread;
  }
  return n;
}

bool anyItems() {
  const std::vector<FeedDef>& defs = app::feeds();
  for (size_t i = 0; i < defs.size(); i++) {
    const CacheFeed* f = infoFor(defs[i].url);
    if (f && f->count > 0) return true;
  }
  return false;
}

std::string agoText(int64_t when) {
  if (!timeValid() || when <= 0) return std::string();
  const std::string age = dt::formatAge((int64_t)time(nullptr), when);
  if (age.empty()) return age;
  if (age == "ahora") return "recién";
  return "hace " + age;
}

std::string dateText(int64_t when) {
  if (when <= 0) return std::string();
  const time_t t = (time_t)when;
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[72];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour,
           tm.tm_min);
  return buf;
}

// ---------------------------------------------------------------------------
// Transiciones
// ---------------------------------------------------------------------------

// Los items del feed abierto ocupan decenas de KB: se sueltan al salir para
// que la proxima actualizacion tenga toda la memoria disponible.
void releaseItems() {
  std::vector<FeedItem>().swap(g_items);
  std::vector<size_t>().swap(g_readOffs);
  std::vector<DocLine>().swap(g_doc);
}

void enterFeeds() {
  releaseItems();
  net::getIndex(g_idx);
  const int n = (int)app::feeds().size();
  if (g_feedSel >= n) g_feedSel = n > 0 ? n - 1 : 0;
  go(SC_FEEDS);
}

void rebuildWifiRows() {
  std::vector<net::ScanNet> scan;
  net::getScan(scan);
  const std::vector<WifiNet>& known = app::known();
  std::string selSsid;
  if (g_wifiSel >= 0 && g_wifiSel < (int)g_wifiRows.size()) selSsid = g_wifiRows[g_wifiSel].ssid;
  g_wifiRows.clear();
  for (size_t i = 0; i < known.size(); i++) {
    WifiRow r;
    r.ssid = known[i].ssid;
    r.saved = true;
    r.inRange = false;
    r.open = known[i].pass.empty();
    r.rssi = -100;
    for (size_t k = 0; k < scan.size(); k++) {
      if (scan[k].ssid == r.ssid) {
        r.inRange = true;
        r.rssi = scan[k].rssi;
        r.open = scan[k].open;
      }
    }
    g_wifiRows.push_back(r);
  }
  for (size_t k = 0; k < scan.size(); k++) {
    bool isSaved = false;
    for (size_t i = 0; i < known.size(); i++) {
      if (known[i].ssid == scan[k].ssid) isSaved = true;
    }
    if (isSaved) continue;
    WifiRow r;
    r.ssid = scan[k].ssid;
    r.saved = false;
    r.inRange = true;
    r.open = scan[k].open;
    r.rssi = scan[k].rssi;
    g_wifiRows.push_back(r);
  }
  // la seleccion sigue a la misma red aunque cambie el orden
  int sel = 0;
  for (size_t i = 0; i < g_wifiRows.size(); i++) {
    if (!selSsid.empty() && g_wifiRows[i].ssid == selSsid) sel = (int)i;
  }
  g_wifiSel = sel;
  if (g_wifiSel < g_wifiTop) g_wifiTop = g_wifiSel;
  if (g_wifiSel >= g_wifiTop + ROWS) g_wifiTop = g_wifiSel - ROWS + 1;
  if (g_wifiTop < 0) g_wifiTop = 0;
}

void enterWifi(bool rescan) {
  rebuildWifiRows();
  if (rescan) net::requestScan();
  go(SC_WIFI);
}

void enterItems(int feedIdx) {
  const std::vector<FeedDef>& defs = app::feeds();
  if (feedIdx < 0 || feedIdx >= (int)defs.size()) return;
  const CacheFeed* info = infoFor(defs[feedIdx].url);
  if (!info || info->count == 0) {
    const bool failed = info && !info->status.empty() && info->status != "ok";
    const std::string body = failed ? "La última descarga falló: " + info->status + "."
                                    : std::string("Todavía no hay contenido guardado. Probá con Update.");
    message(defs[feedIdx].name.c_str(), body.c_str(), SC_FEEDS);
    return;
  }
  CacheFeed loaded;
  g_items.clear();
  g_readOffs.clear();
  if (!store::readCacheFeed(info->offset, loaded, &g_items, &g_readOffs, true) || g_items.empty()) {
    message(defs[feedIdx].name.c_str(), "No se pudo leer cache.json.", SC_FEEDS);
    return;
  }
  g_curName = defs[feedIdx].name;
  g_curUrl = defs[feedIdx].url;
  g_itemSel = 0;
  g_itemTop = 0;
  for (size_t i = 0; i < g_items.size(); i++) {  // arrancar en el primero sin leer
    if (!g_items[i].read) {
      g_itemSel = (int)i;
      break;
    }
  }
  go(SC_ITEMS);
}

void addDocLines(uint8_t src, const std::string& s, FontId f, int lineH, int& y, bool paraGaps) {
  std::vector<WrapLine> lines;
  wrapText(s, kTextW, wrapWidthCb, (void*)(intptr_t)f, lines, 600);
  for (size_t i = 0; i < lines.size(); i++) {
    DocLine d;
    d.src = src;
    d.h = (uint8_t)lineH;
    d.start = lines[i].start;
    d.len = lines[i].len;
    d.y = (int16_t)y;
    g_doc.push_back(d);
    y += lineH;
    if (paraGaps && lines[i].paragraphEnd) y += kParaGap;
  }
}

void buildDoc() {
  const FeedItem& it = g_items[g_itemSel];
  g_doc.clear();
  g_docMeta = g_curName;
  const std::string when = dateText(it.date);
  if (!when.empty()) g_docMeta += " · " + when;
  g_docMeta = fit(g_docMeta, kTextW, F_SMALL);
  if (it.text.empty()) g_docTail = "(el feed no trae texto para este ítem)";
  else g_docTail.clear();
  if (!it.link.empty()) {
    if (!g_docTail.empty()) g_docTail += "\n";
    g_docTail += "Q: QR para abrir la nota completa";
  }
  int y = 1;
  addDocLines(SRC_TITLE, it.title, F_BIG, kTitleH, y, false);
  y += 1;
  addDocLines(SRC_META, g_docMeta, F_SMALL, kMetaH, y, false);
  y += 4;
  addDocLines(SRC_BODY, it.text, F_READ, kBodyH, y, true);
  if (!g_docTail.empty()) {
    y += 3;
    addDocLines(SRC_TAIL, g_docTail, F_SMALL, kMetaH, y, false);
  }
  g_docHeight = y + 3;
  g_scrollY = g_doc.empty() ? 0 : g_doc[0].y;
}

// El scroll siempre queda alineado al comienzo de una linea.
// Tope: la primera linea desde la que se ve entero el final del documento.
int maxDocScroll() {
  if (g_doc.empty()) return 0;
  const int end = g_doc.back().y + g_doc.back().h;
  for (size_t i = 0; i < g_doc.size(); i++) {
    if (end - g_doc[i].y <= VIEW_H) return g_doc[i].y;
  }
  return g_doc.back().y;
}

void scrollDoc(int lines, bool page) {
  const int maxScroll = maxDocScroll();
  int target = g_scrollY;
  if (page) {
    // avanza una pantalla menos una linea, alineado al comienzo de linea
    const int want = g_scrollY + VIEW_H - kBodyH;
    for (size_t i = 0; i < g_doc.size(); i++) {
      if (g_doc[i].y > g_scrollY && g_doc[i].y <= want) target = g_doc[i].y;
    }
    if (target == g_scrollY) lines = 1;  // parrafo muy largo: al menos una linea
    else lines = 0;
  }
  for (; lines > 0; lines--) {
    for (size_t i = 0; i < g_doc.size(); i++) {
      if (g_doc[i].y > target) {
        target = g_doc[i].y;
        break;
      }
    }
  }
  for (; lines < 0; lines++) {
    int prev = 0;
    for (size_t i = 0; i < g_doc.size(); i++) {
      if (g_doc[i].y < target) prev = g_doc[i].y;
    }
    target = prev;
  }
  if (target > maxScroll) target = maxScroll;
  if (target < 0) target = 0;
  g_scrollY = target;
}

void openItem(int i) {
  if (i < 0 || i >= (int)g_items.size()) return;
  g_itemSel = i;
  FeedItem& it = g_items[i];
  if (!it.read) {
    it.read = true;
    if ((size_t)i < g_readOffs.size()) store::markRead(g_readOffs[i]);
    net::noteRead(g_curUrl);
    for (size_t k = 0; k < g_idx.size(); k++) {
      if (g_idx[k].url == g_curUrl && g_idx[k].unread > 0) g_idx[k].unread--;
    }
  }
  buildDoc();
  go(SC_READER);
}

void startConnect(const std::string& ssid, const std::string& pass, bool secured) {
  g_connSsid = ssid;
  g_connPass = pass;
  g_connSecured = secured;
  g_connHandled = false;
  net::requestConnect(ssid, pass);
  g_st.conn = net::CONN_BUSY;
  go(SC_CONNECT);
}

// La conexion funciono: guardar la red (o corregir su clave) en wifi-config.txt.
void rememberNetwork() {
  std::vector<WifiNet>& known = app::known();
  bool found = false;
  bool changed = false;
  for (size_t i = 0; i < known.size(); i++) {
    if (known[i].ssid == g_connSsid) {
      found = true;
      if (known[i].pass != g_connPass) {
        known[i].pass = g_connPass;
        changed = true;
      }
    }
  }
  if (!found && known.size() < kMaxWifiNets) {
    WifiNet n;
    n.ssid = g_connSsid;
    n.pass = g_connPass;
    known.push_back(n);
    changed = true;
  }
  if (changed) {
    if (!store::saveWifi(known)) logf("[wifi] no se pudo guardar wifi-config.txt");
    net::setKnown(known);
  }
}

void forgetNetwork(const std::string& ssid) {
  std::vector<WifiNet>& known = app::known();
  for (size_t i = 0; i < known.size(); i++) {
    if (known[i].ssid == ssid) {
      known.erase(known.begin() + (long)i);
      break;
    }
  }
  if (!store::saveWifi(known)) logf("[wifi] no se pudo guardar wifi-config.txt");
  net::setKnown(known);
}

// ---------------------------------------------------------------------------
// Dibujo: piezas comunes
// ---------------------------------------------------------------------------

int rssiBars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  return 1;
}

void drawWifiIcon(int x, int y) {
  const int cx = x + 8;
  const int cy = y + 11;
  uint32_t on = C_RED;
  uint32_t off = C_RED;  // sin conexion: todo el icono en rojo
  int lit = 3;
  if (g_st.wifi == net::W_UP) {
    on = C_GREEN;
    off = C_DARK;
    lit = rssiBars(g_st.rssi) - 1;
  } else if (g_st.wifi == net::W_CONNECTING) {
    off = C_DKRED;
    lit = (int)((g_anim / 2) % 4);
  } else if (g_st.wifi == net::W_SLEEP) {
    on = off = C_AMBER;  // radio apagada hasta el proximo Update
  }
  canvas.fillArc(cx, cy, 11, 10, 225, 315, lit >= 3 ? on : off);
  canvas.fillArc(cx, cy, 8, 7, 225, 315, lit >= 2 ? on : off);
  canvas.fillArc(cx, cy, 5, 4, 225, 315, lit >= 1 ? on : off);
  canvas.fillCircle(cx, cy, 1, on);
}

void drawBars(int x, int y, int rssi, uint32_t on, uint32_t off) {
  const int lit = rssiBars(rssi);
  for (int i = 0; i < 4; i++) {
    const int h = 3 + i * 2;
    canvas.fillRect(x + i * 4, y + 9 - h, 3, h, i < lit ? on : off);
  }
}

void drawLock(int x, int y, uint32_t color) {
  canvas.drawRoundRect(x + 1, y, 5, 6, 2, color);
  canvas.fillRect(x, y + 4, 7, 5, color);
}

void drawSpinner(int cx, int cy, uint32_t color) {
  const int a0 = (int)((g_anim * 45) % 360);
  const int a1 = a0 + 240;
  if (a1 <= 360) {
    canvas.fillArc(cx, cy, 5, 3, a0, a1, color);
  } else {
    canvas.fillArc(cx, cy, 5, 3, a0, 360, color);
    canvas.fillArc(cx, cy, 5, 3, 0, a1 - 360, color);
  }
}

void drawStatusBar() {
  drawWifiIcon(3, 2);
  text(25, 2, g_clock, F_SMALL, C_GREEN);
  if (g_st.busy) drawSpinner(150, 8, C_GREEN);

  // bateria
  const int bx = 216;
  uint32_t bc = C_GREEN;
  if (g_battery >= 0 && g_battery <= 10) bc = C_RED;
  else if (g_battery >= 0 && g_battery <= 20) bc = C_AMBER;
  canvas.drawRect(bx, 4, 19, 9, bc);
  canvas.fillRect(bx + 19, 6, 2, 5, bc);
  if (g_battery >= 0) {
    const int fill = (15 * g_battery + 50) / 100;
    if (fill > 0) canvas.fillRect(bx + 2, 6, fill, 5, bc);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", g_battery);
    textRight(bx - 3, 2, buf, F_SMALL, bc);
  }
  canvas.drawFastHLine(0, 16, W, C_DARK);
}

void drawBottom(const std::string& left, const std::string& right) {
  const int rw = strWidth(right, F_SMALL);
  if (!right.empty()) text(W - 4 - rw, BOTTOM_Y, right, F_SMALL, C_DIM);
  if (!left.empty()) text(4, BOTTOM_Y, fit(left, W - 14 - rw, F_SMALL), F_SMALL, C_MID);
}

void drawScrollbar(int total, int visible, int top) {
  if (total <= visible) return;
  const int trackH = VIEW_H;
  int thumbH = trackH * visible / total;
  if (thumbH < 8) thumbH = 8;
  const int maxTop = total - visible;
  const int thumbY = VIEW_Y + (trackH - thumbH) * (top > maxTop ? maxTop : top) / maxTop;
  canvas.fillRect(238, VIEW_Y, 2, trackH, C_DARK);
  canvas.fillRect(238, thumbY, 2, thumbH, C_GREEN);
}

void drawMark(int x, int y, bool ok) {
  if (ok) {
    canvas.drawLine(x, y + 4, x + 2, y + 6, C_GREEN);
    canvas.drawLine(x + 1, y + 4, x + 3, y + 6, C_GREEN);
    canvas.drawLine(x + 2, y + 6, x + 7, y + 1, C_GREEN);
    canvas.drawLine(x + 3, y + 6, x + 8, y + 1, C_GREEN);
  } else {
    canvas.drawLine(x + 1, y + 1, x + 6, y + 6, C_RED);
    canvas.drawLine(x + 2, y + 1, x + 7, y + 6, C_RED);
    canvas.drawLine(x + 6, y + 1, x + 1, y + 6, C_RED);
    canvas.drawLine(x + 7, y + 1, x + 2, y + 6, C_RED);
  }
}

// Texto centrado en varias lineas (mensajes).
void drawParagraph(const std::string& s, int y, FontId f, uint32_t color, int lineH, int maxLines) {
  std::vector<WrapLine> lines;
  wrapText(s, W - 16, wrapWidthCb, (void*)(intptr_t)f, lines, (size_t)maxLines);
  for (size_t i = 0; i < lines.size(); i++) {
    textCenter(y + (int)i * lineH, s.substr(lines[i].start, lines[i].len), f, color);
  }
}

// ---------------------------------------------------------------------------
// Dibujo: pantallas
// ---------------------------------------------------------------------------

void drawSplash() {
  // Pantalla 1: texto grande y centrado, verde Matrix sobre negro.
  // Arriba, el icono de RSS "emitiendo": los arcos se encienden de a uno.
  const int cx = 106, cy = 33;  // esquina inferior izquierda del icono (28 x 28)
  const int phase = (int)((g_anim / 2) % 5);
  canvas.fillArc(cx, cy, 28, 23, 270, 360, phase >= 3 ? C_GREEN : C_DARK);
  canvas.fillArc(cx, cy, 18, 13, 270, 360, phase >= 2 ? C_GREEN : C_DARK);
  canvas.fillCircle(cx + 4, cy - 4, 4, phase >= 1 ? C_GREEN : C_DARK);

  textCenter(40, "RSS Cardputer", F_BIG, C_GREEN, 2);  // 32 px de alto
  textCenter(76, "by Teno", F_BOLD, C_GREEN, 2);       // 28 px: apenas menor
  if (g_started && (g_anim / 4) % 3 != 2) textCenter(119, "Pulsá una tecla", F_SMALL, C_MID);
}

void drawNoSd() {
  textCenter(30, "No encuentro la microSD", F_BOLD, C_AMBER);
  drawParagraph("Insertá una tarjeta (FAT32). Ahí se crea la carpeta RSSreader con wifi-config.txt, "
                "RSS.txt y cache.json.",
                54, F_SMALL, C_MID, 14, 4);
  drawBottom("", "Enter reintentar");
}

void drawMenu() {
  static const char* const kLabels[3] = {"RSS", "Update", "WiFi Setup"};
  for (int i = 0; i < 3; i++) {
    const int y = 21 + i * 37;
    const bool sel = i == g_menuSel;
    if (sel) canvas.fillRoundRect(4, y, 232, 34, 5, C_GREEN);
    text(14, y + 5, kLabels[i], F_SMALL, sel ? C_BLACK : C_GREEN, 2);  // 24 px de alto

    std::string info;
    if (i == 0) {
      const int unread = totalUnread();
      char buf[24];
      if (unread > 0) {
        snprintf(buf, sizeof(buf), "%d sin leer", unread);
        info = buf;
      } else {
        info = anyItems() ? "al día" : "sin datos";
      }
    } else if (i == 1) {
      if (g_st.busy) {
        if (g_st.phase == net::PH_FEEDS && g_st.feedTotal) {
          char buf[24];
          snprintf(buf, sizeof(buf), "feed %u/%u", (unsigned)g_st.feedIdx, (unsigned)g_st.feedTotal);
          info = buf;
        } else {
          info = "en curso…";
        }
      } else if (g_st.outcome == net::UP_NO_WIFI) {
        info = "sin WiFi";
      } else {
        info = agoText(g_st.lastSaved);
      }
    } else {
      if (g_st.wifi == net::W_UP) info = g_st.ssid;
      else if (g_st.wifi == net::W_CONNECTING) info = "conectando…";
      else if (g_st.wifi == net::W_SLEEP) info = "en reposo";
      else info = "sin conexión";
    }
    const int labelW = strWidth(kLabels[i], F_SMALL) * 2;
    const int room = 228 - (14 + labelW + 8);
    if (!info.empty() && room > 20) textRight(228, y + 11, fit(info, room, F_SMALL), F_SMALL, sel ? C_BLACK : C_MID);
  }
}

void drawBusy() {
  // "Actualizando..." con los puntos animados; la palabra no se mueve.
  const int x = (W - strWidth("Actualizando...", F_BOLD) * 2) / 2;
  std::string s = "Actualizando";
  s.append((size_t)((g_anim / 3) % 4), '.');
  text(x, 32, s, F_BOLD, C_GREEN, 2);

  float frac = 0.03f;
  std::string detail;
  char buf[96];
  switch (g_st.phase) {
    case net::PH_WIFI:
      detail = "Conectando a WiFi…";
      break;
    case net::PH_TIME:
      frac = 0.06f;
      detail = "Sincronizando la hora…";
      break;
    case net::PH_FEEDS:
      if (g_st.feedTotal) {
        frac = 0.08f + 0.9f * (float)(g_st.feedIdx > 0 ? g_st.feedIdx - 1 : 0) / (float)g_st.feedTotal;
        snprintf(buf, sizeof(buf), "%u/%u  %s", (unsigned)g_st.feedIdx, (unsigned)g_st.feedTotal, g_st.feedName);
        detail = buf;
      }
      break;
    case net::PH_SAVE:
      frac = 0.98f;
      detail = "Guardando…";
      break;
    default:
      break;
  }
  const int bx = 20, by = 74, bw = 200, bh = 9;
  canvas.drawRect(bx, by, bw, bh, C_MID);
  canvas.fillRect(bx + 2, by + 2, (int)((bw - 4) * frac), bh - 4, C_GREEN);
  if (!detail.empty()) textCenter(90, fit(detail, W - 16, F_SMALL), F_SMALL, C_MID);
  drawBottom("", "Esc volver");
}

void drawUpdate() {
  if (g_st.busy) {
    drawBusy();
    return;
  }
  char buf[64];
  switch (g_st.outcome) {
    case net::UP_DONE: {
      text(6, VIEW_Y + 1, g_st.failCount ? "Actualizado con errores" : "Actualizado", F_BOLD, C_GREEN);
      snprintf(buf, sizeof(buf), "+%u", (unsigned)g_st.newItems);
      textRight(232, VIEW_Y + 2, buf, F_BOLD, C_GREEN);
      const int rows = 6;
      const int y0 = VIEW_Y + 19;
      for (int r = 0; r < rows; r++) {
        const int i = g_resTop + r;
        if (i >= (int)g_results.size()) break;
        const net::FeedResult& fr = g_results[i];
        const int y = y0 + r * 14;
        drawMark(6, y + 2, fr.ok);
        std::string right;
        if (fr.ok) {
          snprintf(buf, sizeof(buf), "+%d", fr.added);
          right = fr.added > 0 ? buf : "";
        } else {
          right = fit(fr.msg, 110, F_SMALL);
        }
        const int rw = strWidth(right, F_SMALL);
        if (!right.empty()) text(232 - rw, y, right, F_SMALL, fr.ok ? C_GREEN : C_AMBER);
        text(20, y, fit(fr.name, 232 - rw - 26, F_SMALL), F_SMALL, fr.ok ? C_SOFT : C_AMBER);
      }
      drawScrollbar((int)g_results.size(), rows, g_resTop);
      snprintf(buf, sizeof(buf), "%u ok · %u con error", (unsigned)g_st.okCount, (unsigned)g_st.failCount);
      drawBottom(buf, "Esc volver");
      break;
    }
    case net::UP_NO_WIFI:
      textCenter(36, "Sin conexión WiFi", F_BOLD, C_AMBER);
      drawParagraph(app::known().empty() ? "No hay redes guardadas. Entrá a WiFi Setup para agregar una."
                                         : "No encontré ninguna red conocida. Revisalas en WiFi Setup.",
                    60, F_SMALL, C_MID, 14, 3);
      drawBottom("", "Esc volver");
      break;
    case net::UP_NO_FEEDS:
      textCenter(36, "No hay fuentes", F_BOLD, C_AMBER);
      drawParagraph("Agregá feeds en RSSreader/RSS.txt de la microSD.", 60, F_SMALL, C_MID, 14, 3);
      drawBottom("", "Esc volver");
      break;
    case net::UP_SD_ERROR:
      textCenter(36, "Error en la microSD", F_BOLD, C_RED);
      drawParagraph("No se pudo escribir cache.json.", 60, F_SMALL, C_MID, 14, 3);
      drawBottom("", "Esc volver");
      break;
    default:
      drawBottom("", "Esc volver");
      break;
  }
}

void drawFeeds() {
  const std::vector<FeedDef>& defs = app::feeds();
  if (defs.empty()) {
    textCenter(40, "No hay fuentes", F_BOLD, C_AMBER);
    drawParagraph("Agregá feeds en RSSreader/RSS.txt de la microSD.", 62, F_SMALL, C_MID, 14, 3);
    drawBottom("", "Esc menú");
    return;
  }
  for (int r = 0; r < ROWS; r++) {
    const int i = g_feedTop + r;
    if (i >= (int)defs.size()) break;
    const int y = VIEW_Y + r * ROW_H;
    const bool sel = i == g_feedSel;
    const CacheFeed* info = infoFor(defs[i].url);
    const int unread = info ? info->unread : 0;
    const bool failed = info && !info->status.empty() && info->status != "ok";
    if (sel) canvas.fillRect(0, y, 236, ROW_H, C_GREEN);

    std::string right;
    char buf[24];
    if (!info || info->count == 0) {
      right = failed ? "error" : "sin datos";
    } else {
      snprintf(buf, sizeof(buf), "%d/%d", unread, info->count);
      right = buf;
    }
    const int rw = strWidth(right, F_SMALL);
    uint32_t rc = sel ? C_BLACK : (unread > 0 ? C_GREEN : C_DIM);
    if (failed && !sel) rc = C_AMBER;
    text(232 - rw, y + 3, right, F_SMALL, rc);
    if (failed) text(232 - rw - 9, y + 3, "!", F_SMALL, sel ? C_BLACK : C_AMBER);

    const uint32_t c = sel ? C_BLACK : (unread > 0 ? C_GREEN : C_READ);
    text(6, y + 1, fit(defs[i].name, 232 - rw - 22, F_NORMAL), F_NORMAL, c);
  }
  drawScrollbar((int)defs.size(), ROWS, g_feedTop);
  const std::string ago = agoText(g_st.lastSaved);
  drawBottom(ago.empty() ? std::string() : "Actualizado " + ago, "Enter abrir");
}

// Lineas del titulo del item seleccionado (hasta 3).
int selectedTitleLines(std::vector<std::string>& out) {
  out.clear();
  const std::string& title = g_items[g_itemSel].title;
  const FontId f = F_NORMAL;
  std::vector<WrapLine> lines;
  wrapText(title, 222, wrapWidthCb, (void*)(intptr_t)f, lines, 4);
  for (size_t i = 0; i < lines.size() && i < 3; i++) {
    if (i == 2 && lines.size() > 3) out.push_back(fit(title.substr(lines[i].start), 222, f));  // no entra entero
    else out.push_back(title.substr(lines[i].start, lines[i].len));
  }
  if (out.empty()) out.push_back(std::string());
  return (int)out.size();
}

void drawItems() {
  if (g_items.empty()) return;
  std::vector<std::string> selLines;
  const int selH = selectedTitleLines(selLines) * 15 + 3;

  // el item seleccionado se despliega: hay que asegurar que entre entero
  if (g_itemSel < g_itemTop) g_itemTop = g_itemSel;
  while (g_itemTop < g_itemSel && (g_itemSel - g_itemTop) * ROW_H + selH > VIEW_H) g_itemTop++;

  int y = VIEW_Y;
  for (int i = g_itemTop; i < (int)g_items.size(); i++) {
    const FeedItem& it = g_items[i];
    const bool sel = i == g_itemSel;
    const int h = sel ? selH : ROW_H;
    if (y + h > VIEW_Y + VIEW_H) break;
    if (sel) {
      canvas.fillRect(0, y, 236, h, C_GREEN);
      for (size_t k = 0; k < selLines.size(); k++) text(10, y + 1 + (int)k * 15, selLines[k], F_NORMAL, C_BLACK);
    } else {
      text(10, y + 1, fit(it.title, 222, F_NORMAL), F_NORMAL, it.read ? C_READ : C_GREEN);
    }
    if (!it.read) canvas.fillRect(3, y + 6, 4, 4, sel ? C_BLACK : C_GREEN);  // marca de no leido
    y += h;
  }
  drawScrollbar((int)g_items.size(), ROWS, g_itemTop);

  char buf[48];
  snprintf(buf, sizeof(buf), "%d/%d", g_itemSel + 1, (int)g_items.size());
  std::string left = buf;
  const std::string ago = agoText(g_items[g_itemSel].date);
  if (!ago.empty()) left += " · " + ago;
  else if (g_items[g_itemSel].date > 0) left += " · " + dateText(g_items[g_itemSel].date).substr(0, 10);
  left += " · " + g_curName;
  drawBottom(left, "Enter leer");
}

void drawReader() {
  const FeedItem& it = g_items[g_itemSel];
  canvas.setClipRect(0, VIEW_Y, W, VIEW_H);
  for (size_t i = 0; i < g_doc.size(); i++) {
    const DocLine& d = g_doc[i];
    const int y = VIEW_Y + d.y - g_scrollY;
    if (y < VIEW_Y) continue;                    // por encima de la ventana
    if (y + d.h > VIEW_Y + VIEW_H) break;        // no se muestran lineas cortadas
    switch (d.src) {
      case SRC_TITLE:
        text(4, y, it.title.substr(d.start, d.len), F_BIG, C_GREEN);
        break;
      case SRC_META:
        text(4, y, g_docMeta.substr(d.start, d.len), F_SMALL, C_MID);
        break;
      case SRC_BODY:
        text(4, y, it.text.substr(d.start, d.len), F_READ, C_SOFT);
        break;
      default:
        text(4, y, g_docTail.substr(d.start, d.len), F_SMALL, C_DIM);
        break;
    }
  }
  canvas.clearClipRect();
  const int maxScroll = maxDocScroll();
  if (maxScroll > 0) {
    int thumbH = VIEW_H * VIEW_H / g_docHeight;
    if (thumbH < 8) thumbH = 8;
    canvas.fillRect(238, VIEW_Y, 2, VIEW_H, C_DARK);
    canvas.fillRect(238, VIEW_Y + (VIEW_H - thumbH) * g_scrollY / maxScroll, 2, thumbH, C_GREEN);
  }
  char buf[40];
  const int pct = maxScroll > 0 ? (g_scrollY * 100 + maxScroll / 2) / maxScroll : 100;
  snprintf(buf, sizeof(buf), "%d/%d · %d%%", g_itemSel + 1, (int)g_items.size(), pct);
  drawBottom(buf, it.link.empty() ? "Esc volver" : "Q: QR · Esc volver");
}

// Dibuja el QR mas chico que contenga el texto. false = no entra legible.
bool drawQrCode(const std::string& data, int x, int y, int size) {
  for (uint8_t ver = 1; ver <= 12; ver++) {
    std::vector<uint8_t> buf(lgfx_qrcode_getBufferSize(ver));
    QRCode qr;
    if (lgfx_qrcode_initText(&qr, buf.data(), ver, ECC_LOW, data.c_str()) != 0) continue;
    const int px = size / (qr.size + 2);  // un modulo de margen blanco por lado
    if (px < 2) return false;
    const int span = qr.size * px;
    const int ox = x + (size - span) / 2;
    const int oy = y + (size - span) / 2;
    canvas.fillRect(x, y, size, size, C_BLACK);  // verde Matrix sobre negro
    for (int yy = 0; yy < qr.size; yy++) {
      for (int xx = 0; xx < qr.size; xx++) {
        if (lgfx_qrcode_getModule(&qr, (uint_fast8_t)xx, (uint_fast8_t)yy)) {
          canvas.fillRect(ox + xx * px, oy + yy * px, px, px, C_GREEN);
        }
      }
    }
    return true;
  }
  return false;
}

void drawQr() {
  const FeedItem& it = g_items[g_itemSel];
  const bool ok = !it.link.empty() && it.link.size() <= 360 && drawQrCode(it.link, 0, 0, H);
  const int px = ok ? 141 : 8;
  const int pw = W - px - 3;
  if (ok) {
    text(px, 4, "Escaneá", F_BOLD, C_GREEN);
    std::vector<WrapLine> lines;
    const std::string hint = "para abrir la nota en el celular";
    wrapText(hint, pw, wrapWidthCb, (void*)(intptr_t)F_SMALL, lines, 3);
    int y = 22;
    for (size_t i = 0; i < lines.size(); i++, y += 13) text(px, y, hint.substr(lines[i].start, lines[i].len), F_SMALL, C_MID);
    y += 5;
    lines.clear();
    wrapText(it.title, pw, wrapWidthCb, (void*)(intptr_t)F_SMALL, lines, 4);
    for (size_t i = 0; i < lines.size() && i < 3; i++, y += 13) {
      const std::string part = (i == 2 && lines.size() > 3) ? fit(it.title.substr(lines[i].start), pw, F_SMALL)
                                                           : it.title.substr(lines[i].start, lines[i].len);
      text(px, y, part, F_SMALL, C_DIM);
    }
    text(px, BOTTOM_Y - 1, "Esc volver", F_SMALL, C_DIM);
  } else {
    textCenter(24, it.link.empty() ? "Este ítem no trae link" : "Link demasiado largo para un QR", F_BOLD, C_AMBER);
    if (!it.link.empty()) {
      std::vector<WrapLine> lines;
      wrapText(it.link, W - 16, wrapWidthCb, (void*)(intptr_t)F_SMALL, lines, 6);
      for (size_t i = 0; i < lines.size(); i++) text(8, 46 + (int)i * 12, it.link.substr(lines[i].start, lines[i].len), F_SMALL, C_MID);
    }
    drawBottom("", "Esc volver");
  }
}

void drawWifi() {
  if (g_wifiRows.empty()) {
    textCenter(52, g_st.scanning ? "Buscando redes…" : "No se encontraron redes", F_NORMAL, C_MID);
    drawBottom("", "R buscar · Esc menú");
    return;
  }
  for (int r = 0; r < ROWS; r++) {
    const int i = g_wifiTop + r;
    if (i >= (int)g_wifiRows.size()) break;
    const WifiRow& row = g_wifiRows[i];
    const int y = VIEW_Y + r * ROW_H;
    const bool sel = i == g_wifiSel;
    const bool connected = g_st.wifi == net::W_UP && row.ssid == g_st.ssid;
    if (sel) canvas.fillRect(0, y, 236, ROW_H, C_GREEN);
    const uint32_t fg = sel ? C_BLACK : (row.saved ? C_GREEN : C_SOFT);
    const uint32_t off = sel ? C_MID : C_DARK;

    // marca: disco lleno = conectada, aro = guardada
    if (connected) canvas.fillCircle(6, y + 8, 3, fg);
    else if (row.saved) canvas.drawCircle(6, y + 8, 3, fg);

    int right = 234;
    if (row.inRange) {
      drawBars(right - 15, y + 3, row.rssi, fg, off);
      right -= 19;
      if (!row.open) {
        drawLock(right - 7, y + 4, fg);
        right -= 11;
      }
    }
    if (row.saved) {
      const char* tag = connected ? "conectada" : (row.inRange ? "guardada" : "guardada, lejos");
      const int tw = strWidth(tag, F_SMALL);
      text(right - tw, y + 3, tag, F_SMALL, sel ? C_BLACK : C_DIM);
      right -= tw + 5;
    }
    text(13, y + 1, fit(row.ssid, right - 15, connected ? F_BOLD : F_NORMAL), connected ? F_BOLD : F_NORMAL, fg);
  }
  drawScrollbar((int)g_wifiRows.size(), ROWS, g_wifiTop);
  const bool selSaved = g_wifiSel < (int)g_wifiRows.size() && g_wifiRows[g_wifiSel].saved;
  if (g_st.scanning) drawBottom("Buscando redes…", "");
  else drawBottom("", selSaved ? "Enter conectar · Del borrar · R buscar" : "Enter conectar · R buscar");
}

void drawPass() {
  text(6, VIEW_Y + 2, "Clave de la red", F_SMALL, C_MID);
  text(6, VIEW_Y + 15, fit(g_passSsid, 228, F_BOLD), F_BOLD, C_GREEN);
  canvas.drawRect(5, 54, 230, 23, C_GREEN);
  // si la clave no entra se muestra el final (lo que se esta tipeando)
  std::string shown = g_passInput;
  while (strWidth(shown, F_NORMAL) > 214 && !shown.empty()) shown.erase(0, 1);
  text(10, 58, shown, F_NORMAL, C_GREEN);
  if ((g_anim / 3) % 2 == 0) canvas.fillRect(11 + strWidth(shown, F_NORMAL), 58, 2, 15, C_GREEN);
  if (!g_passError.empty()) {
    text(6, 82, g_passError, F_SMALL, C_AMBER);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u caracteres", (unsigned)g_passInput.size());
    text(6, 82, buf, F_SMALL, C_DIM);
  }
  drawBottom("", "Enter conectar · Fn+Esc cancelar");
}

void drawConnect() {
  if (g_st.conn == net::CONN_OK) {
    textCenter(34, "Conectado", F_BOLD, C_GREEN, 2);
    textCenter(70, fit(g_connSsid, W - 16, F_NORMAL), F_NORMAL, C_SOFT);
    textCenter(88, "La red quedó guardada en wifi-config.txt", F_SMALL, C_DIM);
    drawBottom("", "Enter continuar");
  } else if (g_st.conn == net::CONN_FAIL) {
    textCenter(36, g_st.connMsg, F_BOLD, C_AMBER);
    textCenter(60, fit(g_connSsid, W - 16, F_NORMAL), F_NORMAL, C_SOFT);
    drawBottom("", g_connSecured ? "Enter reingresar clave · Esc volver" : "Esc volver");
  } else {
    textCenter(40, "Conectando a", F_NORMAL, C_MID);
    textCenter(58, fit(g_connSsid, W - 16, F_BOLD), F_BOLD, C_GREEN);
    drawSpinner(W / 2, 92, C_GREEN);
  }
}

void drawConfirm() {
  textCenter(34, "¿Borrar la red guardada?", F_BOLD, C_AMBER);
  textCenter(58, fit(g_confirmSsid, W - 16, F_NORMAL), F_NORMAL, C_GREEN);
  textCenter(80, "Se quita de wifi-config.txt", F_SMALL, C_DIM);
  drawBottom("", "Enter borrar · Esc cancelar");
}

void drawMsg() {
  textCenter(32, fit(g_msgTitle, W - 16, F_BOLD), F_BOLD, C_GREEN);
  drawParagraph(g_msgBody, 56, F_SMALL, C_MID, 14, 4);
  drawBottom("", "Enter continuar");
}

void draw() {
  canvas.fillScreen(C_BLACK);
  if (g_scr != SC_SPLASH && g_scr != SC_QR) drawStatusBar();
  switch (g_scr) {
    case SC_SPLASH:
      drawSplash();
      break;
    case SC_NOSD:
      drawNoSd();
      break;
    case SC_MENU:
      drawMenu();
      break;
    case SC_WAIT:
      drawBusy();
      break;
    case SC_FEEDS:
      drawFeeds();
      break;
    case SC_ITEMS:
      drawItems();
      break;
    case SC_READER:
      drawReader();
      break;
    case SC_QR:
      drawQr();
      break;
    case SC_UPDATE:
      drawUpdate();
      break;
    case SC_WIFI:
      drawWifi();
      break;
    case SC_PASS:
      drawPass();
      break;
    case SC_CONNECT:
      drawConnect();
      break;
    case SC_CONFIRM:
      drawConfirm();
      break;
    case SC_MSG:
      drawMsg();
      break;
  }
  canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Teclas
// ---------------------------------------------------------------------------

void moveSel(int& sel, int& top, int count, int delta, int visible) {
  if (count <= 0) return;
  sel += delta;
  if (sel < 0) sel = 0;
  if (sel >= count) sel = count - 1;
  if (sel < top) top = sel;
  if (sel >= top + visible) top = sel - visible + 1;
}

void keyMenu(const input::Event& ev) {
  switch (input::navOf(ev)) {
    case input::N_UP:
      if (g_menuSel > 0) g_menuSel--;
      break;
    case input::N_DOWN:
      if (g_menuSel < 2) g_menuSel++;
      break;
    case input::N_OK:
    case input::N_RIGHT:
      if (ev.repeat) break;
      if (g_menuSel == 0) {
        if (g_st.busy) {
          g_waitTarget = SC_FEEDS;
          go(SC_WAIT);
        } else {
          enterFeeds();
        }
      } else if (g_menuSel == 1) {
        net::requestUpdate();  // si ya hay uno en curso, se muestra ese
        net::getStatus(g_st);
        g_resTop = 0;
        go(SC_UPDATE);
      } else {
        if (g_st.busy) {
          g_waitTarget = SC_WIFI;
          go(SC_WAIT);
        } else {
          enterWifi(true);
        }
      }
      break;
    default:
      break;
  }
}

void keyFeeds(const input::Event& ev) {
  const int n = (int)app::feeds().size();
  switch (input::navOf(ev)) {
    case input::N_UP:
      moveSel(g_feedSel, g_feedTop, n, -1, ROWS);
      break;
    case input::N_DOWN:
      moveSel(g_feedSel, g_feedTop, n, +1, ROWS);
      break;
    case input::N_OK:
    case input::N_RIGHT:
      if (!ev.repeat) enterItems(g_feedSel);
      break;
    case input::N_BACK:
    case input::N_DEL:
    case input::N_LEFT:
      if (!ev.repeat) go(SC_MENU);
      break;
    default:
      break;
  }
}

void keyItems(const input::Event& ev) {
  const int n = (int)g_items.size();
  switch (input::navOf(ev)) {
    case input::N_UP:
      if (g_itemSel > 0) g_itemSel--;
      break;
    case input::N_DOWN:
      if (g_itemSel < n - 1) g_itemSel++;
      break;
    case input::N_OK:
    case input::N_RIGHT:
      if (!ev.repeat) openItem(g_itemSel);
      break;
    case input::N_BACK:
    case input::N_DEL:
    case input::N_LEFT:
      if (!ev.repeat) enterFeeds();
      break;
    default:
      break;
  }
}

void keyReader(const input::Event& ev) {
  const input::Nav nav = input::navOf(ev);
  if (ev.kind == input::K_CHAR && (ev.base == 'q' || ev.base == 'Q')) {
    if (!ev.repeat) go(SC_QR);
    return;
  }
  if (nav == input::N_UP) {
    scrollDoc(-1, false);
  } else if (nav == input::N_DOWN) {
    scrollDoc(+1, false);
  } else if (nav == input::N_OK || (ev.kind == input::K_CHAR && ev.base == ' ')) {
    scrollDoc(0, true);  // pagina siguiente
  } else if (nav == input::N_LEFT) {
    if (!ev.repeat && g_itemSel > 0) openItem(g_itemSel - 1);
    return;
  } else if (nav == input::N_RIGHT) {
    if (!ev.repeat && g_itemSel < (int)g_items.size() - 1) openItem(g_itemSel + 1);
    return;
  } else if (nav == input::N_BACK || nav == input::N_DEL) {
    go(SC_ITEMS);
    return;
  }
}

void keyWifi(const input::Event& ev) {
  const int n = (int)g_wifiRows.size();
  if (ev.kind == input::K_CHAR && (ev.base == 'r' || ev.base == 'R')) {
    if (!ev.repeat) net::requestScan();
    return;
  }
  switch (input::navOf(ev)) {
    case input::N_UP:
      moveSel(g_wifiSel, g_wifiTop, n, -1, ROWS);
      break;
    case input::N_DOWN:
      moveSel(g_wifiSel, g_wifiTop, n, +1, ROWS);
      break;
    case input::N_OK:
    case input::N_RIGHT: {
      if (ev.repeat || g_wifiSel >= n) break;
      const WifiRow& row = g_wifiRows[g_wifiSel];
      if (row.saved) {
        std::string pass;
        const std::vector<WifiNet>& known = app::known();
        for (size_t i = 0; i < known.size(); i++) {
          if (known[i].ssid == row.ssid) pass = known[i].pass;
        }
        startConnect(row.ssid, pass, !row.open || !pass.empty());
      } else if (row.open) {
        startConnect(row.ssid, std::string(), false);
      } else {
        g_passSsid = row.ssid;
        g_passInput.clear();
        g_passError.clear();
        go(SC_PASS);
      }
      break;
    }
    case input::N_DEL:
      if (g_wifiSel < n && g_wifiRows[g_wifiSel].saved) {
        g_confirmSsid = g_wifiRows[g_wifiSel].ssid;
        go(SC_CONFIRM);
      }
      break;
    case input::N_BACK:
    case input::N_LEFT:
      if (!ev.repeat) go(SC_MENU);
      break;
    default:
      break;
  }
}

void keyPass(const input::Event& ev) {
  if (ev.kind == input::K_G0 || (ev.kind == input::K_CHAR && ev.base == '`' && ev.fn)) {
    enterWifi(false);  // cancelar
    return;
  }
  if (ev.kind == input::K_ENTER) {
    if (ev.repeat) return;
    if (g_passInput.size() < 8) {
      g_passError = "La clave tiene que tener al menos 8 caracteres";
      return;
    }
    startConnect(g_passSsid, g_passInput, true);
    return;
  }
  if (ev.kind == input::K_BKSP) {
    if (!g_passInput.empty()) g_passInput.resize(g_passInput.size() - 1);
    g_passError.clear();
    return;
  }
  if (ev.kind == input::K_CHAR && ev.ch >= 0x20 && ev.ch < 0x7F && g_passInput.size() < 63) {
    g_passInput.push_back(ev.ch);
    g_passError.clear();
  }
}

void keyConnect(const input::Event& ev) {
  const input::Nav nav = input::navOf(ev);
  if (g_st.conn == net::CONN_OK) {
    if (nav == input::N_OK || nav == input::N_BACK) {
      net::clearConnResult();
      g_st.conn = net::CONN_IDLE;
      // si todavia no se pudo actualizar en esta sesion, se hace ahora
      if (!g_st.busy && g_st.outcome != net::UP_DONE) {
        net::requestUpdate();
        net::getStatus(g_st);
      }
      go(SC_MENU);
    }
  } else if (g_st.conn == net::CONN_FAIL) {
    if (nav == input::N_OK && g_connSecured) {
      net::clearConnResult();
      g_st.conn = net::CONN_IDLE;
      g_passSsid = g_connSsid;
      g_passInput.clear();
      g_passError.clear();
      go(SC_PASS);
    } else if (nav == input::N_BACK || nav == input::N_OK) {
      net::clearConnResult();
      g_st.conn = net::CONN_IDLE;
      enterWifi(false);
    }
  }
}

void handleKey(const input::Event& ev) {
  const input::Nav nav = input::navOf(ev);
  switch (g_scr) {
    case SC_SPLASH:
      break;
    case SC_NOSD:
      if (nav == input::N_OK) {
        if (app::startStorageAndNet()) start(true);
      }
      break;
    case SC_MENU:
      keyMenu(ev);
      break;
    case SC_WAIT:
      if (nav == input::N_BACK || nav == input::N_LEFT) go(SC_MENU);
      break;
    case SC_FEEDS:
      keyFeeds(ev);
      break;
    case SC_ITEMS:
      keyItems(ev);
      break;
    case SC_READER:
      keyReader(ev);
      break;
    case SC_QR:
      if (!ev.repeat && (nav == input::N_BACK || nav == input::N_OK || nav == input::N_DEL ||
                         (ev.kind == input::K_CHAR && (ev.base == 'q' || ev.base == 'Q')))) {
        go(SC_READER);
      }
      break;
    case SC_UPDATE:
      if (nav == input::N_UP) {
        if (g_resTop > 0) g_resTop--;
      } else if (nav == input::N_DOWN) {
        if (g_resTop + 6 < (int)g_results.size()) g_resTop++;
      } else if (nav == input::N_BACK || nav == input::N_OK || nav == input::N_LEFT) {
        go(SC_MENU);
      }
      break;
    case SC_WIFI:
      keyWifi(ev);
      break;
    case SC_PASS:
      keyPass(ev);
      break;
    case SC_CONNECT:
      keyConnect(ev);
      break;
    case SC_CONFIRM:
      if (nav == input::N_OK) {
        forgetNetwork(g_confirmSsid);
        enterWifi(false);
      } else if (nav == input::N_BACK) {
        enterWifi(false);
      }
      break;
    case SC_MSG:
      if (nav == input::N_OK || nav == input::N_BACK) {
        if (g_msgNext == SC_FEEDS) enterFeeds();
        else go(g_msgNext);
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// Estado de red, reloj y bateria
// ---------------------------------------------------------------------------

void onStatus(const net::Status& prev) {
  if (prev.busy && !g_st.busy) {
    // termino la secuencia de arranque o un Update
    net::getIndex(g_idx);
    net::getResults(g_results);
    g_resTop = 0;
    if (g_scr == SC_WAIT) {
      if (g_waitTarget == SC_FEEDS) enterFeeds();
      else enterWifi(true);
    }
  }
  if (g_scr == SC_UPDATE && g_st.busy) net::getResults(g_results);
  if (g_st.scanSeq != g_scanSeqSeen) {
    g_scanSeqSeen = g_st.scanSeq;
    if (g_scr == SC_WIFI) rebuildWifiRows();
  }
  if (g_scr == SC_CONNECT && g_st.conn == net::CONN_OK && !g_connHandled) {
    g_connHandled = true;
    rememberNetwork();
  }
}

void updateClock() {
  char buf[sizeof(g_clock)];
  if (timeValid()) {
    static const char* const kDays[7] = {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"};
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(buf, sizeof(buf), "%s %02d/%02d %02d:%02d", kDays[tm.tm_wday % 7], tm.tm_mday, tm.tm_mon + 1, tm.tm_hour,
             tm.tm_min);
  } else {
    strcpy(buf, "--/-- --:--");
  }
  if (strcmp(buf, g_clock) != 0) {
    strcpy(g_clock, buf);
    g_dirty = true;
  }
}

void updateBattery() {
  const uint32_t now = millis();
  if (g_battery >= 0 && now - g_batteryAt < 15000) return;
  g_batteryAt = now;
  int level = (int)M5Cardputer.Power.getBatteryLevel();
  if (level > 100) level = 100;
  if (level != g_battery) {
    g_battery = level;
    g_dirty = true;
  }
}

// Sale de la pantalla de inicio con el estado de red al dia.
void leaveSplash() {
  net::getStatus(g_st);
  g_lastSeq = net::seq();
  net::getIndex(g_idx);
  net::getResults(g_results);
  updateClock();
  updateBattery();
  go(g_afterSplash);
}

bool animated() {
  if (g_st.busy || g_st.wifi == net::W_CONNECTING) return g_scr != SC_SPLASH && g_scr != SC_QR;
  if (g_scr == SC_PASS) return true;
  if (g_scr == SC_CONNECT && g_st.conn == net::CONN_BUSY) return true;
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------

void begin() {
  M5GFX& d = M5Cardputer.Display;
  // Apaisado: segun la version de M5GFX la rotacion inicial ya lo es o no.
  if (d.width() < d.height()) d.setRotation(d.getRotation() ^ 1);
  canvas.setColorDepth(8);
  if (!canvas.createSprite(W, H)) logf("[ui] sin memoria para el lienzo");
  canvas.setTextWrap(false);
  canvas.setTextDatum(textdatum_t::top_left);
  g_scr = SC_SPLASH;
  draw();
}

void start(bool sdOk) {
  g_afterSplash = sdOk ? SC_MENU : SC_NOSD;
  g_started = true;
  if (g_scr != SC_SPLASH) leaveSplash();  // reintento desde "sin microSD"
}

void debugSplash() {
  g_afterSplash = SC_MENU;
  g_scr = SC_SPLASH;
  draw();
}

void loop() {
  if (g_scr == SC_SPLASH) {
    // La pantalla de inicio queda animada hasta que se pulse una tecla; la
    // conexion y la actualizacion ya corren en segundo plano.
    input::Event ev;
    if (g_started && input::next(ev)) {
      while (input::next(ev)) {
      }
      leaveSplash();
      return;
    }
    const uint32_t now = millis();
    if (now - g_lastTick >= 150) {
      g_lastTick = now;
      g_anim++;
      draw();
    }
    return;
  }
  const uint32_t seq = net::seq();
  if (seq != g_lastSeq) {
    g_lastSeq = seq;
    const net::Status prev = g_st;
    net::getStatus(g_st);
    onStatus(prev);
    g_dirty = true;
  }
  input::Event ev;
  while (input::next(ev)) {
    handleKey(ev);
    g_dirty = true;
  }
  const uint32_t now = millis();
  if (now - g_lastTick >= 150) {
    g_lastTick = now;
    g_anim++;
    updateClock();
    updateBattery();
    if (animated()) g_dirty = true;
  }
  if (g_dirty) {
    g_dirty = false;
    draw();
  }
}

void screenshot() {
  static const char kHex[] = "0123456789abcdef";
  const uint8_t* buf = (const uint8_t*)canvas.getBuffer();
  if (!buf) return;
  logLock();
  Serial.setTxTimeoutMs(400);
  Serial.printf("#SHOT %d %d 332\n", W, H);
  char line[2 * W + 1];
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const uint8_t c = buf[y * W + x];
      line[2 * x] = kHex[c >> 4];
      line[2 * x + 1] = kHex[c & 15];
    }
    line[2 * W] = '\n';
    Serial.write((const uint8_t*)line, sizeof(line));
  }
  Serial.print("#END\n");
  Serial.flush();
  Serial.setTxTimeoutMs(60);
  logUnlock();
}

const char* screenName() {
  static const char* const kNames[] = {"splash", "nosd", "menu",  "wait", "feeds",   "items",   "reader",
                                       "qr",     "update", "wifi", "pass", "connect", "confirm", "msg"};
  return kNames[g_scr];
}

}  // namespace ui
