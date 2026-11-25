#!/usr/bin/env python3
"""Offline check of the Pocket name field (B23): every name that can go in the Pocket, en and uk.

Run: python3 tools/lang/pocket_check.py   (from the repo root; no build needed)
The text is 8 px per cell. The field width is the one of PocketName (src/dra/menu.c);
the scroll distance is Marquee_Over (src/pc/quick_item.c), mirrored here.
Rule: it fits (overflow 0) -> no scroll; else the overflow is at least one cell (8 px)."""
import json, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CELL = 8


def read(*p):
    return open(os.path.join(ROOT, *p), encoding="utf-8").read()


menu = read("src", "dra", "menu.c")
const = {k: int(v) for k, v in re.findall(r"#define (POCKET_\w+|EQUIP_WIN_W)\s+(\d+)", menu)}
items = read("include", "items.h")
ids = {n: int(i, 16) for i, n in re.findall(r"/\* 0x([0-9A-F]+) \*/\s+(\w+),", items)}


def field(digits):  # PocketName: the count has `digits` digits, 0 = not drawn
    return const["POCKET_COUNT_X"] + CELL - CELL * digits - const["POCKET_GAP"] - const["POCKET_X"] - 16


def scroll(width, w):  # Marquee_Over
    over = width - w
    return 0 if over <= 0 else max(over, CELL)


rows = {}  # id -> (en name, consumable)
for m in re.finditer(r'/\* 0x([0-9A-F]+) \*/ \{_S\("([^"]*)"\), "[^"]*", ((?:[^,}]+, ){25}[^,}]+)\}',
                     read("src", "dra", "gen", "us", "equipment.h")):
    f = [x.strip() for x in m.group(3).split(",")]
    # attack defense element category weaponId palette unk11 anim unk13 unk14 lock chain unk17 special isConsumable(14)
    wid, u11, u13, u14, cons = int(f[4]), int(f[6]), int(f[8]), int(f[9]), f[14] == "true"
    i = int(m.group(1), 16)
    medicine = u13 == 55 and wid == 255 and i not in (ids["ITEM_LIFE_APPLE"], ids["ITEM_HAMMER"])
    food = u13 == 50 and u11 == 132 and wid == 21 and u14 != 41
    if i == 0 or (i < ids["NUM_HAND_ITEMS"] and (medicine or food)):  # QuickItem_Eligible + empty hand
        rows[i] = (m.group(2), cons)

uk = json.load(open(os.path.join(ROOT, "tools/lang/uk/ua.json"), encoding="utf-8"))
uk_over = json.load(open(os.path.join(ROOT, "tools/lang/uk/ua_items.json"), encoding="utf-8"))["EQUIP_NAME"]
assert len(rows) > 20, f"only {len(rows)} Pocket items found: the parser broke"

bad, table, n = [], [], 0
for i, (en, cons) in sorted(rows.items()):
    for lang, name in (("en", en), ("uk", uk_over.get(en) or uk[f"item names:{i}"])):
        name = name.rstrip(" ")  # blank cells at the end are not text
        width = len(name) * CELL
        for digits in ((1, 2) if cons else (0,)):
            w, n = field(digits), n + 1
            over, dist = width - w, scroll(width, w)
            if over <= 0:
                assert dist == 0, f"{lang} {name!r}: fits but scrolls {dist}"
            else:
                assert over >= CELL, f"{lang} {name!r} ({digits} digits): overflow {over} px < one cell"
                assert dist >= CELL
                table.append((lang, i, name, digits, width, w, over))
print(f"{n} checks, {len(rows)} items, field {field(1)}/{field(2)} px (count 1/2 digits), {field(0)} px (no count)")
print("names with overflow > 0 (they scroll):" if table else "no name overflows")
for lang, i, name, d, width, w, over in table:
    print(f"  {lang} 0x{i:02X} {name!r:32} digits {d}  width {width:3}  field {w:3}  overflow {over}")
print("pocket_check ok")
