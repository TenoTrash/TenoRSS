// TenoRSS - consola de depuracion por USB serie.
//
// Comandos (una linea cada uno, 115200 baudios):
//   #k <teclas>   simula teclas. Escapes: \n Enter  \b Backspace  \t Tab
//                 \e Esc (tecla `)  \g boton G0  \F Fn + la tecla siguiente  \\ barra
//   #shot         vuelca la pantalla (RGB332 en hexadecimal, entre #SHOT y #END)
//   #stat         memoria libre, pantalla actual y estado de red
//   #splash       muestra la pantalla de inicio hasta que se toque una tecla
//   #reboot       reinicia el equipo
#pragma once

namespace dbg {
void poll();
}
