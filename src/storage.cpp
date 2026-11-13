#include "storage.h"

#include <SD.h>
#include <SPI.h>
#include <errno.h>

#include "app.h"

namespace store {

namespace {

SemaphoreHandle_t g_mutex = nullptr;
bool g_mounted = false;

void ensureMutex() {
  if (!g_mutex) g_mutex = xSemaphoreCreateRecursiveMutex();
}

bool writeRaw(const char* path, const std::string& data) {
  fs::File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  size_t done = 0;
  while (done < data.size()) {
    const size_t chunk = (data.size() - done > 1024) ? 1024 : data.size() - done;
    if (f.write((const uint8_t*)data.data() + done, chunk) != chunk) {
      f.close();
      return false;
    }
    done += chunk;
  }
  f.close();
  return true;
}

void writeEmptyCache() {
  fs::File f = SD.open(cfg::kCacheFile, FILE_WRITE);
  if (!f) return;
  FileSink sink(f);
  CacheWriter w(sink);
  w.begin(CacheHeader());
  w.end();
  sink.flush();
  f.close();
}

}  // namespace

void lock() {
  ensureMutex();
  xSemaphoreTakeRecursive(g_mutex, portMAX_DELAY);
}

void unlock() { xSemaphoreGiveRecursive(g_mutex); }

bool mounted() { return g_mounted; }

bool begin() {
  ensureMutex();
  Guard g;
  g_mounted = false;
  SD.end();
  SPI.begin(cfg::kSdSck, cfg::kSdMiso, cfg::kSdMosi, cfg::kSdCs);
  // algunas tarjetas no levantan a 25 MHz: se reintenta mas despacio
  static const uint32_t kSpeeds[] = {25000000, 10000000, 4000000};
  bool ok = false;
  for (size_t i = 0; i < sizeof(kSpeeds) / sizeof(kSpeeds[0]) && !ok; i++) {
    ok = SD.begin(cfg::kSdCs, SPI, kSpeeds[i]);
    if (!ok) {
      SD.end();
      delay(60);
    }
  }
  if (!ok || SD.cardType() == CARD_NONE) {
    logf("[sd] no se pudo montar la microSD");
    return false;
  }
  if (!SD.exists(cfg::kDir) && !SD.mkdir(cfg::kDir)) {
    logf("[sd] no se pudo crear %s", cfg::kDir);
    return false;
  }
  if (!SD.exists(cfg::kWifiFile)) writeRaw(cfg::kWifiFile, buildWifiConfig(std::vector<WifiNet>()));
  if (!SD.exists(cfg::kFeedsFile)) writeRaw(cfg::kFeedsFile, defaultFeedsConfig());
  // Si se corto la energia entre borrar cache.json y renombrar cache.tmp, el
  // temporal esta completo y se recupera. En cualquier otro caso es descartable.
  if (SD.exists(cfg::kCacheTmp)) {
    if (!SD.exists(cfg::kCacheFile)) SD.rename(cfg::kCacheTmp, cfg::kCacheFile);
    else SD.remove(cfg::kCacheTmp);
  }
  if (!SD.exists(cfg::kCacheFile)) writeEmptyCache();
  g_mounted = true;
  logf("[sd] ok, %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

bool readText(const char* path, std::string& out, size_t maxBytes) {
  out.clear();
  Guard g;
  if (!g_mounted) return false;
  fs::File f = SD.open(path, FILE_READ);
  if (!f) return false;
  uint8_t buf[512];
  while (out.size() < maxBytes) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    out.append((const char*)buf, (size_t)n);
  }
  f.close();
  if (out.size() > maxBytes) out.resize(maxBytes);
  return true;
}

bool writeText(const char* path, const std::string& data) {
  Guard g;
  if (!g_mounted) return false;
  const std::string tmp = std::string(path) + ".tmp";
  if (!writeRaw(tmp.c_str(), data)) {
    SD.remove(tmp.c_str());
    return false;
  }
  if (SD.exists(path)) SD.remove(path);
  return SD.rename(tmp.c_str(), path);
}

void loadWifi(std::vector<WifiNet>& nets) {
  std::string content;
  nets.clear();
  if (readText(cfg::kWifiFile, content, 16 * 1024)) parseWifiConfig(content, nets);
}

bool saveWifi(const std::vector<WifiNet>& nets) { return writeText(cfg::kWifiFile, buildWifiConfig(nets)); }

void loadFeeds(std::vector<FeedDef>& feeds, std::string& tz) {
  std::string content;
  feeds.clear();
  tz.clear();
  if (readText(cfg::kFeedsFile, content, 32 * 1024)) parseFeedsConfig(content, feeds, tz);
}

bool readCacheIndex(CacheHeader& hdr, std::vector<CacheFeed>& feeds) {
  Guard g;
  feeds.clear();
  hdr = CacheHeader();
  if (!g_mounted) return false;
  fs::File f = SD.open(cfg::kCacheFile, FILE_READ);
  if (!f) return false;
  FileSource src(f);
  const bool ok = cacheReadIndex(src, hdr, feeds);
  f.close();
  return ok;
}

bool readCacheFeed(size_t offset, CacheFeed& feed, std::vector<FeedItem>* items, std::vector<size_t>* readOffsets,
                   bool withText, const std::vector<uint32_t>* onlyIds) {
  Guard g;
  if (!g_mounted) return false;
  fs::File f = SD.open(cfg::kCacheFile, FILE_READ);
  if (!f) return false;
  bool ok = false;
  if (f.seek(offset)) {
    FileSource src(f);
    ok = cacheReadFeed(src, offset, feed, items, readOffsets, withText, onlyIds);
  }
  f.close();
  return ok;
}

bool markRead(size_t offset) {
  if (offset == kNoOffset) return false;
  Guard g;
  if (!g_mounted) return false;
  fs::File f = SD.open(cfg::kCacheFile, "r+");
  if (!f) return false;
  bool ok = false;
  if (f.seek(offset)) {
    const int c = f.read();
    if (c == '1') {
      ok = true;
    } else if (c == '0' && f.seek(offset)) {
      ok = f.write((uint8_t)'1') == 1;
    }
  }
  f.close();
  return ok;
}

bool commitCache() {
  Guard g;
  if (!g_mounted) return false;
  if (!SD.exists(cfg::kCacheTmp)) {
    logf("[sd] falta %s", cfg::kCacheTmp);
    return false;
  }
  if (SD.exists(cfg::kCacheFile) && !SD.remove(cfg::kCacheFile)) {
    logf("[sd] no se pudo borrar %s (errno %d)", cfg::kCacheFile, errno);
    return false;
  }
  if (!SD.rename(cfg::kCacheTmp, cfg::kCacheFile)) {
    logf("[sd] no se pudo renombrar %s (errno %d)", cfg::kCacheTmp, errno);
    return false;
  }
  return true;
}

int FileSource::readByte() {
  if (i_ >= n_) {
    n_ = f_.read(buf_, sizeof(buf_));
    i_ = 0;
    if (n_ <= 0) {
      n_ = 0;
      return -1;
    }
  }
  return buf_[i_++];
}

bool FileSink::write(const char* data, size_t n) {
  while (n > 0 && ok_) {
    const size_t room = sizeof(buf_) - n_;
    const size_t take = n < room ? n : room;
    memcpy(buf_ + n_, data, take);
    n_ += take;
    data += take;
    n -= take;
    if (n_ == sizeof(buf_)) flush();
  }
  return ok_;
}

bool FileSink::flush() {
  if (n_ > 0) {
    if (ok_ && f_.write(buf_, n_) != n_) ok_ = false;
    n_ = 0;
  }
  return ok_;
}

}  // namespace store
