#!/usr/bin/env python3
"""Generates the icons/diary-*.png icons: the same red leather diary as the
classic Mac version's icon (drawn on a 32x32 and a 16x16 grid), scaled up as
pixel art for 48x48 (16x16 times 3), 64x64 and 128x128.
Run:  python3 tools/make_png.py icons
"""

import os
import struct
import sys
import zlib

COLOURS = {
    'K': (0x00, 0x00, 0x00),    # black outline
    'R': (0xCC, 0x00, 0x00),    # red leather cover
    'H': (0xFF, 0x66, 0x66),    # cover highlight
    'D': (0x66, 0x00, 0x00),    # dark red spine / strap
    'W': (0xFF, 0xFF, 0xFF),    # page white
    'G': (0xCC, 0xCC, 0xCC),    # page edge lines
    'Y': (0xFF, 0xCC, 0x00),    # gold
    'O': (0xCC, 0x99, 0x00),    # dark gold
    'B': (0x00, 0x66, 0xFF),    # ribbon
}


def new_grid(n):
    return [['.'] * n for _ in range(n)]


def rect(g, top, left, bottom, right, c):
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            g[y][x] = c


def frame(g, top, left, bottom, right, c):
    for x in range(left, right + 1):
        g[top][x] = g[bottom][x] = c
    for y in range(top, bottom + 1):
        g[y][left] = g[y][right] = c


def large():
    g = new_grid(32)
    # Page block peeking out at the right and bottom
    rect(g, 3, 5, 29, 27, 'W')
    frame(g, 3, 5, 29, 27, 'K')
    for y in range(5, 28, 2):
        g[y][26] = 'G'
    for x in range(7, 26, 2):
        g[28][x] = 'G'
    # Cover
    rect(g, 1, 3, 27, 25, 'R')
    frame(g, 1, 3, 27, 25, 'K')
    for x in range(9, 25):
        g[2][x] = 'H'
    for y in range(2, 27):
        g[y][9] = 'H'
    # Spine
    rect(g, 2, 4, 26, 7, 'D')
    for x in range(4, 8):
        g[5][x] = g[23][x] = 'Y'
    for y in range(2, 27):
        g[y][8] = 'K'
    # Gold label with two text lines
    rect(g, 6, 12, 12, 22, 'W')
    frame(g, 6, 12, 12, 22, 'O')
    for x in range(14, 21):
        g[8][x] = 'G'
    for x in range(14, 19):
        g[10][x] = 'G'
    # Strap across the right edge with a gold clasp
    rect(g, 15, 19, 19, 27, 'D')
    frame(g, 15, 19, 19, 27, 'K')
    rect(g, 14, 20, 20, 23, 'Y')
    frame(g, 14, 20, 20, 23, 'K')
    g[17][21] = g[17][22] = 'O'
    # Ribbon bookmark with a notched end
    rect(g, 28, 11, 31, 13, 'B')
    g[31][12] = '.'
    for y in (30, 31):
        g[y][10] = g[y][14] = 'K'
    return g


def small():
    g = new_grid(16)
    rect(g, 2, 3, 15, 14, 'W')
    frame(g, 2, 3, 15, 14, 'K')
    rect(g, 0, 1, 13, 12, 'R')
    frame(g, 0, 1, 13, 12, 'K')
    rect(g, 1, 2, 12, 3, 'D')
    for y in range(1, 13):
        g[y][4] = 'K'
    rect(g, 3, 6, 5, 10, 'W')
    frame(g, 3, 6, 5, 10, 'O')
    rect(g, 7, 10, 9, 14, 'D')
    frame(g, 7, 10, 9, 14, 'K')
    rect(g, 7, 10, 9, 11, 'Y')
    g[14][7] = g[15][7] = 'B'
    return g


def scaled(g, factor):
    return [[c for c in row for _ in range(factor)] for row in g for _ in range(factor)]


def png(g):
    """An RGBA PNG; '.' is transparent."""
    rows = b''.join(b'\0' + b''.join(bytes(COLOURS[c]) + b'\xff' if c != '.' else b'\0\0\0\0'
                                      for c in row) for row in g)
    size = len(g)

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    big, sml = large(), small()
    icons = {16: sml, 32: big, 48: scaled(sml, 3), 64: scaled(big, 2), 128: scaled(big, 4)}
    for size, grid in icons.items():
        with open(os.path.join(sys.argv[1], 'diary-%d.png' % size), 'wb') as f:
            f.write(png(grid))


main()
