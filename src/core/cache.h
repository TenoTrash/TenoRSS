// TenoRSS - formato de cache.json.
//
//   {"app":"TenoRSS","version":1,"saved":<epoch>,"tz_offset":<seg>,"feeds":[
//   {"name":"...","url":"...","updated":<epoch>,"status":"ok","etag":"...","modified":"...","items":[
//   {"id":"9f8a7c21","read":0,"date":<epoch>,"title":"...","link":"...","text":"..."},
//   ...
//   ]},
//   ...
//   ]}
//
// Es JSON valido y legible (un item por linea). Se lee por partes: primero un
// indice liviano (nombre, cantidades y offset de cada feed) y recien al abrir
// un feed se cargan sus items. "read" siempre ocupa un solo caracter (0/1)
// para poder marcar un item como leido reescribiendo un unico byte.
//
// Codigo portable: no depende de Arduino.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "feedparser.h"
#include "json.h"

struct CacheHeader {
  bool hasTz;        // hay un corrimiento horario autodetectado guardado
  int32_t tzOffset;  // segundos respecto de UTC
  int64_t saved;     // cuando se escribio el archivo (0 = sin hora)

  CacheHeader() : hasTz(false), tzOffset(0), saved(0) {}
};

struct CacheFeed {
  std::string name;
  std::string url;
  std::string status;    // "ok" o el error de la ultima descarga
  std::string etag;      // para pedir solo si cambio (If-None-Match)
  std::string modified;  // idem (If-Modified-Since)
  int64_t updated;       // ultima descarga exitosa (epoch UTC, 0 = nunca)
  int count;
  int unread;
  size_t offset;  // posicion del objeto del feed dentro de cache.json

  CacheFeed() : updated(0), count(0), unread(0), offset(0) {}
};

static const size_t kNoOffset = (size_t)-1;

// Lee el encabezado y el indice de feeds (sin cargar los items).
bool cacheReadIndex(ByteSource& src, CacheHeader& hdr, std::vector<CacheFeed>& feeds);

// Lee un feed completo. src debe entregar bytes desde el '{' del feed y
// baseOffset ser la posicion de ese byte en el archivo.
//  - items: recibe los items (con o sin el texto, segun withText)
//  - readOffsets: recibe la posicion del valor "read" de cada item, o
//    kNoOffset si no ocupa un unico caracter (archivo editado a mano)
//  - onlyIds: si se pasa, solo se cargan los items con esos ids (el resto se
//    saltea sin ocupar memoria)
bool cacheReadFeed(ByteSource& src, size_t baseOffset, CacheFeed& feed, std::vector<FeedItem>* items,
                   std::vector<size_t>* readOffsets, bool withText, const std::vector<uint32_t>* onlyIds = nullptr);

class CacheWriter {
 public:
  explicit CacheWriter(ByteSink& out);
  void begin(const CacheHeader& hdr);
  // Escribe un feed y devuelve el offset donde quedo su objeto.
  size_t writeFeed(const CacheFeed& feed, const std::vector<FeedItem>& items);
  void end();
  bool ok() const { return ok_; }
  size_t bytes() const { return bytes_; }

 private:
  struct Counter : ByteSink {
    ByteSink* out;
    size_t* bytes;
    bool* ok;
    bool write(const char* data, size_t n) override;
  };

  ByteSink& out_;
  Counter counter_;
  size_t bytes_;
  bool ok_;
  bool firstFeed_;
};

// Combinacion de los items recien descargados (fresh) con los del cache, en
// dos pasos para no tener nunca mas de maxItems textos en memoria:
//
// 1) mergeReadState: quita repetidos de fresh, copia el estado leido/no leido
//    de los que ya estaban (oldLight puede venir sin texto) y deja en keepIds
//    los ids de los items viejos que hay que conservar porque ya no vienen en
//    el feed y todavia hay lugar. Devuelve cuantos items son nuevos de verdad.
int mergeReadState(std::vector<FeedItem>& fresh, const std::vector<FeedItem>& oldLight, size_t maxItems,
                   std::vector<uint32_t>& keepIds);
// 2) mergeAppend: agrega los viejos conservados (ya cargados con texto) y, si
//    todos tienen fecha, ordena del mas nuevo al mas viejo.
void mergeAppend(std::vector<FeedItem>& fresh, std::vector<FeedItem>& oldKept, size_t maxItems);

// Los dos pasos juntos, cuando los items viejos ya estan completos en memoria.
int mergeItems(std::vector<FeedItem>& fresh, const std::vector<FeedItem>& old, size_t maxItems);
