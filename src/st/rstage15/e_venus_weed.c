// SPDX-License-Identifier: AGPL-3.0-or-later
// Saturn 0x2C-0x32: the PS1's Blue Venus Weed (RNO4) with the converted
// Saturn sprites (the Saturn keeps the PS1 frame numbers). The Saturn bank
// has 11 palettes, the PS1 41: 0 the flower and the darts, 1 the plant, 2-10
// the tendril segments, from green to pale. The colour steps use them:
// - the death fade: the plant's palette + 1 to + 6 (DEATH_CLUT, relative to
//   the parts' own cluts);
// - the spike glow: palettes 2-10;
// - the flower and the darts keep palette 0 (DEATH_CLUT_END, DART_CLUT_MAX).
// The leaves and the stem are frames 59 and 2 (gdn_defs.h, PRIM_PARTS).
#include "rstage15.h"

#define BLUE
#define SPIKE_SPRITE(n) (((s16**)g_api.o.spriteBanks[7])[n])
#define TENDRIL_COUNT 8
#define DART_CLUT_START 0
#define DART_CLUT_MAX 0
#define DEATH_CLUT 6
#define DEATH_CLUT_END 0
#define PLANT_CLUT MOD_VENUS_LEAVES_CLUT
#define SPIKE_CLUT_START (MOD_VENUS_PAL + 2)
#define SPIKE_CLUT_END (MOD_VENUS_PAL + 10)
#define GROWTH_SPEED 2

#define VENUS_LEAVES_TPAGE MOD_VENUS_LEAVES_TPAGE
#define VENUS_LEAVES_U MOD_VENUS_LEAVES_U
#define VENUS_LEAVES_V MOD_VENUS_LEAVES_V
#define VENUS_LEAVES_W MOD_VENUS_LEAVES_W
#define VENUS_LEAVES_H MOD_VENUS_LEAVES_H
#define VENUS_STEM_TPAGE MOD_VENUS_STEM_TPAGE
#define VENUS_STEM_U MOD_VENUS_STEM_U
#define VENUS_STEM_V MOD_VENUS_STEM_V
#define VENUS_STEM_W MOD_VENUS_STEM_W
#define VENUS_STEM_H MOD_VENUS_STEM_H
// a converted part's tileset is its tpage * 4 + its quadrant in the tpage
#define VENUS_PART_FIXUP(prim, d)                                              \
    do {                                                                       \
        u8 du_ = ((d)[6] & 1) * 128, dv_ = ((d)[6] & 2) * 64;                  \
        (prim)->tpage = (d)[6] >> 2;                                           \
        (prim)->u0 += du_, (prim)->u1 += du_;                                  \
        (prim)->u2 += du_, (prim)->u3 += du_;                                  \
        (prim)->v0 += dv_, (prim)->v1 += dv_;                                  \
        (prim)->v2 += dv_, (prim)->v3 += dv_;                                  \
    } while (0)

#include "../e_venus_weed.h"
