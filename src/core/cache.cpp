#include "cache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

const size_t kMaxName = 200;
const size_t kMaxUrl = 800;
const size_t kMaxStatus = 120;
const size_t kMaxTag = 300;
const size_t kMaxTitle = 1300;
const size_t kMaxLink = 700;
const size_t kMaxText = 12000;

// Lee una cadena; si el valor no es una cadena (null, numero) lo saltea.
bool readStr(JsonReader& r, std::string& out, size_t maxBytes) {
  if (r.peekValue() == '"') return r.readString(out, maxBytes);
  out.clear();
  return r.skipValue();
}

bool hasId(const std::vector<uint32_t>& ids, uint32_t id) {
  for (size_t i = 0; i < ids.size(); i++) {
    if (ids[i] == id) return true;
  }
  return false;
}

// wanted = false si el item no esta en onlyIds (no hay que guardarlo).
bool parseItem(JsonReader& r, FeedItem& it, size_t& readOffset, bool wantStrings, bool wantText,
               const std::vector<uint32_t>* onlyIds, bool& wanted) {
  readOffset = kNoOffset;
  wanted = true;
  bool idKnown = false;
  if (!r.beginObject()) return false;
  std::string key, tmp;
  while (r.nextKey(key)) {
    // "id" es la primera clave: si el item no interesa, sus textos ni se cargan
    const bool skipStrings = !wantStrings || (onlyIds && idKnown && !wanted);
    if (key == "id") {
      if (r.peekValue() == '"') {
        if (!r.readString(tmp, 32)) return false;
        it.id = (uint32_t)strtoul(tmp.c_str(), nullptr, 16);
      } else {
        int64_t v = 0;
        if (!r.readInt(v)) return false;
        it.id = (uint32_t)v;
      }
      idKnown = true;
      if (onlyIds) wanted = hasId(*onlyIds, it.id);
    } else if (key == "read") {
      const size_t off = r.valueOffset();
      const int c = r.peekValue();
      int64_t v = 0;
      if (!r.readInt(v)) return false;
      it.read = v != 0;
      if ((c == '0' || c == '1') && r.offset() == off + 1) readOffset = off;
    } else if (key == "date") {
      int64_t v = 0;
      if (!r.readInt(v)) return false;
      it.date = v;
    } else if (key == "title" && !skipStrings) {
      if (!readStr(r, it.title, kMaxTitle)) return false;
    } else if (key == "link" && !skipStrings) {
      if (!readStr(r, it.link, kMaxLink)) return false;
    } else if (key == "text" && !skipStrings && wantText) {
      if (!readStr(r, it.text, kMaxText)) return false;
    } else if (!r.skipValue()) {
      return false;
    }
  }
  if (onlyIds && !idKnown) wanted = false;
  // las cadenas crecen duplicando capacidad: se devuelve lo que sobra
  it.title.shrink_to_fit();
  it.link.shrink_to_fit();
  it.text.shrink_to_fit();
  return r.ok();
}

bool parseFeed(JsonReader& r, CacheFeed& feed, std::vector<FeedItem>* items, std::vector<size_t>* readOffsets,
               bool withText, const std::vector<uint32_t>* onlyIds) {
  if (!r.beginObject()) return false;
  feed.count = 0;
  feed.unread = 0;
  std::string key;
  while (r.nextKey(key)) {
    if (key == "name") {
      readStr(r, feed.name, kMaxName);
    } else if (key == "url") {
      readStr(r, feed.url, kMaxUrl);
    } else if (key == "status") {
      readStr(r, feed.status, kMaxStatus);
    } else if (key == "etag") {
      readStr(r, feed.etag, kMaxTag);
    } else if (key == "modified") {
      readStr(r, feed.modified, kMaxTag);
    } else if (key == "updated") {
      int64_t v = 0;
      r.readInt(v);
      feed.updated = v;
    } else if (key == "items" && r.peekValue() == '[') {
      r.beginArray();
      while (r.nextElement()) {
        FeedItem it;
        size_t off = kNoOffset;
        bool wanted = true;
        if (!parseItem(r, it, off, items != nullptr, withText, onlyIds, wanted)) return false;
        feed.count++;
        if (!it.read) feed.unread++;
        if (!wanted) continue;
        if (items) items->push_back(std::move(it));
        if (readOffsets) readOffsets->push_back(off);
      }
    } else {
      r.skipValue();
    }
    if (!r.ok()) return false;
  }
  return r.ok();
}

}  // namespace

bool cacheReadIndex(ByteSource& src, CacheHeader& hdr, std::vector<CacheFeed>& feeds) {
  feeds.clear();
  hdr = CacheHeader();
  JsonReader r(src, 0);
  if (!r.beginObject()) return false;
  std::string key;
  while (r.nextKey(key)) {
    if (key == "saved") {
      int64_t v = 0;
      r.readInt(v);
      hdr.saved = v;
    } else if (key == "tz_offset" && r.peekValue() != 'n') {
      int64_t v = 0;
      if (r.readInt(v)) {
        hdr.hasTz = true;
        hdr.tzOffset = (int32_t)v;
      }
    } else if (key == "feeds" && r.peekValue() == '[') {
      r.beginArray();
      while (r.nextElement()) {
        CacheFeed f;
        f.offset = r.valueOffset();
        // Si el archivo esta cortado se conservan los feeds leidos hasta aca.
        if (!parseFeed(r, f, nullptr, nullptr, false, nullptr)) return false;
        feeds.push_back(f);
      }
    } else {
      r.skipValue();
    }
    if (!r.ok()) return false;
  }
  return r.ok();
}

bool cacheReadFeed(ByteSource& src, size_t baseOffset, CacheFeed& feed, std::vector<FeedItem>* items,
                   std::vector<size_t>* readOffsets, bool withText, const std::vector<uint32_t>* onlyIds) {
  JsonReader r(src, baseOffset);
  const bool ok = parseFeed(r, feed, items, readOffsets, withText, onlyIds);
  feed.offset = baseOffset;
  return ok;
}

// ---------------------------------------------------------------------------

bool CacheWriter::Counter::write(const char* data, size_t n) {
  *bytes += n;
  if (*ok && !out->write(data, n)) *ok = false;
  return *ok;
}

CacheWriter::CacheWriter(ByteSink& out) : out_(out), bytes_(0), ok_(true), firstFeed_(true) {
  counter_.out = &out_;
  counter_.bytes = &bytes_;
  counter_.ok = &ok_;
}

void CacheWriter::begin(const CacheHeader& hdr) {
  jsonWriteRaw(counter_, "{\"app\":\"TenoRSS\",\"version\":1,\"saved\":");
  jsonWriteInt(counter_, hdr.saved);
  if (hdr.hasTz) {
    jsonWriteRaw(counter_, ",\"tz_offset\":");
    jsonWriteInt(counter_, hdr.tzOffset);
  }
  jsonWriteRaw(counter_, ",\"feeds\":[\n");
}

size_t CacheWriter::writeFeed(const CacheFeed& f, const std::vector<FeedItem>& items) {
  if (!firstFeed_) jsonWriteRaw(counter_, ",\n");
  firstFeed_ = false;
  const size_t off = bytes_;
  jsonWriteRaw(counter_, "{\"name\":");
  jsonWriteString(counter_, f.name);
  jsonWriteRaw(counter_, ",\"url\":");
  jsonWriteString(counter_, f.url);
  jsonWriteRaw(counter_, ",\"updated\":");
  jsonWriteInt(counter_, f.updated);
  jsonWriteRaw(counter_, ",\"status\":");
  jsonWriteString(counter_, f.status);
  if (!f.etag.empty()) {
    jsonWriteRaw(counter_, ",\"etag\":");
    jsonWriteString(counter_, f.etag);
  }
  if (!f.modified.empty()) {
    jsonWriteRaw(counter_, ",\"modified\":");
    jsonWriteString(counter_, f.modified);
  }
  jsonWriteRaw(counter_, ",\"items\":[\n");
  for (size_t i = 0; i < items.size(); i++) {
    const FeedItem& it = items[i];
    char buf[64];
    // "read" va segundo y ocupa un solo caracter: se puede parchear en el lugar
    snprintf(buf, sizeof(buf), "{\"id\":\"%08x\",\"read\":%d,\"date\":", (unsigned)it.id, it.read ? 1 : 0);
    jsonWriteRaw(counter_, buf);
    jsonWriteInt(counter_, it.date);
    jsonWriteRaw(counter_, ",\"title\":");
    jsonWriteString(counter_, it.title);
    jsonWriteRaw(counter_, ",\"link\":");
    jsonWriteString(counter_, it.link);
    jsonWriteRaw(counter_, ",\"text\":");
    jsonWriteString(counter_, it.text);
    jsonWriteRaw(counter_, i + 1 < items.size() ? "},\n" : "}\n");
  }
  jsonWriteRaw(counter_, "]}");
  return off;
}

void CacheWriter::end() { jsonWriteRaw(counter_, "\n]}\n"); }

// ---------------------------------------------------------------------------

namespace {
bool newerFirst(const FeedItem& a, const FeedItem& b) { return a.date > b.date; }
}  // namespace

int mergeReadState(std::vector<FeedItem>& fresh, const std::vector<FeedItem>& oldLight, size_t maxItems,
                   std::vector<uint32_t>& keepIds) {
  keepIds.clear();
  // repetidos dentro del mismo feed
  for (size_t i = 0; i < fresh.size(); i++) {
    for (size_t j = i + 1; j < fresh.size();) {
      if (fresh[j].id == fresh[i].id) fresh.erase(fresh.begin() + (long)j);
      else j++;
    }
  }
  if (fresh.size() > maxItems) fresh.resize(maxItems);

  int added = 0;
  for (size_t i = 0; i < fresh.size(); i++) {
    bool found = false;
    for (size_t j = 0; j < oldLight.size(); j++) {
      if (oldLight[j].id == fresh[i].id) {
        fresh[i].read = oldLight[j].read;
        found = true;
        break;
      }
    }
    if (!found) {
      fresh[i].read = false;
      added++;
    }
  }
  // los viejos que ya no vienen en el feed se conservan mientras haya lugar
  for (size_t j = 0; j < oldLight.size() && fresh.size() + keepIds.size() < maxItems; j++) {
    bool present = false;
    for (size_t i = 0; i < fresh.size(); i++) {
      if (fresh[i].id == oldLight[j].id) {
        present = true;
        break;
      }
    }
    if (!present && !hasId(keepIds, oldLight[j].id)) keepIds.push_back(oldLight[j].id);
  }
  return added;
}

void mergeAppend(std::vector<FeedItem>& fresh, std::vector<FeedItem>& oldKept, size_t maxItems) {
  for (size_t j = 0; j < oldKept.size() && fresh.size() < maxItems; j++) fresh.push_back(std::move(oldKept[j]));
  oldKept.clear();
  bool allDated = !fresh.empty();
  for (size_t i = 0; i < fresh.size(); i++) {
    if (fresh[i].date <= 0) {
      allDated = false;
      break;
    }
  }
  if (allDated) std::stable_sort(fresh.begin(), fresh.end(), newerFirst);
}

int mergeItems(std::vector<FeedItem>& fresh, const std::vector<FeedItem>& old, size_t maxItems) {
  std::vector<uint32_t> keepIds;
  const int added = mergeReadState(fresh, old, maxItems, keepIds);
  std::vector<FeedItem> kept;
  for (size_t j = 0; j < old.size(); j++) {
    if (hasId(keepIds, old[j].id)) kept.push_back(old[j]);
  }
  mergeAppend(fresh, kept, maxItems);
  return added;
}
