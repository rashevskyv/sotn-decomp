// SPDX-License-Identifier: AGPL-3.0-or-later
// The inverted Cursed Prison (Saturn RSTAGE16) on the PC: the PS1 stage
// framework (create_entity, collision, st_update) around its enemies. The
// Gargoyle is the Garden's SH-2 code (e_mod_gargoyle.h); the Flying Zombie,
// Jack O'Bones and Nova Skeleton are PS1 enemies and run the PS1 code (RNO2,
// RNO4) with the Saturn sprites (gdn_entities.py RSTAGE16); the Wight and
// the Wraith are Saturn-only (e_mod_wight.c, e_mod_wraith.c). The rooms,
// tiles, renderer, doors and music stay in src/pc/stages/stage_saturn.c.
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
    // Saturn 0x20-0x22 (the Garden's e_mod_gargoyle.h)
    E_MOD_GARGOYLE,
    E_GARGOYLE_DEBRIS,
    E_GARGOYLE_PEDESTAL,
    // Saturn 0x1C-0x1F (the PS1's RNO2 code)
    E_FLYING_ZOMBIE,
    E_FLYING_ZOMBIE_TORSO,
    E_BLOOD_DRIPS,
    E_BLOOD_SPLATTER,
    // Saturn 0x23-0x25 (RNO4)
    E_JACK_O_BONES,
    E_JACKO_DEATH_PARTS,
    E_JACKO_JACK,
    // Saturn 0x26-0x2A (RNO4)
    E_NOVA_SKELETON,
    E_NOVA_LASER,
    E_NOVA_PULSE,
    E_NOVA_DEATH_PARTS,
    // Saturn 0x15-0x17 (e_mod_wight.c)
    E_MOD_WIGHT,
    E_MOD_WIGHT_ATTACK,
    E_MOD_WIGHT_PART,
    // Saturn 0x18-0x1B (e_mod_wraith.c)
    E_MOD_WRAITH,
    E_MOD_WRAITH_19,
    E_MOD_WRAITH_1A,
    E_MOD_WRAITH_1B,
    NUM_RPRISON_ENTITIES,
} EntityID;

// The Saturn-only sounds are not in the PS1 sound banks: PS1 stand-ins.
#define MOD_SFX_GARGOYLE_CLAW SFX_WEAPON_SWISH_C
#define MOD_SFX_GARGOYLE_DEBRIS SFX_WALL_DEBRIS_B

// the converted Saturn sprite banks (gdn_entities.py RSTAGE16)
#define MOD_CANDLE_ANIMSET ANIMSET_OVL(2)
#define MOD_GARGOYLE_ANIMSET ANIMSET_OVL(7)
#define MOD_FLYING_ZOMBIE_ANIMSET ANIMSET_OVL(8)
#define MOD_JACK_O_BONES_ANIMSET ANIMSET_OVL(9)
#define MOD_NOVA_SKELETON_ANIMSET ANIMSET_OVL(10)
#define MOD_WIGHT_ANIMSET ANIMSET_OVL(11)
#define MOD_WRAITH_ANIMSET ANIMSET_OVL(12)
// the palette blocks and primitive textures (generated in the build folder)
#include "gdn_defs.h"

// The Nova Skeleton's primitives (e_nova_skeleton.h) read the Saturn bank:
// the charge ring is frame 35 (palette A; frame 38 has palette B), the laser
// frame 37's three parts (the near end, the beam, the far end)
#define NOVA_RING_TPAGE MOD_NOVA_RING_TPAGE
#define NOVA_RING_CLUT MOD_NOVA_RING_CLUT
#define NOVA_RING_U MOD_NOVA_RING_U
#define NOVA_RING_V MOD_NOVA_RING_V
#define NOVA_LASER_CLUT_B MOD_NOVA_RING_B_CLUT
#define NOVA_LASER_PART(n)                                                     \
    {MOD_NOVA_LASER##n##_TPAGE, MOD_NOVA_LASER##n##_U,                         \
     MOD_NOVA_LASER##n##_U + MOD_NOVA_LASER##n##_W - 1, MOD_NOVA_LASER##n##_V, \
     MOD_NOVA_LASER##n##_V + MOD_NOVA_LASER##n##_H - 1}
#define NOVA_LASER_PARTS                                                       \
    {NOVA_LASER_PART(0), NOVA_LASER_PART(1), NOVA_LASER_PART(2)}

void PlaySfxPositional(s16 sfxId);

extern EInit g_EInitLockCamera;
extern EInit g_EInitObtainable;
extern EInit g_EInitParticle;
extern EInit g_EInitInteractable;
extern EInit g_EInitCommon;
extern EInit g_EInitFlyingZombie;
extern EInit g_EInitFlyingZombieTorso;
extern EInit g_EInitBloodyZombie;
extern EInit g_EInitJackOBones;
extern EInit g_EInitJackOBones2;
extern EInit g_EInitJackOBones3;
extern EInit g_EInitNovaSkeleton;
extern EInit g_EInitNovaSkeleton2;
