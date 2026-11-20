#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Draws the HD stage-name banners and the ST0 title card, uk and en.

usage: stage_names_hd.py <MTCORSVA.TTF> <out dir>
  writes <out>/uk/*.png and <out>/en/*.png (copy them to data/textures/uk|en)

All the pictures live on VRAM 64,256 (4 bpp, 128x128 texels). A banner is a
240x40 strip cut in two: strip texels 0..119 on rows 0..39 at u 0, texels
120..239 on rows 64..103 at u 8 (src/st/e_stage_name_us.h). The ST0 card
has two lines on rows 1..30 (src/st/st0/title_card.c).
This script draws the same layout at 8x (1024x1024, transparent), in the
chancery italic of the stock art (Monotype Corsiva: not in git, neither
are the pictures).

uk: the recipe of the 1x labels in tools/lang/uk/labels (26 pt, baseline
row 27, a 0.3 texel stroke, the widest tracking that fits 224 texels; the
card 14 pt + 17 pt), so the HD ink sits on the 1x ink.
en: size, tracking and place fitted to the stock en art (EN_FIT).

The stock picture is a 16-level coverage ramp and each pass colours it with
its own CLUT: fill 0x19F (dark to white), outline 0x19D (inverted: light at
the glyph edge, dark inside), card 0x210 (gold). An HD pixel takes the CLUT
colour of the 1x-equivalent coverage (an 8x8 box of the 8x ink) and the
alpha of the 8x ink grown by the 1x footprint, so both passes keep the
stock look (the light edge of the outline pass, the dark ring of the card)
with smooth curves.
One file per CLUT. A page hash is the FNV-1a of the picture in VRAM (uk:
art.bin applied); a new label gives a new hash, read it from the
"[hd] 64 256 32x128 4: hash ... no file" log line.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

Z = 8  # HD pixels per texel
PAGE = 128
STRIP_W, STRIP_ROWS, STRIP_CUT, STRIP_U, STRIP_V = 240, 40, 120, 8, 64
KEY = "__64_256_32x128_4_"

# CLUT hash -> the 15 RGB colours of indices 1..15 (index 0 is transparent)
FILL, OUTLINE, CARD = "3c70fb2d", "44264fd7", "7e2742b6"
CLUTS = {
    FILL: [(v, v, v) for v in (16, 32, 49, 65, 82, 98, 115, 131, 148, 164,
                               180, 197, 213, 230, 246)],
    OUTLINE: [(v, v, v) for v in (222, 189, 164, 139, 115, 98, 82, 65, 57,
                                  49, 41, 32, 24, 16, 8)],
    CARD: [(0, 0, 0), (24, 24, 8), (49, 49, 24), (82, 74, 49), (74, 74, 24),
           (98, 98, 32), (106, 106, 65), (131, 123, 41), (156, 139, 41),
           (148, 139, 82), (164, 156, 106), (197, 189, 65), (222, 205, 123),
           (197, 189, 123), (222, 222, 148)],
}

# stage -> (en page hash, en name, uk page hash, uk name)
ZONES = {
    "are": ("7b4ca22e", "Colosseum", "817c84d1", "Колізей"),
    "cat": ("3a4cc0ec", "Catacombs", "d2a1eb99", "Катакомби"),
    "chi": ("e4062d4f", "Abandoned Mine", "1b6c7766", "Покинута копальня"),
    "dai": ("bebf4767", "Royal Chapel", "42e54620", "Королівська каплиця"),
    "dre": ("7ee874b4", "Nightmare", "aec7b4a3", "Жахіття"),
    "lib": ("eb4b08e2", "Long Library", "ee4ba2af", "Довга бібліотека"),
    "no0": ("5e3fbf3b", "Marble Gallery", "29211397", "Мармурова галерея"),
    "no1": ("16c9f4b8", "Outer Wall", "968e7de7", "Зовнішній мур"),
    "no2": ("5ade5379", "Olrox's Quarters", "34131c43", "Покої Олрокса"),
    "no4": ("c989bb3f", "Underground Caverns", "df558e0c", "Підземні печери"),
    "nz0": ("13bdab64", "Alchemy Laboratory", "98998db8",
            "Алхімічна лабораторія"),
    "nz1": ("bae6cddf", "Clock Tower", "169488ef", "Годинникова вежа"),
    "top": ("a26aef59", "Castle Keep", "8fedea36", "Головна вежа"),
}
CARD_EN = ("9372167d", (("Final Stage :", "st0a"), ('"Bloodlines"', "st0b")))
CARD_UK = ("fa122c59", (("Останній етап :", 14), ('"Кровні узи"', 17)))

# en: point size, tracking (texels), ink top-left in the strip (HD px),
# fitted to the stock art by correlation of the 1x box. No stroke: the
# stock strokes are thin, a fitted 1..2 px stroke made the HD text bold
EN_FIT = {
    "are": (28.5, 1.5, 496, 44),
    "cat": (29.5, 1.5, 476, 44),
    "chi": (28.5, 1.5, 168, 44),
    "dai": (30.5, 1.5, 316, 44),
    "dre": (31.0, 1.0, 456, 36),
    "lib": (29.5, 1.5, 360, 44),
    "no0": (29.5, 1.5, 248, 44),
    "no1": (29.0, 1.5, 424, 44),
    "no2": (28.5, 2.0, 192, 44),
    "no4": (29.0, 0.0, 68, 44),
    "nz0": (29.5, 0.0, 96, 44),
    "nz1": (29.0, 1.5, 384, 36),
    "top": (29.5, 1.5, 416, 44),
    "st0a": (14.5, 1.0, 236, 12),
    "st0b": (21.0, 0.5, 148, 116),
}


def draw(font_path, text, size, track, stroke, base=40 * Z):
    """One line at 8x on a wide canvas: (canvas, ink bbox)."""
    font = ImageFont.truetype(font_path, round(size * Z))
    big = Image.new("L", (320 * Z, 60 * Z))
    d, x = ImageDraw.Draw(big), 10.0 * Z
    if track is None:  # the whole line in one call (kerning), as banner()
        d.text((x, base), text, font=font, fill=255, anchor="ls",
               stroke_width=stroke, stroke_fill=255)
        return big, big.getbbox()
    for ch in text:
        d.text((x, base), ch, font=font, fill=255, anchor="ls",
               stroke_width=stroke, stroke_fill=255)
        x += font.getlength(ch) + track * Z
    return big, big.getbbox()


def uk_zone(font_path, text):
    """tools/lang/uk recipe (Picoripi ps1_vram._line) without the 1x box."""
    for track, shrink in ((2, 0), (1.5, 0), (1, 0), (0.5, 0), (0, 0), (0, 1),
                          (0, 2)):
        big, (left, _, right, _) = draw(font_path, text, 26 - shrink, track,
                                        Z * 3 // 10, 27 * Z)
        lw = -(-(right - left) // Z)
        if lw <= 224:
            strip = Image.new("L", (STRIP_W * Z, STRIP_ROWS * Z))
            strip.paste(big.crop((left, 0, right, STRIP_ROWS * Z)),
                        (((STRIP_W - lw) // 2) * Z, 0))
            return strip
    raise ValueError(f"{text!r} does not fit")


def en_zone(font_path, text, fit):
    size, track, x, y = fit
    big, box = draw(font_path, text, size, track, 0)
    strip = Image.new("L", (STRIP_W * Z, STRIP_ROWS * Z))
    strip.paste(big.crop(box), (x, y))
    return strip


def strip_page(strip):
    page = Image.new("L", (PAGE * Z, PAGE * Z))
    page.paste(strip.crop((0, 0, STRIP_CUT * Z, STRIP_ROWS * Z)), (0, 0))
    page.paste(strip.crop((STRIP_CUT * Z, 0, STRIP_W * Z, STRIP_ROWS * Z)),
               (STRIP_U * Z, STRIP_V * Z))
    return page


def uk_card(font_path):
    """Picoripi ps1_vram.banner without the 1x box: 96x30 at page 16,1."""
    page = Image.new("L", (PAGE * Z, PAGE * Z))
    for k, (text, size) in enumerate(CARD_UK[1]):
        big, box = draw(font_path, text, size, None, Z * 3 // 10)
        ink = big.crop(box)
        lw, lh = -(-ink.width // Z), -(-ink.height // Z)
        page.paste(ink, ((16 + (96 - lw) // 2) * Z,
                         (1 + (30 - lh if k else 1)) * Z))
    return page


def en_card(font_path):
    page = Image.new("L", (PAGE * Z, PAGE * Z))
    for text, key in CARD_EN[1]:
        size, track, x, y = EN_FIT[key]
        big, box = draw(font_path, text, size, track, 0)
        page.paste(big.crop(box), (x, y))
    return page


def box8(a):
    """Mean over the 8x8 box around each pixel (1x-equivalent coverage)."""
    p = np.pad(a, ((Z // 2, Z // 2), (Z // 2, Z // 2)))
    c = np.pad(p.cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    h, w = a.shape
    return (c[Z:Z + h, Z:Z + w] - c[:h, Z:Z + w] - c[Z:Z + h, :w]
            + c[:h, :w]) / (Z * Z)


def colour(page, clut):
    """The 8x ink as RGBA through one CLUT."""
    a = np.asarray(page, np.float64) / 255
    index = box8(a) * 15  # the 1x index, sampled at every HD pixel
    # alpha: the 8x ink, grown by the 1x footprint (index 1 and up), so the
    # low indices keep their colour: the light edge of the outline pass, the
    # dark ring of the fill and of the card
    a = np.maximum(a, np.clip((index - 0.25) * 2, 0, 1))
    level = np.clip(index, 1, 15) - 1  # 0..14 into CLUTS[clut]
    ramp = np.array(CLUTS[clut], np.float64)
    lo = np.floor(level).astype(int)
    hi = np.minimum(lo + 1, 14)
    t = (level - lo)[..., None]
    rgb = ramp[lo] * (1 - t) + ramp[hi] * t
    out = np.dstack([rgb, a * 255]).round().astype(np.uint8)
    out[a == 0] = 0
    return Image.fromarray(out, "RGBA")


def check_uk(font_path):
    """The uk HD ink, boxed to 1x, must give the 1x labels (same recipe)."""
    labels = os.path.join(os.path.dirname(os.path.abspath(__file__)), "uk",
                          "labels")
    pages = [(f"zone_{st.upper()}", strip_page(uk_zone(font_path, z[3])))
             for st, z in ZONES.items()] + [("bloodlines", uk_card(font_path))]
    for name, page in pages:
        one = page.resize((PAGE, PAGE), Image.BOX).point(
            lambda v: (v * 15 + 127) // 255)
        lab = Image.open(os.path.join(labels, name + ".png"))
        if lab.size != (PAGE, PAGE):  # the card label is its 96x30 rect
            one = one.crop((16, 1, 16 + lab.width, 1 + lab.height))
        bad = sum(a != b for a, b in zip(one.tobytes(), lab.tobytes()))
        print("check", name, "texels off by any level:", bad)


def main():
    font_path, out = sys.argv[1], sys.argv[2]
    if out == "--check":
        return check_uk(font_path)
    for lang in ("uk", "en"):
        os.makedirs(os.path.join(out, lang), exist_ok=True)
    jobs = []
    for st, (en_hash, en, uk_hash, uk) in ZONES.items():
        jobs.append(("uk", st, uk_hash, strip_page(uk_zone(font_path, uk)),
                     (FILL, OUTLINE)))
        jobs.append(("en", st, en_hash,
                     strip_page(en_zone(font_path, en, EN_FIT[st])),
                     (FILL, OUTLINE)))
    jobs.append(("uk", "st0", CARD_UK[0], uk_card(font_path), (CARD,)))
    jobs.append(("en", "st0", CARD_EN[0], en_card(font_path), (CARD,)))
    for lang, st, page_hash, page, cluts in jobs:
        for clut in cluts:
            name = f"stagename_{st}{KEY}{page_hash}_c{clut}.png"
            colour(page, clut).save(os.path.join(out, lang, name),
                                    optimize=True)
            print(lang, name)


if __name__ == "__main__":
    main()
