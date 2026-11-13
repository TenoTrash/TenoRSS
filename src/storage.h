// TenoRSS - acceso a la microSD: configuracion y cache.
#pragma once

#include <FS.h>

#include <string>
#include <vector>

#include "core/cache.h"
#include "core/conffiles.h"
#include "core/json.h"

namespace store {

// Monta la SD y crea /RSSreader con sus tres archivos si no existen.
bool begin();
bool mounted();

// Exclusion mutua para la SD (la usan la interfaz y la tarea de red).
void lock();
void unlock();
struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
};

bool readText(const char* path, std::string& out, size_t maxBytes);
// Escritura atomica: archivo temporal + rename.
bool writeText(const char* path, const std::string& data);

void loadWifi(std::vector<WifiNet>& nets);
bool saveWifi(const std::vector<WifiNet>& nets);
void loadFeeds(std::vector<FeedDef>& feeds, std::string& tz);

bool readCacheIndex(CacheHeader& hdr, std::vector<CacheFeed>& feeds);
bool readCacheFeed(size_t offset, CacheFeed& feed, std::vector<FeedItem>* items, std::vector<size_t>* readOffsets,
                   bool withText, const std::vector<uint32_t>* onlyIds = nullptr);
// Marca un item como leido cambiando un solo byte de cache.json.
bool markRead(size_t offset);
// Reemplaza cache.json por cache.tmp (ya escrito y cerrado).
bool commitCache();

// Lector con buffer sobre un archivo abierto.
class FileSource : public ByteSource {
 public:
  explicit FileSource(fs::File& f) : f_(f), n_(0), i_(0) {}
  int readByte() override;

 private:
  fs::File& f_;
  uint8_t buf_[512];
  int n_;
  int i_;
};

// Escritor con buffer sobre un archivo abierto.
class FileSink : public ByteSink {
 public:
  explicit FileSink(fs::File& f) : f_(f), n_(0), ok_(true) {}
  bool write(const char* data, size_t n) override;
  bool flush();
  bool ok() const { return ok_; }

 private:
  fs::File& f_;
  uint8_t buf_[512];
  size_t n_;
  bool ok_;
};

}  // namespace store
