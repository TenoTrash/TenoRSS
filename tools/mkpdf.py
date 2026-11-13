#!/usr/bin/env python3
"""Genera TenoRSS-diagrama.pdf (3 paginas apaisadas, tema oscuro)."""
import sys

from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfbase.pdfmetrics import stringWidth
from reportlab.pdfgen import canvas

# uso (desde la raiz del repo, requiere reportlab): python3 tools/mkpdf.py
OUT = sys.argv[1] if len(sys.argv) > 1 else "docs/TenoRSS-diagrama.pdf"
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "docs/img"
W, H = landscape(A4)  # 842 x 595

BG = HexColor("#0A0F0A")
PANEL = HexColor("#0F1A10")
CHIP = HexColor("#143018")
GREEN = HexColor("#00FF41")
MID = HexColor("#00B62D")
DIM = HexColor("#3C7A48")
TEXT = HexColor("#D8F5DC")
SOFT = HexColor("#9CCBA4")
AMBER = HexColor("#FFDB00")

c = canvas.Canvas(OUT, pagesize=(W, H))
c.setTitle("TenoRSS - diagrama de bloques")
c.setAuthor("Teno")
c.setSubject("Lector RSS para M5Stack Cardputer ADV")


def txt(x, t, s, size=9, color=TEXT, font="Helvetica", align="l"):
    c.setFont(font, size)
    c.setFillColor(color)
    y = H - t
    if align == "c":
        c.drawCentredString(x, y, s)
    elif align == "r":
        c.drawRightString(x, y, s)
    else:
        c.drawString(x, y, s)


def rect(x, t, w, h, fill=PANEL, stroke=MID, r=6, lw=1, dash=None):
    c.setLineWidth(lw)
    c.setStrokeColor(stroke)
    c.setFillColor(fill)
    if dash:
        c.setDash(dash)
    c.roundRect(x, H - t - h, w, h, r, stroke=1, fill=1)
    c.setDash()


def box(x, t, w, h, title, lines=(), fill=PANEL, stroke=MID, tsize=10, lsize=8, tcolor=GREEN, center=False):
    rect(x, t, w, h, fill, stroke)
    cx = x + w / 2
    if center:
        txt(cx, t + tsize + 5, title, tsize, tcolor, "Helvetica-Bold", "c")
    else:
        txt(x + 8, t + tsize + 5, title, tsize, tcolor, "Helvetica-Bold")
    for i, line in enumerate(lines):
        ty = t + tsize + 8 + (i + 1) * (lsize + 3)
        assert stringWidth(line, "Helvetica", lsize) <= w - 10, ("no entra", line)
        if center:
            txt(cx, ty, line, lsize, SOFT, align="c")
        else:
            txt(x + 8, ty, line, lsize, SOFT)


def arrow(x1, t1, x2, t2, color=GREEN, both=False, lw=1.3):
    import math

    c.setStrokeColor(color)
    c.setFillColor(color)
    c.setLineWidth(lw)
    y1, y2 = H - t1, H - t2
    c.line(x1, y1, x2, y2)

    def head(xa, ya, xb, yb):
        ang = math.atan2(yb - ya, xb - xa)
        s = 6
        p = c.beginPath()
        p.moveTo(xb, yb)
        p.lineTo(xb - s * math.cos(ang - 0.4), yb - s * math.sin(ang - 0.4))
        p.lineTo(xb - s * math.cos(ang + 0.4), yb - s * math.sin(ang + 0.4))
        p.close()
        c.drawPath(p, stroke=0, fill=1)

    head(x1, y1, x2, y2)
    if both:
        head(x2, y2, x1, y1)


def page(n, section):
    c.setFillColor(BG)
    c.rect(0, 0, W, H, stroke=0, fill=1)
    txt(30, 36, "TenoRSS", 26, GREEN, "Helvetica-Bold")
    txt(150, 36, "Lector RSS para M5Stack Cardputer ADV", 13, TEXT)
    txt(W - 30, 36, section, 12, MID, "Helvetica-Bold", "r")
    c.setStrokeColor(DIM)
    c.setLineWidth(0.8)
    c.line(30, H - 48, W - 30, H - 48)
    txt(30, 580, "RSS Cardputer by Teno  ·  ESP32-S3  ·  PlatformIO + M5Cardputer / M5Unified / M5GFX", 8, DIM)
    txt(W - 30, 580, f"{n} / 3", 8, DIM, align="r")


# ---------------------------------------------------------------- pagina 1
page(1, "1 · Arquitectura")

# hardware
rect(30, 62, 150, 302, fill=BG, stroke=DIM)
txt(38, 78, "Cardputer ADV (hardware)", 9.5, TEXT, "Helvetica-Bold")
hw = [("Pantalla 240 x 135", "ST7789 · SPI"), ("Teclado", "TCA8418 · I2C"), ("microSD", "SPI · FAT32"),
      ("WiFi 2,4 GHz", "ESP32-S3"), ("Batería", "lectura por ADC")]
for i, (a, b) in enumerate(hw):
    box(40, 88 + i * 54, 130, 44, a, [b])

# firmware
rect(205, 62, 440, 402, fill=BG, stroke=GREEN, lw=1.4)
txt(215, 78, "Firmware  (C++ · Arduino · dos tareas FreeRTOS)", 9.5, TEXT, "Helvetica-Bold")
box(220, 88, 195, 112, "Tarea de interfaz", ["ui: 14 pantallas, lienzo de 8 bits", "input: teclas y autorrepetición",
                                             "debugcon: teclas y capturas por USB", "Redibuja solo cuando algo cambia"])
box(435, 88, 195, 112, "Tarea de red (segundo plano)", ["WiFi: red conocida con mejor señal", "Hora por NTP y zona horaria",
                                                        "HTTP / HTTPS con límites de tiempo", "Baja los feeds y escribe el cache"])
arrow(415, 144, 435, 144, both=True)
box(220, 210, 410, 26, "Cola de comandos  ·  estado compartido con mutex  ·  \"Actualizando...\" evita choques", [],
    fill=CHIP, stroke=DIM, tsize=8.5, tcolor=TEXT, center=True)
box(220, 248, 410, 50, "storage", ["Configuración editable a mano y cache.json  ·  lectura por partes  ·",
                                   "escritura atómica (archivo temporal + rename)"])
rect(220, 310, 410, 142, fill=PANEL, stroke=MID)
txt(228, 325, "Núcleo portable  (sin Arduino · 200 pruebas en PC)", 10, GREEN, "Helvetica-Bold")
chips = [("feedparser", "RSS / Atom / RDF", "por streaming"), ("textutil", "HTML a texto", "UTF-8, entidades"),
         ("datetime", "fechas de feeds", "zona horaria"), ("json", "lector y escritor", "por streaming"),
         ("cache", "índice, mezcla", "leído / no leído"), ("conffiles", "wifi-config.txt", "RSS.txt"),
         ("wrap", "corte de líneas", "para la pantalla")]
for i, (a, b, d) in enumerate(chips):
    cx = 226 + (i % 4) * 101
    ct = 334 + (i // 4) * 58
    box(cx, ct, 95, 50, a, [b, d], fill=CHIP, stroke=DIM, tsize=9, lsize=7.5)

# internet
rect(670, 62, 142, 196, fill=BG, stroke=DIM)
txt(678, 78, "Internet", 9.5, TEXT, "Helvetica-Bold")
net = [("Feeds RSS / Atom", "HTTPS · 7 de ejemplo"), ("NTP", "pool.ntp.org"), ("ip-api.com", "zona horaria por IP")]
for i, (a, b) in enumerate(net):
    box(680, 88 + i * 54, 122, 44, a, [b])

# archivos
rect(670, 272, 142, 192, fill=PANEL, stroke=MID)
txt(678, 288, "microSD: /RSSreader", 9.5, GREEN, "Helvetica-Bold")
files = [("wifi-config.txt", "redes conocidas"), ("RSS.txt", "fuentes y zona horaria"),
         ("cache.json", "20 ítems por fuente,"), ("", "leído / no leído")]
t = 306
for a, b in files:
    if a:
        txt(680, t, a, 8.5, TEXT, "Courier-Bold")
        t += 12
    txt(680, t, b, 8, SOFT)
    t += 17 if a else 12
txt(680, 448, "Se crean solos al primer arranque", 7.5, AMBER)

# flechas
arrow(220, 120, 170, 110)             # ui -> pantalla
arrow(170, 164, 220, 150)             # teclado -> input
arrow(220, 273, 170, 218, both=True)  # storage <-> microSD
arrow(630, 130, 680, 110, both=True)  # red <-> internet
arrow(630, 273, 670, 330, both=True)  # storage <-> archivos
txt(655, 150, "WiFi", 7.5, MID, align="c")

# numeros
stats = [("2,6 MB", "firmware"), ("0", "PSRAM: todo por streaming"), ("32 KB", "lienzo de pantalla (8 bits)"),
         ("~15 s", "para actualizar 7 feeds"), ("~87 KB", "RAM libre mínima al actualizar")]
for i, (a, b) in enumerate(stats):
    x = 31 + i * 158
    rect(x, 482, 148, 62, fill=PANEL, stroke=DIM)
    txt(x + 74, 510, a, 20, GREEN, "Helvetica-Bold", "c")
    txt(x + 74, 530, b, 8, SOFT, align="c")
c.showPage()

# ---------------------------------------------------------------- pagina 2
page(2, "2 · Flujo de pantallas")

box(30, 62, 235, 40, "Barra de estado (en todas las pantallas)",
    ["WiFi verde / rojo  ·  fecha y hora por NTP  ·  batería"], fill=BG, stroke=DIM, tsize=9, tcolor=TEXT)

box(30, 132, 100, 56, "Inicio", ["ícono animado", "\"Pulsá una tecla\""], center=True)
box(165, 132, 100, 56, "Menú", ["RSS · Update", "WiFi Setup"], center=True)
arrow(130, 160, 165, 160)

# fila RSS
row = [("Fuentes", "no leídos / total"), ("Ítems", "título completo"), ("Lectura", "texto con scroll"), ("QR", "link al celular")]
for i, (a, b) in enumerate(row):
    box(330 + i * 126, 74, 100, 46, a, [b], center=True)
    if i:
        arrow(304 + i * 126, 97, 330 + i * 126, 97)
# fila Update
box(330, 142, 130, 46, "Actualizando...", ["progreso por fuente"], center=True)
box(498, 142, 130, 46, "Resultado", ["ok / error por fuente"], center=True)
arrow(460, 165, 498, 165)
# fila WiFi
row = [("Redes", "guardadas y visibles"), ("Clave", "se tipea en el equipo"), ("Conectando", ""), ("Conectado", "se guarda la red")]
for i, (a, b) in enumerate(row):
    box(330 + i * 126, 210, 100, 46, a, [b] if b else [], center=True)
    if i:
        arrow(304 + i * 126, 233, 330 + i * 126, 233)
box(330, 282, 100, 38, "¿Borrar red?", ["tecla Del"], center=True, tsize=9)
arrow(380, 256, 380, 282)
box(582, 282, 100, 38, "Falló", ["reingresar clave"], center=True, tsize=9, stroke=AMBER, tcolor=AMBER)
arrow(632, 256, 632, 282, color=AMBER)
arrow(582, 301, 506, 258, color=AMBER)

# ramas desde el menu
arrow(265, 150, 330, 97)
arrow(265, 162, 330, 165)
arrow(265, 174, 330, 233)
txt(284, 112, "RSS", 8, MID, "Helvetica-Bold")
txt(283, 157, "Update", 8, MID, "Helvetica-Bold")
txt(268, 216, "WiFi Setup", 8, MID, "Helvetica-Bold")

rect(30, 210, 235, 50, fill=BG, stroke=AMBER, dash=(4, 3))
txt(38, 226, "Si hay una actualización en curso", 9, AMBER, "Helvetica-Bold")
txt(38, 239, "RSS y WiFi Setup muestran \"Actualizando...\"", 8, SOFT)
txt(38, 251, "y siguen solos cuando termina.", 8, SOFT)

box(30, 272, 235, 62, "Teclas", ["  ;  arriba      .  abajo      ,  izquierda      /  derecha",
                                 "Enter aceptar   ·   esc ( ` ) o botón G0 volver",
                                 "Q  QR   ·   R  buscar redes   ·   Del  borrar red"], fill=BG, stroke=DIM, tsize=9, tcolor=TEXT)

txt(30, 362, "Capturas reales del equipo", 10, GREEN, "Helvetica-Bold")
shots = [("inicio.png", "Inicio"), ("actualizando.png", "Actualizando..."), ("lectura.png", "Lectura"),
         ("qr.png", "QR del link")]
for i, (f, cap) in enumerate(shots):
    x = 45 + i * 194
    c.drawImage(f"{SHOTS}/{f}", x, H - 372 - 168 * 135 / 240 - 6, width=170, height=170 * 135 / 240)
    rect(x - 1, 377, 172, 97.6, fill=None, stroke=DIM, r=2) if False else None
    c.setStrokeColor(DIM)
    c.setLineWidth(1)
    c.rect(x - 1, H - 372 - 168 * 135 / 240 - 7, 172, 170 * 135 / 240 + 2, stroke=1, fill=0)
    txt(x + 85, 492, cap, 9, TEXT, align="c")
txt(30, 520, "La pantalla mide 240 x 135 píxeles (1,14\"). Toda la interfaz se dibuja en un lienzo de 8 bits y se envía de una vez: sin parpadeo.", 8.5, SOFT)
txt(30, 534, "Las pantallas se pueden manejar y capturar por USB con tools/cardctl.py (así se hicieron estas imágenes).", 8.5, SOFT)
c.showPage()

# ---------------------------------------------------------------- pagina 3
page(3, "3 · Arranque, actualización y datos")

steps = [("Arranque", "Pantalla de inicio. Monta la microSD y lee wifi-config.txt, RSS.txt", "y el índice de cache.json (sin cargar los ítems)."),
         ("WiFi", "Escanea y conecta a la red conocida con mejor señal.", "Ícono verde si conectó, rojo si no."),
         ("Hora", "NTP. Con TZ=\"auto\" consulta ip-api.com y recuerda", "el último corrimiento detectado."),
         ("Descarga", "Un GET por fuente, con redirecciones y ETag. El parser procesa", "los bytes a medida que llegan y corta al juntar 20 ítems."),
         ("Limpieza", "HTML a texto plano: sin etiquetas ni scripts, entidades resueltas,", "hasta 1500 caracteres por ítem."),
         ("Mezcla", "Conserva leído / no leído y completa con ítems viejos hasta 20.", "Si una fuente falla, queda lo que había."),
         ("Guardado", "Escribe cache.tmp y lo renombra a cache.json: un corte de energía", "no corrompe el cache. Avisa a la interfaz.")]
for i, (a, l1, l2) in enumerate(steps):
    t = 62 + i * 71
    rect(30, t, 345, 56)
    c.setFillColor(GREEN)
    c.circle(50, H - t - 28, 11, stroke=0, fill=1)
    txt(50, t + 32, str(i + 1), 11, BG, "Helvetica-Bold", "c")
    txt(70, t + 16, a, 10, GREEN, "Helvetica-Bold")
    txt(70, t + 30, l1, 8, SOFT)
    txt(70, t + 42, l2, 8, SOFT)
    if i < len(steps) - 1:
        arrow(202, t + 56, 202, t + 71)

rect(400, 62, 412, 212)
txt(410, 79, "Decisiones de diseño", 11, GREEN, "Helvetica-Bold")
ideas = ["Sin PSRAM: ningún feed ni el cache se cargan enteros en memoria (hay feeds de casi 1 MB).",
         "La red vive en su propia tarea: el menú responde mientras se actualiza en segundo plano.",
         "Marcar un ítem como leído cambia un solo byte de cache.json, sin reescribir el archivo.",
         "Los tres archivos de la microSD son texto legible y se pueden editar a mano.",
         "El núcleo (parser, cache, configuración) no depende del equipo y se prueba en la PC",
         "contra feeds reales: RSS 2.0, Atom, RDF e ISO-8859-1.",
         "Consola por USB serie para simular teclas y sacar capturas: pruebas sin tocar el equipo.",
         "HTTPS sin validar certificados (contenido público); TZ=\"auto\" consulta un servicio externo."]
t = 98
for s in ideas:
    cont = s[0].islower()
    if not cont:
        c.setFillColor(GREEN)
        c.rect(410, H - t - 1, 4, 4, stroke=0, fill=1)
    txt(420, t + 3, s, 8.3, TEXT if not cont else TEXT)
    t += 21 if not cont else 21
    if cont:
        pass

rect(400, 288, 412, 256)
txt(410, 305, "Archivos en la microSD  (/RSSreader)", 11, GREEN, "Helvetica-Bold")


def code(x, t, title, lines):
    txt(x, t, title, 9, TEXT, "Helvetica-Bold")
    for i, line in enumerate(lines):
        txt(x, t + 14 + i * 11, line, 7.6, SOFT, "Courier")


code(410, 326, "wifi-config.txt", ["[wifi 1]", "SSID=\"mi red\"", "PASS=\"mi clave\"", "", "[wifi 2]", "SSID=\"otra red\"",
                                   "PASS=\"otra clave\""])
code(560, 326, "RSS.txt", ["TZ=\"auto\"", "", "[Hackaday]", "URL=\"https://hackaday.com/blog/feed/\"", "",
                           "[BBC Mundo]", "URL=\"https://feeds.bbci.co.uk/mundo/rss.xml\""])
code(410, 436, "cache.json  (un ítem por línea)",
     ["{\"app\":\"TenoRSS\",\"version\":1,\"saved\":1794572373,\"tz_offset\":-10800,\"feeds\":[",
      "{\"name\":\"Hackaday\",\"url\":\"...\",\"updated\":1794572373,\"status\":\"ok\",\"items\":[",
      "{\"id\":\"9f8a7c21\",\"read\":0,\"date\":1794570000,\"title\":\"...\",\"link\":\"...\",\"text\":\"...\"},",
      "..."])
txt(410, 512, "\"read\" ocupa siempre un carácter (0/1): por eso se puede marcar leído cambiando un byte.", 8, AMBER)
txt(410, 526, "Los tres archivos se crean solos si no existen.", 8, SOFT)
c.showPage()
c.save()
print("ok", OUT)
