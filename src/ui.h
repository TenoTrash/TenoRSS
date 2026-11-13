// TenoRSS - interfaz: pantallas, dibujo y manejo de teclas.
#pragma once

namespace ui {

// Crea el lienzo y muestra la pantalla de inicio.
void begin();
// Sale de la pantalla de inicio: va al menu, o avisa que falta la microSD.
void start(bool sdOk);
// Procesa teclas y estado de red; redibuja solo si algo cambio.
void loop();

// Consola de depuracion.
void screenshot();
const char* screenName();
void debugSplash();  // vuelve a mostrar la pantalla de inicio hasta que se toque una tecla

}  // namespace ui
