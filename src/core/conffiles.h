// TenoRSS - archivos de configuracion editables a mano (wifi-config.txt y RSS.txt).
// Codigo portable: no depende de Arduino.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct WifiNet {
  std::string ssid;
  std::string pass;
};

struct FeedDef {
  std::string name;
  std::string url;
};

static const size_t kMaxWifiNets = 20;
static const size_t kMaxFeeds = 30;

// wifi-config.txt
//   [wifi 1]
//   SSID="nombre de la red"
//   PASS="clave"
void parseWifiConfig(const std::string& content, std::vector<WifiNet>& nets);
std::string buildWifiConfig(const std::vector<WifiNet>& nets);

// RSS.txt
//   TZ="auto"
//   [Nombre del feed]
//   URL="https://..."
void parseFeedsConfig(const std::string& content, std::vector<FeedDef>& feeds, std::string& tz);
std::string defaultFeedsConfig();
