#include "feedparser.h"

#include <cctype>
#include <cstring>

#include "datetime.h"

namespace {

const size_t kMaxTitleChars = 240;
const size_t kMaxTitleBytes = 600;
// Tope en bytes del texto de un item: en escrituras de 3 bytes por caracter
// (chino, japones) 1500 caracteres serian 4,5 KB por item.
const size_t kMaxTextBytes = 3000;
const size_t kMaxLinkBytes = 600;
const size_t kMaxGuidBytes = 400;
const size_t kMaxDateBytes = 64;
const size_t kMaxAttrBytes = 600;
const size_t kMaxPiBytes = 160;

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Reemplaza las entidades XML predefinidas dentro de un valor de atributo.
std::string xmlUnescape(const std::string& in) {
  if (in.find('&') == std::string::npos) return in;
  std::string out;
  out.reserve(in.size());
  size_t i = 0;
  while (i < in.size()) {
    if (in[i] == '&') {
      const size_t semi = in.find(';', i + 1);
      if (semi != std::string::npos && semi - i <= 10) {
        const std::string name = in.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        if (name == "amp") cp = '&';
        else if (name == "lt") cp = '<';
        else if (name == "gt") cp = '>';
        else if (name == "quot") cp = '"';
        else if (name == "apos") cp = '\'';
        else if (!name.empty() && name[0] == '#') cp = txt::numericEntity(name.data(), name.size());
        if (cp) {
          txt::utf8Append(out, cp);
          i = semi + 1;
          continue;
        }
      }
    }
    out.push_back(in[i++]);
  }
  return out;
}

}  // namespace

FeedParser::FeedParser(ItemFn fn, void* ctx, size_t maxTextChars)
    : fn_(fn),
      ctx_(ctx),
      maxTextChars_(maxTextChars),
      latin1_(false),
      declaredEncoding_(false),
      badEncoding_(false),
      bomState_(0),
      st_(X_TEXT),
      nameLen_(0),
      attrNameLen_(0),
      quote_(0),
      entLen_(0),
      bangLen_(0),
      bangKind_(0),
      sub_(0),
      dash_(0),
      rb_(0),
      qmark_(false),
      depth_(0),
      rootKind_(0),
      inItem_(false),
      itemDepth_(0),
      stopped_(false),
      items_(0),
      cap_(CAP_NONE),
      capDepth_(0),
      capPrio_(0),
      linkIsAlternate_(false),
      datePrio_(0),
      textPrio_(0) {
  date_ = 0;
}

void FeedParser::applyEncoding(const std::string& encIn) {
  const std::string enc = txt::toLower(encIn);
  if (enc.find("utf-16") != std::string::npos || enc.find("utf16") != std::string::npos ||
      enc.find("ucs-2") != std::string::npos || enc.find("utf-32") != std::string::npos) {
    badEncoding_ = true;
  } else if (enc.find("8859-1") != std::string::npos || enc.find("latin1") != std::string::npos ||
             enc.find("latin-1") != std::string::npos || enc.find("1252") != std::string::npos) {
    latin1_ = true;  // incluye iso-8859-15: casi identico para nuestros fines
  } else {
    latin1_ = false;  // utf-8, us-ascii y desconocidos se leen como UTF-8
  }
}

void FeedParser::setCharsetHint(const char* charset) {
  if (charset && *charset && !declaredEncoding_) applyEncoding(charset);
}

void FeedParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (stopped_ || badEncoding_) return;
    const uint8_t b = (uint8_t)data[i];
    if (bomState_ < 3) {
      static const uint8_t kBom[3] = {0xEF, 0xBB, 0xBF};
      if (bomState_ == 0 && (b == 0xFF || b == 0xFE)) {  // BOM de UTF-16
        badEncoding_ = true;
        return;
      }
      if (b == kBom[bomState_]) {
        bomBuf_[bomState_++] = b;
        continue;
      }
      const uint8_t held = bomState_;  // no era un BOM: se reprocesa lo retenido
      bomState_ = 3;
      for (uint8_t k = 0; k < held; k++) charIn((char)bomBuf_[k]);
    }
    if (latin1_ && b >= 0x80) {
      std::string u;
      txt::utf8Append(u, txt::cp1252ToUnicode(b));
      for (size_t k = 0; k < u.size(); k++) charIn(u[k]);
    } else {
      charIn((char)b);
    }
  }
}

void FeedParser::finish() {
  // Un item a medio leer (conexion cortada) se descarta: podria estar incompleto.
  cap_ = CAP_NONE;
  inItem_ = false;
}

void FeedParser::pushName(char c) {
  if (nameLen_ < sizeof(name_) - 1) name_[nameLen_++] = c;
}

void FeedParser::normalizeName() {
  name_[nameLen_] = 0;
  for (uint8_t i = 0; i < nameLen_; i++) name_[i] = (char)tolower((unsigned char)name_[i]);
  // los feeds Atom embebidos suelen usar el prefijo atom: (o a10:)
  if (!strncmp(name_, "atom:", 5)) {
    memmove(name_, name_ + 5, (size_t)nameLen_ - 5 + 1);
    nameLen_ = (uint8_t)(nameLen_ - 5);
  } else if (!strncmp(name_, "a10:", 4)) {
    memmove(name_, name_ + 4, (size_t)nameLen_ - 4 + 1);
    nameLen_ = (uint8_t)(nameLen_ - 4);
  }
}

void FeedParser::decodeXmlEntity() {
  ent_[entLen_] = 0;
  uint32_t cp = 0;
  if (ent_[0] == '#') cp = txt::numericEntity(ent_, entLen_);
  else if (!strcmp(ent_, "amp")) cp = '&';
  else if (!strcmp(ent_, "lt")) cp = '<';
  else if (!strcmp(ent_, "gt")) cp = '>';
  else if (!strcmp(ent_, "quot")) cp = '"';
  else if (!strcmp(ent_, "apos")) cp = '\'';
  if (cp) {
    std::string u;
    txt::utf8Append(u, cp);
    for (size_t i = 0; i < u.size(); i++) textChar(u[i]);
  } else {
    // entidad HTML sin declarar (&nbsp;, &aacute;...): la resuelve HtmlToText
    textChar('&');
    for (uint8_t i = 0; i < entLen_; i++) textChar(ent_[i]);
    textChar(';');
  }
}

void FeedParser::attrDone() {
  attrName_[attrNameLen_] = 0;
  for (uint8_t i = 0; i < attrNameLen_; i++) attrName_[i] = (char)tolower((unsigned char)attrName_[i]);
  if (!strcmp(attrName_, "href")) {
    attrHref_ = txt::cleanLine(xmlUnescape(attrVal_), kMaxLinkBytes);
  } else if (!strcmp(attrName_, "rel")) {
    attrRel_ = txt::toLower(txt::trim(attrVal_));
  }
  attrVal_.clear();
}

void FeedParser::charIn(char c) {
  switch (st_) {
    case X_TEXT:
      if (c == '<') {
        st_ = X_LT;
      } else if (c == '&') {
        st_ = X_ENT;
        entLen_ = 0;
      } else {
        textChar(c);
      }
      break;

    case X_ENT:
      if (c == ';') {
        st_ = X_TEXT;
        decodeXmlEntity();
      } else if (entLen_ < sizeof(ent_) - 1 && (isalnum((unsigned char)c) || c == '#')) {
        ent_[entLen_++] = c;
      } else {
        st_ = X_TEXT;  // '&' suelto (XML invalido, pero pasa): se deja como texto
        textChar('&');
        for (uint8_t i = 0; i < entLen_; i++) textChar(ent_[i]);
        charIn(c);
      }
      break;

    case X_LT:
      if (c == '/') {
        st_ = X_ENDNAME;
        nameLen_ = 0;
      } else if (c == '!') {
        st_ = X_BANG;
        bangLen_ = 0;
        bangKind_ = 0;
      } else if (c == '?') {
        st_ = X_PI;
        pi_.clear();
        qmark_ = false;
      } else if (isalpha((unsigned char)c) || c == '_' || c == ':' || (uint8_t)c >= 0x80) {
        st_ = X_NAME;
        nameLen_ = 0;
        pushName(c);
        attrHref_.clear();
        attrRel_.clear();
      } else {
        st_ = X_TEXT;  // '<' suelto
        textChar('<');
        charIn(c);
      }
      break;

    case X_NAME:
      if (isSpace(c)) {
        st_ = X_ATTRS;
      } else if (c == '/') {
        st_ = X_SELFCLOSE;
      } else if (c == '>') {
        st_ = X_TEXT;
        startTag(false);
      } else {
        pushName(c);
      }
      break;

    case X_ATTRS:
      if (isSpace(c)) {
      } else if (c == '/') {
        st_ = X_SELFCLOSE;
      } else if (c == '>') {
        st_ = X_TEXT;
        startTag(false);
      } else {
        st_ = X_ATTRNAME;
        attrNameLen_ = 0;
        attrName_[attrNameLen_++] = c;
      }
      break;

    case X_ATTRNAME:
      if (c == '=') {
        st_ = X_ATTRQ;
      } else if (isSpace(c)) {
        st_ = X_ATTREQ;
      } else if (c == '>') {
        st_ = X_TEXT;
        startTag(false);
      } else if (c == '/') {
        st_ = X_SELFCLOSE;
      } else if (attrNameLen_ < sizeof(attrName_) - 1) {
        attrName_[attrNameLen_++] = c;
      }
      break;

    case X_ATTREQ:
      if (c == '=') {
        st_ = X_ATTRQ;
      } else if (isSpace(c)) {
      } else if (c == '>') {
        st_ = X_TEXT;
        startTag(false);
      } else if (c == '/') {
        st_ = X_SELFCLOSE;
      } else {
        st_ = X_ATTRNAME;
        attrNameLen_ = 0;
        attrName_[attrNameLen_++] = c;
      }
      break;

    case X_ATTRQ:
      if (c == '"' || c == '\'') {
        quote_ = c;
        attrVal_.clear();
        st_ = X_ATTRVAL;
      } else if (isSpace(c)) {
      } else if (c == '>') {
        st_ = X_TEXT;
        startTag(false);
      } else {
        quote_ = 0;  // valor sin comillas: invalido en XML, se tolera
        attrVal_.clear();
        attrVal_.push_back(c);
        st_ = X_ATTRVAL;
      }
      break;

    case X_ATTRVAL:
      if (quote_) {
        if (c == quote_) {
          attrDone();
          st_ = X_ATTRS;
        } else if (attrVal_.size() < kMaxAttrBytes) {
          attrVal_.push_back(c);
        }
      } else if (isSpace(c)) {
        attrDone();
        st_ = X_ATTRS;
      } else if (c == '>') {
        attrDone();
        st_ = X_TEXT;
        startTag(false);
      } else if (attrVal_.size() < kMaxAttrBytes) {
        attrVal_.push_back(c);
      }
      break;

    case X_SELFCLOSE:
      if (c == '>') {
        st_ = X_TEXT;
        startTag(true);
      } else {
        st_ = X_ATTRS;
        charIn(c);
      }
      break;

    case X_ENDNAME:
      if (c == '>') {
        st_ = X_TEXT;
        endTag();
      } else if (!isSpace(c)) {
        pushName(c);
      }
      break;

    case X_BANG:
      if (bangLen_ == 0) {
        if (c == '-') {
          bangKind_ = 1;
          bangLen_ = 1;
        } else if (c == '[') {
          bangKind_ = 2;
          bangLen_ = 1;
        } else {
          st_ = X_DOCTYPE;
          sub_ = 0;
          charIn(c);
        }
      } else if (bangKind_ == 1) {
        if (c == '-') {
          st_ = X_COMMENT;
          dash_ = 0;
        } else {
          st_ = X_DOCTYPE;
          sub_ = 0;
          charIn(c);
        }
      } else {
        static const char kCdata[] = "[CDATA[";
        if (c == kCdata[bangLen_]) {
          if (++bangLen_ == 7) {
            st_ = X_CDATA;
            rb_ = 0;
          }
        } else {
          st_ = X_DOCTYPE;
          sub_ = 1;
          charIn(c);
        }
      }
      break;

    case X_DOCTYPE:
      if (c == '[') {
        if (sub_ < 250) sub_++;
      } else if (c == ']') {
        if (sub_) sub_--;
      } else if (c == '>' && sub_ == 0) {
        st_ = X_TEXT;
      }
      break;

    case X_COMMENT:
      if (c == '-') {
        if (dash_ < 2) dash_++;
      } else if (c == '>' && dash_ >= 2) {
        st_ = X_TEXT;
      } else {
        dash_ = 0;
      }
      break;

    case X_CDATA:
      if (c == ']') {
        if (rb_ < 2) rb_++;
        else textChar(']');
      } else if (c == '>' && rb_ == 2) {
        rb_ = 0;
        st_ = X_TEXT;
      } else {
        while (rb_) {
          textChar(']');
          rb_--;
        }
        textChar(c);
      }
      break;

    case X_PI:
      if (c == '?') {
        if (qmark_ && pi_.size() < kMaxPiBytes) pi_.push_back('?');
        qmark_ = true;
      } else if (c == '>' && qmark_) {
        st_ = X_TEXT;
        piDone();
      } else {
        if (qmark_) {
          if (pi_.size() < kMaxPiBytes) pi_.push_back('?');
          qmark_ = false;
        }
        if (pi_.size() < kMaxPiBytes) pi_.push_back(c);
      }
      break;
  }
}

void FeedParser::piDone() {
  // <?xml version="1.0" encoding="ISO-8859-1"?>
  const std::string low = txt::toLower(pi_);
  pi_.clear();
  if (low.size() < 4 || low.compare(0, 3, "xml") != 0 || !isSpace(low[3])) return;
  size_t k = low.find("encoding");
  if (k == std::string::npos) return;
  k = low.find_first_of("\"'", k);
  if (k == std::string::npos) return;
  const size_t e = low.find(low[k], k + 1);
  if (e == std::string::npos) return;
  applyEncoding(low.substr(k + 1, e - k - 1));
  declaredEncoding_ = true;
}

void FeedParser::textChar(char c) {
  switch (cap_) {
    case CAP_NONE:
      return;
    case CAP_TITLE:
    case CAP_TEXT:
      html_.put(c);
      return;
    case CAP_LINK:
    case CAP_ORIGLINK:
      if (tmp_.size() < kMaxLinkBytes) tmp_.push_back(c);
      return;
    case CAP_GUID:
      if (tmp_.size() < kMaxGuidBytes) tmp_.push_back(c);
      return;
    case CAP_DATE:
      if (tmp_.size() < kMaxDateBytes) tmp_.push_back(c);
      return;
  }
}

void FeedParser::startTag(bool selfClosing) {
  normalizeName();
  depth_++;
  const char* n = name_;
  if (depth_ == 1) {
    if (!strcmp(n, "rss")) rootKind_ = 1;
    else if (!strcmp(n, "feed")) rootKind_ = 2;
    else if (!strcmp(n, "rdf:rdf") || !strcmp(n, "rdf")) rootKind_ = 3;
    else if (!strcmp(n, "html")) {
      rootKind_ = 4;  // una pagina web, no un feed: no vale la pena seguir
      stopped_ = true;
      return;
    }
  }
  if (cap_ != CAP_NONE) {
    // elemento anidado dentro de un campo capturado (contenido XHTML)
    if (captureIsHtml()) {
      html_.put('<');
      html_.write(n);
      html_.put('>');
    }
  } else if (!inItem_) {
    if (!strcmp(n, "item") || !strcmp(n, "entry")) {
      inItem_ = true;
      itemDepth_ = depth_;
      resetItem();
    }
  } else if (depth_ == itemDepth_ + 1) {
    beginField(n);
  } else if (!strcmp(n, "media:description")) {
    beginCapture(CAP_TEXT, 1);  // YouTube: <media:group><media:description>
  }
  if (selfClosing) closeElement(n);
}

void FeedParser::endTag() {
  normalizeName();
  closeElement(name_);
}

void FeedParser::closeElement(const char* n) {
  if (cap_ != CAP_NONE) {
    if (depth_ <= capDepth_) {
      finishCapture();
    } else if (captureIsHtml()) {
      html_.put('<');
      html_.put('/');
      html_.write(n);
      html_.put('>');
    }
  }
  if (inItem_ && depth_ <= itemDepth_) {
    inItem_ = false;
    finishItem();
  }
  if (depth_ > 0) depth_--;
}

void FeedParser::beginField(const char* n) {
  if (!strcmp(n, "title")) {
    beginCapture(CAP_TITLE, 1);
  } else if (!strcmp(n, "link")) {
    if (!attrHref_.empty()) {
      // Atom: <link rel="alternate" href="..."/>
      if (attrRel_.empty() || attrRel_ == "alternate") {
        if (!linkIsAlternate_) {
          link_ = attrHref_;
          linkIsAlternate_ = true;
        }
      } else if (link_.empty() && attrRel_ != "self" && attrRel_ != "edit" && attrRel_ != "replies" &&
                 attrRel_ != "enclosure" && attrRel_ != "hub" && attrRel_ != "license") {
        link_ = attrHref_;
      }
    } else {
      beginCapture(CAP_LINK, 1);  // RSS: <link>https://...</link>
    }
  } else if (!strcmp(n, "feedburner:origlink")) {
    beginCapture(CAP_ORIGLINK, 1);
  } else if (!strcmp(n, "guid") || !strcmp(n, "id")) {
    beginCapture(CAP_GUID, 1);
  } else if (!strcmp(n, "pubdate") || !strcmp(n, "published") || !strcmp(n, "dc:date") || !strcmp(n, "date")) {
    beginCapture(CAP_DATE, 2);
  } else if (!strcmp(n, "updated") || !strcmp(n, "modified") || !strcmp(n, "issued") ||
             !strcmp(n, "dc:modified")) {
    beginCapture(CAP_DATE, 1);
  } else if (!strcmp(n, "content:encoded") || !strcmp(n, "content")) {
    beginCapture(CAP_TEXT, 3);
  } else if (!strcmp(n, "description") || !strcmp(n, "summary")) {
    beginCapture(CAP_TEXT, 2);
  } else if (!strcmp(n, "media:description")) {
    beginCapture(CAP_TEXT, 1);
  }
}

void FeedParser::beginCapture(Capture cap, uint8_t prio) {
  cap_ = cap;
  capDepth_ = depth_;
  capPrio_ = prio;
  tmp_.clear();
  if (cap == CAP_TITLE) {
    html_.begin(&tmp_, kMaxTitleChars, true, kMaxTitleBytes);
  } else if (cap == CAP_TEXT) {
    // si ya hay un texto de mayor prioridad no hace falta guardar este
    html_.begin(&tmp_, prio > textPrio_ ? maxTextChars_ : 0, false, kMaxTextBytes);
  }
}

void FeedParser::finishCapture() {
  switch (cap_) {
    case CAP_TITLE:
      html_.finish();
      if (title_.empty()) title_.swap(tmp_);
      break;
    case CAP_TEXT:
      html_.finish();
      if (!tmp_.empty() && capPrio_ > textPrio_) {
        text_.swap(tmp_);
        textPrio_ = capPrio_;
      }
      break;
    case CAP_LINK:
      if (!linkIsAlternate_) {
        const std::string v = txt::cleanLine(tmp_, kMaxLinkBytes);
        if (!v.empty()) {
          link_ = v;
          linkIsAlternate_ = true;
        }
      }
      break;
    case CAP_ORIGLINK:
      origLink_ = txt::cleanLine(tmp_, kMaxLinkBytes);
      break;
    case CAP_GUID:
      if (guid_.empty()) guid_ = txt::cleanLine(tmp_, kMaxGuidBytes);
      break;
    case CAP_DATE:
      if (capPrio_ > datePrio_) {
        const int64_t d = dt::parseDate(tmp_.c_str());
        if (d > 0) {
          date_ = d;
          datePrio_ = capPrio_;
        }
      }
      break;
    case CAP_NONE:
      break;
  }
  cap_ = CAP_NONE;
  tmp_.clear();
}

void FeedParser::resetItem() {
  title_.clear();
  link_.clear();
  origLink_.clear();
  guid_.clear();
  text_.clear();
  date_ = 0;
  linkIsAlternate_ = false;
  datePrio_ = 0;
  textPrio_ = 0;
  cap_ = CAP_NONE;
}

void FeedParser::finishItem() {
  FeedItem it;
  it.title.swap(title_);
  it.text.swap(text_);
  if (it.title.empty() && !it.text.empty()) {
    // items sin titulo (microblogs): se usa el comienzo del texto
    size_t i = 0, chars = 0;
    while (i < it.text.size() && chars < 80 && it.text[i] != '\n') {
      uint32_t cp;
      const size_t n = txt::utf8Decode(it.text.data() + i, it.text.size() - i, cp);
      i += n ? n : 1;
      chars++;
    }
    it.title = it.text.substr(0, i);
    if (i < it.text.size()) txt::utf8Append(it.title, 0x2026);
  }
  if (it.title.empty()) it.title = "(sin título)";

  std::string link = !origLink_.empty() ? origLink_ : link_;
  if (link.empty() && txt::startsWithNoCase(guid_, "http")) link = guid_;
  const std::string& key = !guid_.empty() ? guid_ : (!link.empty() ? link : it.title);
  it.id = txt::fnv1a(key.data(), key.size());
  it.link.swap(link);
  it.date = date_;
  // las cadenas crecen duplicando capacidad: se devuelve lo que sobra
  it.title.shrink_to_fit();
  it.link.shrink_to_fit();
  it.text.shrink_to_fit();
  items_++;
  if (fn_ && !fn_(ctx_, it)) stopped_ = true;
}
