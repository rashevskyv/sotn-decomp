// SPDX-License-Identifier: AGPL-3.0-or-later
#include "stage15.h"

// The Garden's candles are the Saturn's ram-head wall sconces (bank 16 frames
// 0x26-0x29 at 5 ticks: ANIMSET_OVL(2) frames 1-4, gdn_entities.py). Type 1
// is the Saturn's item candle, drawn and dropped the same here.
static u8 anim_1[] = {5, 1, 5, 2, 5, 3, 5, 4, 0, 0};
static u8* g_eBreakableAnimations[8] = {anim_1, anim_1};
static u8 g_eBreakableHitboxes[8] = {8, 8};
static u8 g_eBreakableExplosionTypes[8] = {0, 0};
static u16 g_eBreakableanimSets[8] = {MOD_CANDLE_ANIMSET, MOD_CANDLE_ANIMSET};
static u8 blend_modes[8] = {BLEND_NO, BLEND_NO};

// stripped on PSP
static u8 unused[] = {0, 0, 0, 0, 0, 0, 0, 0};

#include "../e_breakable.h"
