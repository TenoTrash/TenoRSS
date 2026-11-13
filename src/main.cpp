// TenoRSS - lector RSS para M5Stack Cardputer ADV.
//
//   src/core/   nucleo portable (parser de feeds, cache.json, configuracion);
//               se prueba en PC con test/host/run.sh
//   storage     microSD: /RSSreader/{wifi-config.txt, RSS.txt, cache.json}
//   net         tarea en segundo plano: WiFi, NTP, zona horaria y feeds
//   ui          pantallas y teclas
//   debugcon    consola de depuracion por USB serie
#include <M5Cardputer.h>
#include <stdarg.h>
#include <time.h>

#include "app.h"
#include "core/datetime.h"
#include "debugcon.h"
#include "input.h"
#include "net.h"
#include "storage.h"
#include "ui.h"

// La tarea de la interfaz dibuja, arma el QR y recorre cache.json: los 8 KB
// que da Arduino por defecto quedan justos.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

SemaphoreHandle_t g_logMux = nullptr;
std::vector<FeedDef> g_feeds;
std::vector<WifiNet> g_known;

}  // namespace

void logLock() {
  if (g_logMux) xSemaphoreTakeRecursive(g_logMux, portMAX_DELAY);
}

void logUnlock() {
  if (g_logMux) xSemaphoreGiveRecursive(g_logMux);
}

void logf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  logLock();
  Serial.println(buf);
  logUnlock();
}

bool timeValid() { return time(nullptr) > 1700000000; }

namespace app {

std::vector<FeedDef>& feeds() { return g_feeds; }
std::vector<WifiNet>& known() { return g_known; }

bool startStorageAndNet() {
  if (!store::begin()) return false;
  std::string tzValue;
  store::loadWifi(g_known);
  store::loadFeeds(g_feeds, tzValue);
  CacheHeader hdr;
  std::vector<CacheFeed> index;
  if (!store::readCacheIndex(hdr, index)) logf("[cache] cache.json incompleto: se usa lo que se pudo leer");
  logf("[app] %u redes guardadas, %u fuentes, %u feeds en cache, TZ=\"%s\"", (unsigned)g_known.size(),
       (unsigned)g_feeds.size(), (unsigned)index.size(), tzValue.c_str());
  net::begin(g_known, g_feeds, dt::parseTz(tzValue), hdr, index);
  return true;
}

}  // namespace app

void setup() {
  g_logMux = xSemaphoreCreateRecursiveMutex();
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(60);  // sin nadie leyendo del otro lado, los logs se descartan

  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // el puerto serie ya esta configurado arriba
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  M5Cardputer.begin(cfg, true);

  ui::begin();  // pantalla de inicio
  logf("\n[app] " APP_NAME " " APP_VERSION ", placa %d, heap %u", (int)M5.getBoard(), (unsigned)ESP.getFreeHeap());

  // Con la pantalla de inicio a la vista se monta la SD y arranca la red; la
  // pantalla sigue hasta que se pulse una tecla.
  ui::start(app::startStorageAndNet());
}

void loop() {
  input::poll();
  dbg::poll();
  ui::loop();
  delay(5);
}
