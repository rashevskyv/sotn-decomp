// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rstage15.h"

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
void EntityModBreakableBase(Entity*);

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
    EntityModBreakableBase, // Saturn 0x12
    EntityUnkId13,
    EntityExplosionVariants,
    EntityGreyPuff,
    EntitySaturnStageMasonry,
    EntitySaturnStageSpecialController,
};
_Static_assert(LEN(EntityUpdates) == NUM_RGDN_ENTITIES - 1, "EntityUpdates");

// clang-format off
EInit g_EInitBreakable = {ANIMSET_OVL(1), 0x00, 0x00, PAL_NONE, ENEMY_BREAKABLE};
#include "../e_init_common.h"
// clang-format on
