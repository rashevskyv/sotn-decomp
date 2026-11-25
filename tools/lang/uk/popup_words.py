#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Ukrainian status popup words (EntityGuardText, src/dra/7879C.c): GUARD, POISON, CURSE, STONE,
the potion texts and the protection texts.

Change a word: edit WORDS, run `popup_words.py write-c` (it rewrites D_800ACFEC_uk in 7879C.c), build.
The pack (sotn_lang.py pack) calls records() and fails if the table in 7879C.c is not the one WORDS makes.

usage: popup_words.py check              the self-tests (needs the disc)
       popup_words.py c                  print D_800ACFEC_uk
       popup_words.py write-c            put it into src/dra/7879C.c, and the HD allow-list rects into src/pc/main.c
       popup_words.py hd                 write the HD pictures into tools/lang/textures/uk (needs numpy, PIL, the disc)
       popup_words.py preview <folder>   uk_preview_4x.png, uk_alphabet_4x.png, uk_hd_preview.png, uk_hd_alphabet.png

After a change of WORDS or of a letter run write-c and hd (in this order), then build.

The font is the stock one (tpage 0x1A, 4bpp): a word is 8 rows high; row 0 is clear; the letters are
3 wide (M 4, I and ! 1) and 5 high on rows 2..6, ink 4 on the two top letter rows and 5 on the three
lower; each texel that touches ink (the 8 neighbours) is the outline 2; a clear column is on the left
and one outline column ends each letter, so a word is 2 + the letter widths + the letter count wide.
The Latin twins (А Е К М Н О Р С Т І !) are copied from the stock words on the disc at pack time (no
stock pixel is in git); the other letters are drawn below with the same rules. check() draws each
English word of the sheet with the same code and compares it with the disc.

HD (the game runs at 720p and more, so one 1x texel is 3 to 6 screen pixels): hd() draws the same words
again at 8x (the scale of the pick-up banners), one picture for each of the 7 rects of the records and each
palette the words of that rect use. The shapes come from the letter masks, but are drawn as geometry, not
scaled: every ink texel is a square at 16 px, the convex corners are cut (an opening with an octagon), the
outline is the ink grown by one texel with an octagon (a real stroke, flat on the straight runs, 45 degree on
the corners), edges are 2x supersampled; the fill is split at the 1x row (two light rows, three dark rows)
with the 1x colours 4 and 5 of the palette, the outline is colour 2. A picture is matched by tools/psyz
hdtex.h as the banners are: the rect of the record (x y w h, 4 bpp), the FNV-1a of the rect's texels (the
Ukrainian ones, as lang.c puts them into VRAM) and the FNV-1a of the 16 palette words. The rect must be on
the allow-list for the gameplay (Psyz_HdTexAllow in src/pc/main.c, written by write-c). Under English
nothing changes: the pictures are in the uk pack only.

The texels: the 6 rows of English words (the rects of the table D_800ACFEC; under Ukrainian the game
draws them only in EntityDamageDisplay, see STOCK_RECT) and a strip that is zero in F_GAME.BIN and in F_GAME2.BIN (blocks 8..11 are equal in
both): there are too few zero texels for all the words. CRITICAL!!, PHYSICAL and the digits are not
touched. src/pc/lang.c puts a record into VRAM only under Ukrainian and only when the stock texels of
the record (FNV of the rows) are there.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DATA = os.environ.get("SOTN_DATA_ROOT", REPO)
C_SRC = os.path.join(REPO, "src", "dra", "7879C.c")
BEGIN, END = "// POPUP_UK_BEGIN", "// POPUP_UK_END"
BLOCK = 0x2000
MAIN_C = os.path.join(REPO, "src", "pc", "main.c")
HD_BEGIN, HD_END = "// POPUP_HD_BEGIN", "// POPUP_HD_END"
HD_DIR = os.path.join(REPO, "tools", "lang", "textures", "uk")
STRIP = "strip"
Z = 8      # HD pixels for one 1x texel (the pick-up banners: 8x too)
CUT = 3    # a cut corner: HD pixels on each side (at Z)
SS = 2     # supersampling, a mask is drawn at Z * SS pixels a texel and averaged down

# param, Ukrainian word, band: the top v of an English row of the sheet (that row's English rects are
# reused) or STRIP (zero texels, u 0x84.., v 0x78). Words sit left to right in the order of this list.
# 5 and 7 are the arrows (5 is the 2nd quad of the mode 2 popups); 6 is the 2nd quad of the mode 3
# popups. 20 is a copy of 17.
WORDS = [
    (0, "БЛОК", 0x4A),
    (1, "ОТРУЄННЯ", STRIP),
    (2, "ПРОКЛЯТТЯ", STRIP),
    (3, "СКАМ'ЯНІННЯ", 0x53),
    (4, "ПРОМАХ!!", 0x65),
    (5, "↑", STRIP),
    (6, "ОПІР", 0x77),
    (7, "↓", 0x53),
    (8, "ЗАХИСТ", 0x65),
    (9, "АТАКА", 0x6E),
    (10, "УДАЧА", 0x6E),
    (11, "РОЗУМ", 0x65),
    (12, "СИЛА", 0x4A),
    (13, "ВОГНЮ", 0x6E),
    (14, "ЛЬОДУ", 0x6E),
    (15, "БЛИСКАВЦІ", 0x53),
    (16, "ПРОКЛЯТТЮ", STRIP),
    (17, "СВЯТОМУ", 0x5C),
    (18, "КАМЕНЮ", 0x5C),
    (19, "ТЕМРЯВІ", 0x5C),
]
# EntityDamageDisplay (src/st/entity_damage_display.h:92) draws the stock GUARD rect u 0x43..0x59 under the
# Spirit Orb: БЛОК gets that whole rect, so no letter of the next word shows in it. {param: rect width}
STOCK_RECT = {0: 22}
STRIP_V, STRIP_U0, STRIP_U1 = 0x78, 0x84, 0xFF  # u1: the u of a quad is a byte, so a rect ends at 0xFF at most

# The English words of the sheet, to copy the Latin twins from and to test the drawing code.
EN_WORDS = {0: "GUARD", 1: "POISON", 2: "CURSE", 3: "STONE", 4: "MISS!!", 8: "DEFENCE", 9: "OFFENCE",
            10: "LUCKY", 11: "INTELLIGENCE", 12: "STRENGTH", 13: "FIRE", 14: "ICE", 15: "THUNDER",
            17: "HOLY", 19: "DARK"}
TWINS = {"А": "A", "Е": "E", "К": "K", "М": "M", "Н": "H", "О": "O", "Р": "P", "С": "C", "Т": "T",
         "І": "I", "!": "!"}

# The drawn letters, 5 rows each. # = ink (4 on rows 0 and 1, 5 on rows 2..4).
DRAWN = {
    "Б": ["###", "#..", "##.", "#.#", "##."],
    "В": ["##.", "#.#", "##.", "#.#", "##."],
    "Г": ["###", "#..", "#..", "#..", "#.."],
    "Д": [".###.", ".#.#.", ".#.#.", "#####", "#...#"],
    "Є": [".###", "#...", "###.", "#...", ".###"],
    "Ж": ["#.#.#", "#.#.#", ".###.", "..#..", "#.#.#"],
    "З": ["###", "..#", ".##", "..#", "###"],
    "И": ["#..#", "#.##", "##.#", "#..#", "#..#"],
    "Й": [".##.", "#..#", "#.##", "##.#", "#..#"],
    "Л": [".##", "#.#", "#.#", "#.#", "#.#"],
    "П": ["###", "#.#", "#.#", "#.#", "#.#"],
    "У": ["#.#", "#.#", ".##", "..#", "##."],
    "Ф": [".#.", "###", "#.#", "###", ".#."],
    "Х": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "Ц": ["#..#.", "#..#.", "#..#.", "#####", "....#"],
    "Ч": ["#.#", "#.#", "###", "..#", "..#"],
    "Ш": ["#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"],
    "Щ": ["#.#.#.", "#.#.#.", "#.#.#.", "#.#.#.", "######"],
    "Ь": ["#..", "#..", "##.", "#.#", "##."],
    "Ю": ["#.##.", "#.#.#", "###.#", "#.#.#", "#.##."],
    "Я": ["###", "#.#", ".##", "#.#", "#.#"],
    "'": ["#", "#", ".", ".", "."],
    "↑": ["..#..", ".###.", "#.#.#", "..#..", "..#.."],
    "↓": ["..#..", "..#..", "#.#.#", ".###.", "..#.."],
}
ROW_INK = (4, 4, 5, 5, 5)
SHOW = ["АБВГДЕЄЖЗИЙ", "КЛМНОПРСТУФ", "ХЦЧШЩЬЮЯ'І", "!↑↓"]  # the alphabet picture, one word a line


def gw(ch):
    """width of a stock letter"""
    return 1 if ch in "I!" else 4 if ch == "M" else 3


def page(data):
    """tpage 0x1A as 256 rows of 256 texel nibbles. F_GAME.BIN: blocks of 0x2000 bytes (32 halfwords x 128
    rows); blocks 8..11 are u 0..127 / 128..255 of v 0..127 / 128..255."""
    rows = [[0] * 256 for _ in range(256)]
    for blk, (u0, v0) in ((8, (0, 0)), (9, (128, 0)), (10, (0, 128)), (11, (128, 128))):
        b = data[blk * BLOCK:(blk + 1) * BLOCK]
        for y in range(128):
            for x in range(128):
                byte = b[y * 64 + x // 2]
                rows[v0 + y][u0 + x] = (byte >> 4) if x & 1 else (byte & 15)
    return rows


def clut(data, n):
    """16 RGBA colours of palette n (0x100 first) of F_GAME.BIN, colour 0 clear"""
    off = 0x40000 + (n - 0x100) * 32
    out = []
    for i in range(16):
        c = data[off + i * 2] | data[off + i * 2 + 1] << 8
        r, g, b = c & 31, c >> 5 & 31, c >> 10 & 31
        out.append((r * 255 // 31, g * 255 // 31, b * 255 // 31, 0 if c == 0 else 255))
    return out


def en_table():
    """D_800ACFEC of 7879C.c: 21 tuples (left, top, right, bottom, clut, mode)"""
    text = open(C_SRC, encoding="utf-8").read()
    body = re.search(r"GuardTextControl D_800ACFEC\[\] = \{(.*?)\};", text, re.S).group(1)
    v = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", body)]
    assert len(v) == 21 * 6, len(v)
    return [tuple(v[i:i + 6]) for i in range(0, len(v), 6)]


def own_glyphs(pg, en, i):
    """the letters of the stock word i, one by one: 5 rows of colours (0 clear, 4/5 ink), read from the disc"""
    w, out = EN_WORDS[i], []
    top, right = en[i][1], en[i][2]
    x = right - (2 + sum(gw(c) for c in w) + len(w)) + 2
    for ch in w:
        out.append([[pg[top + 2 + y][x + k] if pg[top + 2 + y][x + k] in (4, 5) else 0
                     for k in range(gw(ch))] for y in range(5)])
        x += gw(ch) + 1
    return out


def english_glyphs(pg, en):
    """every Latin letter of the sheet; the first word that has it (the stock N has two colourings)"""
    out = {}
    for i in EN_WORDS:
        for ch, g in zip(EN_WORDS[i], own_glyphs(pg, en, i)):
            out.setdefault(ch, g)
    return out


def glyphs(pg, en):
    """every letter of the Ukrainian alphabet: the Latin twins from the disc, the others drawn"""
    eng = english_glyphs(pg, en)
    out = {ch: [[ROW_INK[y] if c == "#" else 0 for c in row] for y, row in enumerate(rows)]
           for ch, rows in DRAWN.items()}
    for cyr, lat in TWINS.items():
        out[cyr] = eng[lat]
    return out


def place(text, gl, width=None):
    """(rect width, [(x, glyph)]): where the letters of a word start in its rect. width: the rect width (a clear
    margin is added on the left); default: the natural width, made even."""
    natural = 2 + sum(len(gl[c][0]) for c in text) + len(text)
    w = width or natural + natural % 2
    assert w >= natural and (width or w % 2 == 0), (text, w, natural)  # the stock MISS!! is 21 wide
    x, out = w - natural + 2, []
    for ch in text:
        out.append((x, gl[ch]))
        x += len(gl[ch][0]) + 1
    return w, out


def render(text, gl, width=None):
    """8 rows of texel nibbles for the word (see place for width)"""
    w, pl = place(text, gl, width)
    ink = [[0] * w for _ in range(8)]
    for x, g in pl:
        for y in range(5):
            for k, c in enumerate(g[y]):
                ink[2 + y][x + k] = c
    out = [[0] * w for _ in range(8)]
    for y in range(8):
        for x in range(w):
            if ink[y][x]:
                out[y][x] = ink[y][x]
            elif any(ink[yy][xx] for yy in range(max(0, y - 1), min(8, y + 2))
                     for xx in range(max(0, x - 1), min(w, x + 2))):
                out[y][x] = 2
    return out


def groups(en):
    """the English rows of the sheet: {top v: (u from, u to)}, from the first rect to the last (build() checks
    that the texels between two rects, one column, are zero in both files)"""
    rows = {}
    for left, top, right, _b, _c, _m in en:
        lo, hi = rows.get(top, (left, right))
        rows[top] = (min(lo, left), max(hi, right))
    return rows


def layout(en, gl):
    """({param: (left, top, right, bottom)}, {band: (u from, u to)})"""
    area = {t: a for t, a in groups(en).items() if t in {w[2] for w in WORDS}}
    area[STRIP] = (STRIP_U0, STRIP_U1)
    cur = {b: a[0] for b, a in area.items()}
    rect = {}
    for param, text, band in WORDS:
        width = max(len(render(text, gl)[0]), STOCK_RECT.get(param, 0))
        top = STRIP_V if band == STRIP else band
        rect[param] = (cur[band], top, cur[band] + width, top + 8)
        cur[band] += width
        assert cur[band] <= area[band][1], f"band {band}: {text} does not fit ({cur[band]} > {area[band][1]})"
    assert sorted(rect) == list(range(20))
    rect[20] = rect[17]
    return rect, area


def uk_table(en, rect):
    return [rect[i] + (en[i][4], en[i][5]) for i in range(21)]


def table_text(tab):
    lines = [BEGIN + " (tools/lang/uk/popup_words.py writes this table; edit WORDS there)",
             "GuardTextControl D_800ACFEC_uk[] = {"]
    for i, (l, t, r, b, c, m) in enumerate(tab):
        lines.append(f"    0x{l:02X}, 0x{t:02X}, 0x{r:02X}, 0x{b:02X}, 0x{c:04X}, 0x{m:04X}, // {i}")
    lines += ["};", END]
    return "\n".join(lines)


def fnv(data, h=2166136261):
    for c in data:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def build(d1, d2):
    """(records, new page, stock page, layout, English table, glyphs). d1, d2: F_GAME.BIN, F_GAME2.BIN"""
    for blk in (8, 9, 10, 11):
        assert d1[blk * BLOCK:(blk + 1) * BLOCK] == d2[blk * BLOCK:(blk + 1) * BLOCK], "F_GAME2 differs"
    pg, en = page(d1), en_table()
    gl = glyphs(pg, en)
    rect, area = layout(en, gl)
    p2 = page(d2)
    for band, (lo, hi) in area.items():
        if band != STRIP:
            for x in range(lo, hi):
                if not any(l <= x < r and t == band for l, t, r, _b, _c, _m in en):
                    assert all(pg[band + y][x] == 0 and p2[band + y][x] == 0 for y in range(8)), (band, x)
    new = [r[:] for r in pg]
    for left, top, right, bottom, _c, _m in en:  # the English words go: this run never draws them
        for y in range(top, bottom):
            for x in range(left, right):
                new[y][x] = 0
    for param, text, band in WORDS:
        left, top, right, bottom = rect[param]
        assert right <= 0xFF and bottom <= 0x100
        for y, row in enumerate(render(text, gl, right - left)):
            new[top + y][left:right] = row
    recs = []
    for band, (lo, hi) in area.items():
        top = STRIP_V if band == STRIP else band
        used = max(rect[p][2] for p, _t, b in WORDS if b == STRIP) if band == STRIP else hi
        u0, u1 = lo - lo % 4, used + (-used) % 4
        # the strip is zero in the stock page, which proves no place: its record starts 8 rows higher, where
        # the stock page has texels (the new ones there are the stock ones)
        if band == STRIP:
            top -= 8
        h = 16 if band == STRIP else 8
        assert u0 // 128 == (u1 - 1) // 128 and top // 128 == (top + h - 1) // 128
        old = [bytes(r[i] | r[i + 1] << 4 for i in range(u0, u1, 2)) for r in pg[top:top + h]]
        nw = [bytes(r[i] | r[i + 1] << 4 for i in range(u0, u1, 2)) for r in new[top:top + h]]
        recs.append({"name": "popup_" + ("strip" if band == STRIP else "%02X" % band), "x": 640 + u0 // 4,
                     "y": 256 + top, "w": (u1 - u0) // 4, "h": h, "first": fnv(old[0]),
                     "whole": fnv(b"".join(old)), "le": b"".join(nw)})
    return recs, new, pg, rect, en, gl


def disc(name):
    for p in glob.glob(os.path.join(DATA, "disks", "us", "**", name), recursive=True):
        with open(p, "rb") as f:
            return f.read()
    return None


def check_c(rect, en):
    """the table in 7879C.c must be the one WORDS makes"""
    text = open(C_SRC, encoding="utf-8").read()
    m = re.search(re.escape(BEGIN) + r".*?" + re.escape(END), text, re.S)
    return bool(m) and m.group(0) == table_text(uk_table(en, rect))


def records():
    """the art.bin records for sotn_lang.py pack, or [] without the disc"""
    d1, d2 = disc("F_GAME.BIN"), disc("F_GAME2.BIN")
    if not d1 or not d2:
        print("popup words: F_GAME.BIN / F_GAME2.BIN not found, the popups stay English")
        return []
    recs, _new, _pg, rect, en, _gl = build(d1, d2)
    if not check_c(rect, en) or not check_main(recs):
        sys.exit("popup words: D_800ACFEC_uk in src/dra/7879C.c or the allow-list block in src/pc/main.c is not what"
                 " tools/lang/uk/popup_words.py makes: run popup_words.py write-c")
    if not check_hd(d1, recs, rect, en):
        sys.exit("popup words: the HD pictures in tools/lang/textures/uk (popup_*) are not the ones the records"
                 " ask for: run popup_words.py hd")
    return recs


def popup_clut(en, param):
    """the palette a table entry is drawn with: the 2nd quads (5, 6) copy the main word's prim, so they use the
    palette of the mode 2 / mode 3 words"""
    return en[{5: 8, 6: 13}.get(param, param)][4]


def clut_hash(data, n):
    """FNV-1a of the 16 palette words, as hdtex.h hashes the CLUT of a primitive"""
    off = 0x40000 + (n - 0x100) * 32
    return fnv(data[off:off + 32])


def hd_rect(r):
    """(x, y, w, h, texel bytes) of the HD rect of a record: its last 8 rows (the words; the strip record
    has 8 more rows above them that only prove the place)"""
    off = r["h"] - 8
    return r["x"], r["y"] + off, r["w"], 8, r["le"][off * r["w"] * 2:]


def hd_keys(d1, recs, rect, en):
    """[(file name, record, band, palette n)]: one picture for each record and each palette its words use.
    Name and key as the pick-up banners (tools/psyz hdtex.h): <text>__<x>_<y>_<w>x<h>_<bpp>_<hash>_c<clut>.png"""
    out = []
    for r in recs:
        band = STRIP if r["name"] == "popup_strip" else int(r["name"][6:], 16)
        for n in sorted({popup_clut(en, p) for p, _t, b in WORDS if b == band}):
            x, y, w, h, le = hd_rect(r)
            out.append(("%s__%d_%d_%dx%d_4_%08x_c%08x.png" % (r["name"], x, y, w, h, fnv(le), clut_hash(d1, n)),
                        r, band, n))
    return out


def _dil(m, a, b):
    """grow a mask by an octagon: a square of radius a, then a diamond of radius b (pixels). The octagon reaches
    a + b along the axes and is cut at 45 degrees on the corners"""
    for ax in (0, 1):
        src, m = m, m.copy()
        for k in range(1, a + 1):
            lo, hi = [slice(None)] * 2, [slice(None)] * 2
            lo[ax], hi[ax] = slice(k, None), slice(None, -k)
            m[tuple(lo)] |= src[tuple(hi)]
            m[tuple(hi)] |= src[tuple(lo)]
    for _ in range(b):
        src, m = m, m.copy()
        m[1:] |= src[:-1]
        m[:-1] |= src[1:]
        m[:, 1:] |= src[:, :-1]
        m[:, :-1] |= src[:, 1:]
    return m


def hd_masks(items, w):
    """items: [(x, glyph)] letters of a rect w texels wide (8 rows). Returns (light, dark, outline): float
    coverage 0..1, each (8 * Z, w * Z): the light and the dark half of the fill (colours 4 and 5 of 1x) and the
    outline (colour 2) that also holds the fill"""
    import numpy as np
    from PIL import Image, ImageDraw
    S = Z * SS
    texels = {(x + k, 2 + y) for x, g in items for y, row in enumerate(g) for k, c in enumerate(row) if c}
    # the ink: every texel is a square of S pixels; a corner that no neighbour touches (neither of the two
    # sides nor the diagonal one) is cut at 45 degrees. A corner where two texels only touch diagonally stays
    # sharp, so a diagonal stroke stays one piece.
    img = Image.new("L", (w * S, 8 * S), 0)
    draw = ImageDraw.Draw(img)
    cut = CUT * SS
    for tx, ty in texels:
        x0, y0, x1, y1 = tx * S, ty * S, (tx + 1) * S - 1, (ty + 1) * S - 1
        pts = []
        for sx, sy, corner, (p, q) in (
                (-1, -1, (x0, y0), ((x0, y0 + cut), (x0 + cut, y0))),
                (1, -1, (x1, y0), ((x1 - cut, y0), (x1, y0 + cut))),
                (1, 1, (x1, y1), ((x1, y1 - cut), (x1 - cut, y1))),
                (-1, 1, (x0, y1), ((x0 + cut, y1), (x0, y1 - cut)))):
            alone = not any(t in texels for t in ((tx + sx, ty), (tx, ty + sy), (tx + sx, ty + sy)))
            pts += [p, q] if alone else [corner]
        draw.polygon(pts, fill=255)
    ink = np.asarray(img) > 0
    # the outline: the ink grown by one texel (flat on the straight runs and across the gap between two letters)
    edge = _dil(ink, S * 3 // 4, S // 4)
    top = (np.arange(8 * S) < 4 * S)[:, None]  # letter rows 0 and 1 are the light ones, as in the stock font

    def down(m):
        return m.reshape(8 * Z, SS, w * Z, SS).mean((1, 3))
    return down(ink & top), down(ink & ~top), down(edge)


def hd_picture(masks, cl):
    """RGBA picture from the masks and a palette (clut(): colours 2, 4, 5). Straight alpha; the colour under a
    clear pixel is the outline colour, so a filtered edge shows no halo."""
    import numpy as np
    from PIL import Image
    light, dark, edge = masks
    cov = np.maximum(edge, light + dark)
    c2, c4, c5 = (np.array(cl[i][:3], float) for i in (2, 4, 5))
    mix = (light[..., None] * c4 + dark[..., None] * c5 + (cov - light - dark)[..., None] * c2)
    rgb = np.where(cov[..., None] > 1e-6, mix / np.maximum(cov, 1e-6)[..., None], c2)
    out = np.dstack([np.rint(np.clip(rgb, 0, 255)), np.rint(cov * 255)]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def hd_images(d1, d2):
    """{file name: PIL image} of every HD picture, drawn from WORDS"""
    recs, _new, _pg, rect, en, gl = build(d1, d2)
    out = {}
    for name, r, band, n in hd_keys(d1, recs, rect, en):
        items = []
        for p, t, b in WORDS:
            if b == band:
                left, _top, right, _bottom = rect[p]
                items += [(left - (r["x"] - 640) * 4 + x, g) for x, g in place(t, gl, right - left)[1]]
        out[name] = hd_picture(hd_masks(items, r["w"] * 4), clut(d1, n))
    return out


def write_hd(d1, d2):
    for f in glob.glob(os.path.join(HD_DIR, "popup_*__*.png")):
        os.remove(f)
    imgs = hd_images(d1, d2)
    for name, im in imgs.items():
        im.save(os.path.join(HD_DIR, name), optimize=True)
    return sorted(imgs)


def check_hd(d1, recs, rect, en):
    """the HD files in the folder must be the ones the records ask for (names carry the hashes)"""
    want = {k[0] for k in hd_keys(d1, recs, rect, en)}
    have = {os.path.basename(f) for f in glob.glob(os.path.join(HD_DIR, "popup_*__*.png"))}
    return want == have


def allow_text(recs):
    lines = [HD_BEGIN + " (tools/lang/uk/popup_words.py writes this block)",
             "            {",
             "                static const short pr[][4] = {  // x, y, w, h of the popup rects, 4 bpp"]
    lines += ["                    {%d, %d, %d, %d}," % hd_rect(r)[:4] for r in recs]
    lines += ["                };",
              "                int pi;",
              "                for (pi = 0; pi < (int)(sizeof(pr) / sizeof(pr[0])); pi++) {",
              "                    if (!Psyz_HdTexAllow(pr[pi][0], pr[pi][1], pr[pi][2], pr[pi][3], 4)) {",
              '                        fprintf(stderr, "[hd] allow table full: popup rect %d\\n", pi);',
              "                    }",
              "                }",
              "            }",
              "            " + HD_END]
    return "\n".join(lines)


def check_main(recs):
    m = re.search(re.escape(HD_BEGIN) + r".*?" + re.escape(HD_END), open(MAIN_C, encoding="utf-8").read(), re.S)
    return bool(m) and m.group(0) == allow_text(recs)


def join(cut, tab, i, s=1):
    """the popup of table entry i as the game joins its quads (no stretch): RGBA image. cut(rect, palette n)
    is the picture of a rect at scale s; both quads use the palette of the main word."""
    from PIL import Image
    n = tab[i][4]
    word = cut(tab[i], n)
    mode = tab[i][5]
    if mode == 1:
        return word
    fixed = cut(tab[5 if mode == 2 else 6], n)
    gap = 0 if mode == 2 else 3 * s
    img = Image.new("RGBA", (word.width + fixed.width + gap, 8 * s))
    img.paste(word, (0, 0) if mode == 2 else (fixed.width + gap, 0))
    img.paste(fixed, (word.width, 0) if mode == 2 else (0, 0))
    return img


def cut1(page_, palette):
    """cut(rect, palette n) of join(): the texels of a rect at 1x"""
    from PIL import Image

    def cut(e, n):
        l, t, r, b = e[:4]
        im = Image.new("RGBA", (r - l, b - t))
        for y in range(t, b):
            for x in range(l, r):
                im.putpixel((x - l, y - t), palette[n][page_[y][x]])
        return im
    return cut


def cut_hd(recs, pics):
    """cut(rect, palette n) of join() at Z: the rect cropped out of the HD file (read from the folder) of its
    record and palette"""
    from PIL import Image
    cache = {}

    def cut(e, n):
        l, t, r, b = e[:4]
        for rec in recs:
            u0, v0 = (rec["x"] - 640) * 4, hd_rect(rec)[1] - 256
            if v0 == t and u0 <= l and r <= u0 + rec["w"] * 4:
                key = (rec["name"], n)
                if key not in cache:
                    cache[key] = Image.open(os.path.join(HD_DIR, pics[key])).convert("RGBA")
                return cache[key].crop(((l - u0) * Z, 0, (r - u0) * Z, 8 * Z))
        raise KeyError(e)
    return cut


def preview(out):
    import numpy as np
    from PIL import Image, ImageDraw
    d1, d2 = disc("F_GAME.BIN"), disc("F_GAME2.BIN")
    recs, new, pg, rect, en, gl = build(d1, d2)
    tab = uk_table(en, rect)
    if not check_hd(d1, recs, rect, en):
        sys.exit("popup words: the HD files are not the ones of the records: run popup_words.py hd")
    pics = {(k[1]["name"], k[3]): k[0] for k in hd_keys(d1, recs, rect, en)}
    # the page again, from the records only (what lang.c puts into VRAM)
    page_ = [r[:] for r in pg]
    for r in recs:
        u0, v0 = (r["x"] - 640) * 4, r["y"] - 256
        for y in range(r["h"]):
            row = r["le"][y * r["w"] * 2:(y + 1) * r["w"] * 2]
            for k, byte in enumerate(row):
                page_[v0 + y][u0 + 2 * k], page_[v0 + y][u0 + 2 * k + 1] = byte & 15, byte >> 4
    assert page_ == new, "the records do not make the page"
    rows = [0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]
    bg, S, pad = (36, 36, 44, 255), 4, 6
    pal = {n: clut(d1, n) for n in {e[4] for e in en}}
    ims = [(i, join(cut1(pg, pal), en, i), join(cut1(page_, pal), tab, i)) for i in rows]
    wen = max(a.width for _i, a, _b in ims)
    wuk = max(b.width for _i, _a, b in ims)
    sheet = Image.new("RGBA", (24 + (wen + wuk) * S + pad * 3, (8 * S + pad) * len(ims) + pad), bg)
    dr = ImageDraw.Draw(sheet)
    for n, (i, a, b) in enumerate(ims):
        y = pad + n * (8 * S + pad)
        dr.text((3, y + 8), str(i), fill=(150, 150, 160, 255))
        for k, im in enumerate((a, b)):
            big = im.resize((im.width * S, im.height * S), Image.NEAREST)
            sheet.alpha_composite(big, (24 + pad + k * (wen * S + pad), y))
    sheet.save(os.path.join(out, "uk_preview_4x.png"))
    lines = [render(t, gl) for t in SHOW]
    aw = max(len(r[0]) for r in lines)
    al = Image.new("RGBA", (aw * S + 2 * pad, (8 * S + pad) * len(lines) + pad), bg)
    for n, r in enumerate(lines):
        im = Image.new("RGBA", (len(r[0]), 8))
        for y in range(8):
            for x in range(len(r[0])):
                im.putpixel((x, y), pal[0x1B4][r[y][x]])
        al.alpha_composite(im.resize((im.width * S, im.height * S), Image.NEAREST),
                           (pad, pad + n * (8 * S + pad)))
    al.save(os.path.join(out, "uk_alphabet_4x.png"))
    # HD: every popup from the files, next to the 1x popup scaled up nearest to the same size
    hd = cut_hd(recs, pics)
    ims = [(i, join(cut1(page_, pal), tab, i), join(hd, tab, i, Z)) for i in rows]
    w1 = max(a.width for _i, a, _b in ims) * Z
    wh = max(b.width for _i, _a, b in ims)
    pd, lab = 12, 36
    sheet = Image.new("RGBA", (lab + w1 + wh + pd * 3, (8 * Z + pd) * len(ims) + pd + 20), bg)
    dr = ImageDraw.Draw(sheet)
    dr.text((lab + pd, 4), "1x, scaled up nearest", fill=(150, 150, 160, 255))
    dr.text((lab + w1 + 2 * pd, 4), "HD (uk pack files), %dx" % Z, fill=(150, 150, 160, 255))
    for n, (i, a, b) in enumerate(ims):
        y = 20 + pd + n * (8 * Z + pd)
        dr.text((6, y + 28), str(i), fill=(150, 150, 160, 255))
        sheet.alpha_composite(a.resize((a.width * Z, a.height * Z), Image.NEAREST), (lab + pd, y))
        sheet.alpha_composite(b, (lab + w1 + 2 * pd, y))
    sheet.save(os.path.join(out, "uk_hd_preview.png"))
    ah = Image.new("RGBA", (aw * Z + 2 * pd, (8 * Z + pd) * len(lines) + pd), bg)
    for n, text in enumerate(SHOW):
        w, pl = place(text, gl)
        ah.alpha_composite(hd_picture(hd_masks(pl, w), pal[0x1B4]), (pd, pd + n * (8 * Z + pd)))
    ah.save(os.path.join(out, "uk_hd_alphabet.png"))
    print("wrote uk_preview_4x.png, uk_alphabet_4x.png, uk_hd_preview.png, uk_hd_alphabet.png in", out)


def selftest():
    d1, d2 = disc("F_GAME.BIN"), disc("F_GAME2.BIN")
    if not d1 or not d2:
        sys.exit("popup words: no disc (SOTN_DATA_ROOT)")
    recs, new, pg, rect, en, gl = build(d1, d2)
    # 1. the drawing code makes each English word of the sheet from the letters of that word
    for i, w in EN_WORDS.items():
        left, top, right, bottom = en[i][:4]
        got = render(list(range(len(w))), dict(enumerate(own_glyphs(pg, en, i))), right - left)
        assert got == [pg[top + y][left:right] for y in range(8)], f"{w}: not the stock word"
    # 2. the Latin twins are the stock letters; every rect is as wide as its word and even
    for cyr, lat in TWINS.items():
        assert gl[cyr] == english_glyphs(pg, en)[lat]
    for p, t, b in WORDS:
        assert max(len(render(t, gl)[0]), STOCK_RECT.get(p, 0)) == rect[p][2] - rect[p][0] and (rect[p][2] - rect[p][0]) % 2 == 0
    # 3. the strip is zero in both files; only English rects and the strip change
    d2p = page(d2)
    allowed = [[False] * 256 for _ in range(256)]
    for l, t, r, b, _c, _m in en:
        for y in range(t, b):
            for x in range(l, r):
                allowed[y][x] = True
    nstrip = 0
    for p, t, b in WORDS:
        if b == STRIP:
            l, tp, r, bt = rect[p]
            for y in range(tp, bt):
                for x in range(l, r):
                    assert pg[y][x] == 0 and d2p[y][x] == 0, (p, x, y)
                    allowed[y][x] = True
                    nstrip += 1
    changed = 0
    for y in range(256):
        for x in range(256):
            if new[y][x] != pg[y][x]:
                assert allowed[y][x] or pg[y][x] == 0 and d2p[y][x] == 0 and any(
                    t <= y < t + 8 and lo <= x < hi for t, (lo, hi) in groups(en).items()), (x, y)
                changed += 1
    # 4. each record holds what it should: the stock rows' hash, the new texels
    for r in recs:
        u0, v0 = (r["x"] - 640) * 4, r["y"] - 256
        for y in range(r["h"]):
            row = r["le"][y * r["w"] * 2:(y + 1) * r["w"] * 2]
            assert [t for b in row for t in (b & 15, b >> 4)] == new[v0 + y][u0:u0 + r["w"] * 4]
    # 5. the same table is in 7879C.c, the same rects are on the allow-list in main.c
    assert check_c(rect, en), "D_800ACFEC_uk in 7879C.c is not the table of WORDS (popup_words.py write-c)"
    assert check_main(recs), "the allow-list block in src/pc/main.c is not the one of the records (write-c)"
    # 6. the HD files are the ones of the records, are what hd_images() draws now, and have the footprint of
    # the 1x words: where a 1x texel is clear the HD texel is clear; where it is ink or outline, its centre is solid
    assert check_hd(d1, recs, rect, en), "the HD files are not the ones of the records (popup_words.py hd)"
    import numpy as np
    from PIL import Image
    nhd = 0
    for name, im in hd_images(d1, d2).items():
        on_disk = np.asarray(Image.open(os.path.join(HD_DIR, name)).convert("RGBA"))
        assert on_disk.shape == np.asarray(im).shape and (on_disk == np.asarray(im)).all(), name + ": stale (hd)"
        r = next(r for r in recs if name.startswith(r["name"] + "__"))
        u0, v0 = (r["x"] - 640) * 4, hd_rect(r)[1] - 256
        assert on_disk.shape[:2] == (8 * Z, r["w"] * 4 * Z) and r["w"] * 4 * Z <= 2048, (name, on_disk.shape)
        band = STRIP if r["name"] == "popup_strip" else int(r["name"][6:], 16)
        cols = {u - u0 for p, _t, bd in WORDS if bd == band for u in range(rect[p][0], rect[p][2])}
        for y in range(8):
            for x in sorted(cols):  # (the rest of a rect, left of the first word, may hold other stock texels)
                a = on_disk[y * Z:(y + 1) * Z, x * Z:(x + 1) * Z, 3]
                if new[v0 + y][u0 + x] == 0:
                    assert not a.any(), (name, x, y, "HD ink on a clear 1x texel")
                else:
                    assert a[Z // 2, Z // 2] == 255 or a[Z // 2 - 1, Z // 2 - 1] == 255, (name, x, y, "hole")
        nhd += 1
    over = sum(1 for y in range(256) for x in range(256) if new[y][x] != pg[y][x] and pg[y][x])
    print(f"popup words selftest ok: {len(recs)} records, {nhd} HD pictures at {Z}x, {changed} texels change ({over} stock English texels "
          f"replaced), {nstrip} zero strip texels in the words, both files zero there")
    for r in recs:
        print(f"  {r['name']}: VRAM x {r['x']} y {r['y']} w {r['w']} h {r['h']} (u 0x{(r['x'] - 640) * 4:02X}.."
              f"0x{(r['x'] - 640 + r['w']) * 4:02X}, v 0x{r['y'] - 256:02X}..0x{r['y'] - 256 + r['h'] - 1:02X})")
    for p, t, b in WORDS:
        l, tp, r, bt = rect[p]
        print(f"  {p:2d} {t:12} u 0x{l:02X}..0x{r:02X} v 0x{tp:02X}..0x{bt:02X} width {r - l}")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "check":
        selftest()
    elif cmd in ("c", "write-c"):
        d1 = disc("F_GAME.BIN")
        if not d1:
            sys.exit("no disc")
        _r, _n, _p, rect, en, _g = build(d1, d1)
        text = table_text(uk_table(en, rect))
        if cmd == "c":
            print(text)
            return
        for path, begin, end, block in ((C_SRC, BEGIN, END, text), (MAIN_C, HD_BEGIN, HD_END, allow_text(_r))):
            src = open(path, encoding="utf-8").read()
            new = re.sub(re.escape(begin) + r".*?" + re.escape(end), lambda _m: block, src, flags=re.S)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(new)
            print("wrote", path)
    elif cmd == "hd":
        d1, d2 = disc("F_GAME.BIN"), disc("F_GAME2.BIN")
        if not d1 or not d2:
            sys.exit("no disc")
        for name in write_hd(d1, d2):
            print("wrote", os.path.join(HD_DIR, name))
    elif cmd == "preview" and len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        preview(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
