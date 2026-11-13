#!/bin/bash
# Compila TenoRSS y lo graba en el Cardputer por USB.
#
#   tools/flash.sh           compila y graba la aplicacion
#   tools/flash.sh --full    graba tambien bootloader y tabla de particiones
#                            (primera vez, o si el equipo tenia otro firmware)
#
# No usa "pio run -t upload": el esptool 4.5.1 que trae la plataforma falla con
# el USB nativo de este equipo ("No serial data received"). Se usa el esptool
# del entorno de PlatformIO, con reinicio por watchdog: en el ESP32-S3 un reset
# comun por USB puede dejar el chip en modo descarga ("waiting for download").
set -e
cd "$(dirname "$0")/.."
PY="$HOME/.platformio/penv/bin/python"
ENV=cardputer-adv
B=.pio/build/$ENV
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

pio run -e $ENV | grep -E "error|RAM:|Flash:|SUCCESS|FAILED" || true
[ -f "$B/firmware.bin" ] || { echo "no se genero $B/firmware.bin"; exit 1; }

PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
[ -n "$PORT" ] || { echo "no encuentro el Cardputer (/dev/ttyACM*)"; exit 1; }

FILES="0x10000 $B/firmware.bin"
if [ "$1" = "--full" ]; then
  FILES="0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 $BOOT_APP0 $FILES"
else
  # Grabar solo la app es seguro unicamente si el equipo ya tiene nuestra tabla
  # de particiones. Con otra (por ejemplo la de M5Launcher) se pisaria lo que
  # haya instalado y el equipo quedaria sin arrancar.
  TMP=$(mktemp)
  "$PY" -m esptool --chip esp32s3 --port "$PORT" --after no-reset read-flash 0x8000 0xC00 "$TMP" >/dev/null
  if ! cmp -s "$TMP" "$B/partitions.bin"; then
    rm -f "$TMP"
    echo "El equipo tiene otra tabla de particiones (M5Launcher u otro firmware)."
    echo "No grabo nada. Opciones: tools/flash.sh --full (reemplaza todo), o instalar"
    echo "TenoRSS.bin desde el Launcher."
    exit 1
  fi
  rm -f "$TMP"
  PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
fi
"$PY" -m esptool --chip esp32s3 --port "$PORT" --baud 460800 --after watchdog-reset \
  write-flash -z --flash-mode dio --flash-freq 80m --flash-size 8MB $FILES
