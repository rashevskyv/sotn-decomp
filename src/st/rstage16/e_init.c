// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rstage16.h"

void EntityBreakable(Entity*);
void EntityExplosion(Entity*);
void EntityPrizeDrop(Entity*);
void EntityDamageDisplay(Entity*);
void EntityRedDoor(Entity*);
void EntityIntenseExplosion(Entity*);
void EntitySoulStealOrb(Entity*);
void EntityEquipItemDrop(Entity*);
void EntityRelicOrb(Entity*);
void EntityPersistentItemDrop(Entity*);
void EntityEnemyBlood(Entity*);
void EntityMessageBox(Entity*);
void EntityDummy(Entity*);
void EntityBackgroundBlock(Entity*);
void EntityUnkId13(Entity*);
void EntityExplosionVariants(Entity*);
void EntityGreyPuff(Entity*);
void EntitySaturnStageMasonry(Entity*);
void EntitySaturnStageSpecialController(Entity*);
void EntityModGargoyle(Entity*);
void EntityModGargoyleDebris(Entity*);
void EntityModGargoylePedestal(Entity*);
void EntityModBreakableBase(Entity*);
void EntityFlyingZombie(Entity*);
void EntityFlyingZombieTorso(Entity*);
void EntityBloodDrips(Entity*);
void EntityBloodSplatter(Entity*);
void EntityJackOBones(Entity*);
void EntityJackOBonesDeathParts(Entity*);
void EntityJackOBonesJack(Entity*);
void EntityNovaSkeleton(Entity*);
void EntityNovaLaser(Entity*);
void EntityNovaLaserPulse(Entity*);
void EntityNovaSkeletonDeathParts(Entity*);
void EntityModWight(Entity*);
void EntityModWightAttack(Entity*);
void EntityModWightPart(Entity*);
void EntityModWraith(Entity*);
void EntityModWraith19(Entity*);
void EntityModWraith1A(Entity*);
void EntityModWraith1B(Entity*);

// index = EntityID - 1
PfnEntityUpdate EntityUpdates[] = {
    EntityBreakable,
    EntityExplosion,
    EntityPrizeDrop,
    EntityDamageDisplay,
    EntityRedDoor,
    EntityIntenseExplosion,
    EntitySoulStealOrb,
    EntityDummy, // E_ROOM_FOREGROUND: the native renderer draws it
    EntityDummy, // E_STAGE_NAME_POPUP: no layout places a banner
    EntityEquipItemDrop,
    EntityRelicOrb,
    EntityPersistentItemDrop,
    EntityEnemyBlood,
    EntityMessageBox,
    EntityDummy,
    EntityDummy,
    EntityBackgroundBlock,
    EntityModBreakableBase, // the broken sconce or brazier base
    EntityUnkId13,
    EntityExplosionVariants,
    EntityGreyPuff,
    EntitySaturnStageMasonry,
    EntitySaturnStageSpecialController,
    EntityModGargoyle,
    EntityModGargoyleDebris,
    EntityModGargoylePedestal,
    EntityFlyingZombie,
    EntityFlyingZombieTorso,
    EntityBloodDrips,
    EntityBloodSplatter,
    EntityJackOBones,
    EntityJackOBonesDeathParts,
    EntityJackOBonesJack,
    EntityNovaSkeleton,
    EntityNovaLaser,
    EntityNovaLaserPulse,
    EntityNovaSkeletonDeathParts,
    EntityModWight,
    EntityModWightAttack,
    EntityModWightPart,
    EntityModWraith,
    EntityModWraith19,
    EntityModWraith1A,
    EntityModWraith1B,
};
_Static_assert(LEN(EntityUpdates) == NUM_RPRISON_ENTITIES - 1, "EntityUpdates");

// clang-format off
EInit g_EInitBreakable = {ANIMSET_OVL(1), 0x00, 0x00, PAL_NONE, ENEMY_BREAKABLE};
// The PS1 inits (rno2/e_init.c, rno4/e_init.c) with the converted Saturn
// banks: their parts carry their own cluts and tpages, so palette and unk5A
// are 0. The Saturn records 14, 15, 116, 117, 126 and 127 are the PS1's.
EInit g_EInitFlyingZombie = {MOD_FLYING_ZOMBIE_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_FLYING_ZOMBIE_2};
EInit g_EInitFlyingZombieTorso = {MOD_FLYING_ZOMBIE_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_FLYING_ZOMBIE_1};
// e_bloody_zombie.h's enemy: no layout places it (its blood entities only)
EInit g_EInitBloodyZombie = {MOD_FLYING_ZOMBIE_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_BLOODY_ZOMBIE};
EInit g_EInitJackOBones = {MOD_JACK_O_BONES_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_JACK_O_BONES};
EInit g_EInitJackOBones2 = {MOD_JACK_O_BONES_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_PARTICLE};
EInit g_EInitJackOBones3 = {MOD_JACK_O_BONES_ANIMSET, 0x15, 0x00, PAL_NONE, ENEMY_JACK_O_BONES_3};
EInit g_EInitNovaSkeleton = {MOD_NOVA_SKELETON_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_NOVA_SKELETON};
EInit g_EInitNovaSkeleton2 = {MOD_NOVA_SKELETON_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_NOVA_SKELETON_LASER};
#include "../e_init_common.h"
// clang-format on
