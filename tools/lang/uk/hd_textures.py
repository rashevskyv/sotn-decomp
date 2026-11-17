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
