// TenoRSS - tarea de red en segundo plano.
//
// Todo lo que toca WiFi/HTTP corre en una tarea propia para que la interfaz
// siga respondiendo: conexion a redes conocidas, hora por NTP, deteccion de
// zona horaria, descarga de feeds y escritura de cache.json. La interfaz le
// manda comandos y lee una copia del estado.
#pragma once

#include <string>
#include <vector>

#include "core/cache.h"
#include "core/conffiles.h"
#include "core/datetime.h"

namespace net {

enum WifiState : uint8_t { W_DOWN = 0, W_CONNECTING = 1, W_UP = 2 };
enum Phase : uint8_t { PH_IDLE = 0, PH_WIFI, PH_TIME, PH_FEEDS, PH_SAVE };
enum ConnState : uint8_t { CONN_IDLE = 0, CONN_BUSY, CONN_OK, CONN_FAIL };
enum UpdateOutcome : uint8_t { UP_NEVER = 0, UP_DONE, UP_NO_WIFI, UP_NO_FEEDS, UP_SD_ERROR };

struct Status {
  // wifi
  WifiState wifi;
  char ssid[33];
  int8_t rssi;
  // tarea en curso: secuencia de arranque o Update
  bool busy;
  Phase phase;
  uint16_t feedIdx;
  uint16_t feedTotal;
  char feedName[64];
  // resultado del ultimo Update
  UpdateOutcome outcome;
  uint16_t okCount;
  uint16_t failCount;
  uint16_t newItems;
  int64_t lastSaved;  // epoch de la ultima actualizacion guardada (0 = nunca / sin hora)
  // WiFi Setup
  bool scanning;
  uint32_t scanSeq;  // cambia cuando hay un escaneo nuevo
  ConnState conn;
  char connMsg[48];
  // cambia con cualquier modificacion del estado
  uint32_t seq;
};

struct FeedResult {
  std::string name;
  bool ok;
  int added;
  std::string msg;
};

struct ScanNet {
  std::string ssid;
  int8_t rssi;
  bool open;
};

// Arranca la tarea y la secuencia inicial: conectar a una red conocida,
// sincronizar la hora y actualizar los feeds.
void begin(const std::vector<WifiNet>& known, const std::vector<FeedDef>& feeds, const dt::TzSpec& tz,
           const CacheHeader& hdr, const std::vector<CacheFeed>& index);
bool started();

void getStatus(Status& out);
uint32_t seq();

// Comandos (no bloquean). requestUpdate marca "busy" en el acto.
void requestUpdate();
void requestScan();
void requestConnect(const std::string& ssid, const std::string& pass);
void clearConnResult();
void setKnown(const std::vector<WifiNet>& known);

void getScan(std::vector<ScanNet>& out);
void getResults(std::vector<FeedResult>& out);
void getIndex(std::vector<CacheFeed>& out);
// La interfaz marco un item como leido: ajusta el contador del indice.
void noteRead(const std::string& url);

// Diagnostico.
uint32_t workerStackFree();

}  // namespace net
