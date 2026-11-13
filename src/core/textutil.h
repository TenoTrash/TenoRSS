// TenoRSS - utilidades de texto (UTF-8, entidades HTML, HTML -> texto plano).
// Codigo portable: no depende de Arduino, se prueba tambien en PC.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace txt {

// Decodifica un code point UTF-8 de s[0..len). Devuelve los bytes consumidos
// (>= 1). Una secuencia invalida devuelve cp = 0xFFFD consumiendo 1 byte.
size_t utf8Decode(const char* s, size_t len, uint32_t& cp);
void utf8Append(std::string& out, uint32_t cp);
size_t utf8Count(const std::string& s);
// Recorta a maxBytes sin partir un caracter multibyte.
void utf8Truncate(std::string& s, size_t maxBytes);
// Devuelve solo UTF-8 valido, sin caracteres de control ni espacios al borde.
std::string cleanLine(const std::string& in, size_t maxBytes);

// Windows-1252 (0x80..0x9F) -> Unicode.
uint32_t cp1252ToUnicode(uint8_t b);
// Entidad HTML con nombre ("aacute", "nbsp"...). 0 si no existe.
uint32_t namedEntity(const char* name, size_t len);
// Referencia numerica ("#233", "#xE9"). 0 si es invalida.
uint32_t numericEntity(const char* ref, size_t len);

uint32_t fnv1a(const char* s, size_t n);
std::string trim(const std::string& s);
std::string toLower(const std::string& s);
bool startsWithNoCase(const std::string& s, const char* prefix);

// Conversor incremental de HTML a texto plano.
//  - quita etiquetas, comentarios y el contenido de <script>/<style>
//  - decodifica entidades
//  - convierte etiquetas de bloque y <br> en saltos de linea
//  - colapsa espacios, descarta caracteres invisibles y fuera del BMP
//  - corta en maxChars caracteres o maxBytes bytes, lo que ocurra primero
//    (agrega "..." tipografico si recorto)
class HtmlToText {
 public:
  HtmlToText();
  void begin(std::string* out, size_t maxChars, bool singleLine, size_t maxBytes = (size_t)-1);
  void put(char c);
  void write(const char* s, size_t n);
  void write(const char* s);
  void finish();
  bool full() const { return full_; }

 private:
  enum State : uint8_t { TEXT, LT, TAGNAME, TAGREST, DECL, COMMENT, ENTITY, SKIP };

  void codepoint(uint32_t cp);
  void emit(uint32_t cp);
  void breakLine();
  void tagAction();
  void entityLiteral();

  std::string* out_;
  size_t maxChars_;
  size_t maxBytes_;
  size_t chars_;
  State st_;
  bool singleLine_;
  bool full_;
  bool truncated_;
  bool pendSpace_;
  bool pendNewline_;
  bool pendBullet_;
  // decodificador UTF-8
  uint32_t ucp_;
  uint8_t uneed_;
  // etiqueta en curso
  char tag_[12];
  uint8_t tagLen_;
  bool closing_;
  bool afterEq_;
  char quote_;
  uint8_t declLen_;
  uint8_t dash_;
  // entidad en curso
  char ent_[12];
  uint8_t entLen_;
  // salto de <script>/<style>
  char skipTag_[8];
  uint8_t skipMatch_;
  bool enterSkip_;
};

}  // namespace txt
