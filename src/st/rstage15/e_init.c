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
void func_us_801D5E90(Entity*);
void func_us_801D68E0(Entity*);
void func_us_801D6B8C(Entity*);
void EntityBitterfly(Entity*);
void EntityCloakedKnight(Entity*);
void EntityCloakedKnightCloak(Entity*);
void EntityCloakedKnightSword(Entity*);
void EntityCloakedKnightAura(Entity*);
void EntityVenusWeed(Entity*);
void EntityVenusWeedFlower(Entity*);
void EntityVenusWeedTendril(Entity*);
void EntityVenusWeedDart(Entity*);
void EntityVenusWeedSpike(Entity*);
void EntityModCopper(Entity*);
void EntityModCopper22(Entity*);
void EntityModEfreet(Entity*);
void EntityModEfreet24(Entity*);
void EntityModEfreet25(Entity*);
void EntityModEfreet26(Entity*);
void EntityModEfreet27(Entity*);
void EntityModEfreet28(Entity*);
void EntityModEfreet29(Entity*);
void EntityModEfreet2A(Entity*);
void EntityModEfreet2B(Entity*);
void EntityModGuardian(Entity*);
void EntityModGuardian39(Entity*);
void EntityModGuardian3A(Entity*);
void EntityModGuardian3B(Entity*);

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
    func_us_801D5E90,
    func_us_801D68E0,
    func_us_801D6B8C,
    EntityBitterfly,
    EntityCloakedKnight,
    EntityCloakedKnightCloak,
    EntityCloakedKnightSword,
    EntityCloakedKnightAura,
    EntityVenusWeed,
    EntityVenusWeedFlower,
    EntityVenusWeedTendril,
    EntityVenusWeedDart,
    EntityVenusWeedSpike,
    EntityModCopper,
    EntityModCopper22,
    EntityModEfreet,
    EntityModEfreet24,
    EntityModEfreet25,
    EntityModEfreet26,
    EntityModEfreet27,
    EntityModEfreet28,
    EntityModEfreet29,
    EntityModEfreet2A,
    EntityModEfreet2B,
    EntityModGuardian,
    EntityModGuardian39,
    EntityModGuardian3A,
    EntityModGuardian3B,
};
_Static_assert(LEN(EntityUpdates) == NUM_RGDN_ENTITIES - 1, "EntityUpdates");

// clang-format off
EInit g_EInitBreakable = {ANIMSET_OVL(1), 0x00, 0x00, PAL_NONE, ENEMY_BREAKABLE};
// The PS1 inits (rno4, rnz0, rnz1 e_init.c) with the converted Saturn banks:
// their parts carry their own cluts and tpages, so palette and unk5A are 0
EInit g_EInitCaveTroll = {MOD_TROLL_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_CAVE_TROLL};
EInit D_us_80180B6C = {MOD_TROLL_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_CAVE_TROLL_UNK_9A};
EInit D_us_80180B78 = {MOD_TROLL_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_CAVE_TROLL_UNK_9A};
EInit g_EInitBitterfly = {MOD_BITTERFLY_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_BITTERFLY};
EInit g_EInitCloakedKnight = {MOD_KNIGHT_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_CLOAKED_KNIGHT};
EInit g_EInitCloakedKnightAura = {MOD_KNIGHT_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_PARTICLE};
EInit g_EInitCloakedKnightSword = {MOD_KNIGHT_ANIMSET, 0x01, 0x00, PAL_NONE, ENEMY_CLOAKED_KNIGHT_SWORD};
EInit g_EInitVenusWeedRoot = {MOD_VENUS_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_BLUE_VENUS_WEED_ROOT};
EInit g_EInitVenusWeedFlower = {MOD_VENUS_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_BLUE_VENUS_WEED_FLOWER};
EInit g_EInitVenusWeedTendril = {MOD_VENUS_ANIMSET, 0x00, 0x00, PAL_NONE, ENEMY_BLUE_VENUS_WEED_TENDRIL};
EInit g_EInitVenusWeedDart = {MOD_VENUS_ANIMSET, 0x37, 0x00, PAL_NONE, ENEMY_BLUE_VENUS_WEED_DART};
#include "../e_init_common.h"
// clang-format on
