// TenoRSS - lector RSS para M5Stack Cardputer ADV.
// Constantes y utilidades compartidas por todos los modulos del equipo.
#pragma once

#include <Arduino.h>

#include <vector>

#include "core/conffiles.h"

#define APP_NAME "TenoRSS"
#define APP_VERSION "1.0.0"

namespace cfg {

// Archivos en la microSD (los tres son editables a mano).
static const char* const kDir = "/RSSreader";
static const char* const kWifiFile = "/RSSreader/wifi-config.txt";
static const char* const kFeedsFile = "/RSSreader/RSS.txt";
static const char* const kCacheFile = "/RSSreader/cache.json";
static const char* const kCacheTmp = "/RSSreader/cache.tmp";

// Limites del cache.
static const size_t kMaxItems = 20;        // items que se guardan por feed
static const size_t kMaxTextChars = 1500;  // caracteres de texto por item

// Zona horaria si TZ="auto" y todavia no se pudo detectar: Argentina (UTC-3).
static const int32_t kDefaultTzOffset = -3 * 3600;

// Red.
static const char* const kUserAgent = "Mozilla/5.0 (compatible; RSS reader)";
static const char* const kTzUrl = "http://ip-api.com/json/?fields=status,offset";
static const uint32_t kWifiConnectMs = 12000;    // espera por cada red conocida
static const uint32_t kHttpConnectMs = 8000;
static const uint32_t kHttpStallMs = 10000;      // sin recibir datos
static const uint32_t kFeedDeadlineMs = 45000;   // tiempo total por feed
static const size_t kFeedMaxBytes = 3 * 1024 * 1024;

// Pines de la microSD del Cardputer / Cardputer ADV.
static const int kSdSck = 40;
static const int kSdMiso = 39;
static const int kSdMosi = 14;
static const int kSdCs = 12;

}  // namespace cfg

// Log por USB serie, seguro entre tareas. No bloquea si no hay nadie leyendo.
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logLock();
void logUnlock();

// La hora del sistema ya fue sincronizada por NTP.
bool timeValid();

namespace app {

// Monta la microSD, carga wifi-config.txt / RSS.txt / cache.json y arranca la
// tarea de red (que conecta y actualiza en segundo plano). false = sin microSD.
bool startStorageAndNet();

// Datos cargados de la microSD (solo los toca la tarea de la interfaz).
std::vector<FeedDef>& feeds();
std::vector<WifiNet>& known();

}  // namespace app
