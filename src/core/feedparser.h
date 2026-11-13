// TenoRSS - parser incremental de feeds RSS 2.0, Atom y RDF (RSS 1.0).
//
// Se le entregan los bytes a medida que llegan de la red y avisa por callback
// cada vez que completa un item. Nunca guarda el documento: la memoria que usa
// esta acotada por el item en curso (el Cardputer no tiene PSRAM y hay feeds
// de casi 1 MB).
//
// Codigo portable: no depende de Arduino.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "textutil.h"

struct FeedItem {
  std::string title;
  std::string link;
  std::string text;  // contenido en texto plano
  uint32_t id;       // hash estable (guid, o link, o titulo)
  int64_t date;      // epoch UTC; 0 = sin fecha
  bool read;

  FeedItem() : id(0), date(0), read(false) {}
};

class FeedParser {
 public:
  // Devuelve false para pedir que el parser se detenga (ya hay suficientes).
  typedef bool (*ItemFn)(void* ctx, FeedItem& item);

  FeedParser(ItemFn fn, void* ctx, size_t maxTextChars);

  // Charset del encabezado HTTP; lo pisa la declaracion <?xml encoding=...?>.
  void setCharsetHint(const char* charset);

  void feed(const char* data, size_t len);
  void finish();

  bool stopped() const { return stopped_; }
  int items() const { return items_; }
  // 0 = desconocido, 1 = RSS, 2 = Atom, 3 = RDF, 4 = HTML (no es un feed)
  int rootKind() const { return rootKind_; }
  bool unsupportedEncoding() const { return badEncoding_; }

 private:
  enum XState : uint8_t {
    X_TEXT,
    X_ENT,
    X_LT,
    X_NAME,
    X_ATTRS,
    X_ATTRNAME,
    X_ATTREQ,
    X_ATTRQ,
    X_ATTRVAL,
    X_SELFCLOSE,
    X_ENDNAME,
    X_BANG,
    X_DOCTYPE,
    X_COMMENT,
    X_CDATA,
    X_PI
  };
  enum Capture : uint8_t { CAP_NONE, CAP_TITLE, CAP_LINK, CAP_ORIGLINK, CAP_GUID, CAP_DATE, CAP_TEXT };

  void charIn(char c);
  void textChar(char c);
  void pushName(char c);
  void normalizeName();
  void decodeXmlEntity();
  void attrDone();
  void startTag(bool selfClosing);
  void endTag();
  void closeElement(const char* name);
  void piDone();
  void applyEncoding(const std::string& enc);
  void beginField(const char* name);
  void beginCapture(Capture cap, uint8_t prio);
  void finishCapture();
  void resetItem();
  void finishItem();
  bool captureIsHtml() const { return cap_ == CAP_TITLE || cap_ == CAP_TEXT; }

  ItemFn fn_;
  void* ctx_;
  size_t maxTextChars_;

  // entrada
  bool latin1_;
  bool declaredEncoding_;
  bool badEncoding_;
  uint8_t bomState_;
  uint8_t bomBuf_[3];

  // tokenizador XML
  XState st_;
  char name_[40];
  uint8_t nameLen_;
  char attrName_[24];
  uint8_t attrNameLen_;
  std::string attrVal_;
  char quote_;
  char ent_[12];
  uint8_t entLen_;
  uint8_t bangLen_;
  uint8_t bangKind_;
  uint8_t sub_;
  uint8_t dash_;
  uint8_t rb_;
  bool qmark_;
  std::string pi_;
  // atributos de interes de la etiqueta en curso
  std::string attrHref_;
  std::string attrRel_;

  // estructura del documento
  int depth_;
  int rootKind_;
  bool inItem_;
  int itemDepth_;
  bool stopped_;
  int items_;

  // captura del campo en curso
  Capture cap_;
  int capDepth_;
  uint8_t capPrio_;
  std::string tmp_;
  txt::HtmlToText html_;

  // item en curso
  std::string title_;
  std::string link_;
  std::string origLink_;
  std::string guid_;
  std::string text_;
  int64_t date_;
  bool linkIsAlternate_;
  uint8_t datePrio_;
  uint8_t textPrio_;
};
