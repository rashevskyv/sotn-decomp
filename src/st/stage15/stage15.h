// SPDX-License-Identifier: AGPL-3.0-or-later
// Underground Garden (Saturn STAGE_15) on the PC: the PS1 stage framework
// (create_entity, collision, st_update) around the Saturn enemies that
// sotn-ultimate translated from the SH-2 (e_mod_*.h). The rooms, tiles,
// renderer, doors and music stay in src/pc/stages/stage_saturn.c.
#include <stage.h>

#define STAGE_IS_GDN

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
    // the poison drips (Saturn 0x1E/0x1F)
    E_MOD_DRIP_SPAWNER,
    E_MOD_DRIP,
    // Saturn 0x23-0x25
    E_MOD_GARGOYLE,
    E_GARGOYLE_DEBRIS,
    E_GARGOYLE_PEDESTAL,
    // Saturn 0x40-0x43
    E_MOD_SKBREEDER,
    E_MOD_SKBREEDER_WEAPON,
    E_MOD_SKBREEDER_BEAST,
    E_MOD_SKBREEDER_DEBRIS,
    // Saturn 0x34-0x35
    E_MOD_LEAPER,
    E_MOD_LEAPER_SPAWNER,
    // Saturn 0x3D-0x3F
    E_MOD_GARDENER,
    E_MOD_GARDENER_SHEARS,
    E_MOD_GARDENER_HITBOX,
    // Saturn 0x2E-0x33
    E_MOD_TREE,
    E_MOD_TREE_FACE,
    E_MOD_TREE_DROP,
    E_MOD_TREE_BLOOD,
    E_MOD_TREE_LEAF,
    E_MOD_TREE_BASE,
    // Saturn 0x3B-0x3C
    E_MOD_MANEATER,
    E_MOD_MANEATER_DEBRIS,
    // Saturn 0x44, 0x45, 0x26-0x2D, then the PS1 death puffs and Life Max Up
    E_MOD_BOSS_CONTROLLER,
    E_MOD_BOSS_DOOR,
    E_MOD_BOSS,
    E_MOD_BOSS_SWORD,
    E_MOD_BOSS_BONE,
    E_MOD_BOSS_BOMB,
    E_MOD_BOSS_GHOST,
    E_MOD_BOSS_DEBRIS,
    E_MOD_BOSS_BEAM,
    E_MOD_BOSS_WAVE,
    E_EXPLODE_PUFF_OPAQUE,
    E_LIFE_UP_SPAWN,
    // Saturn 0x20 (the PS1's NO0 code, e_slinger.c)
    E_SLINGER,
    E_SLINGER_THROWN_BONE,
    E_SLINGER_PIECES,
    // Saturn 0x36 (the PS1's CHI code, e_thornweed.c)
    E_THORNWEED,
    E_CORPSEWEED,
    E_CORPSEWEED_PROJECTILE,
    // Saturn 0x17-0x1C (the PS1's water_effects.h) and 0x15 (e_water.c)
    E_ALUCARD_WATER_EFFECT,
    E_SPLASH_WATER,
    E_SURFACING_WATER,
    E_SIDE_WATER_SPLASH,
    E_SMALL_WATER_DROP,
    E_WATER_DROP,
    E_MOD_WATER_TINT,
    NUM_GDN_ENTITIES,
} EntityID;
// the name e_mod_boss.h uses
#define E_EXPLOSION_PUFF_OPAQUE E_EXPLODE_PUFF_OPAQUE

// the enemies are drawn above room 0's ground flowers (sotn-ultimate gdn.h)
#define MOD_GDN_ENEMY_FRONT_Z 0xA8

// The Saturn-only sounds are not in the PS1 sound banks: the stage plays the
// Saturn ids. src/pc/saturn_sfx.c plays their WAVs (data/ST/STAGE15/sfx/,
// tools/saturn/saturn_audio.py) or, with no WAV, a PS1 stand-in.
#define MOD_SFX_GARGOYLE_CLAW 0x8FE
#define MOD_SFX_GARGOYLE_DEBRIS 0x8FF
#define MOD_SFX_BOSS_BOMB 0x8F7
#define MOD_SFX_BOSS_DEATH 0x8F8
#define MOD_SFX_GARDENER_SHEARS 0x90F
#define MOD_SFX_GARDENER_DEATH 0x90E
#define MOD_SFX_WATER_SURFACE SFX_WATER_SPLASH_MOVE // Saturn 0x778
#define MOD_SFX_WATER_SPLASH SFX_WATER_SPLASH_JUMP  // Saturn 0x777
#define MOD_SFX_LEAPER_LEAP 0x912
#define MOD_SFX_LEAPER_FALL 0x913
#define MOD_SFX_LEAPER_DEATH 0x911
#define MOD_SFX_MANEATER_SNAP 0x90D
#define MOD_SFX_MANEATER_OPEN 0x90C
#define MOD_SFX_MANEATER_BURST 0x90B
#define MOD_SFX_TREE_HURT 0x8FC
#define MOD_SFX_TREE_FACE_FALL 0x8FA
#define MOD_SFX_TREE_FACE_BURST 0x8FB
#define MOD_SFX_TREE_DEATH 0x8FD

// the Garden track is the stage music the PC module sets (g_StageMusicId,
// stage_saturn.c). The Skeleton Leader's own track (Saturn SD2B) plays from
// data/ST/STAGE15/music/535.wav (src/pc/music.c, saturn_sfx.c); with no file
// the fight keeps the Garden track.
#define MOD_GDN_STAGE_MUSIC g_StageMusicId
#define MOD_BOSS_MUSIC SATURN_BOSS_MUSIC
// a fg tile with solid collision, found at module start (stage_saturn.c)
extern u16 g_GdnSolidTile;
#define MOD_BOSS_DOOR_TILE g_GdnSolidTile

// the converted Saturn sprite banks (gdn_entities.py, stage_saturn.c)
#define MOD_CANDLE_ANIMSET ANIMSET_OVL(2)
#define MOD_DRIP_ANIMSET ANIMSET_OVL(4)
#define MOD_GARGOYLE_ANIMSET ANIMSET_OVL(7)
#define MOD_SKBREEDER_ANIMSET ANIMSET_OVL(8)
#define MOD_LEAPER_ANIMSET ANIMSET_OVL(9)
#define MOD_GARDENER_ANIMSET ANIMSET_OVL(10)
#define MOD_TREE_ANIMSET ANIMSET_OVL(11)
#define MOD_MANEATER_ANIMSET ANIMSET_OVL(12)
#define MOD_BOSS_ANIMSET ANIMSET_OVL(13)
#define MOD_BOSS_DOOR_ANIMSET ANIMSET_OVL(14)
#define MOD_BOSS_DOOR_FRAME 1
#define MOD_SLINGER_ANIMSET ANIMSET_OVL(15)
#define MOD_THORNWEED_ANIMSET ANIMSET_OVL(16)
// the palette blocks (generated in the build folder by gdn_entities.py)
#include "gdn_defs.h"
#define MOD_TREE_PAL_ABS MOD_TREE_PAL

// e_mod_boss.h defines it; the other enemies call it first
void PlaySfxPositional(s16 sfxId);

extern EInit g_EInitLockCamera;
extern EInit g_EInitObtainable;
extern EInit g_EInitParticle;
extern EInit g_EInitInteractable;
extern EInit g_EInitCommon;
extern EInit g_EInitSlinger;
extern EInit g_EInitSlingerPieces;
extern EInit g_EInitSlingerRib;
