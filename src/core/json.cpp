#include "json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "textutil.h"

JsonReader::JsonReader(ByteSource& src, size_t baseOffset)
    : src_(src), base_(baseOffset), consumed_(0), look_(-1), has_(false), ok_(true) {}

int JsonReader::peek() {
  if (!has_) {
    look_ = src_.readByte();
    has_ = true;
    if (look_ >= 0) consumed_++;
  }
  return look_;
}

int JsonReader::get() {
  const int c = peek();
  if (c >= 0) has_ = false;  // el fin de datos queda "pegado"
  return c;
}

void JsonReader::skipWs() {
  for (;;) {
    const int c = peek();
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') get();
    else break;
  }
}

bool JsonReader::fail() {
  ok_ = false;
  return false;
}

size_t JsonReader::valueOffset() {
  skipWs();
  return offset();
}

int JsonReader::peekValue() {
  skipWs();
  return peek();
}

bool JsonReader::beginObject() {
  if (!ok_) return false;
  skipWs();
  if (peek() != '{') return fail();
  get();
  return true;
}

bool JsonReader::nextKey(std::string& key) {
  if (!ok_) return false;
  skipWs();
  int c = peek();
  if (c == ',') {
    get();
    skipWs();
    c = peek();
  }
  if (c == '}') {
    get();
    return false;
  }
  if (c != '"') return fail();
  if (!readString(key, 64)) return false;
  skipWs();
  if (peek() != ':') return fail();
  get();
  return true;
}

bool JsonReader::beginArray() {
  if (!ok_) return false;
  skipWs();
  if (peek() != '[') return fail();
  get();
  return true;
}

bool JsonReader::nextElement() {
  if (!ok_) return false;
  skipWs();
  int c = peek();
  if (c == ',') {
    get();
    skipWs();
    c = peek();
  }
  if (c == ']') {
    get();
    return false;
  }
  if (c < 0) return fail();
  return true;
}

uint32_t JsonReader::hex4() {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    const int c = get();
    uint32_t d;
    if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
    else {
      fail();
      return 0;
    }
    v = v * 16 + d;
  }
  return v;
}

bool JsonReader::readString(std::string& out, size_t maxBytes) {
  out.clear();
  if (!ok_) return false;
  skipWs();
  if (peek() != '"') return fail();
  get();
  bool clipped = false;
  for (;;) {
    int c = get();
    if (c < 0) return fail();
    if (c == '"') break;
    if (c == '\\') {
      c = get();
      if (c < 0) return fail();
      switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
          uint32_t cp = hex4();
          if (!ok_) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {  // par sustituto
            uint32_t lo = 0;
            if (get() == '\\' && get() == 'u') lo = hex4();
            if (!ok_) return false;
            cp = (lo >= 0xDC00 && lo <= 0xDFFF) ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
          }
          if (out.size() + 4 <= maxBytes) txt::utf8Append(out, cp);
          else clipped = true;
          continue;
        }
        default:
          break;  // \" \\ \/ => el mismo caracter
      }
    }
    if (out.size() < maxBytes) out.push_back((char)c);
    else clipped = true;
  }
  if (clipped && !out.empty()) {
    // no dejar un caracter multibyte por la mitad
    size_t i = out.size();
    size_t back = 0;
    while (i > 0 && back < 4 && ((uint8_t)out[i - 1] & 0xC0) == 0x80) {
      i--;
      back++;
    }
    if (i > 0) {
      const uint8_t lead = (uint8_t)out[i - 1];
      size_t need = 0;
      if ((lead & 0xE0) == 0xC0) need = 1;
      else if ((lead & 0xF0) == 0xE0) need = 2;
      else if ((lead & 0xF8) == 0xF0) need = 3;
      if (lead >= 0x80 && back < need) out.resize(i - 1);
    }
  }
  return true;
}

bool JsonReader::readInt(int64_t& v) {
  v = 0;
  if (!ok_) return false;
  skipWs();
  int c = peek();
  if (c == '"') {
    std::string s;
    if (!readString(s, 32)) return false;
    v = strtoll(s.c_str(), nullptr, 10);
    return true;
  }
  if (c == 't' || c == 'f' || c == 'n') {
    std::string w;
    while ((c = peek()) >= 'a' && c <= 'z' && w.size() < 8) w.push_back((char)get());
    v = (w == "true") ? 1 : 0;
    return true;
  }
  bool neg = false;
  if (c == '-') {
    neg = true;
    get();
    c = peek();
  }
  if (c < '0' || c > '9') return fail();
  while ((c = peek()) >= '0' && c <= '9') {
    v = v * 10 + (c - '0');
    get();
  }
  // parte fraccionaria / exponente: se descartan
  while ((c = peek()) == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' || (c >= '0' && c <= '9')) get();
  if (neg) v = -v;
  return true;
}

bool JsonReader::skipString() {
  get();  // comilla inicial
  for (;;) {
    const int c = get();
    if (c < 0) return fail();
    if (c == '\\') {
      if (get() < 0) return fail();
    } else if (c == '"') {
      return true;
    }
  }
}

bool JsonReader::skipValue() {
  if (!ok_) return false;
  skipWs();
  int c = peek();
  if (c < 0) return fail();
  if (c == '"') return skipString();
  if (c == '{' || c == '[') {
    int depth = 0;
    for (;;) {
      c = peek();
      if (c < 0) return fail();
      if (c == '"') {
        if (!skipString()) return false;
        continue;
      }
      get();
      if (c == '{' || c == '[') {
        depth++;
      } else if (c == '}' || c == ']') {
        if (--depth == 0) return true;
      }
    }
  }
  // numero o literal
  while ((c = peek()) >= 0 && c != ',' && c != '}' && c != ']' && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
    get();
  }
  return true;
}

void jsonWriteRaw(ByteSink& out, const char* s) { out.write(s, strlen(s)); }

void jsonWriteInt(ByteSink& out, int64_t v) {
  char buf[24];
  const int n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
  if (n > 0) out.write(buf, (size_t)n);
}

void jsonWriteString(ByteSink& out, const std::string& s) {
  char buf[256];
  size_t n = 0;
  buf[n++] = '"';
  for (size_t i = 0; i < s.size(); i++) {
    if (n > sizeof(buf) - 8) {
      out.write(buf, n);
      n = 0;
    }
    const unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"':
        buf[n++] = '\\';
        buf[n++] = '"';
        break;
      case '\\':
        buf[n++] = '\\';
        buf[n++] = '\\';
        break;
      case '\n':
        buf[n++] = '\\';
        buf[n++] = 'n';
        break;
      case '\r':
        buf[n++] = '\\';
        buf[n++] = 'r';
        break;
      case '\t':
        buf[n++] = '\\';
        buf[n++] = 't';
        break;
      default:
        if (c < 0x20) {
          n += (size_t)snprintf(buf + n, 7, "\\u%04x", c);
        } else {
          buf[n++] = (char)c;
        }
    }
  }
  buf[n++] = '"';
  out.write(buf, n);
}
