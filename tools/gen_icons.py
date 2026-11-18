#!/usr/bin/env python3
"""Генерация иконок nc-notify всех размеров (только stdlib)."""
import os
import struct
import zlib

SIZES = [16, 22, 24, 32, 48, 64, 128, 256]
BLUE = (44, 111, 187, 255)
WHITE = (255, 255, 255, 255)
TRANS = (0, 0, 0, 0)

SVG = '''<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <circle cx="32" cy="32" r="30" fill="#2c6fbb"/>
  <path d="M19,20 L25,20 L39,38 L39,20 L45,20 L45,44 L39,44 L25,26 L25,44 L19,44 Z" fill="#ffffff"/>
</svg>
'''


def render(size):
    k = size / 64.0
    cx = cy = 31.5 * k
    r = 30.0 * k
    y0, y1 = 20 * k, 43 * k
    lx0, lx1 = 19 * k, 24 * k
    rx0, rx1 = 39 * k, 44 * k
    w = max(0.9, 2.6 * k)
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            dx, dy = x - cx, y - cy
            if dx * dx + dy * dy > r * r:
                raw += bytes(TRANS)
                continue
            color = BLUE
            if y0 <= y <= y1:
                if lx0 <= x <= lx1 or rx0 <= x <= rx1:
                    color = WHITE
                else:
                    diag = lx0 + (y - y0) * (rx1 - lx0) / max(1e-6, (y1 - y0))
                    if abs(x - diag) <= w:
                        color = WHITE
            raw += bytes(color)
    return bytes(raw)


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))


def write_png(path, size):
    raw = render(size)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


for s in SIZES:
    write_png("icons/hicolor/%dx%d/apps/nc-notify.png" % (s, s), s)
    print("icons/hicolor/%dx%d/apps/nc-notify.png" % (s, s))
write_png("icons/nc-notify.png", 64)
os.makedirs("icons/hicolor/scalable/apps", exist_ok=True)
with open("icons/hicolor/scalable/apps/nc-notify.svg", "w", encoding="utf-8") as f:
    f.write(SVG)
print("icons/hicolor/scalable/apps/nc-notify.svg")
print("Готово: иконки сгенерированы")
