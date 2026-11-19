#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Draws the uk prologue title «КАСТЕЛЬВАНІЯ» for the HD pack.

usage: prologue_title.py <serif font .ttf> [<out png>]
  out: tools/lang/uk/local/hd/prologue_title__704_0_32x128_4_00000000.png

The prologue (src/st/st0/prologue_scroll.c) draws its title from VRAM
704,0 (4bpp, 128x128 texels) as four 32-row strips side by side: texels
0..128 of rows 0..32 at frame x 32, the next strip at x 160 and so on, the
last strip 64 texels wide. The title spans frame x 32..480, y 104..136.
This script writes thin, widely spaced capitals at 8x in the same layout
(transparent background). The font file is not in git; neither is the
picture (hd_textures.py copies it into the pack).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

TEXT = "КАСТЕЛЬВАНІЯ"
Z = 8  # HD texels per PS1 texel
W, H = 448, 32  # the title area in PS1 texels
LEFT, RIGHT = 6, 442  # the outer letter edges (stock title: 3..445)
BASE, CAP = 27, 23  # baseline row and capital height (stock: rows 4..27)
COLOR = (232, 226, 200)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        here, "local", "hd", "prologue_title__704_0_32x128_4_00000000.png")
    font = ImageFont.truetype(sys.argv[1], 100 * Z)
    cap = font.getbbox("Н")
    font = ImageFont.truetype(sys.argv[1], round(100 * Z * CAP * Z / (cap[3] - cap[1])))
    boxes = [font.getbbox(c) for c in TEXT]
    widths = [b[2] - b[0] for b in boxes]
    # letter centres evenly spaced between the two outer edges
    c0 = LEFT * Z + widths[0] / 2
    c1 = RIGHT * Z - widths[-1] / 2
    strip = Image.new("RGBA", (W * Z, H * Z), COLOR + (0,))
    draw = ImageDraw.Draw(strip)
    base = font.getbbox("Н")[3]
    for i, (c, b, w) in enumerate(zip(TEXT, boxes, widths)):
        x = c0 + (c1 - c0) * i / (len(TEXT) - 1) - w / 2 - b[0]
        draw.text((round(x), BASE * Z - base), c, font=font, fill=COLOR + (255,))
    pic = Image.new("RGBA", (128 * Z, 128 * Z), COLOR + (0,))
    for i in range(4):
        x0 = i * 128 * Z
        pic.paste(strip.crop((x0, 0, min(x0 + 128 * Z, W * Z), H * Z)),
                  (0, i * H * Z))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    pic.save(out, optimize=True)
    print(out, pic.size)


if __name__ == "__main__":
    main()
