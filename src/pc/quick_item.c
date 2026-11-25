// SPDX-License-Identifier: AGPL-3.0-or-later
// The Pocket (the Saturn third hand, equipment[2]): one food, medicine or Library
// card on the logical L2 button (ZL on the Switch, W on the keyboard).
// The routine borrows hand 1: g_QuickItem goes into equipment[1] for one call
// of func_8010EDB8, so the attack code, the chain limit, the count and the
// Duplicator rule stay the stock ones. Only the food entity is its own
// (EntityPocketFood), so no hand 1 picture, palette or sprite bank is used.
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include "pc.h"
#include "weapon.h"

extern Equipment g_EquipDefs[]; // src/dra/dra.h
int func_8010EDB8(void);
void make_all(void);
void Saturn_QuickSaveTest(void);
void LoadEquipIcon(s32 equipIcon, s32 palette, s32 index);
void func_800F892C(s32 index, s32 x, s32 y, void* ctx);

void MenuDrawStr(const char* str, s32 x, s32 y, MenuContext* ctx);

u32 g_QuickItem;
int g_QuickEnabled = 1;
int g_QuickUse;

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

// How far (px) a name of cells reaches past a field of w px: 0 = it fits (the
// blank cells at its end are not text), else at least one cell, so a name never
// wobbles by 1-2 px. The Pocket field is a multiple of 8, so the clamp is a net.
s32 Marquee_Over(const char* s, s32 w) {
    s32 n = 0, over;

    while ((u8)s[n] != 0xFF) {
        n++;
    }
    while (n > 0 && s[n - 1] == 0) {
        n--;
    }
    over = n * 8 - w;
    return over <= 0 ? 0 : over < 8 ? 8 : over;
}

void Marquee_Str(const char* str, s32 x, s32 y, s32 w, MenuContext* ctx,
                 u32 frame) {
    MenuContext c = *ctx; // the window's clip rect becomes the field
    const char* s = LANG_CELL(str);
    s32 over = Marquee_Over(s, w), ph, off = 0;

    if (over > 0) {
        ph = frame % (120 + 3 * over);
        off = ph < 60 ? 0 : ph < 60 + 3 * over ? (ph - 60) / 3 : over;
    }
    c.unk1.x = x;
    c.unk1.w = w;
    MenuDrawStr(s, x - off, y, &c);
}

// ---- the food of the Pocket: its own DRA entity (E_POCKET_FOOD) ----------
// The stock food is an entity of the hand 1 weapon program (w_021.c): its
// pictures, palette and sprite bank sit in the hand's own VRAM page. The
// Pocket food uses none of that. It is drawn with the item's icon (icon
// slots 0x1D and 0x1E, the toast keeps 0x1F) and runs the same rules as
// EntityWeaponAttack of w_021.c: fall, bounce, eat for healKind 1 and
// healAmount = the item's attack, the Peanuts bounce, the Toadstool kills.
#define FOOD_SLOT0 0x1D
#define FOOD_KIND_PEANUTS 6
#define FOOD_KIND_TOADSTOOL 7

void DestroyEntity(Entity* entity);
void DecelerateX(s32 amount);

// the stock test for "the player caught it": from the Peanuts pose or the box
static bool FoodCaught(Entity* self, s32 kind, bool onGround) {
    s16 xDist, yDist;

    if (!onGround && self->velocityY < FIX(2)) {
        return false;
    }
    if (kind == FOOD_KIND_PEANUTS) {
        if (PLAYER.animCurFrame == 0x7B && PLAYER.ext.player.anim == 0 &&
            self->posY.i.hi <= PLAYER.posY.i.hi - 16 &&
            self->posY.i.hi >= PLAYER.posY.i.hi - 24 &&
            self->posX.i.hi <= PLAYER.posX.i.hi + 4 &&
            self->posX.i.hi >= PLAYER.posX.i.hi - 4) {
            PLAYER.pose = 3;
            PLAYER.poseTimer = 8;
            return true;
        }
        return false;
    }
    xDist = abs(self->posX.i.hi - PLAYER.posX.i.hi + PLAYER.hitboxOffX);
    yDist = abs(self->posY.i.hi - PLAYER.posY.i.hi + PLAYER.hitboxOffY);
    return xDist <= self->hitboxWidth + PLAYER.hitboxWidth &&
           yDist <= self->hitboxHeight + PLAYER.hitboxHeight;
}

// food is eaten: heal now, or wait 8 frames when another heal is running
static bool FoodEaten(Entity* self, s32 kind, bool onGround) {
    if (!FoodCaught(self, kind, onGround)) {
        return false;
    }
    if (kind == FOOD_KIND_TOADSTOOL) {
        self->ext.food.timer = 12;
        self->step = 4;
        g_Player.padSim = onGround ? 0 : PAD_DOWN;
        g_Player.demo_timer = 20; // stun the player for 20 frames
        return true;
    }
    if (g_Player.healKind == 0) {
        g_Player.healKind = 1;
        g_Player.healAmount = g_EquipDefs[self->ext.weapon.equipId].attack;
#ifdef QUICK_TEST
        fprintf(stderr, "[quick] eat %d heal %d\n",
                (int)self->ext.weapon.equipId, (int)g_Player.healAmount);
#endif
        DestroyEntity(self);
        return true;
    }
    self->ext.food.timer = 8;
    self->step = 3;
    return true;
}

// func_8010EDB8 asks for the entity: NULL when no slot is free
Entity* QuickItem_SpawnFood(u32 equipId) {
    Entity* e = g_api.GetFreeEntity(0x11, 0x40);
    Entity* o;
    int i, slot = FOOD_SLOT0;

    if (e == NULL) {
        return NULL;
    }
    for (i = 16, o = &g_Entities[16]; i < 64; i++, o++) { // the free icon slot
        if (o->entityId == E_POCKET_FOOD && o->ext.food.unk7E == slot) {
            slot++;
        }
    }
    DestroyEntity(e);
    e->entityId = E_POCKET_FOOD;
    e->params = (g_EquipDefs[equipId].unk14 & 0x7F) << 8 | 0x8000; // hand 1
    e->ext.weapon.equipId = equipId;
    e->ext.food.unk7E = slot;
    e->posX.val = PLAYER.posX.val;
    e->posY.val = PLAYER.posY.val;
    e->facingLeft = PLAYER.facingLeft;
    e->zPriority = PLAYER.zPriority;
    return e;
}

void EntityPocketFood(Entity* self) {
    const Equipment* def = &g_EquipDefs[self->ext.weapon.equipId];
    s32 kind = (self->params >> 8) & 0x7F;
    s32 slot = self->ext.food.unk7E;
    Primitive* prim;
    Collider col;
    s16 xShift, yShift;

    switch (self->step) {
    case 0:
        self->primIndex = g_api.AllocPrimitives(PRIM_SPRT, 1);
        if (self->primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags = FLAG_POS_CAMERA_LOCKED | FLAG_HAS_PRIMS;
        self->zPriority = PLAYER.zPriority + 2;
        prim = &g_PrimBuf[self->primIndex];
        prim->tpage = 0x1A; // the icon page of the menu
        prim->clut = 0x1D0 + slot;
        prim->u0 = (slot & 7) * 16;
        prim->v0 = ((slot & 0xF8) * 2) | 0x80;
        prim->u1 = prim->v1 = 16;
        prim->r0 = prim->g0 = prim->b0 = 0x80;
        prim->priority = self->zPriority;
        prim->drawMode = DRAW_DEFAULT;
        self->posY.i.hi -= 4;
        g_api.SetSpeedX((rand() & 0x3FFF) + FIX(0.875)); // 1 +- 0.125
        self->velocityY = FIX(-2);
        if (kind == FOOD_KIND_PEANUTS) {
            self->posX.i.hi += self->facingLeft ? -8 : 8;
            g_api.SetSpeedX((rand() & 0xFFF) | FIX(1.0 / 16));
            self->velocityY = FIX(-5.5);
            self->ext.food.unk80 = FIX(-5.5);
        }
        if (g_Player.unk48 != 0) { // the stock DestroyEntityWeapon(1)
            DestroyEntity(&g_Entities[E_WEAPON]);
            g_Player.unk48 = 0;
        }
        self->facingLeft = 0;
        self->hitboxHeight = self->hitboxWidth = 8;
        self->step++;
        break;
    case 1:
        if (FoodEaten(self, kind, false)) {
            if (self->entityId == 0) {
                return;
            }
            break;
        }
        self->posX.val += self->velocityX;
        self->posY.val += self->velocityY;
        self->velocityY += FIX(20.0 / 128);
        g_api.CheckCollision(
            self->posX.i.hi, (s16)(self->posY.i.hi + 8), &col, 0);
        if (col.effects & EFFECT_SOLID) {
            self->posY.i.hi += col.unk18;
            if (kind == FOOD_KIND_PEANUTS) { // the Peanuts bounce
                self->ext.food.unk80 /= 2;
                self->velocityY = self->ext.food.unk80;
                if (self->velocityY <= FIX(-1)) {
                    self->posY.val += self->velocityY;
                } else {
                    DestroyEntity(self);
                    return;
                }
            } else {
                self->ext.food.timer = 0x80;
                self->step++;
                break;
            }
        }
        xShift = self->velocityX < 0 ? -4 : 4;
        g_api.CheckCollision(
            (s16)(xShift + self->posX.i.hi), self->posY.i.hi, &col, 0);
        if (col.effects & EFFECT_SIDE) {
            self->posX.i.hi += xShift < 0 ? col.unkC : col.unk4;
            self->velocityX /= -2;
            if (kind == FOOD_KIND_PEANUTS) {
                DestroyEntity(self);
                return;
            }
        }
        yShift = kind == FOOD_KIND_PEANUTS ? -4 : -8;
        g_api.CheckCollision(
            self->posX.i.hi, (s16)(yShift + self->posY.i.hi), &col, 0);
        if (col.effects & EFFECT_SOLID) {
            self->posY.i.hi += 1 + col.unk20;
            self->velocityX /= 2;
            self->velocityY = FIX(1);
        }
        break;
    case 2:
        DecelerateX(FIX(36.0 / 128));
        self->posX.val += self->velocityX;
        if (FoodEaten(self, kind, true)) {
            if (self->entityId == 0) {
                return;
            }
            break;
        }
        if (--self->ext.food.timer == 0) {
            DestroyEntity(self);
            return;
        }
        break;
    case 3: // another heal runs: the same wait as the stock food
        if (--self->ext.food.timer == 0) {
            if (g_Player.healKind == 0) {
                g_Player.healKind = 1;
                g_Player.healAmount = def->attack;
                DestroyEntity(self);
                return;
            }
            self->ext.food.timer = 8;
        }
        break;
    case 4: // the Toadstool: the player dies when the timer ends
        if (--self->ext.food.timer == 0) {
            PLAYER.hitParams = 0x81;
            PLAYER.hitPoints = 0;
            PLAYER.hitboxState = 0;
            DestroyEntity(self);
            return;
        }
        break;
    }
    prim = &g_PrimBuf[self->primIndex];
    LoadEquipIcon(def->icon, def->iconPalette, slot);
    prim->x0 = self->posX.i.hi - 8;
    prim->y0 = self->posY.i.hi - 8;
    prim->priority = self->zPriority;
    // hidden while eaten, and the last 24 frames on the ground blink
    prim->drawMode = self->step >= 3 || (self->step == 2 &&
                                         self->ext.food.timer <= 24 &&
                                         (self->ext.food.timer & 1))
                         ? DRAW_HIDE
                         : DRAW_DEFAULT;
}

bool UseQuickItem(void) {
    u32 id = g_QuickItem, prev;
    bool ok, spent;

    if (!g_QuickEnabled || g_PlayableCharacter != PLAYER_ALUCARD ||
        g_StageId == STAGE_ST0 || !QuickItem_Eligible(id)) {
        return false;
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
    if (e && frame == 300 && getenv("SOTN_TEST_ACC")) { // a Duplicator
        g_Status.equipment[ACCESSORY_1_SLOT] = ITEM_DUPLICATOR;
    }
    if (e && frame > 300) { // a line for every change
        static int php = -1, pst = -1, pq = -1, ph1 = -1, pn = -1;
        int st = g_QuickItem ? g_Status.equipHandCount[g_QuickItem] + 1 : 0;
        int h1 = g_Status.equipment[1], n = 0, i;
        for (i = 16; i < 64; i++) {
            n += g_Entities[i].entityId == E_POCKET_FOOD;
        }
        if ((int)g_Status.hp != php || st != pst || (int)g_QuickItem != pq ||
            h1 != ph1 || n != pn) {
            fprintf(stderr, "[quick] f%d hp %d pocket %u x%d hand1 %d ents %d\n",
                    frame, (int)g_Status.hp, (unsigned)g_QuickItem, st, h1, n);
            php = g_Status.hp, pst = st, pq = g_QuickItem, ph1 = h1, pn = n;
        }
    }
#endif
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
