// SPDX-License-Identifier: AGPL-3.0-or-later
#include "stage15.h"

// the decorations (Saturn entity 0x11): BackgroundBlockInit is generated
#include "../e_room_bg.h"

// The Saturn enemies, translated from the SH-2 by sotn-ultimate
#include "e_mod_gargoyle.h"
#include "e_mod_skeleton_breeder.h"
#include "e_mod_leaper.h"
#include "e_mod_gardener.h"
#include "e_mod_tree.h"
#include "e_mod_maneater.h"
#include "e_mod_boss.h"

// the boss's death fire (params 3); the others are not used here
#define PAL_PUFF_OPAQUE_OFFSET 0
#define PUFF_PARAMS_0                                                          \
    {                                                                          \
        ANIMSET_DRA(0xE), 0x0079, 0x0000, DRAW_TPAGE | DRAW_TPAGE2,            \
            puff_anim_one                                                      \
    }
#define PUFF_PARAMS_1 PUFF_PARAMS_0
#define PUFF_PARAMS_2 PUFF_PARAMS_0
#define PUFF_PARAMS_3 PUFF_PARAMS_0
#include "../e_explosion_puff_opaque.h"
#include "../e_life_up.h"
