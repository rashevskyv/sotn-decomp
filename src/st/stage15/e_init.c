// SPDX-License-Identifier: AGPL-3.0-or-later
#include "stage15.h"

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
void EntityModDripSpawner(Entity*);
void EntityModDrip(Entity*);
void EntityModGargoyle(Entity*);
void EntityModGargoyleDebris(Entity*);
void EntityModGargoylePedestal(Entity*);
void EntityModSkBreeder(Entity*);
void EntityModSkBreederWeapon(Entity*);
void EntityModSkBreederBeast(Entity*);
void EntityModSkBreederDebris(Entity*);
void EntityModLeaper(Entity*);
void EntityModLeaperSpawner(Entity*);
void EntityModGardener(Entity*);
void EntityModGardenerShears(Entity*);
void EntityModGardenerHitbox(Entity*);
void EntityModTree(Entity*);
void EntityModTreeFace(Entity*);
void EntityModTreeDrop(Entity*);
void EntityModTreeBlood(Entity*);
void EntityModTreeLeaf(Entity*);
void EntityModTreeBase(Entity*);
void EntityModManEater(Entity*);
void EntityModManEaterDebris(Entity*);
void EntityModBossController(Entity*);
void EntityModBossDoor(Entity*);
void EntityModBoss(Entity*);
void EntityModBossSword(Entity*);
void EntityModBossBone(Entity*);
void EntityModBossBomb(Entity*);
void EntityModBossGhost(Entity*);
void EntityModBossDebris(Entity*);
void EntityModBossBeam(Entity*);
void EntityModBossWave(Entity*);
void EntityExplosionPuffOpaque(Entity*);
void EntityLifeUpSpawn(Entity*);
void EntityModBanner(Entity*);

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
    EntityDummy,
    EntityUnkId13,
    EntityExplosionVariants,
    EntityGreyPuff,
    EntitySaturnStageMasonry,
    EntitySaturnStageSpecialController,
    EntityModDripSpawner,
    EntityModDrip,
    EntityModGargoyle,
    EntityModGargoyleDebris,
    EntityModGargoylePedestal,
    EntityModSkBreeder,
    EntityModSkBreederWeapon,
    EntityModSkBreederBeast,
    EntityModSkBreederDebris,
    EntityModLeaper,
    EntityModLeaperSpawner,
    EntityModGardener,
    EntityModGardenerShears,
    EntityModGardenerHitbox,
    EntityModTree,
    EntityModTreeFace,
    EntityModTreeDrop,
    EntityModTreeBlood,
    EntityModTreeLeaf,
    EntityModTreeBase,
    EntityModManEater,
    EntityModManEaterDebris,
    EntityModBossController,
    EntityModBossDoor,
    EntityModBoss,
    EntityModBossSword,
    EntityModBossBone,
    EntityModBossBomb,
    EntityModBossGhost,
    EntityModBossDebris,
    EntityModBossBeam,
    EntityModBossWave,
    EntityExplosionPuffOpaque,
    EntityLifeUpSpawn,
};
_Static_assert(LEN(EntityUpdates) == NUM_GDN_ENTITIES - 1, "EntityUpdates");

// clang-format off
EInit g_EInitBreakable = {ANIMSET_OVL(1), 0x00, 0x00, PAL_NONE, ENEMY_BREAKABLE};
#include "../e_init_common.h"
// clang-format on
