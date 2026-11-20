// SPDX-License-Identifier: AGPL-3.0-or-later
// Cursed Prison (Saturn STAGE_16) on the PC: the PS1 stage framework
// (create_entity, collision, st_update) around its enemies. The Red Gargoyle
// is the Garden's (the same SH-2 code, e_mod_gargoyle.h); the Spear Guard,
// Bone Archer and Slinger are PS1 enemies and run the PS1 code (NO4, NO0)
// with the Saturn sprites (gdn_entities.py STAGE_16). The rooms, tiles,
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
    // Saturn 0x21-0x23
    E_MOD_GARGOYLE,
    E_GARGOYLE_DEBRIS,
    E_GARGOYLE_PEDESTAL,
    // Saturn 0x1A (the PS1's NO4 code)
    E_SPEAR_GUARD,
    E_SPEAR_GUARD_BLOCK,
    E_THROWN_SPEAR,
    // Saturn 0x18-0x19 (NO4)
    E_BONE_ARCHER,
    E_BONE_ARCHER_ARROW,
    // Saturn 0x1D-0x1E (NO0)
    E_SLINGER,
    E_SLINGER_THROWN_BONE,
    E_SLINGER_PIECES,
    // Saturn 0x20, 0x28-0x2C (e_mod_ghosts.c)
    E_MOD_SPECTER,
    E_MOD_WISP_SPAWNER,
    E_MOD_WISP,
    E_MOD_WISP_FLAME,
    E_MOD_WISP_TRAIL,
    E_MOD_WISP_BURST,
    // Saturn 0x16, 0x17, 0x15 (e_mod_pillar.c)
    E_MOD_PILLAR_SWITCH,
    E_MOD_PILLAR,
    E_MOD_PILLAR_DEBRIS,
    NUM_PRISON_ENTITIES,
} EntityID;

// The Saturn-only sounds are not in the PS1 sound banks: PS1 stand-ins.
#define MOD_SFX_GARGOYLE_CLAW SFX_WEAPON_SWISH_C
#define MOD_SFX_GARGOYLE_DEBRIS SFX_WALL_DEBRIS_B

// the converted Saturn sprite banks (gdn_entities.py STAGE_16)
#define MOD_CANDLE_ANIMSET ANIMSET_OVL(2)
#define MOD_GARGOYLE_ANIMSET ANIMSET_OVL(7)
#define MOD_SPEAR_GUARD_ANIMSET ANIMSET_OVL(8)
#define MOD_BONE_ARCHER_ANIMSET ANIMSET_OVL(9)
#define MOD_SLINGER_ANIMSET ANIMSET_OVL(10)
#define MOD_SPECTER_ANIMSET ANIMSET_OVL(11)
#define MOD_WISP_ANIMSET ANIMSET_OVL(12)
#define MOD_PILLAR_ANIMSET ANIMSET_OVL(13)
// the palette blocks (generated in the build folder by gdn_entities.py)
#include "gdn_defs.h"

void PlaySfxPositional(s16 sfxId);

extern EInit g_EInitLockCamera;
extern EInit g_EInitObtainable;
extern EInit g_EInitParticle;
extern EInit g_EInitInteractable;
extern EInit g_EInitCommon;
extern EInit g_EInitSpearGuard;
extern EInit g_EInitThrownSpear;
extern EInit g_EInitBoneArcher;
extern EInit g_EInitBoneArcherArrow;
extern EInit g_EInitSlinger;
extern EInit g_EInitSlingerPieces;
extern EInit g_EInitSlingerRib;
