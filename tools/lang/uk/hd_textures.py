#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Writes the Ukrainian HD pictures to <out>, named by their HD key.

usage: hd_textures.py <out>   (for example <game folder>/data/textures/uk)

The sources are the owner's hi-res pictures in tools/lang/uk/local/ (not in
git: Konami-derived art). A missing source is skipped. The hash in a name
is the FNV-1a of the uk picture in VRAM (art.bin applied): a new art.bin
picture gives a new hash, read it from the "[hd] ... no file" log line.
"""
import os
import sys

from PIL import Image

LOCAL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "local")

# (source, crop box or None, size or None, HD file name)
PICTURES = [
    # title background, 16:9: the title stretches it over the whole canvas
    ("titlebg_uk_169.png", None, None, "titlebg_uk__512_0_256x240_8_55efe6dc.png"),
    # game-over picture: the top 16:15 of the square source, as the uk
    # low-res picture was cut
    ("go_uk_hires.png", (0, 0, 1254, 1176), (1024, 960),
     "gameover_uk__640_0_128x240_8_0a671fce.png"),
    # the artist's redraws, already at the HD size (4x, PRESS START 8x)
    ("hd/title_logo__768_0_256x136_8_00000000.png", None, None,
     "title_logo__768_0_256x136_8_3b5d70e2.png"),
    ("hd/press_start__544_432_32x48_4_00000000.png", None, None,
     "press_start__544_432_32x48_4_96f550ed.png"),
    ("hd/gameover_letters__512_0_64x128_4_00000000.png", None, None,
     "gameover_letters__512_0_64x128_4_763c876e.png"),
    # «Загрузка» and its logo; the game also draws an older picture in the
    # same place while the screen is black (03bdc4c5, bcc31dc5): no file
    ("hd/loading_text__672_488_32x24_4_00000000.png", None, None,
     "loading_text__672_488_32x24_4_871f1858.png"),
    ("hd/loading_logo__832_384_64x64_8_00000000.png", None, None,
     "loading_logo__832_384_64x64_8_9adaf1ae.png"),
]


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for src, box, size, name in PICTURES:
        path = os.path.join(LOCAL, src)
        if not os.path.exists(path):
            print("skip", src, "(no source)")
            continue
        im = Image.open(path).convert("RGBA")
        if box:
            im = im.crop(box)
        if size:
            im = im.resize(size, Image.LANCZOS)
        im.save(os.path.join(out, name), optimize=True)
        print(name, im.size)


if __name__ == "__main__":
    main()
