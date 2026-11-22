#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Castle room table for the teleport map and the editor page.

Reads the extracted (git-ignored) assets/st/<stage>/**/rooms.json and writes
one C file to the build directory: every room of the castle stages with its
stage id, its index in the stage's room list (door stubs counted, as the game
counts them) and its rect in map cells. Door stubs (tileDefId -1) are left
out after numbering. No assets: an empty table, the map then says so.

usage: export_castle.py <assets dir> <out.c>
"""
import glob
import json
import os
import sys

# castle stages that have an overlay in this port, name -> stage id
# (include/game.h, enum Stages). ST0, DRE, MAD and the boss overlays have no
# place of their own on the castle map.
NORMAL = {
    "no0": 0x00, "no1": 0x01, "lib": 0x02, "cat": 0x03, "no2": 0x04,
    "chi": 0x05, "dai": 0x06, "np3": 0x07, "cen": 0x08, "no4": 0x09,
    "are": 0x0A, "top": 0x0B, "nz0": 0x0C, "nz1": 0x0D, "wrp": 0x0E,
    "no3": 0x41,
}
STAGES = dict(NORMAL)
for name, sid in NORMAL.items():
    if name not in ("no3", "np3"):
        STAGES["r" + name] = sid | 0x20
STAGES["rno3"] = 0x07 | 0x20  # the inverted entrance is RNO3, id of NP3
# boss stages with a teleport target: BO4 (Doppelganger10) has no castle
# door the warp can use, its room is on top of NO1 room 11
BOSSES = {"bo4": 0x1A}
# Saturn stage PRGs (disks/saturn), src/pc/pc.h ids. Only Saturn builds pass
# the folder. Room records: 6 bytes from the pointer at 0x10, end 0x40.
SATURN = {"STAGE_15.PRG": (0x4C, "ST15"), "STAGE_16.PRG": (0x4D, "ST16"),
          "RSTAGE15.PRG": (0x6C, "RS15"), "RSTAGE16.PRG": (0x6D, "RS16")}
SATURN_LOAD_ADDRESS = 0x060DC000
# stages with no place on the castle map: a name only, for the apron patch
# file (apron/<NAME>.txt) and the editor page. Written with or without assets.
NAME_ONLY = {"DRE": 0x12, "BO7": 0x16, "MAR": 0x17, "BO6": 0x18, "BO5": 0x19,
             "BO4": 0x1A, "BO3": 0x1B, "BO2": 0x1C, "BO1": 0x1D, "BO0": 0x1E,
             "ST0": 0x1F, "RBO8": 0x36, "RBO7": 0x37, "RBO6": 0x38,
             "RBO5": 0x39, "RBO4": 0x3A, "RBO3": 0x3B, "RBO2": 0x3C,
             "RBO1": 0x3D, "RBO0": 0x3E, "MAD": 0x40}


def rooms_file(assets, name, kind="st"):
    found = sorted(glob.glob(os.path.join(assets, kind, name, "**", "rooms.json"),
                             recursive=True))
    return found[0] if found else None


def saturn_rooms(prg):
    """(index, left, top, right, bottom) of the rooms a player can stand in:
    the loading records (byte 5 = 0xFF) come last and are left out."""
    at = int.from_bytes(prg[0x10:0x14], "big") - SATURN_LOAD_ADDRESS
    i = 0
    while prg[at + i * 6] != 0x40:
        b = prg[at + i * 6:at + i * 6 + 6]
        if b[5] != 0xFF:
            yield (i, b[0], b[1], b[2], b[3])
        i += 1


MARK_BOSS, MARK_SAVE, MARK_WARP = 0, 1, 2
# F_MAP.BIN, the game's map: 256x256 texels 4bpp, one cell = 4x4 texels at
# (4x, 4y). Inside a cell colour 4 is a save room (red on screen), colour 5
# a teleporter (yellow). The inverted castle uses it turned by 180 degrees
# (func_800F1B08: cell 63 - x, 63 - y).
MAP_COLOURS = {4: MARK_SAVE, 5: MARK_WARP}


def map_marks(fmap):
    def texel(x, y):
        b = fmap[y * 128 + x // 2]
        return b & 15 if x % 2 == 0 else b >> 4
    marks = []
    for cy in range(64):
        for cx in range(64):
            inner = {texel(cx * 4 + i, cy * 4 + j) for i in (1, 2, 3) for j in (1, 2, 3)}
            for colour, kind in MAP_COLOURS.items():
                if colour in inner:
                    marks.append((0x00, kind, cx, cy, cx, cy))
                    marks.append((0x20, kind, 63 - cx, 63 - cy, 63 - cx, 63 - cy))
    return marks


# The Saturn castle map (SUB_DISP.MAP, 320x240 texels 4bpp, high nibble
# first): a cell is 5x4 texels, colour 1 a room, 4 a save room, 6 a teleporter.
# F_MAP.BIN is the same map with 4x4 texels a cell: Saturn columns 1, 0, 3, 4
# become its columns 0..3, and the teleporter colour is 5. The cells only the
# Saturn has (the Garden and the Prison) are added to F_MAP.BIN's.
SATURN_MAP_COLUMNS = (1, 0, 3, 4)
SATURN_MAP_COLOUR = {0: 0, 1: 1, 4: 4, 6: 5}


def saturn_map_cells(sub_disp, fmap):
    sat = bytearray()
    for b in sub_disp[:320 * 240 // 2]:
        sat += bytes((b >> 4, b & 15))
    cells = []
    for cy in range(60):
        for cx in range(64):
            t = [SATURN_MAP_COLOUR.get(sat[(cy * 4 + j) * 320 + cx * 5 + c], 1)
                 for j in range(4) for c in SATURN_MAP_COLUMNS]
            old = [fmap[(cy * 4 + j) * 128 + cx * 2 + i // 2] >> (4 * (i % 2)) & 15
                   for j in range(4) for i in range(4)]
            if any(t) and not any(old):
                cells.append((cx, cy, bytes(t[i] | t[i + 1] << 4 for i in range(0, 16, 2))))
    return cells


def boss_marks(assets):
    """The rooms of the boss overlays (BO0..BO7, RBO0..RBO8); MAR is the
    Maria scene, not a boss. Bosses fought inside a castle stage have no
    overlay and no mark."""
    marks = []
    for path in sorted(glob.glob(os.path.join(assets, "boss", "*", "rooms.json"))):
        name = os.path.basename(os.path.dirname(path))
        if not (name.startswith("bo") or name.startswith("rbo")):
            continue
        for r in json.load(open(path)):
            if r["tileDefId"] >= 0 and r["left"] < 0x40:
                marks.append((0x20 if name.startswith("r") else 0x00, MARK_BOSS,
                              r["left"], r["top"], r["right"], r["bottom"]))
    return marks


def main():
    assets, out = sys.argv[1], sys.argv[2]
    saturn_dir = sys.argv[3] if len(sys.argv) > 3 else None
    rows, names = [], []
    stages = [(sid, name, "st") for name, sid in STAGES.items()]
    stages += [(sid, name, "boss") for name, sid in BOSSES.items()]
    for sid, name, kind in sorted(stages):
        path = rooms_file(assets, name, kind)
        if not path:
            continue
        names.append((sid, name.upper()))
        for i, r in enumerate(json.load(open(path))):
            if r["tileDefId"] < 0 or r["left"] >= 0x40:
                continue
            rows.append((sid, i, r["left"], r["top"], r["right"], r["bottom"]))
    for prg_name, (sid, name) in sorted(SATURN.items(), key=lambda kv: kv[1]):
        path = os.path.join(saturn_dir or "", prg_name)
        if not saturn_dir or not os.path.exists(path):
            continue
        names.append((sid, name))
        rows += [(sid,) + r for r in saturn_rooms(open(path, "rb").read())]
    named = {sid for sid, _ in names}
    names += [(sid, n) for n, sid in sorted(NAME_ONLY.items(), key=lambda kv: kv[1])
              if sid not in named]
    marks = boss_marks(assets)
    fmap_path = os.path.join(assets, "..", "disks", "us", "F_MAP.BIN")
    sat_cells = []
    if os.path.exists(fmap_path):
        fmap = bytearray(open(fmap_path, "rb").read())
        marks += map_marks(fmap)
        sub_disp = os.path.join(saturn_dir or "", "SUB_DISP.MAP")
        if saturn_dir and os.path.exists(sub_disp):
            sat_cells = saturn_map_cells(open(sub_disp, "rb").read(), fmap)
            for cx, cy, tex in sat_cells:  # the marks of the new cells too
                for j in range(4):
                    fmap[(cy * 4 + j) * 128 + cx * 2:][:2] = tex[j * 2:j * 2 + 2]
            marks = boss_marks(assets) + map_marks(fmap)
    # check: each yellow map cell is a room of WRP (normal) or RWRP (turned);
    # the Saturn's own teleporter (a room of its WARP.PRG) has none
    own = {(0, cx, cy) for cx, cy, _ in sat_cells} | {(0x20, 63 - cx, 63 - cy) for cx, cy, _ in sat_cells}
    warp = {(m[0], m[2], m[3]) for m in marks if m[1] == MARK_WARP} - own
    wrp = {(r[0] & 0x20, r[2], r[3]) for r in rows if r[0] & 0x1F == 0x0E}
    if wrp and not warp <= wrp:
        print("castle: WARNING teleporter cells outside WRP:", sorted(warp - wrp))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out + ".tmp", "w") as f:
        f.write("// generated by tools/editor/export_castle.py from the\n"
                "// git-ignored assets/st/*/rooms.json. Do not commit.\n"
                "#include <game.h>\n\n")
        f.write("const struct CastleRoom g_CastleRooms[] = {\n")
        for row in rows:
            f.write("    {0x%02X, %d, %d, %d, %d, %d},\n" % row)
        f.write("    {0xFF, 0, 0, 0, 0, 0},\n};\n")
        f.write("const int g_CastleRoomCount = %d;\n\n" % len(rows))
        f.write("const struct CastleStage g_CastleStages[] = {\n")
        for sid, name in names:
            f.write('    {0x%02X, "%s"},\n' % (sid, name))
        f.write('    {0xFF, ""},\n};\n')
        f.write("const int g_CastleStageCount = %d;\n\n" % len(names))
        f.write("const struct CastleMark g_CastleMarks[] = {\n")
        for m in marks:
            f.write("    {0x%02X, %d, %d, %d, %d, %d},\n" % m)
        f.write("    {0xFF, 0, 0, 0, 0, 0},\n};\n")
        f.write("const int g_CastleMarkCount = %d;\n\n" % len(marks))
        f.write("const struct SaturnMapCell g_SaturnMapCells[] = {\n")
        for cx, cy, tex in sat_cells:
            f.write("    {%d, %d, {%s}},\n" % (cx, cy, ", ".join("0x%02X" % b for b in tex)))
        f.write("    {0xFF, 0xFF, {0}},\n};\n")
        f.write("const int g_SaturnMapCellCount = %d;\n" % len(sat_cells))
    os.replace(out + ".tmp", out)
    print("castle: %d stages, %d rooms, %d marks (boss %d, save %d, warp %d) -> %s" % (
        len(names), len(rows), len(marks), *(sum(m[1] == k for m in marks) for k in range(3)), out))


if __name__ == "__main__":
    main()
