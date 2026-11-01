#!/usr/bin/env python3
"""Draws the launcher icons (app/icon.png 80x80, app/largeIcon.png 130x130).

Pure standard library: shapes are evaluated as distance fields and
supersampled, then written as RGBA PNG. The glyph is three slider tracks with
knobs on a dark rounded tile, matching the app's look.
"""
import math
import os
import struct
import zlib

BG = (28, 28, 34)
FG = (255, 255, 255)
ACCENT = (255, 204, 77)  # warm "brightness" knob


def capsule(px, py, ax, ay, bx, by, r):
    """Distance from (px, py) to a segment of radius r."""
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy) - r


def rounded_rect(px, py, x0, y0, x1, y1, r):
    qx = max(x0 + r - px, px - (x1 - r), 0.0)
    qy = max(y0 + r - py, py - (y1 - r), 0.0)
    return math.hypot(qx, qy) - r


def shade(u, v):
    """Colour and alpha at normalised coordinates (0..1)."""
    if rounded_rect(u, v, 0.0, 0.0, 1.0, 1.0, 0.22) > 0:
        return (0, 0, 0, 0)
    color = BG
    # Slider rows: (y, knob x, knob colour)
    for y, knob, knob_color in ((0.32, 0.64, ACCENT), (0.5, 0.38, FG), (0.68, 0.56, FG)):
        if capsule(u, v, 0.22, y, 0.78, y, 0.025) <= 0:
            color = tuple(int(c * 0.55 + b * 0.45) for c, b in zip(FG, BG))
        if math.hypot(u - knob, v - y) <= 0.075:
            color = knob_color
    return color + (255,)


def render(size, samples=4):
    rows = []
    for y in range(size):
        row = bytearray([0])  # PNG filter type: none
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(samples):
                for sx in range(samples):
                    r, g, b, a = shade((x + (sx + 0.5) / samples) / size, (y + (sy + 0.5) / samples) / size)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = samples * samples
            alpha = acc[3] / n
            row += bytes([int(acc[i] / acc[3]) if acc[3] else 0 for i in range(3)] + [int(alpha)])
        rows.append(bytes(row))
    return b''.join(rows)


def write_png(path, size):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    ihdr = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(render(size), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    app = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'app')
    write_png(os.path.join(app, 'icon.png'), 80)
    write_png(os.path.join(app, 'largeIcon.png'), 130)
