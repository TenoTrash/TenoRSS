// TenoRSS - lector/escritor JSON minimo, por streaming.
//
// cache.json puede pesar cientos de KB y el equipo no tiene PSRAM, asi que no
// se arma ningun arbol en memoria: JsonReader recorre el archivo token a token
// y deja saltear lo que no interesa; ademas informa el offset de cada valor,
// lo que permite marcar un item como leido cambiando un solo byte del archivo.
//
// Codigo portable: no depende de Arduino.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct ByteSource {
  virtual ~ByteSource() {}
  virtual int readByte() = 0;  // -1 = fin de datos
};

struct ByteSink {
  virtual ~ByteSink() {}
  virtual bool write(const char* data, size_t n) = 0;  // false = error de escritura
};

class JsonReader {
 public:
  // baseOffset: posicion dentro del archivo del primer byte que entrega src.
  explicit JsonReader(ByteSource& src, size_t baseOffset = 0);

  bool ok() const { return ok_; }

  bool beginObject();
  // Avanza a la proxima clave del objeto. false = fin del objeto (o error).
  bool nextKey(std::string& key);
  bool beginArray();
  // Avanza al proximo elemento del array. false = fin del array (o error).
  bool nextElement();

  bool readString(std::string& out, size_t maxBytes);
  bool readInt(int64_t& v);  // acepta numeros, true/false/null y "123"
  bool skipValue();

  // Offset en el archivo del proximo byte sin leer.
  size_t offset() const { return base_ + consumed_ - ((has_ && look_ >= 0) ? 1 : 0); }
  // Offset en el archivo del proximo valor (salteando blancos).
  size_t valueOffset();
  // Primer caracter del proximo valor, sin consumirlo (-1 = fin).
  int peekValue();

 private:
  int peek();
  int get();
  void skipWs();
  bool fail();
  bool skipString();
  uint32_t hex4();

  ByteSource& src_;
  size_t base_;
  size_t consumed_;
  int look_;
  bool has_;
  bool ok_;
};

void jsonWriteString(ByteSink& out, const std::string& s);
void jsonWriteRaw(ByteSink& out, const char* s);
void jsonWriteInt(ByteSink& out, int64_t v);
