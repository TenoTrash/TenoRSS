// TenoRSS - corte de texto en lineas para la pantalla.
// Codigo portable: el ancho de cada caracter lo informa quien llama.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct WrapLine {
  uint32_t start;     // offset en bytes dentro del texto
  uint16_t len;       // largo en bytes (sin espacios al final)
  bool paragraphEnd;  // la linea cierra un parrafo (salto explicito o fin del texto)
};

// Ancho en pixeles de un code point.
typedef int (*CharWidthFn)(void* ctx, uint32_t cp);

// Parte text en lineas de hasta widthPx. Corta en espacios; las palabras mas
// largas que la linea y los ideogramas se cortan donde haga falta. Se detiene
// al llegar a maxLines.
void wrapText(const std::string& text, int widthPx, CharWidthFn widthFn, void* ctx, std::vector<WrapLine>& out,
              size_t maxLines);
