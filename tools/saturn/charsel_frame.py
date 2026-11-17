#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# The card frame of the PC select screen (src/st/sel/charsel.h) and Maria's
# variant arrows, drawn in code: no disc data, safe for git. Imported by
# charsel_gfx.py.
#
# The frame: a blue gothic moulding lit from the top left, red stones in gold
# rings at the corners, gothic pinnacles on the top edge and a pointed crest
# with a gold cross. 4bpp: index 0 is transparent, CLUTS[0] is a card,
# CLUTS[1] the chosen card. The window inside the moulding is the portrait.
import math

W, H = 100, 120  # the frame
TOP = 12  # the moulding's top edge; the crest and the pinnacles are above it
BAND = 8  # the moulding's width
WIN = (BAND, TOP + BAND, W - 2 * BAND, H - TOP - 2 * BAND)  # x, y, w, h
CX = (W - 1) / 2

# the moulding across, outside to inside: lit sides (top, left), shaded
# sides; the groove at 4, the window's gold rim at 7
LIT = [1, 6, 5, 4, 3, 4, 1, 9]
SHADE = [1, 4, 3, 3, 2, 3, 1, 7]

# 0 clear, 1 outline, 2..6 blue dark to light, 7..10 gold dark to light,
# 11..12 red stone, 13 sparkle
CLUTS = [
    [(0, 0, 0), (6, 8, 28), (16, 28, 78), (30, 54, 132), (52, 92, 188),
     (96, 146, 232), (176, 210, 255), (112, 70, 16), (196, 142, 40),
     (244, 204, 96), (255, 244, 196), (110, 10, 24), (200, 30, 48),
     (255, 255, 255), (0, 0, 0), (0, 0, 0)],
    [(0, 0, 0), (10, 14, 44), (40, 70, 160), (70, 116, 224), (110, 168, 255),
     (160, 210, 255), (230, 246, 255), (160, 108, 24), (236, 184, 56),
     (255, 232, 128), (255, 252, 224), (150, 16, 32), (248, 56, 72),
     (255, 255, 255), (0, 0, 0), (0, 0, 0)],
]
# the window's fill, top and bottom: a vertical gradient (a POLY_G4)
FILL_TOP = (36, 58, 132)
FILL_BOTTOM = (8, 14, 52)


def put(px, x, y, c):
    if 0 <= x < len(px[0]) and 0 <= y < len(px):
        px[y][x] = c


def moulding(px):
    for y in range(TOP, H):
        for x in range(W):
            t, l, b, r = y - TOP, x, H - 1 - y, W - 1 - x
            d = min(t, l, b, r)
            if d >= BAND:
                continue
            px[y][x] = (LIT if d in (t, l) else SHADE)[d]
            # gold beads along the groove
            along = x if d in (t, b) else y
            far = min(l, r) if d in (t, b) else min(t, b)
            if d == 4 and along % 6 == 3 and far > 12:
                px[y][x] = 9 if d in (t, l) else 8


def boss(px, x0, y0):
    """an 11x11 corner block: a red stone in a gold ring"""
    for y in range(11):
        for x in range(11):
            dx, dy = x - 5, y - 5
            r = math.hypot(dx, dy)
            lit = dx + dy < 0
            if x in (0, 10) or y in (0, 10):
                c = 1
            elif r < 2.0:
                c = 13 if (dx, dy) == (-1, -1) else 12 if lit or r < 1 else 11
            elif r < 3.6:
                c = 9 if lit else 8 if dx + dy == 0 else 7
            elif r < 4.4:
                c = 1
            elif x == 1 or y == 1:
                c = 6
            elif x == 9 or y == 9:
                c = 2
            else:
                c = 4
            put(px, x0 + x, y0 + y, c)


def stud(px, cx, cy):
    """a gold diamond on the moulding"""
    for y in range(int(cy) - 4, int(cy) + 5):
        for x in range(int(cx) - 4, int(cx) + 5):
            dx, dy = x - cx, y - cy
            d = abs(dx) + abs(dy)
            if d <= 2.5:
                put(px, x, y, 10 if d < 1 else 9 if dx + dy < 0 else 7)
            elif d <= 3.5:
                put(px, x, y, 1)


def arch(px, cx, apex, base, half, inner):
    """a pointed arch from apex down to base, half wide at the base; its
    outline, a bevel and a dark inside from `inner` pixels in"""
    hc = base - apex
    r = (half * half + hc * hc) / (2 * half)
    for y in range(apex, base + 1):
        hw = half - r + math.sqrt(max(0.0, r * r - (base - y) ** 2))
        for x in range(int(cx - half) - 1, int(cx + half) + 2):
            t = hw - abs(x - cx)
            if t < -0.5:
                continue
            left = x < cx
            if t < 0.7 or y == apex:
                c = 1
            elif t < 1.7:
                c = 6 if left else 3
            elif t < inner - 1:
                c = 4 if left else 3
            elif t < inner:
                c = 1
            else:
                c = 2
            put(px, x, y, c)


def crest(px):
    arch(px, CX, 2, TOP + 5, 11, 4.2)
    # a gold knob on the point
    for y, row in enumerate(["1981", "199871", "1871"]):
        x0 = int(CX) - len(row) // 2 + 1
        for i, ch in enumerate(row):
            put(px, x0 + i, y, int(ch))
    # the cross: its outline, then the gold bars, a red stone in the middle
    bars = [(x, y) for x in (49, 50) for y in range(6, 17)]
    bars += [(x, y) for x in range(46, 54) for y in (9, 10)]
    for x, y in bars:
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                if (x + ox, y + oy) not in bars:
                    put(px, x + ox, y + oy, 1)
    for x, y in bars:
        c = 9 if x <= 49 and y <= 9 else 8 if x <= 49 or y <= 9 else 7
        if (x, y) in ((49, 6), (46, 9)):
            c = 10
        put(px, x, y, c)
    for x, y in ((49, 9), (50, 9), (49, 10), (50, 10)):
        put(px, x, y, 13 if (x, y) == (49, 9) else 12)


def pinnacle(px, cx):
    """a small spire on the top edge with a gold tip"""
    arch(px, cx, TOP - 6, TOP, 3.5, 9)
    put(px, int(cx), TOP - 7, 9)
    put(px, int(cx) + 1, TOP - 7, 8)
    put(px, int(cx), TOP - 8, 1)
    put(px, int(cx) + 1, TOP - 8, 1)


def frame():
    px = [[0] * W for _ in range(H)]
    for cx in (5.5, 21.5, 77.5, 93.5):
        pinnacle(px, cx)
    moulding(px)
    crest(px)
    # the window's top rim runs under the crest
    for x in range(BAND - 2, W - BAND + 2):
        px[TOP + 6][x] = 1
        if BAND - 1 <= x <= W - BAND:
            px[TOP + 7][x] = 9
    for x0, y0 in ((0, TOP), (W - 11, TOP), (0, H - 11), (W - 11, H - 11)):
        boss(px, x0, y0)
    for y in (TOP + (H - TOP) // 2,):
        stud(px, 3.5, y)
        stud(px, W - 4.5, y)
    stud(px, CX, H - 4.5)
    return px


def arrow(up):
    """Maria's variant arrow, 12x7, gold"""
    px = [[0] * 12 for _ in range(7)]
    for r in range(6):
        for x in range(5 - r, 7 + r):
            edge = x in (5 - r, 6 + r)
            px[r][x] = 1 if edge or r == 0 else 10 if r == 1 else 9 if x <= 5 else 7
    for x in range(12):
        px[6][x] = 1
    return px if up else px[::-1]


def words4(px):
    """4bpp rows as u16 halfwords, the left pixel in the low nibble"""
    out = []
    for row in px:
        for x in range(0, len(row), 4):
            out.append(sum(row[x + i] << (4 * i) for i in range(4)))
    return out


if __name__ == "__main__":
    # self-check: the shape is symmetric, the window is clear, every index
    # has a colour
    f = frame()
    assert all([c > 0 for c in row] == [c > 0 for c in row[::-1]] for row in f)
    x, y, w, h = WIN
    clear = sum(f[j][i] == 0 for j in range(y, y + h) for i in range(x, x + w))
    assert clear * 10 > w * h * 9, clear
    assert max(max(r) for r in f) < len(CLUTS[0])
    assert len(words4(f)) == W * H // 4
    assert arrow(False) == arrow(True)[::-1]
    print("ok")
