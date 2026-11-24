// SPDX-License-Identifier: AGPL-3.0-or-later
// The Saturn's own hand items (include/items.h ITEM_ASTRAL_DAGGER..) in a
// save. SaveData.status keeps the PS1 layout, so the saves of the PS1 and of
// 0.8.60 still load, and an older build still loads a new save. The Saturn
// items go to a block right after SaveData, in the zeroed tail of the card
// block (StoreSaveData clears 0x2000 bytes).
#include "game.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SATURN_SAVE_MAGIC 0x49544153 // "SATI"

typedef struct {
    u32 magic;
    u32 equipment[2]; // the real hand items: the PS1 area has stand-ins
    u8 handCount[NUM_SATURN_HAND_ITEMS];
    u8 handOrder[NUM_HAND_ORDER];
    // the third hand (quick_item.c): its item, and the stock without the held
    // copy. The PS1 area counts the held copy too, so an older build keeps it
    // in the inventory. An older build that saved over this one leaves a
    // count that does not match: the item goes back to the stock then.
    u8 quickItem, quickStock;
} SaturnSave;

STATIC_ASSERT(sizeof(PlayerStatusPs1) == 0x334, "the PS1 PlayerStatus");
STATIC_ASSERT(sizeof(SaveData) + sizeof(SaturnSave) <= 0x2000, "card block");

// what an older build equips in place of a Saturn item (its PS1 stand-in)
static const u8 StandIn[NUM_SATURN_HAND_ITEMS] = {
    ITEM_COMBAT_KNIFE, ITEM_TURKEY, ITEM_LEATHER_SHIELD, ITEM_CLAYMORE};

// the bytes from saveName to the end: the same in both layouts
#define TAIL_SIZE (sizeof(PlayerStatus) - offsetof(PlayerStatus, saveName))
#define PS1_EQUIPMENT(ps1)                                                     \
    ((u8*)(ps1)->saveName + offsetof(PlayerStatus, equipment) -                \
     offsetof(PlayerStatus, saveName))

void Status_ToPs1(PlayerStatusPs1* dst, const PlayerStatus* src, void* ext) {
    SaturnSave* sat = ext;
    u32 equipment[2];
    int i, n;

    memcpy(dst->relics, src->relics, sizeof(dst->relics));
    memcpy(dst->spells, src->spells, sizeof(dst->spells));
    memcpy(dst->equipHandCount, src->equipHandCount, NUM_HAND_ITEMS);
    memcpy(dst->equipBodyCount, src->equipBodyCount, NUM_BODY_ITEMS);
    for (i = n = 0; i < NUM_HAND_ORDER && n < NUM_HAND_ITEMS; i++) {
        if (!IS_SATURN_HAND_ITEM(src->equipHandOrder[i])) {
            dst->equipHandOrder[n++] = src->equipHandOrder[i];
        }
    }
    memcpy(dst->equipBodyOrder, src->equipBodyOrder, NUM_BODY_ITEMS);
    memcpy(dst->saveName, src->saveName, TAIL_SIZE);
    for (i = 0; i < 2; i++) {
        equipment[i] = src->equipment[i];
        if (IS_SATURN_HAND_ITEM(equipment[i])) {
            equipment[i] = StandIn[equipment[i] - ITEM_ASTRAL_DAGGER];
        }
    }
    memcpy(PS1_EQUIPMENT(dst), equipment, sizeof(equipment));
    if (g_QuickItem && g_QuickItem < NUM_HAND_ITEMS) {
        dst->equipHandCount[g_QuickItem]++;
    }

    if (sat) {
        sat->magic = SATURN_SAVE_MAGIC;
        memcpy(sat->equipment, src->equipment, sizeof(sat->equipment));
        memcpy(sat->handCount, &src->equipHandCount[ITEM_ASTRAL_DAGGER],
               NUM_SATURN_HAND_ITEMS);
        memcpy(sat->handOrder, src->equipHandOrder, NUM_HAND_ORDER);
        sat->quickItem = g_QuickItem < NUM_HAND_ITEMS ? g_QuickItem : 0;
        sat->quickStock = src->equipHandCount[sat->quickItem];
    }
}

// every PS1 and Saturn hand item once
static bool OrderOk(const u8* order) {
    u8 seen[NUM_HAND_IDS] = {0};
    int i;

    for (i = 0; i < NUM_HAND_ORDER; i++) {
        u8 id = order[i];
        if ((id >= NUM_HAND_ITEMS && !IS_SATURN_HAND_ITEM(id)) || seen[id]) {
            return false;
        }
        seen[id] = 1;
    }
    return true;
}

void Status_FromPs1(
    PlayerStatus* dst, const PlayerStatusPs1* src, const void* ext) {
    const SaturnSave* sat = ext;
    int i;

    g_QuickItem = 0;
    memcpy(dst->relics, src->relics, sizeof(dst->relics));
    memcpy(dst->spells, src->spells, sizeof(dst->spells));
    memset(dst->equipHandCount, 0, sizeof(dst->equipHandCount));
    memcpy(dst->equipHandCount, src->equipHandCount, NUM_HAND_ITEMS);
    memcpy(dst->equipBodyCount, src->equipBodyCount, NUM_BODY_ITEMS);
    memcpy(dst->equipBodyOrder, src->equipBodyOrder, NUM_BODY_ITEMS);
    memcpy(dst->saveName, src->saveName, TAIL_SIZE);

    if (sat && sat->magic == SATURN_SAVE_MAGIC && OrderOk(sat->handOrder)) {
        memcpy(dst->equipHandOrder, sat->handOrder, NUM_HAND_ORDER);
        memcpy(&dst->equipHandCount[ITEM_ASTRAL_DAGGER], sat->handCount,
               NUM_SATURN_HAND_ITEMS);
        for (i = 0; i < 2; i++) {
            u32 id = sat->equipment[i];
            // an older build saved over it: its own item stays
            if (IS_SATURN_HAND_ITEM(id) &&
                dst->equipment[i] == StandIn[id - ITEM_ASTRAL_DAGGER]) {
                dst->equipment[i] = id;
            }
        }
        i = sat->quickItem;
        if (QuickItem_Eligible(i) && src->equipHandCount[i] == sat->quickStock + 1) {
            dst->equipHandCount[i]--;
            g_QuickItem = i;
        }
        return;
    }
    // a PS1 or 0.8.60 save: the Saturn items go last, none owned
    memcpy(dst->equipHandOrder, src->equipHandOrder, NUM_HAND_ITEMS);
    for (i = 0; i < NUM_SATURN_HAND_ITEMS; i++) {
        dst->equipHandOrder[NUM_HAND_ITEMS + i] = ITEM_ASTRAL_DAGGER + i;
    }
}

// test (SOTN_TEST_QUICK, quick_item.c): the third hand through a save, and
// through a save of 0.8.112 (zero tail), of the PS1 (no tail) and of an older
// build that saved over it (its count differs)
#ifdef QUICK_TEST
void Saturn_QuickSaveTest(void) {
    static u8 buf[0x2000];
    PlayerStatusPs1 ps1;
    PlayerStatus out;
    SaturnSave* sat = (SaturnSave*)buf;
    u32 q = g_QuickItem;
    int stock = g_Status.equipHandCount[q];

    Status_ToPs1(&ps1, &g_Status, buf);
    g_QuickItem = 0;
    Status_FromPs1(&out, &ps1, buf);
    fprintf(stderr, "[quick] save: item %u->%u stock %d->%d ps1 count %d\n",
            (unsigned)q, (unsigned)g_QuickItem, stock, out.equipHandCount[q],
            ps1.equipHandCount[q]);
    sat->quickItem = sat->quickStock = 0; // a save of 0.8.112
    Status_FromPs1(&out, &ps1, buf);
    fprintf(stderr, "[quick] old save: item %u, count %d\n",
            (unsigned)g_QuickItem, out.equipHandCount[q]);
    Status_FromPs1(&out, &ps1, NULL); // no tail (PS1)
    fprintf(stderr, "[quick] ps1 save: item %u\n", (unsigned)g_QuickItem);
    sat->quickItem = q;
    sat->quickStock = stock + 5; // an older build changed the count
    Status_FromPs1(&out, &ps1, buf);
    fprintf(stderr, "[quick] stale save: item %u, count %d\n",
            (unsigned)g_QuickItem, out.equipHandCount[q]);
    g_QuickItem = q;
}
#endif
