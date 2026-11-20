// SPDX-License-Identifier: AGPL-3.0-or-later
#include "stage16.h"

// The Prison's breakables (Saturn entity 0x01, type = params >> 12) use bank
// 16 (ANIMSET_OVL(2) frames 1-12, gdn_entities.py STAGE_16): type 0 frames
// 6-9, type 1 frames 1-4, type 2 the static frame 12.
static u8 anim_0[] = {5, 6, 5, 7, 5, 8, 5, 9, 0, 0};
static u8 anim_1[] = {5, 1, 5, 2, 5, 3, 5, 4, 0, 0};
static u8 anim_2[] = {0x40, 12, 0, 0};
static u8* g_eBreakableAnimations[8] = {anim_0, anim_1, anim_2};
static u8 g_eBreakableHitboxes[8] = {8, 8, 8};
static u8 g_eBreakableExplosionTypes[8] = {0, 0, 0};
static u16 g_eBreakableanimSets[8] = {
    MOD_CANDLE_ANIMSET, MOD_CANDLE_ANIMSET, MOD_CANDLE_ANIMSET};
static u8 blend_modes[8] = {BLEND_NO, BLEND_NO, BLEND_NO};

// stripped on PSP
static u8 unused[] = {0, 0, 0, 0, 0, 0, 0, 0};

#include "../e_breakable.h"
