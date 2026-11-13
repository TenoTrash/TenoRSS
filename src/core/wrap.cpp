#include "wrap.h"

#include "textutil.h"

namespace {

const size_t kNone = (size_t)-1;

// Escrituras sin espacios entre palabras: se puede cortar entre dos caracteres.
inline bool isIdeograph(uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFF00 && cp <= 0xFFEF);
}

void emitLine(const std::string& text, size_t start, size_t end, bool paragraphEnd, std::vector<WrapLine>& out) {
  while (end > start && text[end - 1] == ' ') end--;
  WrapLine l;
  l.start = (uint32_t)start;
  l.len = (uint16_t)(end - start > 0xFFFF ? 0xFFFF : end - start);
  l.paragraphEnd = paragraphEnd;
  out.push_back(l);
}

}  // namespace

void wrapText(const std::string& text, int widthPx, CharWidthFn widthFn, void* ctx, std::vector<WrapLine>& out,
              size_t maxLines) {
  const size_t n = text.size();
  size_t i = 0;
  bool paragraphStart = true;
  while (i < n && out.size() < maxLines) {
    if (!paragraphStart) {
      while (i < n && text[i] == ' ') i++;  // una linea de continuacion no arranca con espacio
      if (i >= n) break;
    }
    const size_t lineStart = i;
    int width = 0;
    size_t breakEnd = kNone;  // fin de linea si se corta en el ultimo punto permitido
    size_t breakNext = kNone; // donde arranca la linea siguiente en ese caso
    bool prevIdeo = false;
    bool done = false;
    size_t j = i;
    while (j < n) {
      uint32_t cp;
      size_t k = txt::utf8Decode(text.data() + j, n - j, cp);
      if (k == 0) k = 1;
      if (cp == '\n') {
        emitLine(text, lineStart, j, true, out);
        i = j + 1;
        paragraphStart = true;
        done = true;
        break;
      }
      const int w = widthFn(ctx, cp);
      const bool ideo = isIdeograph(cp);
      if (width + w > widthPx && j > lineStart) {
        if (cp == ' ' || ideo || prevIdeo || breakEnd == kNone) {
          emitLine(text, lineStart, j, false, out);  // se corta justo aca
          i = j;
        } else {
          emitLine(text, lineStart, breakEnd, false, out);
          i = breakNext;
        }
        paragraphStart = false;
        done = true;
        break;
      }
      if (cp == ' ') {
        breakEnd = j;
        breakNext = j + k;
      } else if ((ideo || prevIdeo) && j > lineStart) {
        breakEnd = j;
        breakNext = j;
      }
      prevIdeo = ideo;
      width += w;
      j += k;
    }
    if (!done) {
      emitLine(text, lineStart, n, true, out);
      i = n;
    }
  }
}
