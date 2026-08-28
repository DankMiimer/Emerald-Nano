#!/usr/bin/env python3
"""Generate the small, original emerald-gem OPK icon using only stdlib."""

import binascii
import struct
import sys
import zlib


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)


def inside_polygon(x, y, points):
    inside = False
    previous = points[-1]
    for current in points:
        if ((current[1] > y) != (previous[1] > y)) and (
            x < (previous[0] - current[0]) * (y - current[1]) / (previous[1] - current[1]) + current[0]
        ):
            inside = not inside
        previous = current
    return inside


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "pokemon-emerald-nano.png"
    width = height = 128
    gem = [(64, 10), (108, 42), (92, 101), (64, 120), (36, 101), (20, 42)]
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            dx = x - 64
            dy = y - 64
            if dx * dx + dy * dy < 61 * 61:
                color = (7, 27, 24, 255)
            else:
                color = (0, 0, 0, 0)
            if inside_polygon(x + 0.5, y + 0.5, gem):
                if x < 64:
                    color = (10, 129, 75, 255)
                else:
                    color = (18, 164, 91, 255)
                if 48 <= x <= 80 and 42 <= y <= 55:
                    color = (203, 255, 215, 255)
            row.extend(color)
        rows.append(row)
    raw = b"".join(rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(output, "wb") as stream:
        stream.write(png)


if __name__ == "__main__":
    main()
