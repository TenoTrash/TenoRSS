#include "datetime.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "textutil.h"

namespace dt {

namespace {

// Dias desde 1970-01-01 (algoritmo de Howard Hinnant).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void skipSpaces(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
}

// Lee entre minDigits y maxDigits digitos decimales.
bool readNum(const char*& p, int minDigits, int maxDigits, int& out) {
  int n = 0, v = 0;
  while (n < maxDigits && p[n] >= '0' && p[n] <= '9') {
    v = v * 10 + (p[n] - '0');
    n++;
  }
  if (n < minDigits) return false;
  p += n;
  out = v;
  return true;
}

bool validCivil(int y, int mo, int d, int h, int mi, int s) {
  return y >= 1970 && y <= 2200 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 24 && mi >= 0 &&
         mi <= 59 && s >= 0 && s <= 60;
}

// "+HH", "+HHMM", "+HH:MM" con el signo ya consumido.
bool readOffset(const char*& p, int& seconds) {
  int n = 0;
  while (p[n] >= '0' && p[n] <= '9') n++;
  int hh = 0, mm = 0;
  if (n == 4 || n == 3) {
    int v = 0;
    readNum(p, n, n, v);
    hh = v / 100;
    mm = v % 100;
  } else if (n == 1 || n == 2) {
    readNum(p, n, n, hh);
    if (*p == ':') {
      const char* q = p + 1;
      if (readNum(q, 2, 2, mm)) p = q;
    }
  } else {
    return false;
  }
  if (hh > 14 || mm > 59) return false;
  seconds = hh * 3600 + mm * 60;
  return true;
}

int monthFromName(const char* p) {
  static const char* const names[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
  char a[4] = {(char)tolower((unsigned char)p[0]), (char)tolower((unsigned char)p[1]),
               (char)tolower((unsigned char)p[2]), 0};
  for (int i = 0; i < 12; i++) {
    if (!strcmp(a, names[i])) return i + 1;
  }
  // abreviaturas en castellano que difieren del ingles
  if (!strcmp(a, "ene")) return 1;
  if (!strcmp(a, "abr")) return 4;
  if (!strcmp(a, "ago")) return 8;
  if (!strcmp(a, "set")) return 9;
  if (!strcmp(a, "dic")) return 12;
  return 0;
}

int zoneFromName(const char* p, size_t len) {
  struct Z {
    const char* name;
    int hours;
  };
  static const Z zones[] = {{"gmt", 0},  {"ut", 0},   {"utc", 0},  {"z", 0},    {"est", -5}, {"edt", -4},
                            {"cst", -6}, {"cdt", -5}, {"mst", -7}, {"mdt", -6}, {"pst", -8}, {"pdt", -7},
                            {"cet", 1},  {"cest", 2}, {"bst", 1},  {"art", -3}, {"wet", 0},  {"eet", 2}};
  char a[6];
  if (len == 0 || len >= sizeof(a)) return 0;
  for (size_t i = 0; i < len; i++) a[i] = (char)tolower((unsigned char)p[i]);
  a[len] = 0;
  for (size_t i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
    if (!strcmp(a, zones[i].name)) return zones[i].hours * 3600;
  }
  return 0;
}

int64_t parseIso(const char* p) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (!readNum(p, 4, 4, y)) return 0;
  if (*p != '-' && *p != '/') return 0;
  p++;
  if (!readNum(p, 1, 2, mo)) return 0;
  if (*p != '-' && *p != '/') return 0;
  p++;
  if (!readNum(p, 1, 2, d)) return 0;
  if (*p == 'T' || *p == 't' || *p == ' ') {
    p++;
    skipSpaces(p);
    if (readNum(p, 1, 2, h)) {
      if (*p == ':') {
        p++;
        if (!readNum(p, 1, 2, mi)) return 0;
        if (*p == ':') {
          p++;
          if (!readNum(p, 1, 2, s)) return 0;
          if (*p == '.' || *p == ',') {
            p++;
            while (*p >= '0' && *p <= '9') p++;
          }
        }
      }
    }
  }
  skipSpaces(p);
  int off = 0;
  if (*p == '+' || *p == '-') {
    const int sign = (*p == '-') ? -1 : 1;
    p++;
    int v = 0;
    if (readOffset(p, v)) off = sign * v;
  }
  if (!validCivil(y, mo, d, h, mi, s)) return 0;
  return epochFromCivil(y, mo, d, h, mi, s) - off;
}

int64_t parseRfc822(const char* p) {
  // se saltea el dia de la semana y cualquier cosa hasta el primer digito
  int guard = 0;
  while (*p && !(*p >= '0' && *p <= '9')) {
    p++;
    if (++guard > 16) return 0;
  }
  int d = 0, mo = 0, y = 0, h = 0, mi = 0, s = 0;
  if (!readNum(p, 1, 2, d)) return 0;
  while (*p == ' ' || *p == '-' || *p == '.' || *p == ',') p++;
  if (!isalpha((unsigned char)p[0]) || !isalpha((unsigned char)p[1]) || !isalpha((unsigned char)p[2])) return 0;
  mo = monthFromName(p);
  if (!mo) return 0;
  while (isalpha((unsigned char)*p)) p++;
  while (*p == ' ' || *p == '-' || *p == '.' || *p == ',') p++;
  const char* ystart = p;
  if (!readNum(p, 2, 4, y)) return 0;
  const int ydigits = (int)(p - ystart);
  if (ydigits == 2) y += (y < 70) ? 2000 : 1900;
  else if (ydigits == 3) y += 1900;
  while (*p == ' ' || *p == 'T' || *p == ',') p++;
  if (readNum(p, 1, 2, h)) {
    if (*p == ':') {
      p++;
      if (!readNum(p, 1, 2, mi)) return 0;
      if (*p == ':') {
        p++;
        if (!readNum(p, 1, 2, s)) return 0;
      }
    }
  }
  skipSpaces(p);
  int off = 0;
  if (*p == '+' || *p == '-') {
    const int sign = (*p == '-') ? -1 : 1;
    p++;
    int v = 0;
    if (readOffset(p, v)) off = sign * v;
  } else if (isalpha((unsigned char)*p)) {
    const char* q = p;
    while (isalpha((unsigned char)*q)) q++;
    off = zoneFromName(p, (size_t)(q - p));
  }
  if (!validCivil(y, mo, d, h, mi, s)) return 0;
  return epochFromCivil(y, mo, d, h, mi, s) - off;
}

}  // namespace

int64_t epochFromCivil(int year, int month, int day, int hour, int min, int sec) {
  return daysFromCivil(year, (unsigned)month, (unsigned)day) * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
}

int64_t parseDate(const char* s) {
  if (!s) return 0;
  skipSpaces(s);
  if (s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9' && s[3] >= '0' &&
      s[3] <= '9' && (s[4] == '-' || s[4] == '/')) {
    return parseIso(s);
  }
  return parseRfc822(s);
}

std::string formatAge(int64_t now, int64_t when) {
  if (now <= 0 || when <= 0) return std::string();
  int64_t diff = now - when;
  if (diff < 60) return "ahora";
  char buf[16];
  if (diff < 3600) {
    snprintf(buf, sizeof(buf), "%dm", (int)(diff / 60));
  } else if (diff < 86400) {
    snprintf(buf, sizeof(buf), "%dh", (int)(diff / 3600));
  } else if (diff < 14 * 86400) {
    snprintf(buf, sizeof(buf), "%dd", (int)(diff / 86400));
  } else if (diff < 70 * 86400) {
    snprintf(buf, sizeof(buf), "%dsem", (int)(diff / (7 * 86400)));
  } else if (diff < 365 * 86400) {
    snprintf(buf, sizeof(buf), "%dmes", (int)(diff / (30 * 86400)));
  } else {
    snprintf(buf, sizeof(buf), "%da", (int)(diff / (365 * 86400)));
  }
  return buf;
}

TzSpec parseTz(const std::string& value) {
  TzSpec spec;
  spec.kind = TzSpec::AUTO;
  spec.offsetSec = 0;
  const std::string t = txt::trim(value);
  const std::string lower = txt::toLower(t);
  if (lower.empty() || lower == "auto") return spec;

  const char* p = lower.c_str();
  if (!strncmp(p, "utc", 3) || !strncmp(p, "gmt", 3)) p += 3;
  skipSpaces(p);
  if (*p == 0) {  // "UTC" a secas
    spec.kind = TzSpec::OFFSET;
    return spec;
  }
  int sign = 1;
  const char* q = p;
  if (*q == '+' || *q == '-') {
    sign = (*q == '-') ? -1 : 1;
    q++;
  }
  int v = 0;
  if (readOffset(q, v)) {
    skipSpaces(q);
    if (*q == 0) {
      spec.kind = TzSpec::OFFSET;
      spec.offsetSec = sign * v;
      return spec;
    }
  }
  // cualquier otra cosa con letras se toma como cadena POSIX
  for (size_t i = 0; i < t.size(); i++) {
    if (isalpha((unsigned char)t[i])) {
      spec.kind = TzSpec::POSIX;
      spec.posix = t;
      return spec;
    }
  }
  return spec;
}

std::string posixFromOffset(int32_t offsetSec) {
  if (offsetSec == 0) return "UTC0";
  const int a = offsetSec < 0 ? -offsetSec : offsetSec;
  const int h = a / 3600;
  const int m = (a % 3600) / 60;
  const char sign = offsetSec < 0 ? '-' : '+';
  // en POSIX el corrimiento va con el signo invertido (positivo = al oeste)
  const char* psign = offsetSec < 0 ? "" : "-";
  char buf[40];
  if (m) {
    snprintf(buf, sizeof(buf), "<%c%02d%02d>%s%d:%02d", sign, h, m, psign, h, m);
  } else {
    snprintf(buf, sizeof(buf), "<%c%02d>%s%d", sign, h, psign, h);
  }
  return buf;
}

}  // namespace dt
