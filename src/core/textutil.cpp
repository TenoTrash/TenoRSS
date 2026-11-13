#include "textutil.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace txt {

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

size_t utf8Decode(const char* s, size_t len, uint32_t& cp) {
  if (len == 0) {
    cp = 0;
    return 0;
  }
  const uint8_t b0 = (uint8_t)s[0];
  if (b0 < 0x80) {
    cp = b0;
    return 1;
  }
  size_t need;
  uint32_t min;
  if ((b0 & 0xE0) == 0xC0) {
    need = 1;
    cp = b0 & 0x1F;
    min = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    need = 2;
    cp = b0 & 0x0F;
    min = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    need = 3;
    cp = b0 & 0x07;
    min = 0x10000;
  } else {
    cp = 0xFFFD;
    return 1;
  }
  if (len < need + 1) {
    cp = 0xFFFD;
    return 1;
  }
  for (size_t i = 1; i <= need; i++) {
    const uint8_t b = (uint8_t)s[i];
    if ((b & 0xC0) != 0x80) {
      cp = 0xFFFD;
      return 1;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = 0xFFFD;
    return 1;
  }
  return need + 1;
}

void utf8Append(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back((char)cp);
  } else if (cp < 0x800) {
    out.push_back((char)(0xC0 | (cp >> 6)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back((char)(0xE0 | (cp >> 12)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  } else {
    out.push_back((char)(0xF0 | (cp >> 18)));
    out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  }
}

size_t utf8Count(const std::string& s) {
  size_t n = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
  }
  return n;
}

void utf8Truncate(std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return;
  size_t cut = maxBytes;
  while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;
  s.resize(cut);
}

std::string cleanLine(const std::string& in, size_t maxBytes) {
  std::string out;
  out.reserve(in.size() < maxBytes ? in.size() : maxBytes);
  size_t i = 0;
  while (i < in.size()) {
    uint32_t cp;
    const size_t n = utf8Decode(in.data() + i, in.size() - i, cp);
    i += n ? n : 1;
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0xFFFD || cp == 0xFEFF) continue;
    if (out.empty() && cp == ' ') continue;
    utf8Append(out, cp);
  }
  while (!out.empty() && out[out.size() - 1] == ' ') out.resize(out.size() - 1);
  utf8Truncate(out, maxBytes);
  return out;
}

// ---------------------------------------------------------------------------
// Tablas
// ---------------------------------------------------------------------------

uint32_t cp1252ToUnicode(uint8_t b) {
  static const uint16_t map[32] = {
      0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
      0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  if (b >= 0x80 && b <= 0x9F) return map[b - 0x80];
  return b;
}

namespace {

struct Entity {
  const char* name;
  uint16_t cp;
};

// Entidades HTML mas usadas en feeds (los nombres distinguen mayusculas).
const Entity kEntities[] = {
    {"quot", 34},     {"amp", 38},      {"apos", 39},     {"lt", 60},       {"gt", 62},       {"nbsp", 160},
    {"iexcl", 161},   {"cent", 162},    {"pound", 163},   {"curren", 164},  {"yen", 165},     {"brvbar", 166},
    {"sect", 167},    {"uml", 168},     {"copy", 169},    {"ordf", 170},    {"laquo", 171},   {"not", 172},
    {"shy", 173},     {"reg", 174},     {"macr", 175},    {"deg", 176},     {"plusmn", 177},  {"sup2", 178},
    {"sup3", 179},    {"acute", 180},   {"micro", 181},   {"para", 182},    {"middot", 183},  {"cedil", 184},
    {"sup1", 185},    {"ordm", 186},    {"raquo", 187},   {"frac14", 188},  {"frac12", 189},  {"frac34", 190},
    {"iquest", 191},  {"Agrave", 192},  {"Aacute", 193},  {"Acirc", 194},   {"Atilde", 195},  {"Auml", 196},
    {"Aring", 197},   {"AElig", 198},   {"Ccedil", 199},  {"Egrave", 200},  {"Eacute", 201},  {"Ecirc", 202},
    {"Euml", 203},    {"Igrave", 204},  {"Iacute", 205},  {"Icirc", 206},   {"Iuml", 207},    {"ETH", 208},
    {"Ntilde", 209},  {"Ograve", 210},  {"Oacute", 211},  {"Ocirc", 212},   {"Otilde", 213},  {"Ouml", 214},
    {"times", 215},   {"Oslash", 216},  {"Ugrave", 217},  {"Uacute", 218},  {"Ucirc", 219},   {"Uuml", 220},
    {"Yacute", 221},  {"THORN", 222},   {"szlig", 223},   {"agrave", 224},  {"aacute", 225},  {"acirc", 226},
    {"atilde", 227},  {"auml", 228},    {"aring", 229},   {"aelig", 230},   {"ccedil", 231},  {"egrave", 232},
    {"eacute", 233},  {"ecirc", 234},   {"euml", 235},    {"igrave", 236},  {"iacute", 237},  {"icirc", 238},
    {"iuml", 239},    {"eth", 240},     {"ntilde", 241},  {"ograve", 242},  {"oacute", 243},  {"ocirc", 244},
    {"otilde", 245},  {"ouml", 246},    {"divide", 247},  {"oslash", 248},  {"ugrave", 249},  {"uacute", 250},
    {"ucirc", 251},   {"uuml", 252},    {"yacute", 253},  {"thorn", 254},   {"yuml", 255},    {"OElig", 338},
    {"oelig", 339},   {"Scaron", 352},  {"scaron", 353},  {"Yuml", 376},    {"fnof", 402},    {"circ", 710},
    {"tilde", 732},   {"Alpha", 913},   {"Beta", 914},    {"Gamma", 915},   {"Delta", 916},   {"Theta", 920},
    {"Lambda", 923},  {"Pi", 928},      {"Sigma", 931},   {"Phi", 934},     {"Psi", 936},     {"Omega", 937},
    {"alpha", 945},   {"beta", 946},    {"gamma", 947},   {"delta", 948},   {"epsilon", 949}, {"zeta", 950},
    {"eta", 951},     {"theta", 952},   {"iota", 953},    {"kappa", 954},   {"lambda", 955},  {"mu", 956},
    {"nu", 957},      {"xi", 958},      {"pi", 960},      {"rho", 961},     {"sigma", 963},   {"tau", 964},
    {"upsilon", 965}, {"phi", 966},     {"chi", 967},     {"psi", 968},     {"omega", 969},   {"ensp", 8194},
    {"emsp", 8195},   {"thinsp", 8201}, {"zwnj", 8204},   {"zwj", 8205},    {"lrm", 8206},    {"rlm", 8207},
    {"hyphen", 8208}, {"dash", 8208},   {"ndash", 8211},  {"mdash", 8212},  {"horbar", 8213}, {"lsquo", 8216},
    {"rsquo", 8217},  {"rsquor", 8217}, {"sbquo", 8218},  {"ldquo", 8220},  {"rdquo", 8221},  {"rdquor", 8221},
    {"bdquo", 8222},  {"dagger", 8224}, {"Dagger", 8225}, {"bull", 8226},   {"bullet", 8226}, {"hellip", 8230},
    {"mldr", 8230},   {"permil", 8240}, {"prime", 8242},  {"Prime", 8243},  {"lsaquo", 8249}, {"rsaquo", 8250},
    {"oline", 8254},  {"frasl", 8260},  {"euro", 8364},   {"trade", 8482},  {"larr", 8592},   {"uarr", 8593},
    {"rarr", 8594},   {"darr", 8595},   {"harr", 8596},   {"rArr", 8658},   {"minus", 8722},  {"lowast", 8727},
    {"radic", 8730},  {"infin", 8734},  {"asymp", 8776},  {"ne", 8800},     {"equiv", 8801},  {"le", 8804},
    {"ge", 8805},     {"star", 9734},   {"starf", 9733},  {"hearts", 9829}, {"check", 10003}, {"cross", 10007},
    {"excl", 33},     {"num", 35},      {"dollar", 36},   {"percnt", 37},   {"lpar", 40},     {"rpar", 41},
    {"ast", 42},      {"plus", 43},     {"comma", 44},    {"period", 46},   {"sol", 47},      {"colon", 58},
    {"semi", 59},     {"equals", 61},   {"quest", 63},    {"commat", 64},   {"lsqb", 91},     {"lbrack", 91},
    {"bsol", 92},     {"rsqb", 93},     {"rbrack", 93},   {"lowbar", 95},   {"grave", 96},    {"lcub", 123},
    {"lbrace", 123},  {"verbar", 124},  {"vert", 124},    {"rcub", 125},    {"rbrace", 125},  {"Tab", 9},
    {"NewLine", 10},
};

}  // namespace

uint32_t namedEntity(const char* name, size_t len) {
  if (len == 0 || len > 8) return 0;
  for (size_t i = 0; i < sizeof(kEntities) / sizeof(kEntities[0]); i++) {
    const char* n = kEntities[i].name;
    if (strncmp(n, name, len) == 0 && n[len] == 0) return kEntities[i].cp;
  }
  return 0;
}

uint32_t numericEntity(const char* ref, size_t len) {
  if (len < 2 || ref[0] != '#') return 0;
  uint32_t v = 0;
  size_t i = 1;
  if (ref[1] == 'x' || ref[1] == 'X') {
    i = 2;
    if (i >= len) return 0;
    for (; i < len; i++) {
      const char c = ref[i];
      uint32_t d;
      if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
      else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
      else return 0;
      v = v * 16 + d;
      if (v > 0x10FFFF) return 0;
    }
  } else {
    for (; i < len; i++) {
      const char c = ref[i];
      if (c < '0' || c > '9') return 0;
      v = v * 10 + (uint32_t)(c - '0');
      if (v > 0x10FFFF) return 0;
    }
  }
  if (v >= 0x80 && v <= 0x9F) v = cp1252ToUnicode((uint8_t)v);  // como hacen los navegadores
  return v;
}

uint32_t fnv1a(const char* s, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
  return s.substr(a, b - a);
}

std::string toLower(const std::string& s) {
  std::string r(s);
  for (size_t i = 0; i < r.size(); i++) {
    if (r[i] >= 'A' && r[i] <= 'Z') r[i] = (char)(r[i] - 'A' + 'a');
  }
  return r;
}

bool startsWithNoCase(const std::string& s, const char* prefix) {
  const size_t n = strlen(prefix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; i++) {
    if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// HtmlToText
// ---------------------------------------------------------------------------

HtmlToText::HtmlToText() { begin(nullptr, 0, false); }

void HtmlToText::begin(std::string* out, size_t maxChars, bool singleLine, size_t maxBytes) {
  out_ = out;
  maxChars_ = maxChars;
  maxBytes_ = maxBytes;
  chars_ = 0;
  st_ = TEXT;
  singleLine_ = singleLine;
  full_ = (maxChars == 0);
  truncated_ = false;
  pendSpace_ = false;
  pendNewline_ = false;
  pendBullet_ = false;
  ucp_ = 0;
  uneed_ = 0;
  tagLen_ = 0;
  closing_ = false;
  afterEq_ = false;
  quote_ = 0;
  declLen_ = 0;
  dash_ = 0;
  entLen_ = 0;
  skipTag_[0] = 0;
  skipMatch_ = 0;
  enterSkip_ = false;
}

void HtmlToText::write(const char* s, size_t n) {
  for (size_t i = 0; i < n; i++) put(s[i]);
}

void HtmlToText::write(const char* s) { write(s, strlen(s)); }

void HtmlToText::put(char c) {
  if (!out_ || truncated_) return;  // ya no puede cambiar el resultado
  const uint8_t b = (uint8_t)c;
  if (uneed_) {
    if ((b & 0xC0) == 0x80) {
      ucp_ = (ucp_ << 6) | (b & 0x3F);
      if (--uneed_ == 0) codepoint(ucp_);
      return;
    }
    uneed_ = 0;  // secuencia rota: se descarta lo acumulado y se reprocesa el byte
  }
  if (b < 0x80) {
    codepoint(b);
  } else if ((b & 0xE0) == 0xC0) {
    ucp_ = b & 0x1F;
    uneed_ = 1;
  } else if ((b & 0xF0) == 0xE0) {
    ucp_ = b & 0x0F;
    uneed_ = 2;
  } else if ((b & 0xF8) == 0xF0) {
    ucp_ = b & 0x07;
    uneed_ = 3;
  }
  // cualquier otro byte es invalido y se descarta
}

void HtmlToText::codepoint(uint32_t cp) {
  switch (st_) {
    case TEXT:
      if (cp == '<') {
        st_ = LT;
      } else if (cp == '&') {
        st_ = ENTITY;
        entLen_ = 0;
      } else {
        emit(cp);
      }
      break;

    case LT:
      if (cp == '/') {
        closing_ = true;
        tagLen_ = 0;
        st_ = TAGNAME;
      } else if (cp < 0x80 && isalpha((int)cp)) {
        closing_ = false;
        tag_[0] = (char)tolower((int)cp);
        tagLen_ = 1;
        st_ = TAGNAME;
      } else if (cp == '!') {
        st_ = DECL;
        declLen_ = 0;
      } else if (cp == '?') {
        st_ = TAGREST;
        quote_ = 0;
        afterEq_ = false;
        enterSkip_ = false;
      } else {
        st_ = TEXT;  // un '<' suelto es texto
        emit('<');
        codepoint(cp);
      }
      break;

    case TAGNAME:
      if (cp < 0x80 && (isalnum((int)cp) || cp == ':' || cp == '-' || cp == '_')) {
        if (tagLen_ < sizeof(tag_) - 1) tag_[tagLen_++] = (char)tolower((int)cp);
      } else {
        tag_[tagLen_] = 0;
        tagAction();
        if (cp == '>') {
          st_ = enterSkip_ ? SKIP : TEXT;
          skipMatch_ = 0;
        } else {
          st_ = TAGREST;
          quote_ = 0;
          afterEq_ = false;
        }
      }
      break;

    case TAGREST:
      if (quote_) {
        if (cp == (uint32_t)(uint8_t)quote_) quote_ = 0;
      } else if (cp == '>') {
        st_ = enterSkip_ ? SKIP : TEXT;
        skipMatch_ = 0;
      } else if (cp == '=') {
        afterEq_ = true;
      } else if ((cp == '"' || cp == '\'') && afterEq_) {
        quote_ = (char)cp;
        afterEq_ = false;
      } else if (cp != ' ' && cp != '\t' && cp != '\n' && cp != '\r') {
        afterEq_ = false;
      }
      break;

    case DECL:
      if (declLen_ == 0 && cp == '-') {
        declLen_ = 1;
      } else if (declLen_ == 1 && cp == '-') {
        st_ = COMMENT;
        dash_ = 0;
      } else if (cp == '>') {
        st_ = TEXT;
      } else {
        declLen_ = 2;
      }
      break;

    case COMMENT:
      if (cp == '-') {
        if (dash_ < 2) dash_++;
      } else if (cp == '>' && dash_ >= 2) {
        st_ = TEXT;
      } else {
        dash_ = 0;
      }
      break;

    case ENTITY:
      if (cp == ';') {
        const uint32_t v =
            (entLen_ && ent_[0] == '#') ? numericEntity(ent_, entLen_) : namedEntity(ent_, entLen_);
        st_ = TEXT;
        if (v) {
          emit(v);
        } else {
          entityLiteral();
          emit(';');
        }
      } else if (cp < 0x80 && (isalnum((int)cp) || cp == '#') && entLen_ < sizeof(ent_) - 1) {
        ent_[entLen_++] = (char)cp;
      } else {
        st_ = TEXT;  // no era una entidad: sale tal cual
        entityLiteral();
        codepoint(cp);
      }
      break;

    case SKIP: {
      // dentro de <script>/<style>: se ignora todo hasta "</nombre"
      if (skipMatch_ == 0) {
        if (cp == '<') skipMatch_ = 1;
      } else if (skipMatch_ == 1) {
        skipMatch_ = (cp == '/') ? 2 : (cp == '<' ? 1 : 0);
      } else {
        const size_t idx = (size_t)skipMatch_ - 2;
        const char lc = (cp < 0x80) ? (char)tolower((int)cp) : 0;
        if (skipTag_[idx] && lc == skipTag_[idx]) {
          skipMatch_++;
          if (skipTag_[idx + 1] == 0) {
            enterSkip_ = false;
            st_ = TAGREST;
            quote_ = 0;
            afterEq_ = false;
          }
        } else {
          skipMatch_ = (cp == '<') ? 1 : 0;
        }
      }
      break;
    }
  }
}

void HtmlToText::entityLiteral() {
  emit('&');
  for (uint8_t i = 0; i < entLen_; i++) emit((uint8_t)ent_[i]);
  entLen_ = 0;
}

void HtmlToText::breakLine() {
  if (singleLine_) {
    pendSpace_ = true;
  } else {
    pendNewline_ = true;
  }
}

void HtmlToText::tagAction() {
  enterSkip_ = false;
  const char* t = tag_;
  if (!strcmp(t, "br")) {
    breakLine();
    return;
  }
  if (!strcmp(t, "script") || !strcmp(t, "style")) {
    if (!closing_) {
      strcpy(skipTag_, t);
      enterSkip_ = true;
    }
    return;
  }
  if (!strcmp(t, "li")) {
    breakLine();
    pendBullet_ = !closing_ && !singleLine_;
    return;
  }
  if (!strcmp(t, "td") || !strcmp(t, "th")) {
    pendSpace_ = true;
    return;
  }
  static const char* const kBlocks[] = {
      "p",      "div",    "tr",      "h1",      "h2",         "h3",      "h4",     "h5",     "h6",
      "ul",     "ol",     "table",   "pre",     "hr",         "dd",      "dt",     "dl",     "blockquote",
      "figure", "header", "footer",  "section", "article",    "aside",   "nav",    "main",   "details",
      "form",   "center", "address", "summary", "figcaption", "caption", "option", "video",  "audio",
  };
  for (size_t i = 0; i < sizeof(kBlocks) / sizeof(kBlocks[0]); i++) {
    if (!strcmp(t, kBlocks[i])) {
      breakLine();
      return;
    }
  }
}

void HtmlToText::emit(uint32_t cp) {
  // todo tipo de espacio se colapsa en uno solo
  if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0x0B || cp == 0x0C || cp == 0xA0 ||
      cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
      cp == 0x205F || cp == 0x3000) {
    pendSpace_ = true;
    return;
  }
  // controles, invisibles, uso privado y fuera del BMP (emoji): la fuente no los dibuja
  if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return;
  if (cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
      (cp >= 0x2060 && cp <= 0x206F) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF ||
      (cp >= 0xFFF0 && cp <= 0xFFFF) || (cp >= 0xD800 && cp <= 0xDFFF) || (cp >= 0xE000 && cp <= 0xF8FF) ||
      cp > 0xFFFF) {
    return;
  }
  if (full_) {
    truncated_ = true;
    return;
  }
  if (!out_->empty()) {
    if (pendNewline_) {
      out_->push_back('\n');
      chars_++;
    } else if (pendSpace_) {
      out_->push_back(' ');
      chars_++;
    }
  }
  pendNewline_ = false;
  pendSpace_ = false;
  if (pendBullet_) {
    utf8Append(*out_, 0x2022);
    out_->push_back(' ');
    chars_ += 2;
    pendBullet_ = false;
  }
  utf8Append(*out_, cp);
  chars_++;
  if (chars_ >= maxChars_ || out_->size() + 8 > maxBytes_) full_ = true;
}

void HtmlToText::finish() {
  if (!out_) return;
  if (st_ == ENTITY) {
    st_ = TEXT;
    entityLiteral();
  }
  if (truncated_) {
    // no cortar una palabra por la mitad si hay un espacio cerca
    const size_t n = out_->size();
    size_t cut = n;
    size_t back = 0;
    while (cut > 0 && back < 24) {
      const char ch = (*out_)[cut - 1];
      if (ch == ' ' || ch == '\n') break;
      cut--;
      back++;
    }
    if (cut > 0 && back < 24 && cut > n / 2) out_->resize(cut - 1);
    utf8Append(*out_, 0x2026);
  }
}

}  // namespace txt
