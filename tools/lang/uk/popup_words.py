#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Ukrainian status popup words (EntityGuardText, src/dra/7879C.c): GUARD, POISON, CURSE, STONE,
the potion texts and the protection texts.

Change a word: edit WORDS, run `popup_words.py write-c` (it rewrites D_800ACFEC_uk in 7879C.c), build.
The pack (sotn_lang.py pack) calls records() and fails if the table in 7879C.c is not the one WORDS makes.

usage: popup_words.py check              the self-tests (needs the disc)
       popup_words.py c                  print D_800ACFEC_uk
       popup_words.py write-c            put it into src/dra/7879C.c
       popup_words.py preview <folder>   uk_preview_4x.png and uk_alphabet_4x.png, made from the records

The font is the stock one (tpage 0x1A, 4bpp): a word is 8 rows high; row 0 is clear; the letters are
3 wide (M 4, I and ! 1) and 5 high on rows 2..6, ink 4 on the two top letter rows and 5 on the three
lower; each texel that touches ink (the 8 neighbours) is the outline 2; a clear column is on the left
and one outline column ends each letter, so a word is 2 + the letter widths + the letter count wide.
The Latin twins (А Е К М Н О Р С Т І !) are copied from the stock words on the disc at run time (no
stock pixel is in git); the other letters are drawn below with the same rules. check() draws each
English word of the sheet with the same code and compares it with the disc.

The texels: the 6 rows of English words (the rects of the table D_800ACFEC; under Ukrainian the game
never draws them) and a strip that is zero in F_GAME.BIN and in F_GAME2.BIN (blocks 8..11 are equal in
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
STRIP = "strip"

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
    "Д": [".##", "#.#", "#.#", "###", "#.#"],
    "Є": ["###", "#.#", "##.", "#.#", "###"],
    "Ж": ["#.#.#", "#.#.#", ".###.", "..#..", "#.#.#"],
    "З": ["###", "..#", ".##", "..#", "###"],
    "И": ["#..#", "#.##", "##.#", "#..#", "#..#"],
    "Й": [".##.", "#..#", "#.##", "##.#", "#..#"],
    "Л": [".##", "#.#", "#.#", "#.#", "#.#"],
    "П": ["###", "#.#", "#.#", "#.#", "#.#"],
    "У": ["#.#", "#.#", ".##", "..#", "##."],
    "Ф": [".#.", "###", "#.#", "###", ".#."],
    "Х": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "Ц": ["#.#.", "#.#.", "#.#.", "#.#.", "####"],
    "Ч": ["#.#", "#.#", "###", "..#", "..#"],
    "Ш": ["#.#.#", "#.#.#", "#.#.#", "#.#.#", "#####"],
    "Щ": ["#.#.#.", "#.#.#.", "#.#.#.", "#.#.#.", "######"],
    "Ь": ["#..", "#..", "##.", "#.#", "##."],
    "Ю": ["#.##.", "#.#.#", "###.#", "#.#.#", "#.##."],
    "Я": ["###", "#.#", ".##", "#.#", "#.#"],
    "'": ["#", "#", ".", ".", "."],
    "↑": [".#.", "###", ".#.", ".#.", ".#."],
    "↓": [".#.", ".#.", ".#.", "###", ".#."],
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


def render(text, gl, width=None):
    """8 rows of texel nibbles for the word. width: the rect width (a clear margin is added on the left);
    default: the natural width, made even."""
    natural = 2 + sum(len(gl[c][0]) for c in text) + len(text)
    w = width or natural + natural % 2
    assert w >= natural and (width or w % 2 == 0), (text, w, natural)  # the stock MISS!! is 21 wide
    ink = [[0] * w for _ in range(8)]
    x = w - natural + 2
    for ch in text:
        g = gl[ch]
        for y in range(5):
            for k, c in enumerate(g[y]):
                ink[2 + y][x + k] = c
        x += len(g[0]) + 1
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
        width = len(render(text, gl)[0])
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
        assert u0 // 128 == (u1 - 1) // 128 and top // 128 == (top + 7) // 128
        old = [bytes(r[i] | r[i + 1] << 4 for i in range(u0, u1, 2)) for r in pg[top:top + 8]]
        nw = [bytes(r[i] | r[i + 1] << 4 for i in range(u0, u1, 2)) for r in new[top:top + 8]]
        recs.append({"name": "popup_" + ("strip" if band == STRIP else "%02X" % band), "x": 640 + u0 // 4,
                     "y": 256 + top, "w": (u1 - u0) // 4, "h": 8, "first": fnv(old[0]),
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
    if not check_c(rect, en):
        sys.exit("popup words: D_800ACFEC_uk in src/dra/7879C.c is not what tools/lang/uk/popup_words.py makes:"
                 " run popup_words.py write-c")
    return recs


def join(page_, clut_, tab, i):
    """the popup of table entry i as the game joins its quads (no stretch): RGBA image"""
    from PIL import Image

    def cut(e):
        l, t, r, b = e[:4]
        im = Image.new("RGBA", (r - l, b - t))
        for y in range(t, b):
            for x in range(l, r):
                im.putpixel((x - l, y - t), clut_[page_[y][x]])
        return im
    word = cut(tab[i])
    mode = tab[i][5]
    if mode == 1:
        return word
    fixed = cut(tab[5 if mode == 2 else 6])
    gap = 0 if mode == 2 else 3
    img = Image.new("RGBA", (word.width + fixed.width + gap, 8))
    img.paste(word, (0, 0) if mode == 2 else (fixed.width + gap, 0))
    img.paste(fixed, (word.width, 0) if mode == 2 else (0, 0))
    return img


def preview(out):
    from PIL import Image, ImageDraw
    d1, d2 = disc("F_GAME.BIN"), disc("F_GAME2.BIN")
    recs, new, pg, rect, en, gl = build(d1, d2)
    tab = uk_table(en, rect)
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
    ims = []
    for i in rows:
        c = clut(d1, en[i][4])
        ims.append((i, join(pg, c, en, i), join(page_, c, tab, i)))
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
    c = clut(d1, 0x1B4)
    lines = [render(t, gl) for t in SHOW]
    aw = max(len(r[0]) for r in lines)
    al = Image.new("RGBA", (aw * S + 2 * pad, (8 * S + pad) * len(lines) + pad), bg)
    for n, r in enumerate(lines):
        im = Image.new("RGBA", (len(r[0]), 8))
        for y in range(8):
            for x in range(len(r[0])):
                im.putpixel((x, y), c[r[y][x]])
        al.alpha_composite(im.resize((im.width * S, im.height * S), Image.NEAREST),
                           (pad, pad + n * (8 * S + pad)))
    al.save(os.path.join(out, "uk_alphabet_4x.png"))
    print("wrote", os.path.join(out, "uk_preview_4x.png"), os.path.join(out, "uk_alphabet_4x.png"))


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
        assert len(render(t, gl)[0]) == rect[p][2] - rect[p][0] and (rect[p][2] - rect[p][0]) % 2 == 0
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
    # 5. the same table is in 7879C.c
    assert check_c(rect, en), "D_800ACFEC_uk in 7879C.c is not the table of WORDS (popup_words.py write-c)"
    over = sum(1 for y in range(256) for x in range(256) if new[y][x] != pg[y][x] and pg[y][x])
    print(f"popup words selftest ok: {len(recs)} records, {changed} texels change ({over} stock English texels "
          f"replaced), {nstrip} zero strip texels in the words, both files zero there")
    for r in recs:
        print(f"  {r['name']}: VRAM x {r['x']} y {r['y']} w {r['w']} h {r['h']} (u 0x{(r['x'] - 640) * 4:02X}.."
              f"0x{(r['x'] - 640 + r['w']) * 4:02X}, v 0x{r['y'] - 256:02X}..0x{r['y'] - 256 + 7:02X})")
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
        src = open(C_SRC, encoding="utf-8").read()
        new = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), lambda _m: text, src, flags=re.S)
        with open(C_SRC, "w", encoding="utf-8", newline="\n") as f:
            f.write(new)
        print("wrote", C_SRC)
    elif cmd == "preview" and len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        preview(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
