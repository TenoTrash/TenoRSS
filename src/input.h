// TenoRSS - teclado del Cardputer convertido en eventos.
#pragma once

#include <cstdint>

namespace input {

enum Kind : uint8_t { K_NONE = 0, K_CHAR, K_ENTER, K_BKSP, K_TAB, K_G0 };

struct Event {
  Kind kind;
  char ch;    // caracter con Shift aplicado (solo K_CHAR)
  char base;  // caracter impreso en la tecla, sin modificadores
  bool fn;
  bool shift;
  bool ctrl;
  bool opt;
  bool alt;
  bool repeat;  // autorrepeticion por mantener la tecla
};

// Lee el teclado y encola las teclas recien apretadas (y sus repeticiones).
void poll();
bool next(Event& ev);
// Encola un evento sintetico (consola de depuracion).
void inject(const Event& ev);

// Lectura como tecla de navegacion: ; arriba  . abajo  , izquierda  / derecha
// Enter aceptar  ` (esc) o boton G0 volver  Backspace borrar.
enum Nav : uint8_t { N_NONE = 0, N_UP, N_DOWN, N_LEFT, N_RIGHT, N_OK, N_BACK, N_DEL };
Nav navOf(const Event& ev);

}  // namespace input
