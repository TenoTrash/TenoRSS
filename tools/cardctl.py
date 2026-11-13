#!/usr/bin/env python3
"""Control remoto de TenoRSS por USB serie (consola de depuracion del firmware).

Uso: cardctl.py ACCION [ACCION ...]   (se ejecutan en orden)

  key:TECLAS        simula teclas. Escapes: \\n Enter, \\b Backspace, \\t Tab,
                    \\e Esc, \\g boton G0, \\F Fn + la tecla siguiente
                    Navegacion: ; arriba  . abajo  , izquierda  / derecha
  shot:ARCHIVO.png  guarda una captura de pantalla (ampliada x3)
  stat              muestra memoria y estado
  wait:SEG          muestra el log durante SEG segundos
  until:TEXTO[:SEG] muestra el log hasta que aparezca TEXTO (o pasen SEG, 60 por defecto)
  reset             reinicia el equipo
  reboot            reinicia por software (#reboot)

Ejemplo:  cardctl.py key:'\\n' wait:1 shot:feeds.png

Requiere pyserial y Pillow (los trae el entorno de PlatformIO:
~/.platformio/penv/bin/python tools/cardctl.py ...).
"""
import sys
import time

import serial
from serial.tools import list_ports

VID, PID = 0x303A, 0x1001  # USB-Serial/JTAG del ESP32-S3


def find_port(timeout=15.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for p in list_ports.comports():
            if p.vid == VID and p.pid == PID:
                return p.device
        time.sleep(0.1)
    raise SystemExit("no encuentro el Cardputer (USB 303A:1001)")


class Card:
    def __init__(self):
        self.ser = None
        self.buf = b""
        self.open()

    def open(self):
        # No se tocan DTR/RTS: bajar uno antes que el otro resetea el chip.
        for _ in range(80):
            try:
                self.ser = serial.Serial(find_port(), 115200, timeout=0.05)
                return
            except (serial.SerialException, OSError):
                time.sleep(0.2)
        raise SystemExit("no pude abrir el puerto serie")

    def read(self):
        try:
            data = self.ser.read(8192)
        except (serial.SerialException, OSError):
            try:
                self.ser.close()
            except Exception:
                pass
            time.sleep(0.3)
            self.open()  # el USB se re-enumera en cada reset
            return b""
        self.buf += data
        return data

    def lines(self):
        """Devuelve las lineas completas recibidas hasta ahora."""
        out = []
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            out.append(line.rstrip(b"\r").decode("utf-8", "replace"))
        return out

    def send(self, text):
        self.ser.write(text.encode("utf-8") + b"\n")
        self.ser.flush()

    def pump(self, seconds, until=None):
        t0 = time.time()
        while time.time() - t0 < seconds:
            self.read()
            for line in self.lines():
                print(line, flush=True)
                if until and until in line:
                    return True
        return False

    def reset(self):
        # Igual que el "hard reset" de esptool: con DTR=0, RTS=1 pone EN a masa.
        # Hay que soltar RTS con DTR todavia en 0; si DTR vuelve a 1 antes, el
        # chip arranca en modo descarga.
        self.ser.dtr = False
        time.sleep(0.12)
        self.ser.rts = False
        self.buf = b""

    def shot(self, path):
        from PIL import Image

        self.pump(0.05)
        self.send("#shot")
        rows, w, h, inside = [], 0, 0, False
        t0 = time.time()
        while time.time() - t0 < 10:
            self.read()
            for line in self.lines():
                if line.startswith("#SHOT"):
                    _, w, h, _fmt = line.split()
                    w, h, inside = int(w), int(h), True
                elif line == "#END":
                    img = Image.new("RGB", (w, h))
                    px = img.load()
                    for y, row in enumerate(rows[:h]):
                        data = bytes.fromhex(row)
                        for x, c in enumerate(data[:w]):
                            r, g, b = c >> 5, (c >> 2) & 7, c & 3
                            px[x, y] = (r * 255 // 7, g * 255 // 7, b * 85)
                    img = img.resize((w * 3, h * 3), Image.NEAREST)
                    img.save(path)
                    print(f"[captura] {path}", flush=True)
                    return
                elif inside:
                    rows.append(line.strip())
                else:
                    print(line, flush=True)
        raise SystemExit("no llego la captura")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    card = Card()
    for action in sys.argv[1:]:
        name, _, arg = action.partition(":")
        if name == "key":
            card.send("#k " + arg)
            card.pump(0.35)
        elif name == "shot":
            card.shot(arg)
        elif name == "stat":
            card.send("#stat")
            card.pump(1.0, until="#STAT")
        elif name == "wait":
            card.pump(float(arg))
        elif name == "until":
            text, _, secs = arg.rpartition(":")
            if not text or not secs.replace(".", "").isdigit():
                text, secs = arg, "60"
            if not card.pump(float(secs), until=text):
                print(f"[sin '{text}' en {secs}s]", flush=True)
        elif name == "reset":
            card.reset()
        elif name == "reboot":
            card.send("#reboot")
            card.pump(0.5)
        else:
            raise SystemExit(f"accion desconocida: {action}")


if __name__ == "__main__":
    main()
