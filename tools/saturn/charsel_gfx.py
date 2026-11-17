#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# The art of the PC select screen (src/st/sel/charsel.h).
#
# usage: charsel_gfx.py <disks/saturn> <art dir> <out.h>
#        charsel_gfx.py --cutout <art dir> <png dir>
#
# The portraits come from the owner's art in <art dir> (the env var
# SOTN_CHARSEL_ART at configure time; see OWNER_ART), cut out of their white
# background, cropped to the card and put on the card's blue fill. A file
# that is not there falls back to the Saturn portrait. The Saturn disc also
# gives the "Player select" header and the gothic names (English art). The
# frame and the arrows are drawn in code (charsel_frame.py).
#
# The output holds Konami's art: it goes into the build dir, never into git.
# --cutout writes the cut-out art and the card portraits as PNGs to check.
#
# Saturn: TITLE.PRG registers its VDP1 sprite banks in the order of BANKS;
# their pixels follow one another in TITLE.CHR. A bank is {w/2, h/2, u16
# offset in 8-byte units} records and a {0, 0, end} terminator. The portraits
# are the 80x88 8bpp banks of PORTRAITS; each palette is a u16 header
# (0x4010) and RGB555 colours, of which 0..127 are used (0 is transparent).
# The header and the gothic names are a VDP2 4bpp cell layer: TITLE.MAP,
# addressed by TITLE.PRG with MAP_DELTA. Its characters and palette are LZSS
# streams; the screen's 40x64-cell name table is raw at LAYER_MAP.
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import charsel_frame as frame  # noqa: E402

BASE = 0x060A5000
BANKS = [0x60B1116, 0x60B1328, 0x60B1438, 0x60B1666, 0x60B1870, 0x60B1A7A]
PORTRAITS = [  # name, bank, palette header
    ("alucard", 0x60B1666, 0x60B1464),
    ("richter", 0x60B1870, 0x60B166E),
    ("maria", 0x60B1A7A, 0x60B1878),
]
W, H, NCOL = 80, 88, 128
MAP_DELTA = 0x4000
LAYER_CHARS = (0x664CC, 0x4D46)
LAYER_PAL = (0x6B212, 0x112)
LAYER_MAP = 0x40FC0
LAYER_ELEMS = [  # name, col, row, w, h (cells)
    ("header", 2, 4, 18, 4),
    ("name_alucard", 15, 23, 11, 3),
    ("name_richter", 3, 23, 11, 3),
    ("name_maria", 27, 23, 11, 3),
]
RAMP = 0x60ACB10  # cursor colours: row 6 of this 20-row ramp

# The card portraits: slot, the owner's file, the crop box in its pixels
# (left, top, right, bottom; the window's aspect) and the rows of a fade to
# white at its bottom (or None). The Saturn has one Maria for both slots.
SLOTS = ["alucard", "richter", "maria", "maria_psx"]
OWNER_ART = {
    "alucard": ("alucard.webp", (190, 8, 466, 310), None),
    "richter": ("richter.webp", (130, 40, 420, 358), None),
    "maria": ("maria_psp.png", (88, 22, 278, 230), (420, 506)),
    "maria_psx": ("maria_psx.png", (80, 20, 368, 335), None),
}
SATURN_OF = {"maria_psx": "maria"}
WIN_X, WIN_Y, WIN_W, WIN_H = frame.WIN


def ps1_colour(c, index):
    if index == 0:
        return 0  # transparent
    v = c & 0x7FFF  # same RGB555 bit order; Saturn bit 15 is not PS1 STP
    return v if v else 0x8000  # opaque black needs STP on the PS1


def rgb555(rgb):
    r, g, b = rgb[:3]
    return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10


def portraits(prg, chr_):
    """the Saturn portraits: {name: (80x88 indices, 128 RGB555 colours)}"""
    cur, offs = 0, {}
    for addr in BANKS:
        o, k = addr - BASE, 0
        offs[addr] = cur
        while True:
            w, h, off = struct.unpack_from(">BBH", prg, o + 4 * k)
            if w == 0:
                cur += off * 8
                break
            k += 1
    out = {}
    for name, bank, pal in PORTRAITS:
        w, h, off = struct.unpack_from(">BBH", prg, bank - BASE)
        assert (w * 2, h * 2, off) == (W, H, 0), (name, w, h, off)
        px = chr_[offs[bank]:offs[bank] + W * H]
        assert len(px) == W * H and max(px) < NCOL, name
        hdr = struct.unpack_from(">H", prg, pal - BASE)[0]
        assert hdr == 0x4010, (name, hex(hdr))
        out[name] = (px, struct.unpack_from(">%dH" % NCOL, prg, pal - BASE + 2))
    return out


def layer(prg, mp):
    from tile_dumper import decompress_lzss

    def stream(off, size):
        return decompress_lzss(mp[off - MAP_DELTA:off - MAP_DELTA + size])

    chars = stream(*LAYER_CHARS)
    pal = struct.unpack(">256H", stream(*LAYER_PAL))
    cells = struct.unpack_from(">1280H", mp, LAYER_MAP - MAP_DELTA)

    def tile(v):
        t, fx, fy = v & 0x3FF, v & 0x400, v & 0x800
        out = []
        for y in range(8):
            sy = 7 - y if fy else y
            for x in range(8):
                sx = 7 - x if fx else x
                c = chars[t * 32 + sy * 4 + sx // 2]
                out.append(c >> 4 if sx % 2 == 0 else c & 15)
        return tuple(out)

    pool, maps, banks = [], {}, {}
    for name, col, row, w, h in LAYER_ELEMS:
        m = []
        for y in range(row, row + h):
            for x in range(col, col + w):
                v = cells[y * 40 + x]
                t = tile(v)
                if name.startswith("name") and y == row:
                    # the old frame's bottom spills into the names' top row:
                    # the letters are greys 2..5, the spill outline 1, greens
                    t = tuple(0 if p == 1 or p >= 6 else p for p in t)
                if not any(t):
                    m.append(0)
                    continue
                banks.setdefault(name, set()).add(v >> 12)
                if t not in pool:
                    pool.append(t)
                m.append(pool.index(t) + 1)
        maps[name] = m
    assert banks["header"] == {6}, banks
    assert all(b <= {2, 7, 13, 14} for k, b in banks.items() if k != "header")
    assert len(pool) < 256
    for b in (2, 7, 14):  # the other frames' banks: the same colours
        assert pal[b * 16 + 1:b * 16 + 16] == pal[13 * 16 + 1:13 * 16 + 16]
    cluts = [[ps1_colour(pal[b * 16 + i], i) for i in range(16)] for b in (6, 13)]
    ramp = struct.unpack_from(">16H", prg, RAMP + 6 * 32 - BASE)
    cluts.append([ps1_colour(ramp[i], i) for i in range(16)])
    return pool, maps, cluts


def cutout(rgb, fade):
    """RGBA: the white background flooded from the top and the sides away,
    a 1-pixel feather on the edge, the fade to white at the bottom undone"""
    import numpy as np
    from PIL import Image

    a = np.asarray(rgb, dtype=np.float32)
    white = a.sum(2) > 765 - 40
    bg = np.zeros(white.shape, bool)
    bg[0], bg[-1] = white[0], white[-1]
    bg[:, 0], bg[:, -1] = white[:, 0], white[:, -1]

    def grow(m):
        g = m.copy()
        g[1:] |= m[:-1]
        g[:-1] |= m[1:]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        return g

    while True:  # flood: grow into white neighbours until it stops
        g = grow(bg) & white
        if (g == bg).all():
            break
        bg = g
    # white pockets the flood cannot reach (between curls of hair): close to
    # the background and pure white on average, where a highlight on skin or
    # cloth is a bit greyer
    s = a.sum(2)
    close = bg
    for _ in range(20):
        close = grow(close)
    todo = white & ~bg
    h, w = todo.shape
    for y0, x0 in zip(*todo.nonzero()):
        if not todo[y0, x0]:
            continue
        comp, stack = [], [(y0, x0)]
        todo[y0, x0] = False
        while stack:
            y, x = stack.pop()
            comp.append((y, x))
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < h and 0 <= nx < w and todo[ny, nx]:
                    todo[ny, nx] = False
                    stack.append((ny, nx))
        ys, xs = zip(*comp)
        if len(comp) >= 30 and s[ys, xs].mean() >= 749 and close[ys, xs].any():
            bg[ys, xs] = True
    near = grow(bg)
    alpha = np.where(bg, 0.0, 1.0)
    edge = near & ~bg
    alpha[edge] = np.clip((255 - a[edge].mean(1)) / 96, 0.2, 1.0)
    if fade:
        y0, y1 = fade
        ramp = np.clip((y1 - np.arange(a.shape[0])) / (y1 - y0), 0.0, 1.0)
        alpha = np.minimum(alpha, ramp[:, None])
    # a pixel blended with white: take the white out of its colour
    k = np.maximum(alpha, 1e-3)[..., None]
    a = np.clip((a - (1 - k) * 255) / k, 0, 255)
    out = np.dstack([a, alpha[..., None] * 255]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def fill():
    """the card's fill under the portrait, as charsel.h draws it"""
    from PIL import Image

    im = Image.new("RGBA", (WIN_W, WIN_H))
    for y in range(WIN_H):
        t = (y + 0.5) / WIN_H
        c = tuple(round(p + (q - p) * t)
                  for p, q in zip(frame.FILL_TOP, frame.FILL_BOTTOM))
        for x in range(WIN_W):
            im.putpixel((x, y), c + (255,))
    return im


def owner_card(path, box, fade):
    from PIL import Image, ImageFilter

    rgba = cutout(Image.open(path).convert("RGB"), fade)
    card = rgba.crop(box).resize((WIN_W, WIN_H), Image.LANCZOS)
    card = card.filter(ImageFilter.UnsharpMask(radius=0.8, percent=50, threshold=2))
    return rgba, Image.alpha_composite(fill(), card).convert("RGB")


def saturn_card(px, cols):
    """the 80x88 Saturn portrait in the middle of the window, unscaled"""
    from PIL import Image

    card = fill()
    ox, oy = (WIN_W - W) // 2, (WIN_H - H) // 2
    for i, v in enumerate(px):
        if v:
            c = cols[v]
            rgb = ((c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3)
            card.putpixel((ox + i % W, oy + i // W), rgb + (255,))
    return card.convert("RGB")


def quantize(card):
    """8bpp: 128 colours, none of them transparent"""
    from PIL import Image

    try:
        q = card.quantize(NCOL, method=Image.Quantize.LIBIMAGEQUANT, dither=0)
    except (ValueError, OSError):
        q = card.quantize(NCOL, method=Image.Quantize.MEDIANCUT, dither=0)
    pal = q.getpalette()[:NCOL * 3]
    pal += [0] * (NCOL * 3 - len(pal))
    clut = [rgb555(pal[i * 3:i * 3 + 3]) or 0x8000 for i in range(NCOL)]
    return list(q.getdata()), clut, q


def cards(saturn, art):
    """{slot: (8bpp pixels, clut)}, or None when a slot has no picture"""
    try:
        import numpy  # noqa: F401
        import PIL  # noqa: F401
    except ImportError as e:
        print("charsel_gfx: no PIL/numpy, no portraits: %r" % (e,))
        return None
    out = {}
    for slot in SLOTS:
        name, box, fade = OWNER_ART[slot]
        path = os.path.join(art, name) if art else ""
        if path and os.path.isfile(path):
            card = owner_card(path, box, fade)[1]
            src = "owner art " + name
        elif saturn:
            card = saturn_card(*saturn[SATURN_OF.get(slot, slot)])
            src = "Saturn"
        else:
            print("charsel_gfx: %s: no picture, no portraits" % slot)
            return None
        print("charsel_gfx: %s <- %s" % (slot, src))
        out[slot] = quantize(card)[:2]
    return out


def arr(lines, decl, vals, fmt, per):
    lines.append("static const %s = {" % decl)
    for j in range(0, len(vals), per):
        lines.append("    " + ", ".join(fmt % v for v in vals[j:j + per]) + ",")
    lines.append("};")


def generate(disc, art):
    lines = [
        "#define CHARSEL_FRAME_W %d" % frame.W,
        "#define CHARSEL_FRAME_H %d" % frame.H,
        "#define CHARSEL_WIN_X %d" % WIN_X,
        "#define CHARSEL_WIN_Y %d" % WIN_Y,
        "#define CHARSEL_WIN_W %d" % WIN_W,
        "#define CHARSEL_WIN_H %d" % WIN_H,
        "#define CHARSEL_ARROW_W %d" % len(frame.arrow(True)[0]),
        "#define CHARSEL_ARROW_H %d" % len(frame.arrow(True)),
        "#define CHARSEL_CLUT_LEN %d" % NCOL,
    ]
    lines.append("static const u8 g_CharselFill[2][3] = {{%d, %d, %d}, {%d, %d, %d}};"
                 % (frame.FILL_TOP + frame.FILL_BOTTOM))
    # the frame and the arrows: 4bpp, two CLUTs (a card, the chosen card)
    arr(lines, "u16 g_CharselFrame[%d]" % (frame.W * frame.H // 4),
        frame.words4(frame.frame()), "0x%04X", 12)
    for name, up in (("up", True), ("down", False)):
        arr(lines, "u16 g_CharselArrow_%s[%d]" % (name, 12 * 7 // 4),
            frame.words4(frame.arrow(up)), "0x%04X", 12)
    lines.append("static const u16 g_CharselFrameClut[2][16] = {")
    for clut in frame.CLUTS:
        vals = [0] + [rgb555(c) or 0x8000 for c in clut[1:]]
        lines.append("    {" + ", ".join("0x%04X" % v for v in vals) + "},")
    lines.append("};")

    rd = lambda n: open(os.path.join(disc, n), "rb").read()
    try:
        prg, chr_, mp = rd("TITLE.PRG"), rd("TITLE.CHR"), rd("TITLE.MAP")
        saturn = portraits(prg, chr_)
        pool, maps, cluts = layer(prg, mp)
    except (OSError, AssertionError, struct.error, IndexError, ValueError) as e:
        print("charsel_gfx: no Saturn art, no header, names in the font: %r" % (e,))
        saturn = None

    # the portraits: 8bpp, two pixels per u16, the left pixel in the low byte
    pics = cards(saturn, art)
    lines.append("#define CHARSEL_PORTRAITS %d" % (pics is not None))
    for slot in SLOTS if pics else []:
        px, clut = pics[slot]
        arr(lines, "u16 g_CharselPix_%s[%d]" % (slot, WIN_W * WIN_H // 2),
            [px[j] | px[j + 1] << 8 for j in range(0, len(px), 2)], "0x%04X", 12)
        arr(lines, "u16 g_CharselClut_%s[%d]" % (slot, NCOL), clut, "0x%04X", 12)

    # the Saturn layer: 8x8 4bpp tiles (PS1 nibble order, one u32 per row),
    # cell maps (0 = empty, n = tile n - 1), CLUT 0 header, 1 name, 2 chosen
    lines.append("#define CHARSEL_SATURN %d" % (saturn is not None))
    if saturn is None:
        return lines
    for name, _, _, w, h in LAYER_ELEMS:
        lines.append("#define CHARSEL_%s_W %d" % (name.upper(), w))
        lines.append("#define CHARSEL_%s_H %d" % (name.upper(), h))
    words = []
    for t in pool:
        for y in range(8):
            words.append(sum(t[y * 8 + x] << (4 * x) for x in range(8)))
    arr(lines, "u32 g_CharselTiles[%d]" % len(words), words, "0x%08X", 8)
    for name, _, _, w, h in LAYER_ELEMS:
        arr(lines, "u8 g_CharselMap_%s[%d]" % (name, w * h), maps[name], "%d", w)
    lines.append("static const u16 g_CharselClut4[%d][16] = {" % len(cluts))
    for c in cluts:
        lines.append("    {" + ", ".join("0x%04X" % v for v in c) + "},")
    lines.append("};")
    return lines


def write_cutouts(art, out):
    """the owner's check: the cut-out art and the card portraits (x1, x4)"""
    from PIL import Image

    os.makedirs(out, exist_ok=True)
    for slot in SLOTS:
        name, box, fade = OWNER_ART[slot]
        rgba, card = owner_card(os.path.join(art, name), box, fade)
        rgba.save(os.path.join(out, "%s_cutout.png" % slot))
        q = quantize(card)[2].convert("RGB")
        q.save(os.path.join(out, "%s_card.png" % slot))
        q.resize((WIN_W * 4, WIN_H * 4), Image.NEAREST).save(
            os.path.join(out, "%s_card_x4.png" % slot))


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--cutout":
        write_cutouts(sys.argv[2], sys.argv[3])
        return
    args = sys.argv[1:]
    if len(args) == 2:  # CMake drops an empty SOTN_CHARSEL_ART
        args.insert(1, "")
    if len(args) != 3:
        sys.exit("usage: charsel_gfx.py <disks/saturn> [art dir] <out.h>")
    disc, art, out = args
    head = ["// Generated by tools/saturn/charsel_gfx.py: the Saturn disc's",
            "// and the owner's art. Do not edit, do not commit."]
    lines = generate(disc, art)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(head + lines) + "\n")


if __name__ == "__main__":
    main()
