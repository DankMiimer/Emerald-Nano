#!/usr/bin/env python3
"""Convert an RG Nano /dev/fb0 dump (RGB565) to PNG.

/dev/fb0 is 240x720: three 240x240 pages. SDL cycles two of them, so a dump
taken at an arbitrary moment can have the live frame in any page -- dump all
three (345600 bytes) and this writes one PNG per page.

Pure stdlib: PIL is not installed in the WSL environment this runs in.
"""
import struct
import sys
import zlib

WIDTH = 240
PAGE_HEIGHT = 240
PAGE_BYTES = WIDTH * PAGE_HEIGHT * 2


def rgb565_to_rgb888(raw):
    """Return PNG scanline bytes (filter byte 0 + RGB triples) for one page."""
    out = bytearray()
    for y in range(PAGE_HEIGHT):
        out.append(0)  # filter: none
        row = raw[y * WIDTH * 2:(y + 1) * WIDTH * 2]
        for x in range(WIDTH):
            value = row[x * 2] | (row[x * 2 + 1] << 8)
            red = (value >> 11) & 0x1F
            green = (value >> 5) & 0x3F
            blue = value & 0x1F
            # 5/6-bit -> 8-bit by replicating the high bits, so 0x1F maps to 255.
            out.append((red << 3) | (red >> 2))
            out.append((green << 2) | (green >> 4))
            out.append((blue << 3) | (blue >> 2))
    return bytes(out)


def write_png(path, scanlines):
    def chunk(tag, data):
        body = tag + data
        return (struct.pack('>I', len(data)) + body
                + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF))

    header = struct.pack('>IIBBBBB', WIDTH, PAGE_HEIGHT, 8, 2, 0, 0, 0)
    with open(path, 'wb') as handle:
        handle.write(b'\x89PNG\r\n\x1a\n')
        handle.write(chunk(b'IHDR', header))
        handle.write(chunk(b'IDAT', zlib.compress(scanlines, 6)))
        handle.write(chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        print('usage: fb2png.py <fb.raw> <out-prefix>', file=sys.stderr)
        return 2
    raw = open(sys.argv[1], 'rb').read()
    pages = max(1, len(raw) // PAGE_BYTES)
    for page in range(pages):
        data = raw[page * PAGE_BYTES:(page + 1) * PAGE_BYTES]
        if len(data) < PAGE_BYTES:
            break
        path = '%s-page%d.png' % (sys.argv[2], page)
        write_png(path, rgb565_to_rgb888(data))
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
