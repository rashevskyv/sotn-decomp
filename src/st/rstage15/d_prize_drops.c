// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rstage15.h"
#include <items.h>

// The room 2 item (Saturn 0x0C, EntityPersistentItemDrop params 0): the
// Saturn's table at 0x060ED20C, 0x12C = its hand item 172, the Alucard Spear
// (GAME.PRG g_EquipDefs). The PS1 has no such item: the Alucard Sword stands
// in.
u16 PrizeDrops[] = {0x80 + ITEM_ALUCARD_SWORD};
