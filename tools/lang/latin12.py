# SPDX-License-Identifier: AGPL-3.0-or-later
"""Crisp one-ink 12x16 Latin glyphs for the A font (the Equip description line, the banners, the message
boxes) in the style of the owner's serif Cyrillic (tools/lang/uk/bios12.png). P7, 2026-11-25.

  python3 tools/lang/latin12.py sheet [--force]   write tools/lang/latin12.png (the editable draft)
  python3 tools/lang/latin12.py before_after OLD.png OUT.png   every cell that differs from OLD.png, 6x
  python3 tools/lang/latin12.py compare DIR       write DIR/P7_compare*.png and DIR/P7_glyphs.png

The sheet is 16 cells a row, a cell 12 x 15 px (the 12 columns and 15 rows func_80106A28 draws; the 16th
row of the 12x16 glyph is empty). Palette index 1 = ink. Once it is committed the PNG is the source of
truth: edit it by hand. `sheet` refuses to overwrite it without --force.

Where the sheet cells come from:
  SAME   a Latin letter with the shape of a Cyrillic one takes the owner's Cyrillic cell as it is
         (the same list as tools/lang/serif_latin.py SAME), so en and uk share those letters.
  BIOS   every other cell is the stock BIOS Latin glyph (tools/psyz/nugget/openbios/charset/font1.raw,
         16 x 15, already 1 px strokes with serifs: the same kind of shape as the uk design), moved to
         the uk grid: cap top row 2 / baseline row 13 (BIOS 1 / 11): one row is repeated in the stem
         part; x-height 8 rows (rows 6..13), so a letter with no ascender only moves down 2 rows;
         a 2 row descender is merged into the one row (14) the cell has; the 16 columns become 12
         by area (a stem of 1 px stays 1 px), with the phase (0..3) that keeps the form best.
  HAND   the quotes, the guillemets, every capital that is not SAME, every small letter and every digit,
         drawn here by hand (P7, after the owner saw the automatic cells in HD). Only @ (8197) still comes
         from the BIOS conversion (see BIOS). in the shape of the 8x11 menu cells (dfont.png).
"""
import os, sys, struct, json
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
UK = os.path.join(HERE, "uk")
SHEET = os.path.join(HERE, "latin12.png")
FONT1 = os.path.join(HERE, "..", "psyz", "nugget", "openbios", "charset", "font1.raw")
W, H = 12, 15
PER_ROW = 16

# the 0x8140.. marks of Lang_SjisCell (src/pc/lang.c jis_marks) that the en texts use
MARKS = [0x8143, 0x8144, 0x8146, 0x8147, 0x8148, 0x8149, 0x814D, 0x814F, 0x8151, 0x815C, 0x815D, 0x815E,
         0x815F, 0x8160, 0x8162, 0x8165, 0x8166, 0x8167, 0x8168, 0x8169, 0x816A, 0x816D, 0x816E, 0x816F,
         0x8170, 0x817B, 0x817C, 0x817E, 0x8181, 0x8183, 0x8184, 0x8190, 0x8193, 0x8194, 0x8195, 0x8196,
         0x8197]
# cell keys, in sheet order: A..Z, a..z, digits, the marks as their Shift-JIS code, then the guillemets
KEYS = ([chr(c) for c in range(65, 91)] + [chr(c) for c in range(97, 123)] + list("0123456789")
        + [f"{m:04X}" for m in MARKS] + ["«", "»"])
SAME = {"A": "А", "B": "В", "C": "С", "E": "Е", "H": "Н", "I": "І", "K": "К", "M": "М", "O": "О", "P": "Р",
        "T": "Т", "X": "Х", "a": "а", "c": "с", "e": "е", "i": "і", "o": "о", "p": "р", "x": "х", "y": "у"}


def sjis(key):
    if len(key) == 4 and key[0] == "8":
        return int(key, 16)
    c = ord(key)
    if "a" <= key <= "z":
        return 0x8281 + c - 97
    if "A" <= key <= "Z":
        return 0x8260 + c - 65
    return 0x824F + c - 48  # digits


_font1 = None


def bios_rows(code):
    """The BIOS glyph of a Shift-JIS code: 15 rows of 16 bools."""
    global _font1
    if _font1 is None:
        _font1 = open(FONT1, "rb").read()
    d = _font1
    # the start of the runs of the stock table (Krom2Offset)
    tab = [(0x8140, 0), (0x8180, 0x3F), (0x81B8, 0x6C), (0x81C8, 0x74), (0x81DA, 0x7B), (0x81F0, 0x8A),
           (0x824F, 0x93), (0x8260, 0x9D), (0x8281, 0xB7)]
    base = max(t for t in tab if t[0] <= code)
    off = base[1] + code - base[0]
    return [[bool(d[off * 30 + y * 2 + x // 8] >> (7 - x % 8) & 1) for x in range(16)] for y in range(15)]


def uk_cells():
    """{char: 15 rows of 12 bools} of the owner's Cyrillic: the 16 columns are d0 d1 d1 d2 groups."""
    g = json.load(open(os.path.join(UK, "glyphs.json")))["bios12"]
    px = Image.open(os.path.join(UK, "bios12.png")).load()
    out = {}
    for i, ch in enumerate(g["serif"] + g["private"]):
        cx, cy = (i % 16) * 16, (i // 16) * 15
        out[ch] = [[px[cx + 4 * k + d, cy + y] != 0 for k in range(4) for d in (0, 1, 3)] for y in range(15)]
    return out


def components(rows):
    seen, n = set(), 0
    for y, r in enumerate(rows):
        for x, v in enumerate(r):
            if v and (y, x) not in seen:
                n += 1
                st = [(y, x)]
                while st:
                    a, b = st.pop()
                    if (a, b) in seen or not (0 <= a < len(rows) and 0 <= b < len(rows[0])) or not rows[a][b]:
                        continue
                    seen.add((a, b))
                    st += [(a + i, b + j) for i in (-1, 0, 1) for j in (-1, 0, 1)]
    return n


def squeeze(rows16):
    """16 columns -> 12 by area, the best of 4 phases."""
    best = None
    ink = sum(map(sum, rows16))
    comp = components(rows16)
    for ph in range(4):
        out = []
        for r in rows16:
            row = []
            for o in range(13):  # 12 + 1: a phase moves the grid by up to 3 columns
                a, b = 4 * o / 3 - ph, 4 * (o + 1) / 3 - ph
                cov = 0.0
                for s in range(16):
                    ov = min(b, s + 1) - max(a, s)
                    if ov > 0 and r[s]:
                        cov += ov
                row.append(cov / (4 / 3) >= 0.49)
            out.append(row)
        n = sum(map(sum, out))
        score = abs(n - 0.75 * ink) + 6 * abs(components(out) - comp) + 0.3 * ph
        if best is None or score < best[0]:
            best = (score, out)
    return best[1]


def regrid(rows):
    """BIOS rows (cap 1..11, baseline 11) -> uk rows (cap 2..13, baseline 13)."""
    ys = [y for y, r in enumerate(rows) if any(r)]
    top = ys[0]
    n = len(rows[0])
    out = [[False] * n for _ in range(H)]
    body = [list(r) for r in rows[:12]]
    dr = [False] * n
    has_desc = False
    for r in rows[12:]:
        if any(r):
            has_desc = True
            dr = [a or b for a, b in zip(dr, r)]
    if top <= 1:  # tall: repeat the quietest row of the stem part: 11 rows become 12
        best = min(range(3, 10), key=lambda y: (sum(a != b for a, b in zip(body[y], body[y + 1])), abs(y - 6)))
        body = body[:best + 1] + [body[best]] + body[best + 1:]
        for y, r in enumerate(body):
            if y + 1 < H:
                out[y + 1] = r
    else:
        for y, r in enumerate(body):
            if y + 2 < H:
                out[y + 2] = r
    if has_desc:
        out[14] = dr
    return out


def place(rows):
    """Move the ink to start at column 1 (the callers trim the left side anyway)."""
    xs = [x for r in rows for x, v in enumerate(r) if v]
    l = min(xs)
    return [[(r[x + l - 1] if 0 <= x + l - 1 < len(r) else False) for x in range(W)] for r in rows]


def R(top, *rows):
    """Rows of 12 characters from row `top` on; None = a gap row to skip."""
    return {top + i: r for i, r in enumerate(rows) if r is not None}


# Hand-drawn cells. 12 columns, "#" = ink. The grid is the uk grid: cap rows 2..13, x-height rows 6..13
# (baseline 13), descender row 14, ascender row 2; 1 px strokes, 3 px serifs; round letters 7 wide (o),
# capitals 9 wide (О), stems at columns 3 and 7/8 like п р н. The guillemets are the chevrons of the 8x11 menu
# cells (dfont.png: two chevrons 3 columns apart) at the A scale.
HAND = {
    "«": R(5, "....#..#....", "...#..#.....", "..#..#......", ".#..#.......",
                "..#..#......", "...#..#.....", "....#..#...."),
    "»": R(5, ".#..#.......", "..#..#......", "...#..#.....", "....#..#....",
                "...#..#.....", "..#..#......", ".#..#......."),
    # quotes: two ticks of the 8x11 apostrophe (2 columns, 4 rows, leaning)
    "8167": R(2, "..##..##....", "..##..##....", "...#...#....", "..#...#....."),
    "8168": R(2, "..##..##....", "..##..##....", "...#...#....", "..#...#....."),
    "8165": R(2, "..##........", "..##........", "...#........", "..#........."),
    "8166": R(2, "..##........", "..##........", "...#........", "..#........."),
    # ---- capitals (rows 2..13)
    "D": R(2, "..#####.....", "...#...#....", "...#....#...", "...#....#...", "...#....#...", "...#....#...",
           "...#....#...", "...#....#...", "...#....#...", "...#....#...", "...#...#....", "..#####....."),
    "F": R(2, "..#######...", "...#....#...", "...#........", "...#..#.....", "...####.....", "...#..#.....",
           "...#........", "...#........", "...#........", "...#........", "...#........", "..###......."),
    "G": R(2, "....####....", "...#....#...", "..#.........", "..#.........", "..#.........", "..#.........",
           "..#.........", "..#...###...", "..#.....#...", "..#.....#...", "...#....#...", "....####...."),
    "J": R(2, "....#####...", "......#.....", "......#.....", "......#.....", "......#.....", "......#.....",
           "......#.....", "......#.....", "......#.....", "......#.....", "..#...#.....", "...###......"),
    "L": R(2, "..###.......", "...#........", "...#........", "...#........", "...#........", "...#........",
           "...#........", "...#........", "...#........", "...#........", "...#....#...", "..#######..."),
    "N": R(2, "###...###...", ".##....#....", ".##....#....", ".#.#...#....", ".#.#...#....", ".#..#..#....",
           ".#..#..#....", ".#...#.#....", ".#...#.#....", ".#....##....", ".#....##....", "###...###..."),
    "Q": R(2, "...###......", "..#...#.....", ".#.....#....", "#.......#...", "#.......#...", "#.......#...",
           "#.......#...", "#.......#...", "#.......#...", ".#.....#....", "..#...##....", "...###.##..."),
    "R": R(2, "..######....", "...#....#...", "...#....#...", "...#....#...", "...#....#...", "...#....#...",
           "...#####....", "...#..#.....", "...#..#.....", "...#...#....", "...#...#....", "..###..###.."),
    "S": R(2, "...####.....", "..#....#....", "..#.........", "...#........", "....#.......", ".....#......",
           ".....#......", "......#.....", ".......#....", "........#...", "...#....#...", "....####...."),
    "U": R(2, "###...###...", ".#.....#....", ".#.....#....", ".#.....#....", ".#.....#....", ".#.....#....",
           ".#.....#....", ".#.....#....", ".#.....#....", ".#.....#....", "..#...#.....", "...###......"),
    "V": R(2, "###...###...", ".#.....#....", ".#.....#....", "..#...#.....", "..#...#.....", "..#...#.....",
           "...#.#......", "...#.#......", "...#.#......", "...#.#......", "....#.......", "....#......."),
    "W": R(2, "###.###.###.", ".#...#...#..", ".#...#...#..", ".#...#...#..", "..#.#.#.#...", "..#.#.#.#...",
           "..#.#.#.#...", "..#.#.#.#...", "...#...#....", "...#...#....", "...#...#....", "...#...#...."),
    "Y": R(2, "###...###...", ".#.....#....", ".#.....#....", "..#...#.....", "..#...#.....", "...#.#......",
           "...#.#......", "....#.......", "....#.......", "....#.......", "....#.......", "...###......"),
    "Z": R(2, "..#######...", ".......##...", ".......#....", "......#.....", "......#.....", ".....#......",
           ".....#......", "....#.......", "....#.......", "...#........", "..##........", "..#######..."),
    # ---- small letters (x-height rows 6..13)
    "b": R(2, "..##........", "...#........", "...#........", "...#........", "...#.###....", "...##...#...",
           "...#....#...", "...#....#...", "...#....#...", "...#....#...", "...##...#...", "...#.###...."),
    "d": R(2, "......##....", ".......#....", ".......#....", ".......#....", "...###.#....", "..#...##....",
           "..#....#....", "..#....#....", "..#....#....", "..#....#....", "..#...##....", "...###.#...."),
    "f": R(2, "....###.....", "...#...#....", "...#........", "...#........", ".#####......", "...#........",
           "...#........", "...#........", "...#........", "...#........", "...#........", "..###......."),
    "g": R(6, "...###.##...", "..#...##....", "..#....#....", "..#....#....", "..#...##....", "...###.#....",
           ".......#....", "......#.....", "...###......"),
    "h": R(2, "..##........", "...#........", "...#........", "...#........", "...#.###....", "...##...#...",
           "...#....#...", "...#....#...", "...#....#...", "...#....#...", "...#....#...", "..###..###.."),
    "j": {3: ".....#......", 6: "....##......", 7: ".....#......", 8: ".....#......", 9: ".....#......",
          10: ".....#......", 11: ".....#......", 12: ".....#......", 13: ".....#......", 14: "...##......."},
    "k": R(2, "..##........", "...#........", "...#........", "...#........", "...#...##...", "...#..#.....",
           "...#.#......", "...##.......", "...#.#......", "...#..#.....", "...#...#....", "..###..###.."),
    "l": R(2, "..##........", "...#........", "...#........", "...#........", "...#........", "...#........",
           "...#........", "...#........", "...#........", "...#........", "...#........", "..###......."),
    "m": R(6, "##.##..##...", ".##..##..#..", ".#...#...#..", ".#...#...#..", ".#...#...#..", ".#...#...#..",
           ".#...#...#..", "###.###.###."),
    "n": R(6, "..##.###....", "...##...#...", "...#....#...", "...#....#...", "...#....#...", "...#....#...",
           "...#....#...", "..###..###.."),
    "q": R(6, "...###.##...", "..#...##....", "..#....#....", "..#....#....", "..#....#....", "..#....#....",
           "..#...##....", "...###.#....", ".......#...."),
    "r": R(6, "..##.###....", "...##...#...", "...#........", "...#........", "...#........", "...#........",
           "...#........", "..###......."),
    "s": R(6, "...####.....", "..#....#....", "..#.........", "...##.......", ".....##.....", ".......#....",
           "..#....#....", "...####....."),
    "t": R(3, "....#.......", "....#.......", "....#.......", "..#####.....", "....#.......", "....#.......",
           "....#.......", "....#.......", "....#.......", "....#..#....", ".....##....."),
    "u": R(6, "..###..###..", "...#....#...", "...#....#...", "...#....#...", "...#....#...", "...#....#...",
           "...#...##...", "....###.##.."),
    "v": R(6, "..###.###...", "...#...#....", "...#...#....", "....#.#.....", "....#.#.....", ".....#......",
           ".....#......", ".....#......"),
    "w": R(6, "###.###.###.", ".#...#...#..", ".#...#...#..", "..#.#.#.#...", "..#.#.#.#...", "...#...#....",
           "...#...#....", "...#...#...."),
    "z": R(6, "..######....", "......#.....", ".....#......", ".....#......", "....#.......", "....#.......",
           "...#........", "..######...."),
    # ---- digits (rows 2..13)
    "0": R(2, "....###.....", "...#...#....", "..#.....#...", "..#.....#...", "..#.....#...", "..#.....#...",
           "..#.....#...", "..#.....#...", "..#.....#...", "..#.....#...", "...#...#....", "....###....."),
    "1": R(2, "....#.......", "...##.......", "..#.#.......", "....#.......", "....#.......", "....#.......",
           "....#.......", "....#.......", "....#.......", "....#.......", "....#.......", "...###......"),
    "2": R(2, "...####.....", "..#....#....", ".......#....", ".......#....", "......#.....", ".....#......",
           "....#.......", "...#........", "..#.........", "..#.........", "..#.........", "..#######..."),
    "3": R(2, "...####.....", "..#....#....", "........#...", "........#...", ".......#....", "....###.....",
           ".......#....", "........#...", "........#...", "........#...", "..#....#....", "...####....."),
    "4": R(2, "......##....", ".....#.#....", "....#..#....", "...#...#....", "..#....#....", ".#.....#....",
           ".#.....#....", ".########...", ".......#....", ".......#....", ".......#....", "......###..."),
    "5": R(2, "..#######...", "..#.........", "..#.........", "..#.........", "..####......", "......#.....",
           ".......#....", "........#...", "........#...", "........#...", "..#....#....", "...####....."),
    "6": R(2, ".....###....", "....#.......", "...#........", "..#.........", "..#.###.....", "..##...#....",
           "..#.....#...", "..#.....#...", "..#.....#...", "..#.....#...", "...#...#....", "....###....."),
    "7": R(2, "..#######...", "........#...", ".......#....", ".......#....", "......#.....", "......#.....",
           ".....#......", ".....#......", "....#.......", "....#.......", "....#.......", "...###......"),
    "8": R(2, "....###.....", "...#...#....", "...#...#....", "...#...#....", "....###.....", "...#...#....",
           "..#.....#...", "..#.....#...", "..#.....#...", "..#.....#...", "...#...#....", "....###....."),
    "9": R(2, "....###.....", "...#...#....", "..#.....#...", "..#.....#...", "..#.....#...", "..#.....#...",
           "...#...##...", "....###.#...", "........#...", ".......#....", "......#.....", "...###......"),
    # ---- marks (the full-width BIOS marks sit in their own cell positions: redrawn on the baseline grid)
    "8143": R(12, "....##......", "....##......", "...#........"),
    "8144": R(12, "....##......", "....##......"),
    "8146": R(8, "....##......", "....##......", None, None, "....##......", "....##......"),
    "8147": R(8, "....##......", "....##......", None, None, "....##......", "....##......", "...#........"),
    "8148": R(2, "...####.....", "..#....#....", ".......#....", ".......#....", "......#.....", ".....#......",
              "....#.......", "....#.......", "....#.......", None, "....##......", "....##......"),
    "8149": R(2, "....#.......", "....#.......", "....#.......", "....#.......", "....#.......", "....#.......",
              "....#.......", "....#.......", "....#.......", None, "....##......", "....##......"),
    "814D": R(2, "..#.........", "...#........"),
    "814F": R(2, "....#.......", "...#.#......", "..#...#....."),
    "8151": {14: "###########."},
    "815C": {10: ".###########"},
    "815D": {10: "..#####....."},
    "817C": {10: "..#######..."},
    "815E": R(2, ".......#....", ".......#....", "......#.....", "......#.....", ".....#......", ".....#......",
              "....#.......", "....#.......", "...#........", "...#........", "..#.........", "..#........."),
    "815F": R(2, "..#.........", "..#.........", "...#........", "...#........", "....#.......", "....#.......",
              ".....#......", ".....#......", "......#.....", "......#.....", ".......#....", ".......#...."),
    "8160": R(9, "..##...#....", ".#..#.#.....", "....##......"),
    "8162": R(2, ".....#......", ".....#......", ".....#......", ".....#......", ".....#......", ".....#......",
              ".....#......", ".....#......", ".....#......", ".....#......", ".....#......", ".....#......"),
    "8169": R(2, "....#.......", "...#........", "...#........", "..#.........", "..#.........", "..#.........",
              "..#.........", "..#.........", "..#.........", "...#........", "...#........", "....#......."),
    "816A": R(2, "..#.........", "...#........", "...#........", "....#.......", "....#.......", "....#.......",
              "....#.......", "....#.......", "....#.......", "...#........", "...#........", "..#........."),
    "816D": R(2, "..###.......", "..#.........", "..#.........", "..#.........", "..#.........", "..#.........",
              "..#.........", "..#.........", "..#.........", "..#.........", "..#.........", "..###......."),
    "816E": R(2, "..###.......", "....#.......", "....#.......", "....#.......", "....#.......", "....#.......",
              "....#.......", "....#.......", "....#.......", "....#.......", "....#.......", "..###......."),
    "816F": R(2, "....##......", "...#........", "...#........", "...#........", "...#........", "..#.........",
              "..#.........", "...#........", "...#........", "...#........", "...#........", "....##......"),
    "8170": R(2, "...##.......", ".....#......", ".....#......", ".....#......", ".....#......", "......#.....",
              "......#.....", ".....#......", ".....#......", ".....#......", ".....#......", "...##......."),
    "817B": R(8, ".....#......", ".....#......", "...#####....", ".....#......", ".....#......"),
    "817E": R(8, "...#...#....", "....#.#.....", ".....#......", "....#.#.....", "...#...#...."),
    "8181": {9: "..#######...", 11: "..#######..."},
    "8183": R(7, "........#...", ".......#....", "......#.....", ".....#......", "......#.....", ".......#....",
              "........#..."),
    "8184": R(7, "..#.........", "...#........", "....#.......", ".....#......", "....#.......", "...#........",
              "..#........."),
    # $: the S of the cells 0x8190 turned 180 degrees onto itself, the bar through it (rows 2..13)
    "8190": R(2, ".....#......", "...#####....", "..#..#..#...", "..#..#......", "...###......", ".....#......",
              ".....#......", ".....###....", ".....#..#...", "..#..#..#...", "...#####....", ".....#......"),
    "8193": R(2, ".##.....#...", "#..#...#....", "#..#..#.....", ".##...#.....", ".....#......", ".....#......",
              "....#.......", "....#..##...", "...#..#..#..", "...#..#..#..", "..#....##...", ".#.........."),
    "8194": R(2, "...#..#.....", "...#..#.....", "...#..#.....", ".########...", "...#..#.....", "...#..#.....",
              "...#..#.....", ".########...", "...#..#.....", "...#..#.....", "...#..#.....", "...#..#....."),
    "8195": R(2, "...###......", "..#...#.....", "..#...#.....", "...#.#......", "....#.......", "...#.#...#..",
              "..#...#.#...", ".#.....#....", ".#.....##...", ".#....#.#...", "..#..#...#..", "...##.....#."),
    "8196": R(3, "....#.......", "..#.#.#.....", "...###......", "..#.#.#.....", "....#......."),
}


def build():
    uk = uk_cells()
    cells = {}
    for key in KEYS:
        if key in HAND:
            rows = [[False] * W for _ in range(H)]
            for y, s in HAND[key].items():
                assert len(s) == W, (key, y, s)
                rows[y] = [c == "#" for c in s]
        elif key in SAME:
            rows = uk[SAME[key]]
        else:
            rows = place(squeeze(regrid(bios_rows(sjis(key)))))
        cells[key] = rows
    return cells


def save_sheet(cells, path=SHEET):
    n = len(KEYS)
    im = Image.new("P", (PER_ROW * W, ((n + PER_ROW - 1) // PER_ROW) * H), 0)
    im.putpalette([0, 0, 0, 255, 255, 255])
    px = im.load()
    for i, key in enumerate(KEYS):
        for y in range(H):
            for x in range(W):
                if cells[key][y][x]:
                    px[(i % PER_ROW) * W + x, (i // PER_ROW) * H + y] = 1
    im.save(path)


def load_sheet(path=SHEET):
    px = Image.open(path).load()
    return {key: [[px[(i % PER_ROW) * W + x, (i // PER_ROW) * H + y] != 0 for x in range(W)] for y in range(H)]
            for i, key in enumerate(KEYS)}


# ---- the comparison pictures (a check for the eye, not game code)
BG, INK = (24, 28, 84), (255, 255, 255)


def pen(rows, x, img, y0):
    """Draw 15 rows of 12 values at x with the BlitChar rules (the left blank and the advance count in
    2 px units). A value is a bool or a shade 0..4 (the stock squeeze). Returns the advance in px."""
    rows = [list(r) for r in rows] + [[0] * 12]
    if any(any(r) for r in rows):
        while not any(r[0] or r[1] for r in rows):
            rows = [r[2:] + [0, 0] for r in rows]
    last = 0
    for r in rows:
        for j in range(6):
            if r[2 * j] or r[2 * j + 1]:
                last = max(last, j)
    lw = last
    if any(r[2 * lw + 1] for r in rows):
        lw += 1
    if lw < 6:
        lw += 1
    for y, r in enumerate(rows):
        for xx, v in enumerate(r):
            if v:
                f = (v if isinstance(v, int) and not isinstance(v, bool) else 4) / 4
                p = tuple(int(BG[k] + (INK[k] - BG[k]) * f) for k in range(3))
                if 0 <= x + xx < img.width and y0 + y < img.height:
                    img.putpixel((x + xx, y0 + y), p)
    return lw * 2


def stock_rows(code):
    """The stock squeeze (func_80106A28 kind 0): 16 columns -> 12, weights 3/1, 2/2, 1/3, shades 0..4."""
    out = []
    for r in bios_rows(code):
        row = []
        for g in range(4):
            b = [int(v) for v in r[4 * g:4 * g + 4]]
            row += [b[0] * 3 + b[1], b[1] * 2 + b[2] * 2, b[2] + b[3] * 3]
        out.append(row)
    return out


# The en texts join words with 81 7C (Nose－devil, Two－handed) and the uk em dash is 81 5C: the A font draws
# both as the hyphen cell 81 5D (owner, 2026-11-25). The sheet keeps the cells 815C and 817C for the marks.
ALIAS = {"815C": "815D", "817C": "815D"}
MARK_OF = {".": "8144", ",": "8143", ":": "8146", "!": "8149", "?": "8148", "-": "817C", '"': "8167",
           "[": "816D", "]": "816E", "'": "8166"}


def keys_of(s):
    return [" " if ch == " " else ch if ch.isalnum() and ch.isascii() else MARK_OF.get(ch, ch) for ch in s]


def render_line(s, mode, cells, uk, font_b):
    img = Image.new("RGB", (560, 16), BG)
    x = 0
    for k in keys_of(s):
        if k == " ":
            x += 4 if mode != "B" else 8
            continue
        if mode == "new":
            k = ALIAS.get(k, k)
            rows = cells.get(k) or uk.get(k)
        elif mode == "cur":
            if k in uk:
                rows = uk[k]
            elif k in ("«", "»"):
                rows = stock_rows(0x8173 if k == "«" else 0x8174)
            else:
                rows = stock_rows(sjis(k))
        else:  # B: 8x11 cells at the menu scale, advance 8
            cell = font_b(k)
            if cell is not None:
                for y, r in enumerate(cell):
                    for xx, v in enumerate(r):
                        if v:
                            img.putpixel((x + xx, 2 + y), INK)
            x += 8
            continue
        if rows is None:
            continue
        x += pen(rows, x, img, 0)
    return img.crop((0, 0, x + 4, 16))


def font_b_loader():
    """8x11 cells: the shipped mfont.bin (menu cells by 8x8 cell code) and dfont.png (the guillemets)."""
    p = os.path.join(HERE, "..", "..", "build", "linux-static", "lang", "uk", "mfont.bin")
    rec = {}
    if os.path.exists(p):
        d = open(p, "rb").read()
        for i in range(struct.unpack_from("<I", d, 8)[0]):
            r = d[12 + i * 48:12 + i * 48 + 48]
            rec[r[0]] = [[v for b in r[4 + y * 4:8 + y * 4] for v in (b & 15, b >> 4)] for y in range(11)]
    dim = Image.open(os.path.join(UK, "dfont.png")).convert("L")
    order = json.load(open(os.path.join(UK, "glyphs.json")))["dfont"]["order"]

    def cell(k):
        if k in order:  # the uk letters and the guillemets of the dialogue font
            i = order.index(k)
            return [[dim.getpixel(((i % 16) * 8 + x, (i // 16) * 11 + y)) != 0 for x in range(8)] for y in range(11)]
        # the cell codes of the 8x8 sheet: ASCII - 0x20 (the " is the cell of <, as in the game)
        c = {"8144": 14, "8143": 12, "8146": 26, "815C": 13, "8167": 2, "8166": 7, "816D": 59, "816E": 61}.get(k)
        if c is None and len(k) == 1:
            c = ord(k) - 0x20
        return rec.get(c)
    return cell


LINES_EN = ["Obtained Spirit Orb", 'Summons "Nose-devil" familiar', "Magically sealed.", "Summons bat ally [use]"]
LINES_UK = ["Викликає фамільяра «Носатий демон»", "Отримано Куб Зої", "Кличе кажана [вжити]"]


def compare(outdir):
    cells = load_sheet() if os.path.exists(SHEET) else build()
    uk = uk_cells()
    fb = font_b_loader()
    fnt = ImageFont.load_default()
    cols = [("current A", "cur"), ("new A", "new"), ("B (8x11)", "B")]
    rows = [("en", s) for s in LINES_EN] + [("uk", s) for s in LINES_UK]
    lines = [[render_line(s, m, cells, uk, fb) for _, m in cols] for _, s in rows]
    pad, lab = 8, 12
    for zoom, name in ((1, "P7_compare.png"), (4, "P7_compare_4x.png")):
        cw = max(li.width for ln in lines for li in ln) * zoom
        img = Image.new("RGB", ((cw + pad) * 3 + pad, (16 * zoom + lab + pad) * len(rows) + 16), (16, 16, 16))
        dr = ImageDraw.Draw(img)
        for ci, (cn, _) in enumerate(cols):
            dr.text((pad + ci * (cw + pad), 2), f"{cn}, {'1:1' if zoom == 1 else '4x'}", fill=(255, 220, 120), font=fnt)
        y = 16
        for (lang, s), ln in zip(rows, lines):
            for ci, li in enumerate(ln):
                x0 = pad + ci * (cw + pad)
                dr.text((x0, y), f"[{lang}] {cols[ci][0]}", fill=(160, 200, 255), font=fnt)
                img.paste(li.resize((li.width * zoom, li.height * zoom), Image.NEAREST), (x0, y + lab))
            y += 16 * zoom + lab + pad
        img.save(os.path.join(outdir, name))

    # glyph sheet: the new Latin set above the uk letters, same scale
    keys = list(KEYS)  # letters, digits, then the marks in the order of MARKS, then the guillemets
    uks = list("АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЩЬЮЯабвгдежзиклмнопрстуфхцчшщьюя")
    z, per = 4, 20

    def sheet(items, get, title):
        im = Image.new("RGB", (per * 14 * z, ((len(items) + per - 1) // per) * 17 * z + 14), (16, 16, 16))
        d = ImageDraw.Draw(im)
        d.text((2, 1), title, fill=(255, 220, 120), font=fnt)
        for i, k in enumerate(items):
            for y in range(15):
                for x in range(12):
                    if get(k)[y][x]:
                        X, Y = (i % per) * 14 * z + (x + 1) * z, 14 + (i // per) * 17 * z + y * z
                        d.rectangle((X, Y, X + z - 1, Y + z - 1), fill=(255, 255, 255))
        return im
    a = sheet(keys, lambda k: cells[k], "new Latin set (12x15 cells, 4x)")
    b = sheet(uks, lambda k: uk[k], "the owner's uk letters (12x15 cells, 4x)")
    allp = Image.new("RGB", (a.width, a.height + b.height), (16, 16, 16))
    allp.paste(a, (0, 0))
    allp.paste(b, (0, a.height))
    allp.save(os.path.join(outdir, "P7_glyphs.png"))


def before_after(old_png, out, z=6):
    old, new = load_sheet(old_png), load_sheet()
    keys = [k for k in KEYS if old[k] != new[k]]
    per, cw, ch = 8, (2 * W + 3) * z, (H + 2) * z
    fnt = ImageFont.load_default()
    im = Image.new("RGB", (per * cw, ((len(keys) + per - 1) // per) * (ch + 12)), (16, 16, 16))
    d = ImageDraw.Draw(im)
    for i, k in enumerate(keys):
        ox, oy = (i % per) * cw, (i // per) * (ch + 12)
        d.text((ox + 2, oy), k if len(k) == 1 else "SJIS " + k, fill=(255, 220, 120), font=fnt)
        for side, cells in enumerate((old, new)):
            for y in range(H):
                for x in range(W):
                    if cells[k][y][x]:
                        X, Y = ox + (side * (W + 1) + x) * z + z, oy + 12 + y * z
                        d.rectangle((X, Y, X + z - 1, Y + z - 1), fill=(255, 255, 255) if side else (150, 150, 150))
    im.save(out)
    return keys


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "sheet":
        if os.path.exists(SHEET) and "--force" not in sys.argv:
            sys.exit("latin12.png exists: it is the source of truth now; --force rebuilds the draft")
        save_sheet(build())
        print("wrote", SHEET)
    elif cmd == "before_after" and len(sys.argv) > 3:
        print(len(before_after(sys.argv[2], sys.argv[3])), "cells changed")
    elif cmd == "compare" and len(sys.argv) > 2:
        compare(sys.argv[2])
    else:
        sys.exit(__doc__)
