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
    marks = boss_marks(assets)
    fmap_path = os.path.join(assets, "..", "disks", "us", "F_MAP.BIN")
    if os.path.exists(fmap_path):
        marks += map_marks(open(fmap_path, "rb").read())
    # check: each yellow map cell is a room of WRP (normal) or RWRP (turned)
    warp = {(m[0], m[2], m[3]) for m in marks if m[1] == MARK_WARP}
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
        f.write("const int g_CastleMarkCount = %d;\n" % len(marks))
    os.replace(out + ".tmp", out)
    print("castle: %d stages, %d rooms, %d marks (boss %d, save %d, warp %d) -> %s" % (
        len(names), len(rows), len(marks), *(sum(m[1] == k for m in marks) for k in range(3)), out))


if __name__ == "__main__":
    main()
