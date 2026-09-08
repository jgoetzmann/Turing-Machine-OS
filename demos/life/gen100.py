#!/usr/bin/env python3
"""Reference for demos/life/gen100.expected.

Simulates exactly what life.c does: a 64x32 torus, B3/S23, seeded with an
R-pentomino whose bounding box top-left is (30,15), for 100 generations, then
prints the 256-byte framebuffer (8 bytes per row, MSB = leftmost pixel) as
512 lowercase hex characters with no newline. Standard library only.

    python3 demos/life/gen100.py > demos/life/gen100.expected
"""
import sys

W, H = 64, 32
GENERATIONS = 100


def run():
    cells = [[0] * W for _ in range(H)]
    for (x, y) in ((31, 15), (32, 15), (30, 16), (31, 16), (31, 17)):
        cells[y][x] = 1
    for _ in range(GENERATIONS):
        nxt = [[0] * W for _ in range(H)]
        for y in range(H):
            ym, yp = (y - 1) % H, (y + 1) % H
            for x in range(W):
                xm, xp = (x - 1) % W, (x + 1) % W
                n = (cells[ym][xm] + cells[ym][x] + cells[ym][xp]
                     + cells[y][xm] + cells[y][xp]
                     + cells[yp][xm] + cells[yp][x] + cells[yp][xp])
                nxt[y][x] = 1 if (n == 3 or (n == 2 and cells[y][x])) else 0
        cells = nxt
    out = []
    for y in range(H):
        for xb in range(8):
            b = 0
            for k in range(8):
                b = (b << 1) | cells[y][xb * 8 + k]
            out.append("%02x" % b)
    return "".join(out)


if __name__ == "__main__":
    sys.stdout.write(run())
