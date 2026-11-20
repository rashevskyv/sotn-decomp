#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Underground Garden (Saturn STAGE_15) and Cursed Prison (STAGE_16) enemy
data for the PC stage modules.

Reads the Saturn disc files and writes one C header into the build folder:
the converted sprite frames (ANIMSET_OVL banks), their 4bpp pages as one
graphics bank per room, the sprite palettes, the decorations' inits and the
Saturn-only enemy records 400-425. Nothing it writes is kept in git.

The frame tables, palettes and offsets follow sotn-ultimate's gdn_assets.py
(SPRITE_SETS, DECORATIONS, PALETTE_BLOCKS); the packing is its
saturn_sprites.py. The PC has the whole PS1 sprite area free (tpages 18-23:
the stage tiles are drawn by the native 8x8 renderer), so each room gets one
bank and no zone streaming is needed.

The Prison's Spear Guard, Bone Archer and Slinger run the PS1 code (NO4,
NO0): the Saturn frames keep the PS1 frame numbers, so frame n of a Saturn
bank becomes frame n of its ANIMSET_OVL bank.

usage: gdn_entities.py <saturn disc dir> <output header> [STAGE_15|STAGE_16]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import saturn_sprites  # noqa: E402
from saturn_sprites import PsxSpriteSheet, SaturnStage  # noqa: E402

SAT_BANK16 = (0x060F0858, 0x060F079C)
TREE_FRAMES = (set(range(1, 21)) | {23} | set(range(26, 34)) | {35, 47} |
               set(range(54, 67)) | set(range(68, 74)) | {87, 88})
# (ANIMSET_OVL n, frames): frame = (group, bank, frame table, index[, dy[, pal]])
SPRITE_SETS = [
    (2, [("candles",) + SAT_BANK16 + (f,) for f in (0x26, 0x27, 0x28, 0x29, 0x2A)]),
    (3, "decorations"),
    (4, [("drips",) + SAT_BANK16 + (f,)
         for f in (1, 2, 3, 0x13, 0x14, 0x15, 0x16, 0x17)]),
    (7, [("gargoyles", 0x060F2ED8, 0x060F4018, f) for f in range(1, 60)]),
    (8, [("skbreeder", 0x060FA7B8, 0x060FAF64, f,
          32 if 13 <= f <= 31 else 16 if f <= 12 else 0) for f in range(1, 38)]),
    (9, [("leaper", 0x060F82C8, 0x060F86CC, f) for f in range(1, 26)]),
    (10, [("gardener", 0x060F96D8, 0x060FA59C, f) for f in range(1, 46)]),
    (11, [("tree", 0x060F6DC4, 0x060F7EFC, f, 0, -3 if 54 <= f <= 66 else 0)
          if f in TREE_FRAMES else None for f in range(1, 89)]),
    (12, [("maneater", 0x060F8BFC, 0x060F9470, f) for f in range(1, 56)]),
    (13, [("boss", 0x060F43C4, 0x060F6A90, f) for f in range(1, 87)]),
    (14, [("boss",) + SAT_BANK16 + (46,)]),
]
DECORATIONS = (0x060F1F90, 6)
DECORATION_GROUPS = ["candles", "candles", "scene", "scene", "scene", "scene"]
PALETTE_BLOCKS = {"GARGOYLE": ("gargoyles", 0x060F2ED8),
                  "TREE": ("tree", 0x060F6DC4)}
BLEED_BANKS = (0x060F82C8, 0x060F6DC4)
# the ordinary rooms (s_SaturnStageRooms 0-3) each group is used in
GROUP_ROOMS = {
    "candles": (0, 1, 3), "scene": (2,), "drips": (1, 3), "gargoyles": (0,),
    "skbreeder": (0,), "leaper": (0,), "gardener": (1,), "tree": (0,),
    "maneater": (3,), "boss": (2,),
}
ROOMS = 4
GARDEN = dict(SPRITE_SETS=SPRITE_SETS, DECORATIONS=DECORATIONS,
              DECORATION_GROUPS=DECORATION_GROUPS, PALETTE_BLOCKS=PALETTE_BLOCKS,
              BLEED_BANKS=BLEED_BANKS, GROUP_ROOMS=GROUP_ROOMS, ROOMS=ROOMS)
# Cursed Prison: bank and frame table addresses from config/saturn
# stage_16_user_syms.txt and the gfx segments of stage_16.prg.yaml
P_BANK16 = (0x060E8F60, 0x060E8F08)
PRISON = dict(
    SPRITE_SETS=[
        # the breakables' frames keep their Saturn numbers (stdata.c)
        (2, [("candles",) + P_BANK16 + (f,) for f in range(1, 13)]),
        (3, "decorations"),
        (7, [("gargoyles", 0x060ECEF0, 0x060EE030, f) for f in range(1, 60)]),
        (8, [("spearguard", 0x060EB498, 0x060EC46C, f) for f in range(1, 65)]),
        (9, [("archer", 0x060EABC4, 0x060EB218, f) for f in range(1, 41)]),
        (10, [("slinger", 0x060EC6AC, 0x060EC9D8, f) for f in range(1, 23)]),
    ],
    DECORATIONS=(0x060EA1CC, 6),
    DECORATION_GROUPS=["candles"] * 6,
    PALETTE_BLOCKS={"GARGOYLE": ("gargoyles", 0x060ECEF0)},
    BLEED_BANKS=(),
    GROUP_ROOMS={"candles": (0, 1), "gargoyles": (1,), "spearguard": (0,),
                 "archer": (0,), "slinger": (0,)},
    ROOMS=2,
)
STAGES = {"STAGE_15": GARDEN, "STAGE_16": PRISON}
QUADRANTS = [(t, q) for t in range(18, 24) for q in range(4)]
CLUT_BASE = 0x210
GAME_BASE, ENEMY_DEFS, FIRST, COUNT = 0x06066000, 0x06080288, 400, 26


def quad_vram(tpage, quad):
    return ((tpage & 15) * 64 + (quad & 1) * 32,
            (tpage >> 4) * 256 + (quad >> 1) * 128)


def allocate(sizes):
    """Quadrant indices per group; groups that share a room never overlap."""
    slots = {}
    for g in sorted(sizes, key=lambda g: -sizes[g]):
        used = {i for h, s in slots.items()
                if set(GROUP_ROOMS[h]) & set(GROUP_ROOMS[g]) for i in s}
        free = [i for i in range(len(QUADRANTS)) if i not in used]
        if len(free) < sizes[g]:
            raise SystemExit("gdn: %s needs %d quadrants, %d free"
                             % (g, sizes[g], len(free)))
        slots[g] = free[:sizes[g]]
    return slots


def main(disc, out_path, stage="STAGE_15"):
    # the stage's tables replace the module's (allocate() reads GROUP_ROOMS)
    globals().update(STAGES[stage])
    st = SaturnStage(os.path.join(disc, stage + ".PRG"),
                     os.path.join(disc, stage + ".CHR"),
                     os.path.join(disc, "0.BIN"))
    decos = []
    for v in range(DECORATIONS[1]):
        o = DECORATIONS[0] - st.base + v * 28
        bank, frames, _pal, z, order = struct.unpack_from(">IIHHB", st.prg, o)
        anim = struct.unpack_from(">I", st.prg, o + 24)[0]
        decos.append(((DECORATION_GROUPS[v], bank, frames, st.prg[anim - st.base + 1]),
                      0x67 + order if z == 0x20 else z))
    sets = [(n, [d[0] for d in decos] if f == "decorations" else f)
            for n, f in SPRITE_SETS]
    saturn_sprites.BLEED_BANKS.update(BLEED_BANKS)

    def fill(sheets):
        for g, bank in PALETTE_BLOCKS.values():
            sheets[g].palette_block(st, bank)
        out = []
        for n, frames in sets:
            bank = []
            for e in frames:
                if e is None:
                    bank.append(None)
                    continue
                parts = sheets[e[0]].frame(st, *e[1:4], e[5] if len(e) > 5 else 0)
                dy = e[4] if len(e) > 4 else 0
                bank.append([dict(p, y=p["y"] + dy) for p in parts])
            out.append((n, bank))
        return out

    seen = {g: PsxSpriteSheet([(0, 0)] * 16, CLUT_BASE) for g in GROUP_ROOMS}
    fill(seen)
    sizes = {}
    for g in GROUP_ROOMS:
        probe = PsxSpriteSheet(QUADRANTS * 2, CLUT_BASE)
        probe.prepack(st, seen[g].requests)
        sizes[g] = probe.used_quadrants()
    while True:
        slots = allocate(sizes)
        cluts = []
        sheets = {g: PsxSpriteSheet([QUADRANTS[i] for i in slots[g]], CLUT_BASE, cluts)
                  for g in GROUP_ROOMS}
        short = None
        for g in GROUP_ROOMS:
            try:
                sheets[g].prepack(st, seen[g].requests)
            except AssertionError:
                short = g
                break
        if short is None:
            break
        sizes[short] += 1
    banks = fill(sheets)
    assert CLUT_BASE + len(cluts) <= 0x300, "too many sprite palettes"

    game = open(os.path.join(disc, "GAME.PRG"), "rb").read()
    L = []
    w = L.append
    w("// Generated by tools/saturn/entities/gdn_entities.py from the Saturn")
    w("// disc. Build output: do not commit.")
    w("// clang-format off")
    blocks = []
    for g in GROUP_ROOMS:
        for k in range(sheets[g].used_quadrants()):
            name = "s_GdnGfx_%s%d" % (g, k)
            data = sheets[g].quadrant_pixels(k)
            blocks.append((sheets[g].quadrants[k], name, g))
            w("static u8 %s[%d] __attribute__((aligned(4))) = {" % (name, len(data)))
            for i in range(0, len(data), 32):
                w("    " + ",".join("%d" % b for b in data[i:i + 32]) + ",")
            w("};")
    for r in range(ROOMS):
        w("static u_long* s_GdnGfxBankRoom%d[] = {" % r)
        w("    (u_long*)GFX_BANK_4BPP,")
        for (tpage, quad), name, g in blocks:
            if r in GROUP_ROOMS[g]:
                vx, vy = quad_vram(tpage, quad)
                w("    GFX_ENTRY(0x%03X, 0x%03X, 128, 128, %s)," % (vy, vx, name))
        w("    GFX_TERMINATE(),")
        w("};")
    w("#define GDN_ROOM_GFX_BANKS %s" % ", ".join(
        "(GfxBank*)s_GdnGfxBankRoom%d" % r for r in range(ROOMS)))
    w("#define GDN_ROOM_GFX_COUNT %d" % ROOMS)
    w("#define GDN_SPRITE_CLUT_BASE 0x%03X" % CLUT_BASE)
    w("#define GDN_SPRITE_CLUT_COUNT %d" % len(cluts))
    w("static u16 s_GdnSpritePals[] = {")
    for pal in cluts:
        w("    " + ", ".join("0x%04X" % c for c in pal) + ",")
    w("};")
    keys = ("flags", "x", "y", "width", "height", "clut", "tileset",
            "left", "top", "right", "bottom")
    table = []
    for n, frames in banks:
        for i, parts in enumerate(frames):
            if parts is None:
                continue
            w("static s16 s_GdnSpr%d_%d[] = {%d," % (n, i + 1, len(parts)))
            for p in parts:
                w("    " + ", ".join("%d" % p[k] for k in keys) + ",")
            w("};")
        w("static s16* s_GdnSprites%d[] = {NULL, %s};" % (n, ", ".join(
            "NULL" if p is None else "s_GdnSpr%d_%d" % (n, i + 1)
            for i, p in enumerate(frames))))
        table.append(n)
    w("#define GDN_SPRITE_BANKS %s" % " ".join(
        "X(%d, s_GdnSprites%d)" % (n, n) for n in table))
    w("// the decorations (Saturn entity 0x11): ANIMSET_OVL(3) frame n + 1")
    for v in range(len(decos)):
        w("static u8 g_GdnBgBlockAnim%d[] = {0x40, %d, 0xFF, 0x00};" % (v, v + 1))
    w("ObjInit2 BackgroundBlockInit[] = {")
    for v, (_, z) in enumerate(decos):
        w("    {.animSet = ANIMSET_OVL(3), .zPriority = 0x%02X, .unk5A = 0, "
          ".palette = 0, .drawFlags = ENTITY_DEFAULT, .blendMode = BLEND_NO, "
          ".flags = 0, .animFrames = g_GdnBgBlockAnim%d}," % (z, v))
    w("};")
    w("#define GDN_ENEMY_FIRST %d" % FIRST)
    w("static EnemyDef s_GdnEnemyDefs[%d] = {" % COUNT)
    for i in range(FIRST, FIRST + COUNT):
        r = struct.unpack_from(">IhhHhHHHHHHHHHHHBBI", game,
                               ENEMY_DEFS - GAME_BASE + i * 0x28)
        (_name, hp, atk, elem, dfn, hbs, weak, strong, imm, absorb, level, exp,
         _rare, _unc, _rr, _ur, bw, bh, flags) = r
        # the Saturn's drops use its own item numbers: none on the PC yet
        # _S(): sotn_str ends the name with the 0xFF 0x00 the popup reads
        w("    {_S(\"\"), %d, %d, 0x%X, %d, %d, 0x%X, 0x%X, 0x%X, 0x%X, %d, %d, "
          "0, 0, 0, 0, %d, %d, 0x%X}," % (hp, atk, elem, dfn, hbs, weak, strong,
                                          imm, absorb, level, exp, bw, bh, flags))
    w("};")
    w("// clang-format on")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w") as f:
        f.write("\n".join(L) + "\n")
    defs = ["// Generated by tools/saturn/entities/gdn_entities.py, do not commit"]
    for name, (g, bank) in PALETTE_BLOCKS.items():
        defs.append("#define MOD_%s_PAL 0x%03X" % (name, sheets[g].blocks[bank]))
    with open(os.path.join(os.path.dirname(os.path.abspath(out_path)), "gdn_defs.h"), "w") as f:
        f.write("\n".join(defs) + "\n")
    for r in range(ROOMS):
        print("gdn: room %d: %d of %d quadrants" % (
            r, sum(1 for _, _, g in blocks if r in GROUP_ROOMS[g]), len(QUADRANTS)))
    print("gdn: %d sprite palettes, %d enemy records" % (len(cluts), COUNT))


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4) or sys.argv[3:] and sys.argv[3] not in STAGES:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
