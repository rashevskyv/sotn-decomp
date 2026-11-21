// SPDX-License-Identifier: AGPL-3.0-or-later
// Reverse Garden (Saturn RSTAGE15) on the PC: the PS1 stage framework
// (create_entity, collision, st_update) around its enemies. The Cave Troll
// and the Blue Venus Weed run the PS1's RNO4 code, the Bitterfly RNZ0's and
// the Cloaked Knight RNZ1's, with the Saturn sprites (gdn_entities.py
// RSTAGE15, rstage15_entities.py). Copper Armor, Efreet and the Skeleton
// Guardian are Saturn-only: SH-2 ports (e_mod_*.c). The rooms, tiles,
// renderer, doors and music stay in src/pc/stages/stage_saturn.c.
#include <stage.h>

enum Palettes {
    PAL_NONE = 0,
};

typedef enum EntityID {
    E_NONE,
    E_BREAKABLE,
    E_EXPLOSION,
    E_PRIZE_DROP,
    E_NUMERIC_DAMAGE,
    E_RED_DOOR,
    E_INTENSE_EXPLOSION,
    E_SOUL_STEAL_ORB,
    E_ROOM_FOREGROUND,
    E_STAGE_NAME_POPUP,
    E_EQUIP_ITEM_DROP,
    E_RELIC_ORB,
    E_PERSISTENT_ITEM_DROP,
    E_ENEMY_BLOOD,
    E_MESSAGE_BOX,
    E_DUMMY_0F,
    E_DUMMY_10,
    E_ID_11,
    E_ID_12,
    E_ID_13,
    E_EXPLOSION_VARIANTS,
    E_GREY_PUFF,
    // the PC stage module's own (stage_saturn.c)
    E_SATURN_MASONRY,
    E_SATURN_CONTROLLER,
    NUM_RGDN_ENTITIES,
} EntityID;

// the converted Saturn sprite banks (rstage15_entities.py)
#define MOD_CANDLE_ANIMSET ANIMSET_OVL(2)
#define MOD_TROLL_ANIMSET ANIMSET_OVL(4)
#define MOD_COPPER_ANIMSET ANIMSET_OVL(5)
#define MOD_EFREET_ANIMSET ANIMSET_OVL(6)
#define MOD_VENUS_ANIMSET ANIMSET_OVL(7)
#define MOD_BITTERFLY_ANIMSET ANIMSET_OVL(8)
#define MOD_KNIGHT_ANIMSET ANIMSET_OVL(9)
#define MOD_GUARDIAN_ANIMSET ANIMSET_OVL(10)
// the palette blocks (generated in the build folder by gdn_entities.py)
#include "gdn_defs.h"

void PlaySfxPositional(s16 sfxId);

extern EInit g_EInitLockCamera;
extern EInit g_EInitObtainable;
extern EInit g_EInitParticle;
extern EInit g_EInitInteractable;
extern EInit g_EInitCommon;
