#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Draws the HD pick-up banners (Level up, Heart max up, Life max up, relic).

usage: banner_hd.py <script .ttf> <textures folder> [preview.png]
  Writes <folder>/en/ and <folder>/uk/ with one PNG for each banner, named by
  its HD key (tools/psyz hdtex.h): the 104x24 picture of 4 bits (the 26x24 words the strips sample) at VRAM
  x 800, y 256 + 0x30 / 0x18 / 0 / 0x48, drawn by EntityLevelUpAnimation
  (src/dra/7E4BC.c) as 14 strips. The hash is the FNV-1a of those 26x24 words of the stock
  picture (the same in every language). The PNG is white ink with alpha at
  8x: the game tints it with the banner colour as it does the stock picture.

The stock banners are a calligraphic italic (Monotype Corsiva look). The font
file is not in git (a local file such as C:\\Windows\\Fonts\\MTCORSVA.TTF,
which has the Cyrillic letters); neither are the pictures.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

Z = 8
W, H = 104, 24  # the sampled rect: 26 words of 4 texels
BASE = 16          # baseline row of the stock pictures
CAP = 11.5         # capital height in texels (stock: about 11.5)
# name, hash, centre x of the stock text, en text, uk text
BANNERS = [
    ("banner_levelup", "11b0e0e7", 45.5, "Level up", "Новий рівень"),
    ("banner_heartmaxup", "2582019e", 48, "Heart max up", "Більше сердець"),
    ("banner_lifemaxup", "79e18e83", 47, "Life max up", "Більше життя"),
    ("banner_relic", "b2be4172", 46, "New relic", "Нова реліквія"),
]
Y0 = {"banner_lifemaxup": 0, "banner_heartmaxup": 0x18, "banner_levelup": 0x30,
      "banner_relic": 0x48}


def draw(fontpath, text, cx):
    cap = ImageFont.truetype(fontpath, 200).getbbox("H")
    size = round(200 * CAP * Z / (cap[3] - cap[1]))
    font = ImageFont.truetype(fontpath, size)
    maxw = 100 * Z
    while font.getlength(text) > maxw:  # a long line: a smaller font
        size = int(size * 0.96)
        font = ImageFont.truetype(fontpath, size)
    w = font.getlength(text)
    x = cx * Z - w / 2
    x = max(3 * Z, min(x, (W - 3) * Z - w))
    mask = Image.new("L", (W * Z, H * Z), 0)
    ImageDraw.Draw(mask).text((x, BASE * Z), text, font=font, fill=255, anchor="ls",
                                  stroke_width=2, stroke_fill=255)  # a little bolder, as the stock
    out = Image.new("RGBA", (W * Z, H * Z), (250, 248, 240, 0))
    out.putalpha(mask)
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: banner_hd.py <script .ttf> <textures folder> [preview.png]", file=sys.stderr)
        sys.exit(2)
    font, root = sys.argv[1], sys.argv[2]
    if not os.path.isfile(font):
        print("font not found: %s" % font, file=sys.stderr)
        sys.exit(2)
    pics = []
    for lang, col in (("en", 3), ("uk", 4)):
        os.makedirs(os.path.join(root, lang), exist_ok=True)
        for b in BANNERS:
            name, h = b[0], b[1]
            im = draw(font, b[col], b[2])
            y = 256 + Y0[name]
            fn = "%s__800_%d_26x24_4_%s.png" % (name, y, h)
            im.save(os.path.join(root, lang, fn), optimize=True)
            pics.append((lang, im))
    if len(sys.argv) > 3:  # a sheet: the rows are the banners, en | uk
        sheet = Image.new("RGBA", (2 * W * Z // 2 + 20, H * Z // 2 * 4), (40, 40, 60, 255))
        for i, (lang, im) in enumerate(pics):
            row = i % 4
            col = 0 if lang == "en" else 1
            small = im.resize((W * Z // 2, H * Z // 2), Image.LANCZOS)
            sheet.alpha_composite(small, (col * (W * Z // 2 + 20), row * H * Z // 2))
        sheet.save(sys.argv[3])


if __name__ == "__main__":
    main()
