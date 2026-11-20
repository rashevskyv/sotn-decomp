// SPDX-License-Identifier: AGPL-3.0-or-later
#include "stage16.h"

// The lanterns' prizes (EntityPersistentItemDrop params 0-2), the Saturn's
// table at 0x060E92E4: 0x98, 0x12E, 0xC6 = its hand items 24, 174 and 70.
// Saturn hand items 0-69 are the PS1's, 72-168 the PS1's 70-166 (the names
// in GAME.PRG g_EquipDefs match); 70-71, 169 and 172-175 are Saturn-only.
// 24 is the Karma Coin. 174 (Astral Dagger) and 70 (Kusai Meshi, a meal)
// are Saturn-only: the Combat knife and the Turkey stand in.
u16 PrizeDrops[] = {0x80 + 24, 0x80 + 20, 0x80 + 69};
