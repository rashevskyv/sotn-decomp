// SPDX-License-Identifier: AGPL-3.0-or-later
#include "stage16.h"

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
void EntitySpearGuard(Entity*);
void EntitySpearGuardBlock(Entity*);
void EntityThrownSpear(Entity*);
void EntityBoneArcher(Entity*);
void EntityBoneArcherArrow(Entity*);
void EntitySlinger(Entity*);
void EntitySlingerRib(Entity*);
void EntitySlingerPieces(Entity*);
void EntityModSpecter(Entity*);
void EntityModWispSpawner(Entity*);
void EntityModWisp(Entity*);
void EntityModWispFlame(Entity*);
void EntityModWispTrail(Entity*);
void EntityModWispBurst(Entity*);
void EntityModBreakableBase(Entity*);
void EntityModPillarSwitch(Entity*);
void EntityModPillar(Entity*);
void EntityModPillarDebris(Entity*);
void EntityModBanner(Entity*);
void EntityModSkBreeder(Entity*);
void EntityModSkBreederWeapon(Entity*);
void EntityModSkBreederBeast(Entity*);
void EntityModSkBreederDebris(Entity*);

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
    EntityModBanner, // E_STAGE_NAME_POPUP: Saturn 0x09 (e_mod_banner.c)
    EntityEquipItemDrop,
    EntityRelicOrb,
    EntityPersistentItemDrop,
    EntityEnemyBlood,
    EntityMessageBox,
    EntityDummy,
    EntityDummy,
    EntityBackgroundBlock,
    EntityModBreakableBase, // Saturn 0x12
    EntityUnkId13,
    EntityExplosionVariants,
    EntityGreyPuff,
    EntitySaturnStageMasonry,
    EntitySaturnStageSpecialController,
    EntityModGargoyle,
    EntityModGargoyleDebris,
    EntityModGargoylePedestal,
    EntitySpearGuard,
    EntitySpearGuardBlock,
    EntityThrownSpear,
    EntityBoneArcher,
    EntityBoneArcherArrow,
    EntitySlinger,
    EntitySlingerRib,
    EntitySlingerPieces,
    EntityModSpecter,
    EntityModWispSpawner,
    EntityModWisp,
    EntityModWispFlame,
    EntityModWispTrail,
    EntityModWispBurst,
    EntityModPillarSwitch,
    EntityModPillar,
    EntityModPillarDebris,
    EntityModSkBreeder,
    EntityModSkBreederWeapon,
    EntityModSkBreederBeast,
    EntityModSkBreederDebris,
};
_Static_assert(LEN(EntityUpdates) == NUM_PRISON_ENTITIES - 1, "EntityUpdates");

// clang-format off
EInit g_EInitBreakable = {ANIMSET_OVL(1), 0x00, 0x00, PAL_NONE, ENEMY_BREAKABLE};
// The PS1 inits (no4/e_init.c, no0/e_init.c) with the converted Saturn banks:
// their parts carry their own cluts and tpages, so palette and unk5A are 0
EInit g_EInitSpearGuard = {MOD_SPEAR_GUARD_ANIMSET, 0x05, 0x00, PAL_NONE, ENEMY_SPEAR_GUARD};
EInit g_EInitThrownSpear = {MOD_SPEAR_GUARD_ANIMSET, 0x05, 0x00, PAL_NONE, ENEMY_SPEAR_GUARD_THROWN_SPEAR};
EInit g_EInitBoneArcher = {MOD_BONE_ARCHER_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_BONE_ARCHER};
EInit g_EInitBoneArcherArrow = {MOD_BONE_ARCHER_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_BONE_ARCHER_ARROW};
EInit g_EInitSlinger = {MOD_SLINGER_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_SLINGER};
EInit g_EInitSlingerPieces = {MOD_SLINGER_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_PARTICLE};
EInit g_EInitSlingerRib = {MOD_SLINGER_ANIMSET, 0x16, 0x00, PAL_NONE, ENEMY_SLINGER_RIB};
#include "../e_init_common.h"
// clang-format on
