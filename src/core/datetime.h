// TenoRSS - fechas de feeds (RFC 822 / ISO 8601) y zona horaria.
// Codigo portable: no depende de Arduino.
#pragma once

#include <cstdint>
#include <string>

namespace dt {

// Segundos desde 1970 (UTC) para una fecha civil.
int64_t epochFromCivil(int year, int month, int day, int hour, int min, int sec);

// Interpreta fechas de RSS ("Fri, 13 Nov 2026 09:19:00 -0300") y de Atom
// ("2026-11-13T09:19:00Z"). Devuelve 0 si no la entiende.
int64_t parseDate(const char* s);

// Antiguedad compacta respecto de "now": "ahora", "5m", "3h", "2d", "4sem".
// Devuelve "" si falta alguna de las dos fechas.
std::string formatAge(int64_t now, int64_t when);

// Valor TZ de RSS.txt.
struct TzSpec {
  enum Kind { AUTO, OFFSET, POSIX } kind;
  int32_t offsetSec;  // para OFFSET
  std::string posix;  // para POSIX
};
// Acepta "auto", corrimientos ("-3", "+5:30", "UTC-3", "GMT+1") o una cadena
// POSIX completa ("CET-1CEST,M3.5.0,M10.5.0/3"). Vacio o invalido => AUTO.
TzSpec parseTz(const std::string& value);

// Cadena POSIX para un corrimiento fijo: -10800 => "<-03>3".
std::string posixFromOffset(int32_t offsetSec);

}  // namespace dt
