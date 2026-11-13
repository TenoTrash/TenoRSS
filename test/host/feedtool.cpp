// Herramienta de PC: pasa un archivo XML por el mismo parser que usa el equipo.
//   feedtool [-c CHUNK] [-n MAXITEMS] [-t MAXCHARS] [-v] archivo.xml [charset]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "../../src/core/feedparser.h"

struct Ctx {
  std::vector<FeedItem> items;
  size_t maxItems;
};

static bool onItem(void* c, FeedItem& it) {
  Ctx* ctx = (Ctx*)c;
  ctx->items.push_back(it);
  return ctx->items.size() < ctx->maxItems;
}

int main(int argc, char** argv) {
  size_t chunk = 1460, maxItems = 20, maxChars = 1500;
  bool verbose = false;
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i++) {
    if (!strcmp(argv[i], "-c")) chunk = (size_t)atoi(argv[++i]);
    else if (!strcmp(argv[i], "-n")) maxItems = (size_t)atoi(argv[++i]);
    else if (!strcmp(argv[i], "-t")) maxChars = (size_t)atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) verbose = true;
  }
  if (i >= argc) {
    fprintf(stderr, "uso: feedtool [-c chunk] [-n items] [-t chars] [-v] archivo.xml [charset]\n");
    return 2;
  }
  FILE* f = fopen(argv[i], "rb");
  if (!f) {
    perror(argv[i]);
    return 2;
  }
  Ctx ctx;
  ctx.maxItems = maxItems;
  FeedParser p(onItem, &ctx, maxChars);
  if (i + 1 < argc) p.setCharsetHint(argv[i + 1]);
  std::vector<char> buf(chunk);
  size_t total = 0, n;
  while (!p.stopped() && (n = fread(buf.data(), 1, chunk, f)) > 0) {
    p.feed(buf.data(), n);
    total += n;
  }
  p.finish();
  fclose(f);
  size_t maxText = 0, withDate = 0, withLink = 0, withText = 0, bytes = 0;
  for (size_t k = 0; k < ctx.items.size(); k++) {
    const FeedItem& it = ctx.items[k];
    if (it.text.size() > maxText) maxText = it.text.size();
    if (it.date) withDate++;
    if (!it.link.empty()) withLink++;
    if (!it.text.empty()) withText++;
    bytes += it.title.size() + it.link.size() + it.text.size();
  }
  printf("root=%d items=%zu leidos=%zuB fecha=%zu link=%zu texto=%zu maxTexto=%zuB ram~%zuB%s\n", p.rootKind(),
         ctx.items.size(), total, withDate, withLink, withText, maxText, bytes,
         p.unsupportedEncoding() ? " ENCODING-NO-SOPORTADO" : "");
  if (verbose) {
    for (size_t k = 0; k < ctx.items.size(); k++) {
      const FeedItem& it = ctx.items[k];
      char when[32] = "-";
      if (it.date) {
        time_t t = (time_t)it.date;
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%SZ", gmtime(&t));
      }
      printf("--- [%zu] id=%08x %s\n    T: %s\n    L: %s\n    X(%zu): %s\n", k, it.id, when, it.title.c_str(),
             it.link.c_str(), it.text.size(), it.text.c_str());
    }
  }
  return 0;
}
