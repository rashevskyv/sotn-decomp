// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rstage16.h"

// The lanterns' prizes (EntityPersistentItemDrop params 0-2), the Saturn's
// table at 0x060E8BC0: 0x128, 0x175, 0xC7 = its hand item 168 (Library
// Card, "Magical Ticket"), its armor 69 (Opal) and its hand item 71
// (Umai Meshi, a meal: Saturn-only, the Pot Roast stands in).
u16 PrizeDrops[] = {ITEMDROP_LIBRARY_CARD, ITEMDROP_OPAL, ITEMDROP_POT_ROAST};
