// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * The inverted Cursed Prison's Wight (Saturn RSTAGE16), translated from the
 * SH-2 (Saturn port mod):
 *   EntityModWight        Saturn 0x15, func 060E2578, enemy 423
 *   EntityModWightAttack  Saturn 0x16, func 060E3244, enemy 397 (attack / 3)
 *   EntityModWightPart    Saturn 0x17, func 060E3438, enemy 5
 * The sprite is the converted Saturn bank 18 (gdn_entities.py RPRISON:
 * ANIMSET_OVL(11), frames 1:1, converted 32 px down: the Saturn draws the
 * sprite at the entity + (0, 32)); the anim tables are the Saturn's
 * (0x060EA2BC..). The attack entity (self + 1) draws the slash (frames
 * 0x4A-0x64) and carries its hitbox; the part is a fading afterimage of it.
 *
 * Simplified: the death's stretching primitive (frame 59 on, 26 frames) is
 * not drawn (its timing is kept); the debug step 0x20 (pad -> frame) is left
 * out; the Saturn-only sounds 0x908 and 0x909 have PS1 stand-ins.
 */
#include "rstage16.h"

// Saturn screen is 320 px wide, PS1 is 256: horizontal quantities * 4/5
#define SAT_X(v) ((v) * 4 / 5)

#define MOD_SFX_WIGHT_SLASH SFX_WEAPON_SWISH_C  // Saturn 0x908
#define MOD_SFX_WIGHT_DEATH SFX_SKELETON_DEATH_C // Saturn 0x909

s32 UnkCollisionFunc2(s16* sensors);
u8 CheckColliderOffsets(s16* sensors, u8 facing);
void EntityExplosionVariantsSpawner(
    Entity* self, u8 count, u8 params, s16 x, s16 y, u8 index, s16 xGap);

static EInit g_EInitModWight = {MOD_WIGHT_ANIMSET, 0, 0, PAL_NONE, 423};
static EInit g_EInitModWightAttack = {MOD_WIGHT_ANIMSET, 0, 0, PAL_NONE, 397};
static EInit g_EInitModWightPart = {MOD_WIGHT_ANIMSET, 0, 0, PAL_NONE, 5};

typedef struct {
    /* Saturn 0x94 */ s16 timer;   // walk: turns or attacks by it
    /* Saturn 0x96 */ s16 counter; // death: the primitive, then the wait
    /* Saturn 0xA4 */ u16 hitboxSave;
} ET_ModWight;
#define WIGHT_EXT(e) ((ET_ModWight*)&(e)->ext)

typedef struct {
    /* Saturn 0x9C */ s32 timer;
} ET_ModWightPart;
#define WIGHT_PART_EXT(e) ((ET_ModWightPart*)&(e)->ext)

enum {
    WIGHT_INIT = 0,
    WIGHT_TURN = 2,
    WIGHT_WALK = 3,
    WIGHT_ATTACK = 4,
    WIGHT_HIT = 0x10,
    WIGHT_DEATH = 0x12,
};

// 0x060EA2BC.. (frames 1:1)
static u8 wight_anim_walk[] = {
    0x0C, 0x01, 0x2A, 0x02, 0x0A, 0x03, 0x0A, 0x04, 0x0E, 0x05, 0x0E, 0x06,
    0x0C, 0x07, 0x2A, 0x08, 0x06, 0x09, 0x06, 0x0A, 0x06, 0x0B, 0x0E, 0x0C,
    0x0E, 0x0D, 0x00, 0x00};
static u8 wight_anim_attack[] = {
    0x05, 0x08, 0x05, 0x0E, 0x05, 0x0F, 0x05, 0x10, 0x07, 0x11, 0x09, 0x12,
    0x08, 0x13, 0x08, 0x14, 0x09, 0x15, 0x09, 0x16, 0x0B, 0x17, 0x07, 0x18,
    0x07, 0x19, 0x03, 0x1A, 0x02, 0x1B, 0x02, 0x48, 0x02, 0x1C, 0x02, 0x49,
    0x02, 0x1D, 0x02, 0x1E, 0x02, 0x1F, 0x02, 0x20, 0x02, 0x21, 0x03, 0x22,
    0x20, 0x21, 0x03, 0x23, 0x04, 0x24, 0x04, 0x25, 0x05, 0x26, 0x05, 0x08,
    0xFF, 0xFF};
// the attack entity's slash, in step with wight_anim_attack
static u8 wight_anim_slash[] = {
    0x05, 0x00, 0x05, 0x4A, 0x05, 0x4B, 0x05, 0x4C, 0x07, 0x4D, 0x09, 0x4E,
    0x08, 0x4F, 0x08, 0x50, 0x09, 0x51, 0x09, 0x52, 0x0B, 0x53, 0x07, 0x54,
    0x07, 0x55, 0x03, 0x56, 0x02, 0x57, 0x02, 0x58, 0x02, 0x59, 0x02, 0x5A,
    0x02, 0x5B, 0x02, 0x5C, 0x02, 0x5D, 0x02, 0x5E, 0x02, 0x5F, 0x03, 0x60,
    0x20, 0x5F, 0x03, 0x61, 0x04, 0x62, 0x04, 0x63, 0x05, 0x64, 0x05, 0x00,
    0xFF, 0xFF};
// knocked down and up again
static u8 wight_anim_hit[] = {
    0x02, 0x0C, 0x02, 0x27, 0x02, 0x28, 0x02, 0x29, 0x02, 0x2A, 0x03, 0x2B,
    0x08, 0x2C, 0x04, 0x2D, 0x05, 0x2E, 0x06, 0x2F, 0x07, 0x30, 0x04, 0x31,
    0x05, 0x32, 0x05, 0x33, 0x05, 0x34, 0x12, 0x35, 0x07, 0x36, 0x05, 0x37,
    0x05, 0x38, 0x09, 0x39, 0x07, 0x3A, 0x40, 0x0C, 0xFF, 0xFF};
static u8 wight_anim_death[] = {
    0x19, 0x0C, 0x03, 0x27, 0x03, 0x28, 0x03, 0x29, 0x18, 0x2A, 0x06, 0x3B,
    0x06, 0x3C, 0x06, 0x3D, 0x06, 0x3E, 0x06, 0x3F, 0x06, 0x40, 0x06, 0x41,
    0x06, 0x42, 0x19, 0x43, 0xFF, 0xFF};

// 0x060EA27C / 0x060EA284 (UnkCollisionFunc2), 0x060EA26A / 0x060EA270
// (CheckColliderOffsets): the feet are 32 px below the entity
static s16 wight_sensors_walk[] = {0, 32, SAT_X(8), 0};
static s16 wight_sensors_death[] = {0, 32, SAT_X(-8), 0};
static s16 wight_wall_walk[] = {SAT_X(24), 28, 0xFF};
static s16 wight_wall_death[] = {SAT_X(32), 28, 0xFF};

// 0x060EA3AE: the palette offsets the Saturn cycles every 6 frames
static u16 wight_pal_cycle[] = {0, 2, 1, 2};

// 0x060EA3E4 / 0x060EA414 (g_RStage16Entity21Offsets / Sizes): the slash
// hitbox for frames 0x4D..0x64, Saturn space ({offX, -offY}, {w, h})
static s8 wight_slash_offsets[24][2] = {
    {15, -18},  {19, -16},  {21, -16},  {22, -8},   {27, 0},    {25, 1},
    {23, 1},    {21, 11},   {13, 20},   {2, 26},    {-7, 29},   {-20, 28},
    {-32, 20},  {-48, 14},  {-64, 0},   {-52, -8},  {-40, -20}, {-32, -22},
    {-28, -24}, {-26, -26}, {-24, -24}, {-22, -24}, {-16, -25}, {-16, -24},
};
static u8 wight_slash_sizes[24][2] = {
    {2, 0xA},    {2, 0xA},    {3, 0xA},     {4, 9},       {5, 8},
    {5, 7},      {6, 6},      {7, 6},       {8, 6},       {9, 8},
    {0xA, 0xB},  {0xA, 0xF},  {0x14, 0x12}, {0x18, 0x12}, {0x30, 0x10},
    {0x1C, 0xC}, {0x14, 0xA}, {0x10, 8},    {0xC, 6},     {0xA, 5},
    {8, 4},      {6, 6},      {4, 6},       {3, 7},
};

static void WightFacePlayer(Entity* self) {
    self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
}

// the Saturn's inlined SetStep also stops the entity
static void WightSetStep(Entity* self, u16 step) {
    SetStep(step);
    self->velocityX = 0;
    self->velocityY = 0;
}

void EntityModWight(Entity* self) {
    ET_ModWight* ext = WIGHT_EXT(self);
    Entity* attack = self + 1;
    Entity* e;
    s16 dx;
    s32 vx;

    if (self->hitParams && (self->step & 1)) {
        WightSetStep(self, WIGHT_HIT);
    }
    if ((self->flags & FLAG_DEAD) && self->step != WIGHT_DEATH) {
        WightSetStep(self, WIGHT_DEATH);
        self->hitboxState = 0;
    }

    switch (self->step) {
    case WIGHT_INIT:
        InitializeEntity(g_EInitModWight);
        self->hitboxWidth = SAT_X(8);
        self->hitboxHeight = 16;
        self->hitboxOffY += 8;
        DestroyEntity(attack);
        CreateEntityFromCurrentEntity(E_MOD_WIGHT_ATTACK, attack);
        WightFacePlayer(self);
        WightSetStep(self, WIGHT_WALK);
        self->animCurFrame = 0;
        break;

    case WIGHT_TURN:
        if (self->step_s == 0) {
            self->facingLeft ^= 1;
        }
        WightSetStep(self, WIGHT_WALK);
        break;

    case WIGHT_WALK:
        dx = GetDistanceToPlayerX();
        if (self->step_s == 0) {
            self->velocityX = self->facingLeft ? SAT_X(0x1000) : SAT_X(-0x1000);
            ext->timer = 540;
            self->step_s++;
        }
        if (dx <= SAT_X(111) && ext->timer <= 479 && (Random() & 1)) {
            WightSetStep(self, WIGHT_ATTACK);
            break;
        }
        AnimateEntity(wight_anim_walk, self);
        UnkCollisionFunc2(wight_sensors_walk);
        if (CheckColliderOffsets(wight_wall_walk, self->facingLeft)) {
            WightSetStep(self, WIGHT_TURN);
            break;
        }
        ext->timer--;
        if (dx <= SAT_X(143) && !(ext->timer % 180)) {
            WightFacePlayer(self);
            self->step_s = 0;
            break;
        }
        if (ext->timer <= 0) {
            WightSetStep(self, WIGHT_TURN);
        }
        break;

    case WIGHT_ATTACK:
        if (self->step_s == 0) {
            WightFacePlayer(self);
            self->step_s++;
        }
        if (self->pose == 14 && self->poseTimer == 0) {
            PlaySfxPositional(MOD_SFX_WIGHT_SLASH);
        }
        if (!AnimateEntity(wight_anim_attack, self)) {
            if (attack->entityId == E_MOD_WIGHT_ATTACK) {
                attack->params = 0;
                attack->pose = 0;
                attack->poseTimer = 0;
            }
            WightSetStep(self, WIGHT_WALK);
        } else if (attack->entityId == E_MOD_WIGHT_ATTACK && !attack->params) {
            attack->params = 1;
        }
        break;

    case WIGHT_HIT:
        if (self->step_s == 0) {
            WightFacePlayer(self);
            self->velocityX =
                self->facingLeft ? SAT_X(-FIX(1.5)) : SAT_X(FIX(1.5));
            ext->hitboxSave = self->hitboxState;
            self->hitboxState = 0;
            self->step_s++;
            PlaySfxPositional(SFX_WHIP_TWIRL_SWISH); // 0x6C7
            PlaySfxPositional(SFX_SCRAPE_B);         // 0x616
        }
        if (self->animCurFrame > 48) {
            self->velocityX = self->velocityX * 15 / 16;
        }
        if (self->animCurFrame == 52) {
            self->velocityX = 0;
        }
        if (!AnimateEntity(wight_anim_hit, self)) {
            WightSetStep(self, WIGHT_WALK);
            self->hitboxState = ext->hitboxSave;
            break;
        }
        UnkCollisionFunc2(wight_sensors_walk);
        if (self->velocityX &&
            CheckColliderOffsets(wight_wall_walk, self->facingLeft ^ 1)) {
            self->velocityX = 0;
            self->facingLeft ^= 1;
        }
        break;

    case WIGHT_DEATH:
        switch (self->step_s) {
        case 0:
            WightFacePlayer(self);
            self->velocityX =
                self->facingLeft ? SAT_X(-FIX(1.5)) : SAT_X(FIX(1.5));
            // ponytail: the Saturn's stretching primitive is not drawn
            ext->counter = 0;
            self->step_s++;
            // fallthrough
        case 1:
            if (self->animCurFrame <= 41 && self->animCurFrame != 12) {
                self->velocityX = self->velocityX * 9 / 10;
            }
            if (self->animCurFrame == 59) {
                self->velocityX = 0;
            }
            if (!AnimateEntity(wight_anim_death, self)) {
                self->step_s++;
                break;
            }
            if (self->velocityX && !UnkCollisionFunc2(wight_sensors_death)) {
                self->velocityX = 0;
                self->facingLeft ^= 1;
            }
            if (self->velocityX &&
                CheckColliderOffsets(wight_wall_death, self->facingLeft ^ 1)) {
                self->velocityX = 0;
                self->facingLeft ^= 1;
            }
            break;
        case 2:
            // the primitive's 26 frames
            if (ext->counter > 25) {
                self->step_s++;
            } else {
                ext->counter++;
            }
            break;
        case 3:
            e = AllocEntity(&g_Entities[224], &g_Entities[256]);
            if (e != NULL) {
                CreateEntityFromEntity(E_EXPLOSION, self, e);
                e->params = 3;
                e->posX.i.hi += self->facingLeft ? SAT_X(-32) : SAT_X(32);
                e->posY.i.hi += 32;
            }
            ext->counter = 30;
            self->step_s++;
            PlaySfxPositional(MOD_SFX_WIGHT_DEATH);
            break;
        case 4:
            if (--ext->counter <= 0) {
                self->step_s++;
            }
            break;
        case 5:
            DestroyEntity(self);
            return;
        }
        break;
    }

    // dust at the feet while it slides
    vx = self->velocityX < 0 ? -self->velocityX : self->velocityX;
    if (vx >= SAT_X(FIX(1)) && (g_Timer & 1)) {
        EntityExplosionVariantsSpawner(
            self, 1, 1, SAT_X(Random() & 7), 32, (Random() & 3) + 1, 0);
        EntityExplosionVariantsSpawner(
            self, 1, 1, SAT_X((Random() & 7) - 8), 32, (Random() & 3) + 1, 0);
    }
    // the palette shimmer (not over the PS1 hit flash)
    if (!(self->flags & 0xF)) {
        self->palette = wight_pal_cycle[((u64)g_Timer * 2 / 3 >> 2) & 3];
    }
}

// Saturn func_060E3244: the slash (self + 1) and its hitbox
void EntityModWightAttack(Entity* self) {
    Entity* parent = self - 1;
    Entity* e;
    u8 i;

    if (parent->entityId != E_MOD_WIGHT || parent->step == WIGHT_DEATH) {
        DestroyEntity(self);
        return;
    }
    if (!self->step) {
        InitializeEntity(g_EInitModWightAttack);
        self->attack /= 3;
        self->zPriority = parent->zPriority + 1;
    }
    self->posX.i.hi = parent->posX.i.hi;
    self->posY.i.hi = parent->posY.i.hi;
    self->facingLeft = parent->facingLeft;
    if (self->params) {
        AnimateEntity(wight_anim_slash, self);
        if (!(g_Timer & 1)) {
            e = AllocEntity(&g_Entities[160], &g_Entities[192]);
            if (e != NULL) {
                CreateEntityFromEntity(E_MOD_WIGHT_PART, self, e);
                e->facingLeft = self->facingLeft;
                e->animCurFrame = self->animCurFrame;
                e->zPriority = self->zPriority;
            }
        }
    }
    i = self->animCurFrame - 0x4D;
    if (i <= 23) {
        self->hitboxOffX = SAT_X(wight_slash_offsets[i][0]);
        self->hitboxOffY = -wight_slash_offsets[i][1];
        self->hitboxWidth = SAT_X(wight_slash_sizes[i][0]);
        self->hitboxHeight = wight_slash_sizes[i][1];
        self->hitboxState = parent->hitboxState;
    } else {
        self->hitboxState = 0;
    }
}

// Saturn func_060E3438: an afterimage of the slash that rises and fades
void EntityModWightPart(Entity* self) {
    ET_ModWightPart* ext = WIGHT_PART_EXT(self);
    s16 frame;

    switch (self->step) {
    case 0:
        frame = self->animCurFrame;
        InitializeEntity(g_EInitModWightPart);
        self->animCurFrame = frame;
        self->hitboxState = 0;
        self->drawFlags = ENTITY_OPACITY;
        self->opacity = 96;
        ext->timer = 0;
        self->zPriority -= 2;
        // fallthrough
    case 1:
        if (!(ext->timer & 3)) {
            self->zPriority--;
        }
        self->posX.val += self->facingLeft ? SAT_X(-0x4000) : SAT_X(0x4000);
        self->posY.val -= 0xC000;
        if (ext->timer <= 11) {
            self->opacity = 96 - ext->timer * 6;
        } else {
            self->step++;
        }
        if (ext->timer == 8) {
            self->drawFlags |= ENTITY_BLINK;
        }
        ext->timer++;
        break;
    default:
        DestroyEntity(self);
        break;
    }
}
