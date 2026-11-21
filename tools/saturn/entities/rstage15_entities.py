# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reverse Garden (Saturn RSTAGE15) tables for gdn_entities.py.

Bank and frame table addresses: the sNNres / sNNgfx segments of
config/saturn/rstage15.prg.yaml and the frame tables the SH-2 code reads
(TekiInit sites in 060E4668-060EC374). The PS1 enemies (Cave Troll, Blue
Venus Weed, Bitterfly, Cloaked Knight) keep the PS1 frame numbers: frame n
of the Saturn table is frame n of its ANIMSET_OVL bank.
"""

R_BANK16 = (0x060ECC88, 0x060ECBCC)
RGARDEN = dict(
    SPRITE_SETS=[
        # the sconces (Saturn 0x01, frames 0x26-0x29) and their broken base
        # (0x12, frame 0x2A): ANIMSET_OVL(2) frames 1-5
        (2, [("candles",) + R_BANK16 + (f,) for f in range(0x26, 0x2B)]),
        (3, "decorations"),
        # Saturn 0x1E-0x20 (RNO4 code)
        (4, [("troll", 0x060EEE94, 0x060EF1A8, f) for f in range(1, 35)]),
        # Saturn 0x21-0x22, Copper Armor (e_mod_copper.c)
        (5, [("copper", 0x060EF3F4, 0x060F0240, f) for f in range(1, 61)]),
        # Saturn 0x23-0x2B, Efreet (e_mod_efreet.c): the Saturn draws the
        # body (frames 0x0F-0x3E, 0x42-0x4F) at the entity - (0, 12)
        (6, [("efreet", 0x060F04E4, 0x060F1648, f,
              -12 if 0x0F <= f <= 0x3E or 0x42 <= f <= 0x4F else 0)
             for f in range(1, 82)]),
        # Saturn 0x2C-0x32 (RNO4 code)
        (7, [("venus", 0x060F1A2C, 0x060F2670, f) for f in range(1, 60)]),
        # Saturn 0x33 (RNZ0 code)
        (8, [("bitterfly", 0x060F2894, 0x060F2B4C, f) for f in range(1, 34)]),
        # Saturn 0x34-0x37 (RNZ1 code)
        (9, [("knight", 0x060F2C38, 0x060F2D68, f) for f in range(1, 9)]),
        # Saturn 0x38-0x3B, Skeleton Guardian (e_mod_guardian.c): the
        # Saturn draws the body (frames 1-18) at the entity + (0, 32)
        (10, [("guardian", 0x060F2E08, 0x060F33B4, f, 32 if f <= 18 else 0)
              for f in range(1, 27)]),
        # Saturn 0x27, the Efreet's puffs: the common bank 0x06046BF8 with the
        # frame table 0x060467E0; the Saturn adds 0x10 to the clut (palette 1)
        (11, [("efreet", 0x06046BF8, 0x060467E0, f, 0, 1) for f in range(1, 14)]),
    ],
    # Saturn 0x11 (func_060DC434): 6 records of 28 bytes
    DECORATIONS=(0x060EE478, 6),
    DECORATION_GROUPS=["candles", "candles", "scene", "scene", "scene", "scene"],
    # the Cloaked Knight's aura draws in the palette after the knight's
    # (palette + 1, e_cloaked_knight.h)
    # the Blue Venus Weed's death and spike colours step through its block
    PALETTE_BLOCKS={"KNIGHT": ("knight", 0x060F2C38),
                    "VENUS": ("venus", 0x060F1A2C)},
    BLEED_BANKS=(),
    STP_BANKS=(),
    TRIM_BANKS={},
    GROUP_ROOMS={"candles": (0, 1, 3), "scene": (2,), "troll": (0,),
                 "copper": (0,), "efreet": (0,), "bitterfly": (0, 3),
                 "venus": (1, 3), "knight": (1,), "guardian": (3,),
                 "water": (0,)},
    ROOMS=4,
    # Saturn 0x16, room 0's water line (e_water.c): bank 0x060ECEF0's 12
    # images, 3 phases of 4 strips, by number as the banner's
    BANNER=(0x060ECEF0, range(12), "water", "WATER_LINE"),
    # the Blue Venus Weed's primitives: the leaves (frame 59, the right
    # half) and the stem (frame 2)
    PRIM_PARTS={"VENUS_LEAVES": (7, 59), "VENUS_STEM": (7, 2)},
)
