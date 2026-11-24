// SPDX-License-Identifier: AGPL-3.0-or-later
// The Pocket (the Saturn third hand, equipment[2]): one food, medicine or Library
// card on the logical L2 button (ZL on the Switch, W on the keyboard).
// The routine borrows hand 1: g_QuickItem goes into equipment[1] for one call
// of func_8010EDB8, so the attack code, the count and the Duplicator rule
// stay the stock ones. Food runs the hand 1 weapon program of weapon 21 from
// its own table (g_QuickWeapon), and its pictures go into the hand 1 page
// until the last food entity is gone.
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include "pc.h"
#include "weapon.h"

extern Equipment g_EquipDefs[]; // src/dra/dra.h
extern Weapon D_8017D000;       // weapon_pc.c
int func_8010EDB8(void);
void make_all(void);
void HandleWeaponPrg(unsigned handId, unsigned weaponId);
void HandleWeaponChr(unsigned handId, unsigned weaponId);
void Saturn_QuickSaveTest(void);
void LoadEquipIcon(s32 equipIcon, s32 palette, s32 index);
void func_800F892C(s32 index, s32 x, s32 y, void* ctx);

void MenuDrawStr(const char* str, s32 x, s32 y, MenuContext* ctx);

u32 g_QuickItem;
int g_QuickEnabled = 1;
int g_QuickUse;
Weapon g_QuickWeapon;
static int s_prg, s_gfx;
static void *s_bank0, *s_bank1; // hand 1 sprite banks 0x12, 0x13 before a food

static int IsFood(u32 id) {
    const Equipment* e = &g_EquipDefs[id];
    // 41 = Meal ticket: it spawns a child that needs the hand 1 table
    return e->unk13 == 50 && e->unk11 == 132 && e->weaponId == 21 &&
           e->unk14 != 41;
}

bool QuickItem_Eligible(u32 id) {
    const Equipment* e;
    if (id == 0 || id >= NUM_HAND_ITEMS) {
        return false;
    }
    e = &g_EquipDefs[id];
    // medicines and the Library card; the menu refuses Life apple, Hammer
    return (e->unk13 == 55 && e->weaponId == 255 && id != ITEM_LIFE_APPLE &&
            id != ITEM_HAMMER) ||
           IsFood(id);
}

void Marquee_Str(const char* str, s32 x, s32 y, s32 w, MenuContext* ctx,
                 u32 frame) {
    MenuContext c = *ctx; // the window's clip rect becomes the field
    const char* s = LANG_CELL(str);
    s32 n = 0, over, ph, off = 0;

    while ((u8)s[n] != 0xFF) {
        n++;
    }
    over = n * 8 - w;
    if (over > 0) {
        ph = frame % (120 + 3 * over);
        off = ph < 60 ? 0 : ph < 60 + 3 * over ? (ph - 60) / 3 : over;
    }
    c.unk1.x = x;
    c.unk1.w = w;
    MenuDrawStr(s, x - off, y, &c);
}

// the dispatch of a new hand 1 weapon entity (src/dra/7879C.c)
void* QuickItem_Table(Entity* e) {
    u32 id = e->ext.weapon.equipId;
    if (s_prg && id > 0 && id < NUM_HAND_ITEMS && g_EquippedWeaponIds[1] != 21 &&
        IsFood(id)) {
        return &g_QuickWeapon;
    }
    return &D_8017D000;
}

static void FoodGfx(u32 id) {
    if (g_EquippedWeaponIds[1] == 21) {
        return;
    }
    if (!s_prg) {
        s_prg = 1;
        HandleWeaponPrg(2, 21);
    }
    if (!s_gfx) { // the food program overwrites the hand 1 banks
        s_bank0 = ((void**)g_api.o.spriteBanks)[0x12];
        s_bank1 = ((void**)g_api.o.spriteBanks)[0x13];
    }
    HandleWeaponChr(1, 21);
    g_QuickWeapon.LoadWeaponPalette(g_EquipDefs[id].palette);
    s_gfx = 1;
}

// what InitWeapon(1) loads for the held weapon
static void RestoreGfx(void) {
    u32 id = g_Status.equipment[1];
    s_gfx = 0;
    ((void**)g_api.o.spriteBanks)[0x12] = s_bank0;
    ((void**)g_api.o.spriteBanks)[0x13] = s_bank1;
    if (g_Status.equipment[ARMOR_SLOT] == ITEM_AXE_LORD_ARMOR) {
        id = 0xD8;
    }
    HandleWeaponChr(1, g_EquippedWeaponIds[1]);
    D_8017D000.LoadWeaponPalette(g_EquipDefs[id].palette);
}

void QuickItem_Restore(void) {
    if (s_gfx) {
        RestoreGfx();
    }
}

bool UseQuickItem(void) {
    u32 id = g_QuickItem, prev;
    bool ok, spent;

    if (!g_QuickEnabled || g_PlayableCharacter != PLAYER_ALUCARD ||
        g_StageId == STAGE_ST0 || !QuickItem_Eligible(id)) {
        return false;
    }
    if (IsFood(id)) {
        FoodGfx(id);
    }
    prev = g_Status.equipment[1];
    g_Status.equipment[1] = id;
    g_QuickUse = 1;
    ok = func_8010EDB8();
    g_QuickUse = 0;
    spent = g_Status.equipment[1] != id; // ReduceWeapon emptied the hand
    if (spent) {
        g_QuickItem = 0;
    }
    g_Status.equipment[1] = prev;
    if (spent) {
        make_all();
    }
    return ok;
}

// ---- the toast: icon, name and count for 2 s ------------------------------
#define TOAST_FRAMES 120
static int s_left;

static void Cells(int x, int y, const char* s, int shade, u32 rgb) {
    int r = (rgb >> 16) * shade / 256, g = (rgb >> 8 & 255) * shade / 256,
        b = (rgb & 255) * shade / 256;
    for (s = LANG_CELL(s); (u8)*s != 0xFF && x < 252; s++, x += 8) {
        PortMenu_Cell(x, y, (u8)*s, r << 16 | g << 8 | b);
    }
}

// every frame of the Alucard HUD (DrawHudSubweapon)
void QuickItem_Draw(void) {
    static int last = -1, frame;
    u32 id = g_QuickItem;
    int count = id ? g_Status.equipHandCount[id] + 1 : 0;
    char num[6];
    int i, n, shade;

#ifdef QUICK_TEST
    const char* e = getenv("SOTN_TEST_QUICK"); // id:count, play frame 300
    if (e && ++frame == 300 && sscanf(e, "%u:%d", &id, &count) == 2) {
        g_QuickItem = id;
        g_Status.equipHandCount[id] = count - 1;
        g_Status.hp = 10;
        fprintf(stderr, "[quick] test: item %u x%d\n", id, count);
    }
    if (e && frame == 300 && getenv("SOTN_TEST_HAND")) { // a food in hand 1
        id = atoi(getenv("SOTN_TEST_HAND"));
        g_Status.equipment[1] = id;
        g_Status.equipHandCount[id] = 4;
    }
    if (e && frame == 340) {
        Saturn_QuickSaveTest();
    }
    if (e && frame > 300 && frame % 60 == 0) {
        fprintf(stderr, "[quick] test: hp %d item %u stock %d\n",
                (int)g_Status.hp, (unsigned)g_QuickItem,
                g_QuickItem ? g_Status.equipHandCount[g_QuickItem] + 1 : 0);
    }
#endif
    if (s_gfx) { // no food of the third hand left: the held weapon's pictures
        for (i = 4; i < 64; i++) {
            if (g_Entities[i].entityId >= 0xF0 &&
                QuickItem_Table(&g_Entities[i]) == &g_QuickWeapon) {
                break;
            }
        }
        if (i == 64) {
            RestoreGfx();
        }
    }
    if (!g_QuickEnabled) {
        return;
    }
    id = g_QuickItem;
    count = id ? g_Status.equipHandCount[id] + 1 : 0;
    if (id && last != (int)(id << 8 | count)) {
        s_left = TOAST_FRAMES;
    }
    last = id << 8 | count;
    if (!id || s_left <= 0) {
        return;
    }
    s_left--;
    shade = s_left < 20 ? 256 * s_left / 20 : 256;
    LoadEquipIcon(g_EquipDefs[id].icon, g_EquipDefs[id].iconPalette, 0x1F);
    func_800F892C(0x1F, 110, 22, NULL);
    Cells(128, 21, g_EquipDefs[id].name, shade, 0xE0E0E0);
    n = 0;
    num[n++] = CH('x');
    if (count > 99) {
        num[n++] = CH('0' + count / 100 % 10);
    }
    if (count > 9) {
        num[n++] = CH('0' + count / 10 % 10);
    }
    num[n++] = CH('0' + count % 10);
    num[n] = 0xFF;
    Cells(128, 32, num, shade, 0xFFD040);
}
