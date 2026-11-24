#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HD digits of the Alucard HUD (src/dra/5F60C.c Hud_Layout): PNG files for
data/textures/common. They are the stock pixel-art digits of your own
F_GAME.BIN, made bigger with no smoothing: the HUD draws the digits at 3/4
and 5/4 of a game pixel, and at the 1x canvas such pixels come out uneven;
over HD every stock pixel keeps one even size. The colours are those of
the mockup: the small row 0x196, the big row 0x171 (white). The game
animates 0x171 with the HP state, so the hearts draw with 0x196 and its
file holds the 0x171 colours. The file names are the psyz HD keys
(hdtex.h): VRAM rect, FNV-1a of the stock rows, FNV-1a of the palette.

usage: hud_digits.py <F_GAME.BIN> <out dir> [--scale N]
"""
import os
import struct
import sys

from PIL import Image

# One rect (VRAM halfwords) holds both digit rows: the small 8x8 row at v 0x18
# and the big 8x16 row at v 0x20. In play only allowed rects get HD
# (src/pc/main.c Psyz_HdTexAllow), so both rows share one rect.
RECT = (968, 280, 20, 24)
# the palette the HUD draws with: (small row, big row) colours.
# 0x196 = normal, 0x193 = the hearts flicker when the sub-weapon can be used
CLUTS = {0x196: (0x196, 0x171), 0x193: (0x193, 0x193)}
SMALL_ROWS = 8  # rect rows 0..7 = the small digits, 8..23 = the big ones


def fnv(data, h=0x811C9DC5):
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def vram_rows(gfx, x, y, w, h):
    """the rect bytes: F_GAME.BIN holds VRAM x 512..1023, y 256..511 in
    blocks of 32 halfwords by 256 rows"""
    out = bytearray()
    for r in range(y - 256, y - 256 + h):
        for hw in range(x - 512, x - 512 + w):
            out += gfx[((hw // 32 * 256 + r) * 32 + hw % 32) * 2:][:2]
    return bytes(out)


def rgba(c):
    if c == 0:
        return (0, 0, 0, 0)
    return tuple(((c >> s & 31) << 3) | ((c >> s & 31) >> 2)
                 for s in (0, 5, 10)) + (255,)


def main():
    args = sys.argv[1:]
    scale = 8
    if "--scale" in args:
        i = args.index("--scale")
        scale = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    data = open(args[0], "rb").read()
    gfx, pals = data[:0x40000], data[0x40000:]
    x, y, w, h = RECT
    rows = vram_rows(gfx, *RECT)
    texels = [b >> s & 15 for b in rows for s in (0, 4)]
    os.makedirs(args[1], exist_ok=True)

    def pal(clut):
        return [rgba(c) for c in struct.unpack("<16H", words(clut))]

    def words(clut):
        return pals[(clut - 0x100) * 32:][:32]

    for clut, (small, big) in CLUTS.items():
        pals2 = (pal(small), pal(big))
        img = Image.new("RGBA", (w * 4, h))
        img.putdata([pals2[i >= SMALL_ROWS * w * 4][t]
                     for i, t in enumerate(texels)])
        img = img.resize((w * 4 * scale, h * scale), Image.NEAREST)
        path = os.path.join(args[1], "hud_digits__%d_%d_%dx%d_4_%08x_c%08x.png"
                            % (RECT + (fnv(rows), fnv(words(clut)))))
        img.save(path)
        print(path)


if __name__ == "__main__":
    main()
