// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * The Efreet (Saturn RSTAGE15, Reverse Garden room 0), translated from the
 * SH-2 (Saturn port mod). All parts are ported:
 *   EntityModEfreet    Saturn 0x23, func 060E6878, enemy 402 (0x192)
 *   EntityModEfreet24  Saturn 0x24, func 060E76C4, parent self - 1
 *   EntityModEfreet25  Saturn 0x25, func 060E78B4, parent self - 2
 *   EntityModEfreet26  Saturn 0x26, trail, func 060E7AC4
 *   EntityModEfreet27  Saturn 0x27, projectile, funcs 060E7C3C / 060E7D60
 *                      (sprite bank 0x06046BF8, see the comment there)
 *   EntityModEfreet28  Saturn 0x28, flame shot with two G4 prims, func 060E7E60
 *   EntityModEfreet29  Saturn 0x29, flame particles (PRIM_TILE), func 060E823C
 *   EntityModEfreet2A  Saturn 0x2A, death rectangles (PRIM_G4), func 060E854C
 *   EntityModEfreet2B  Saturn 0x2B, death sparks (PRIM_TILE), func 060E88A0
 * Sprite: the converted Saturn bank (MOD_EFREET_ANIMSET), frames 1:1. The
 * Saturn draws the body at posY + dy (dy = -12 px, plus a sine bob up to
 * +/-8 px). The PS1 cannot offset the drawing: the body frames (0x0F-0x3E,
 * 0x42-0x4F) are converted 12 px up (rstage15_entities.py). The bob is not
 * drawn: it moves only the hitbox and the shot spawn points. The parts
 * (0x24, 0x25) and the trail follow the body without the bob.
 * Horizontal quantities use SAT_X (Saturn 320 px, PS1 256 px).
 *
 * Sound stand-ins. The Saturn ids 0x8F2..0x8F6 are enemy sounds, not Maria:
 *   0x8F2  death                 -> SFX_EXPLODE_A
 *   0x8F3  fire end              -> SFX_FIRE_SHOT
 *   0x8F4  spit                  -> SFX_FIREBALL_SHOT_B
 *   0x8F5  flame shot start      -> SFX_FIREBALL_SHOT_C
 *   0x8F6  fire wind-up          -> SFX_FIRE_DEMON_ATTACK_CHARGE
 *
 * Simplified or not ported:
 *   - the Saturn sprite setup (SUB_0600B344, SUB_0600B004, FUN_06079BB4) is
 *     dropped, and so is the h-flip bit (0x10 of the sprite flags);
 *   - the Saturn sprite anim 0x060F0840 (0x28 case 2) is not set;
 *   - the prim colours are stand-ins (the Saturn clut is not decoded);
 *   - the Saturn camera offset of the prims (g_Tilemap delta) is not ported:
 *     the prims ease toward their entity, so they catch up within a few frames.
 */
#include "rstage15.h"

// Saturn screen is 320 px wide, PS1 is 256: horizontal quantities * 4/5
#define SAT_X(v) ((v) * 4 / 5)

void SetStep(u8 step);
void CreateEntityFromCurrentEntity(u16 entityId, Entity* entity);

// The palette the Efreet uses. The Saturn changes its clut at run time
// (not decoded yet), so every use stays PAL_NONE until that is known.
#define EFREET_PAL(n) (n)

s32 UnkCollisionFunc2(s16* sensors);
void CreateEntityFromEntity(u16 entityId, Entity* source, Entity* entity);
void MoveEntity(void);

// Saturn enemy records (TekiInit ids)
#define EFREET_TEKI_MAIN 402 // 0x192
#define EFREET_TEKI_PART 0x2C

static EInit g_EInitModEfreet = {
    MOD_EFREET_ANIMSET, 0, 0, PAL_NONE, EFREET_TEKI_MAIN};
static EInit g_EInitModEfreetPart = {
    MOD_EFREET_ANIMSET, 0, 0, PAL_NONE, EFREET_TEKI_PART};
static EInit g_EInitModEfreetTrail = {MOD_EFREET_ANIMSET, 0, 0, PAL_NONE, 5};

// Frame tables: pairs (duration, frame). Duration 0 loops, 0xFF ends.
// Saturn addresses are in the comments. Frames are 1:1 with the converted bank.
/* Saturn 0x060F03DE, part 0x24 idle (loops) */
static u8 efr_anim_e24_idle[] = {
    0x05, 0x01, 0x05, 0x02, 0x05, 0x03, 0x05, 0x04, 0x00, 0x00,
};
/* Saturn 0x060F03E8, part 0x24 death */
static u8 efr_anim_e24_die[] = {
    0x08, 0x01, 0x08, 0x05, 0x08, 0x06, 0x08, 0x07, 0x00, 0x00,
};
/* Saturn 0x060F03F2, part 0x25 idle (loops) */
static u8 efr_anim_e25_idle[] = {
    0x05, 0x08, 0x05, 0x09, 0x05, 0x0A, 0x05, 0x0B, 0x00, 0x00,
};
/* Saturn 0x060F03FC, part 0x25 death */
static u8 efr_anim_e25_die[] = {
    0x08, 0x08, 0x08, 0x0C, 0x08, 0x0D, 0x08, 0x0E, 0x00, 0x00,
};
/* Saturn 0x060F0406, main idle (loops) */
static u8 efr_anim_idle[] = {
    0x1B, 0x0F, 0x0F, 0x10, 0x10, 0x11, 0x0F, 0x10, 0x00, 0x00,
};
/* Saturn 0x060F0410, main turn around */
static u8 efr_anim_turn[] = {
    0x08, 0x0F, 0x03, 0x16, 0x03, 0x17, 0x02, 0x18, 0x02, 0x19, 0x02,
    0x1A, 0x04, 0x1B, 0x02, 0x1C, 0x02, 0x1D, 0x08, 0x1E, 0xFF, 0xFF,
};
/* Saturn 0x060F0426, main step 3 substep 0 */
static u8 efr_anim_charge[] = {
    0x02, 0x0F, 0x02, 0x12, 0x02, 0x13, 0x00, 0x00,
};
/* Saturn 0x060F042E, main step 3 substep 2 */
static u8 efr_anim_jump[] = {
    0x02, 0x13, 0x02, 0x12, 0x02, 0x0F, 0xFF, 0xFF,
};
/* Saturn 0x060F0436, main step 5 substep 0 */
static u8 efr_anim_run_a[] = {
    0x02, 0x0F, 0x02, 0x14, 0x02, 0x15, 0xFF, 0xFF,
};
/* Saturn 0x060F043E, main step 5 substep 2 */
static u8 efr_anim_run_b[] = {
    0x02, 0x15, 0x02, 0x14, 0x02, 0x0F, 0xFF, 0xFF,
};
/* Saturn 0x060F0446, main step 8 substep 0 */
static u8 efr_anim_spit_a[] = {
    0x18, 0x0F, 0x06, 0x1F, 0x06, 0x20, 0x05, 0x21, 0xFF, 0xFF,
};
/* Saturn 0x060F0450, main step 8 substep 1 */
static u8 efr_anim_spit_b[] = {
    0x03, 0x22, 0x03, 0x23, 0x03, 0x24, 0x03, 0x25, 0x00, 0x00,
};
/* Saturn 0x060F045A, main step 8 substep 2 */
static u8 efr_anim_spit_c[] = {
    0x04, 0x22, 0x03, 0x26, 0x03, 0x27, 0x02, 0x28, 0x02, 0x29,
    0x02, 0x2A, 0x03, 0x2B, 0x04, 0x2C, 0x04, 0x2D, 0x0A, 0x2E,
    0x04, 0x2F, 0x04, 0x30, 0x04, 0x31, 0x37, 0x0F, 0xFF, 0xFF,
};
/* Saturn 0x060F0478, main step 6 substep 1 */
static u8 efr_anim_fire_a[] = {
    0x08, 0x0F, 0x08, 0x32, 0x08, 0x34, 0x40, 0x33, 0x08, 0x34, 0x04,
    0x35, 0x03, 0x36, 0x03, 0x37, 0x03, 0x38, 0x03, 0x39, 0xFF, 0xFF,
};
/* Saturn 0x060F048E, main step 6 substep 2 */
static u8 efr_anim_fire_b[] = {
    0x03, 0x3A, 0x03, 0x3B, 0x00, 0x00,
};
/* Saturn 0x060F0494, main step 6 substep 3 */
static u8 efr_anim_fire_c[] = {
    0x03, 0x3C, 0x03, 0x3D, 0x03, 0x3E, 0x03,
    0x36, 0x06, 0x35, 0x18, 0x0F, 0xFF, 0xFF,
};
/* Saturn 0x060F04A2, part 0x28 step 1 (flame shot) */
static u8 efr_anim_e28_fly[] = {
    0x03, 0x3F, 0x03, 0x40, 0x03, 0x41, 0x00, 0x00,
};
/* Saturn 0x060F04AA, main step 4 (lunge) */
static u8 efr_anim_step4[] = {
    0x18, 0x0F, 0x04, 0x1F, 0x04, 0x20, 0x04, 0x47, 0x03, 0x48, 0x03, 0x49,
    0x03, 0x48, 0x03, 0x49, 0x03, 0x4A, 0x02, 0x4B, 0x03, 0x4C, 0x04, 0x4D,
    0x03, 0x4E, 0x04, 0x4F, 0x04, 0x30, 0x04, 0x31, 0x04, 0x0F, 0xFF, 0xFF,
};
/* Saturn 0x060F04CE, main death step 0x14 substep 2 */
static u8 efr_anim_death[] = {
    0x10, 0x0F, 0x05, 0x32, 0x0F, 0x42, 0x02, 0x43, 0x02, 0x42,
    0x02, 0x43, 0x02, 0x44, 0x02, 0x43, 0x2F, 0x44, 0xFF, 0xFF,
};
/* Saturn 0x060F0774, part 0x27 update */
static u8 efr_anim_e27_fly[] = {
    0x03, 0x01, 0x03, 0x02, 0x03, 0x03, 0x03, 0x04, 0x03, 0x05,
    0x03, 0x06, 0x03, 0x07, 0x03, 0x08, 0x03, 0x09, 0x03, 0x0A,
    0x03, 0x0B, 0x03, 0x0C, 0x03, 0x0D, 0xFF, 0x00,
};

/* Saturn 0x060F0534: {w, h, offX, offY} per frame (offX/offY signed) */
static s8 efr_hitbox[81][4] = {
    {6, 14, -120, -96}, {8, 20, -14, -2}, {8, 20, -14, -2}, {8, 20, -14, -2},
    {8, 20, -14, -2},   {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {4, 4, 0, -6},      {4, 4, 0, -6},    {4, 4, 0, -6},    {4, 4, 0, -6},
    {0, 0, 0, 0},       {0, 0, 0, 0},     {0, 0, 0, 0},     {6, 24, 0, 6},
    {6, 24, 0, 6},      {6, 24, 0, 6},    {8, 22, 8, 6},    {8, 22, 8, 6},
    {6, 24, 2, 6},      {6, 24, -2, 6},   {6, 24, 0, 6},    {10, 22, 0, 6},
    {12, 20, 0, 6},     {18, 18, 0, 6},   {18, 18, 0, 6},   {12, 20, 0, 6},
    {10, 22, 0, 6},     {6, 24, 0, 6},    {6, 24, 0, 6},    {6, 24, 0, 6},
    {6, 24, 0, 6},      {6, 24, 0, 6},    {6, 24, 0, 6},    {6, 24, 0, 6},
    {6, 24, 0, 6},      {6, 24, 0, 6},    {6, 24, 0, 6},    {6, 24, 0, 6},
    {6, 24, 6, 6},      {6, 24, 8, 6},    {10, 24, 8, 6},   {10, 24, 8, 6},
    {10, 24, 8, 6},     {10, 24, 8, 6},   {10, 24, 8, 6},   {10, 24, 8, 6},
    {8, 24, 8, 6},      {8, 24, 0, 6},    {8, 24, -2, 6},   {10, 24, -2, 6},
    {10, 24, -2, 6},    {10, 22, 0, 6},   {12, 18, 10, 6},  {16, 18, 26, 6},
    {16, 18, 26, 6},    {16, 18, 26, 6},  {16, 18, 26, 6},  {16, 18, 26, 6},
    {16, 18, 26, 6},    {16, 18, 26, 6},  {16, 18, 26, 6},  {6, 14, -4, 0},
    {6, 14, -4, 0},     {6, 14, -4, 0},   {0, 0, 0, 0},     {0, 0, 0, 0},
    {0, 0, 0, 0},       {0, 0, 0, 0},     {0, 0, 0, 0},     {12, 24, 0, 0},
    {16, 24, 0, 0},     {20, 24, 0, 0},   {12, 28, 0, 0},   {12, 24, 8, 6},
    {32, 12, 20, 0},    {34, 12, 22, 0},  {20, 16, 8, 6},   {17, 18, 8, 6},
    {26, 12, 16, 4},
};

/* Saturn 0x060F0683, indexed by the parent frame */
static s8 efr_child_x24[81] = {
    0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 3, 6, 9, 12, 12, 9, 6, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7,
    0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
/* Saturn 0x060F06D3, indexed by the parent frame */
static s8 efr_child_x25[81] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 1,
    1,  14, 13, 3,  -1, 0,  0,  0,  1,  0,  0,  2,  2,  2,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  16, 16, 17, 18, 18, 17, 17, 17, 11, 2, -8,
    -9, -8, 3,  15, 37, 37, 37, 37, 37, 37, 37, 37, 0,  0,  0,  0, 0,
    0,  0,  0,  0,  0,  0,  0,  16, 17, 17, 17, 17, 17,
};
/* Saturn 0x060F0723, indexed by the parent frame */
static s8 efr_child_y25[81] = {
    17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  1,  1,  0,
    1,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  2,  2,
    2,  2, 2, 2, 2, 2, 3, 1, 2, 0, 1, 0, 6, 15, 15, 15, 15, 15, 15, 15, 15,
    0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2,  2,  2,  2,  2,
};

// Main step ids (from the Saturn jump table at 060E690C; partly verified)
enum {
    EFREET_STEP_IDLE = 1,
    EFREET_STEP_APPROACH = 2,
    EFREET_STEP_CHARGE = 3,
    EFREET_STEP_LUNGE = 4,
    EFREET_STEP_RUN = 5,
    EFREET_STEP_FIRE = 6,
    EFREET_STEP_SPIT = 8,
    EFREET_STEP_TURN = 0x10,
    EFREET_STEP_HIT = 0x12,
    EFREET_STEP_DEATH = 0x14,
};

// Ext fields at the Saturn offsets minus 0x78 (the Saturn ext base).
typedef struct {
    u8 _00[0x0C];
    s16 timer; // Saturn 0x84: step timer
    u8 _0E[2];
    s16 count; // Saturn 0x88: step counter (field_0x88)
    u8 _12[0x06];
    s16 counter90; // Saturn 0x90: the flame shot counter (0x28)
    u8 _1A[0x02];
    s16 sway; // Saturn 0x94: bob phase
    u8 _1E[2];
    s32 spriteY; // Saturn 0x98: sprite Y (posY + bob - 12 px)
    s32 counter; // Saturn 0x9C: counter of the trail (0x26) and projectile
                 // (0x27)
    s16 scale;   // Saturn spr+0x0A: sprite scale of the projectile (0x27)
    u8 _2A[0x02];
    s16 partCounter; // Saturn 0xA4: frame counter of the flame particles (0x29)
    u8 _2E[0x0E];
    s16 lastHitBy; // Saturn 0xB4: index of the child that last hit (g_Entities)
} EfreetExt;
#define EFREET_EXT(e) ((EfreetExt*)&(e)->ext)

// Prim rectangles. A G4 prim has four vertices: 0 = left/top, 1 = right/top,
// 2 = left/bottom, 3 = right/bottom. The Saturn rectangle keeps this layout.
static void EfreetRectX(Primitive* p, s16 l, s16 r) {
    p->x0 = l;
    p->x2 = l;
    p->x1 = r;
    p->x3 = r;
}

static void EfreetRectY(Primitive* p, s16 t, s16 b) {
    p->y0 = t;
    p->y1 = t;
    p->y2 = b;
    p->y3 = b;
}

// Stand-in colour (the Saturn clut is not decoded).
static void EfreetSetColor(Primitive* p, u8 r, u8 g, u8 b) {
    p->r0 = p->r1 = p->r2 = p->r3 = r;
    p->g0 = p->g1 = p->g2 = p->g3 = g;
    p->b0 = p->b1 = p->b2 = p->b3 = b;
}

// Saturn 060E8582 (the death shrink of 0x2A): both prims sink 0x18 and the
// first narrows by 1 on each side. Returns 1 when prim 0 has no width left.
static s32 EfreetRectShrink(Primitive* p0, Primitive* p1) {
    p0->y0 -= 0x18;
    p0->y1 -= 0x18;
    p0->y2 -= 0x18;
    p0->y3 -= 0x18;
    p1->y0 -= 0x18;
    p1->y1 -= 0x18;
    p1->y2 -= 0x18;
    p1->y3 -= 0x18;
    if (p0->x1 <= p0->x0) {
        return 1;
    }
    EfreetRectX(p0, p0->x0 + 1, p0->x1 - 1);
    EfreetRectX(p1, p1->x0 + 1, p1->x1 - 1);
    return 0;
}

// Sensors (Saturn 060F03C6): feet 32 px below, wall 8 px out
static s16 efr_sensors[] = {0, 32, SAT_X(8), 0};

// Saturn enemy sounds with PS1 enemy stand-ins (see the header).
#define EFR_SFX_8F2 SFX_EXPLODE_A
#define EFR_SFX_8F3 SFX_FIRE_SHOT
#define EFR_SFX_8F4 SFX_FIREBALL_SHOT_B
#define EFR_SFX_8F5 SFX_FIREBALL_SHOT_C
#define EFR_SFX_8F6 SFX_FIRE_DEMON_ATTACK_CHARGE

// Saturn 060F03C6 sensors, the walk / fall check (FUN_06079DEC)
static void EfreetSensors(void) { UnkCollisionFunc2(efr_sensors); }

// Saturn velocity halving: v = (v + (v < 0)) >> 1
static void EfreetHalveVelX(Entity* self) {
    self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
}

// Saturn FUN_060E7554: the hitbox from the frame, the sprite Y (bob), the
// trail spawn. The sprite offset is not applied here (see the header).
static void EfreetEpilogue(Entity* self) {
    EfreetExt* ext = EFREET_EXT(self);
    const s8* hb = efr_hitbox[self->animCurFrame];
    s32 sprY;

    self->hitboxWidth = SAT_X(hb[0]);
    self->hitboxHeight = hb[1];
    self->hitboxOffX = SAT_X(-hb[2]);
    self->hitboxOffY = hb[3];

    sprY = self->posY.val + rsin((s16)(ext->sway << 5)) * 0x80 - 0xC0000;
    ext->spriteY = sprY;
    if (self->step != EFREET_STEP_FIRE && self->step != EFREET_STEP_DEATH) {
        ext->sway++;
    }
    self->hitboxOffY += (sprY - self->posY.val) >> 16;

    if (self->step != EFREET_STEP_DEATH && (g_Timer % 6) == 0) {
        Entity* e = AllocEntity(&g_Entities[160], &g_Entities[192]);
        if (e != NULL) {
            DestroyEntity(e);
            CreateEntityFromEntity(E_MOD_EFREET_26, self, e);
            e->facingLeft = self->facingLeft;
            e->animCurFrame = self->animCurFrame;
            e->parent = self; // Saturn ext+0x28 (parent pointer)
            // Saturn: sprY; the body frames are converted 12 px up and the
            // body does not bob (see the header)
            e->posY.val = self->posY.val;
        }
    }
}

// The death column (0x2A) that case 3 of the death made: next step. None
// when AllocEntity failed (lastHitBy -1) or the slot was reused.
static void EfreetStepColumn(EfreetExt* ext) {
    Entity* e;

    if (ext->lastHitBy < 0) {
        return;
    }
    e = &g_Entities[ext->lastHitBy];
    if (e->entityId == E_MOD_EFREET_2A) {
        e->step++;
    }
}

void EntityModEfreet(Entity* self) {
    EfreetExt* ext = EFREET_EXT(self);
    Entity* e;
    s16 dist;
    s16 next;
    u8 r;

    // Saturn 060E6878, the main body. Sprite setup (SUB_0600B344,
    // SUB_0600B004, FUN_06079BB4) is dropped; the h-flip is dropped.
    if (self->hitParams != 0 && (self->step & 1)) {
        self->pose = 0;
        self->poseTimer = 0;
        self->step = EFREET_STEP_HIT;
        self->step_s = 0;
    }
    if ((self->flags & FLAG_DEAD) && self->step != EFREET_STEP_DEATH) {
        self->pose = 0;
        self->poseTimer = 0;
        self->step = EFREET_STEP_DEATH;
        self->step_s = 0;
    }

    switch (self->step) {
    case 0: // init
        InitializeEntity(g_EInitModEfreet);
        self->drawFlags = ENTITY_OPACITY | ENTITY_MASK_B; // Saturn 0x48
        self->opacity = 0x80;
        DestroyEntity(self + 1);
        CreateEntityFromCurrentEntity(E_MOD_EFREET_24, self + 1);
        DestroyEntity(self + 2);
        CreateEntityFromCurrentEntity(E_MOD_EFREET_25, self + 2);
        self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        self->step = EFREET_STEP_IDLE;
        break;

    case EFREET_STEP_IDLE:
        dist = GetDistanceToPlayerX();
        next = EFREET_STEP_IDLE;
        if (self->facingLeft == ((GetSideToPlayer() & 1) ^ 1)) {
            if (dist < SAT_X(0x20)) {
                next = EFREET_STEP_RUN;
            } else if (dist < SAT_X(0x40)) {
                next =
                    (Random() & 1) == 0 ? EFREET_STEP_LUNGE : EFREET_STEP_RUN;
            } else if (dist < SAT_X(0xA0)) {
                r = (Random() & 0xFF) % 60;
                if (r < 2) {
                    next = EFREET_STEP_APPROACH;
                } else if (r == 2) {
                    next = EFREET_STEP_RUN;
                }
            } else {
                r = (Random() & 0xFF) % 30;
                if (r == 0) {
                    next = EFREET_STEP_CHARGE;
                }
            }
        } else {
            next = EFREET_STEP_TURN;
        }
        self->step = next;
        if (self->step == EFREET_STEP_IDLE) {
            AnimateEntity(efr_anim_idle, self);
            EfreetEpilogue(self);
            return;
        }
        self->pose = 0;
        self->poseTimer = 0;
        self->step_s = 0;
        EfreetEpilogue(self);
        return;

    case EFREET_STEP_APPROACH: // step 2: pick the attack by distance
        dist = GetDistanceToPlayerX();
        if (dist < SAT_X(0x40)) {
            self->step = EFREET_STEP_LUNGE;
        } else if (dist < SAT_X(0x81)) {
            self->step = EFREET_STEP_FIRE;
        } else {
            self->step = EFREET_STEP_SPIT;
        }
        break;

    case EFREET_STEP_CHARGE: // step 3, jump at the player
        switch (self->step_s) {
        case 0:
            if (AnimateEntity(efr_anim_charge, self) == 0) {
                self->animCurFrame = 0x13;
                self->velocityX =
                    self->facingLeft == 0 ? -SAT_X(0x40000) : SAT_X(0x40000);
                ext->timer = (Random() & 0xFF) % 15 + 0x2D;
                self->step_s++;
            }
            break;
        case 1:
            dist = GetDistanceToPlayerX();
            if (dist < SAT_X(0xA1)) {
                EfreetHalveVelX(self);
                self->step_s++;
            } else {
                ext->timer--;
                if (ext->timer == 0) {
                    EfreetHalveVelX(self);
                    self->step_s++;
                }
            }
            break;
        case 2:
            if (AnimateEntity(efr_anim_jump, self) == 0) {
                EfreetHalveVelX(self);
                ext->timer = 0x14;
                self->step_s++;
            }
            break;
        case 3:
            ext->timer--;
            if (ext->timer == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->velocityX = 0;
                self->step = EFREET_STEP_IDLE;
                self->step_s = 0;
            }
            break;
        }
        EfreetSensors();
        break;

    case EFREET_STEP_LUNGE: // step 4
        if (self->animCurFrame == 0x4C) {
            self->velocityX =
                self->facingLeft == 0 ? -SAT_X(0x40000) : SAT_X(0x40000);
        }
        EfreetHalveVelX(self);
        if (AnimateEntity(efr_anim_step4, self) == 0) {
            self->pose = 0;
            self->poseTimer = 0;
            self->velocityX = 0;
            self->step = EFREET_STEP_IDLE;
            break;
        }
        EfreetSensors();
        break;

    case EFREET_STEP_RUN: // step 5, and the hit recovery 0x12
    case EFREET_STEP_HIT:
        switch (self->step_s) {
        case 0:
            if (AnimateEntity(efr_anim_run_a, self) == 0) {
                self->animCurFrame = 0x15;
                self->velocityX =
                    self->facingLeft == 0 ? SAT_X(0x30000) : -SAT_X(0x30000);
                ext->timer = (Random() & 0xFF) % 15 + 0xF;
                if (self->step == EFREET_STEP_HIT) {
                    ext->timer = (ext->timer * 2) / 3;
                }
                self->step_s++;
            }
            break;
        case 1:
            ext->timer--;
            if (ext->timer == 0) {
                EfreetHalveVelX(self);
                self->step_s++;
            }
            break;
        case 2:
            if (AnimateEntity(efr_anim_run_b, self) == 0) {
                EfreetHalveVelX(self);
                ext->timer = 0xF;
                self->step_s++;
            }
            break;
        case 3:
            ext->timer--;
            if (ext->timer == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->velocityX = 0;
                self->step = EFREET_STEP_IDLE;
                self->step_s = 0;
            }
            break;
        }
        EfreetSensors();
        break;

    case EFREET_STEP_FIRE: // step 6, the fire
        switch (self->step_s) {
        case 0:
            ext->count = 0;
            self->step_s++;
            PlaySfxPositional(EFR_SFX_8F6);
            // falls into the fire wind-up (Saturn code shares it)
        case 1:
            if (ext->count < 0x20 && (ext->count & 3) == 0) {
                e = AllocEntity(&g_Entities[224], &g_Entities[256]);
                if (e != NULL) {
                    CreateEntityFromEntity(E_MOD_EFREET_29, self, e);
                    e->posX.i.hi +=
                        self->facingLeft == 0 ? SAT_X(4) : -SAT_X(4);
                    e->posY.val = ext->spriteY - 0x120000;
                    e->facingLeft = self->facingLeft;
                    e->parent = self;
                    e->params = (ext->count >> 3) & 1;
                }
            }
            ext->count++;
            if (AnimateEntity(efr_anim_fire_a, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                PlaySfxPositional(EFR_SFX_8F3);
                self->step_s++;
                ext->count = 0x40;
            }
            break;
        case 2:
            if (ext->count != 0) {
                ext->count--;
                AnimateEntity(efr_anim_fire_b, self);
                if ((g_Timer & 1) != 0) {
                    e = AllocEntity(&g_Entities[160], &g_Entities[192]);
                    if (e != NULL) {
                        CreateEntityFromEntity(E_MOD_EFREET_27, self, e);
                        e->posX.i.hi +=
                            self->facingLeft == 0 ? SAT_X(-0x30) : SAT_X(0x30);
                        e->posY.val = ext->spriteY - 0x20000;
                        e->facingLeft = self->facingLeft;
                        *(s32*)&e->ext = ext->count; // Saturn 0x78: the counter
                        EfreetEpilogue(self);
                        return;
                    }
                }
                break;
            }
            self->pose = 0;
            self->poseTimer = 0;
            self->step_s++;
            break;
        case 3:
            if (AnimateEntity(efr_anim_fire_c, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s = 0;
                self->step = EFREET_STEP_IDLE;
            }
            break;
        }
        break;

    case EFREET_STEP_SPIT: // step 8, the spit
        switch (self->step_s) {
        case 0:
            if (AnimateEntity(efr_anim_spit_a, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s++;
                ext->count = 0x3C;
            }
            break;
        case 1:
            ext->count--;
            if (ext->count == 0) {
                do {
                    self->pose = 0;
                    self->poseTimer = 0;
                    self->step_s++;
                    ext->count--;
                } while (ext->count == 0);
            }
            AnimateEntity(efr_anim_spit_b, self);
            break;
        case 2:
            if (AnimateEntity(efr_anim_spit_c, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s = 0;
                self->step = EFREET_STEP_IDLE;
            }
            if (self->animCurFrame == 0x2A && (g_Timer & 1) != 0) {
                e = AllocEntity(&g_Entities[160], &g_Entities[192]);
                if (e != NULL) {
                    CreateEntityFromEntity(E_MOD_EFREET_28, self, e);
                    e->posX.i.hi +=
                        self->facingLeft == 0 ? -SAT_X(0x3C) : SAT_X(0x3C);
                    e->posY.val = ext->spriteY - 0x40000;
                    e->facingLeft = self->facingLeft;
                    PlaySfxPositional(EFR_SFX_8F4);
                }
            }
            break;
        }
        break;

    case EFREET_STEP_TURN: // step 0x10
        if (AnimateEntity(efr_anim_turn, self) != 0) {
            if (self->animCurFrame == 0x19 && self->step_s == 0) {
                self->step_s = 1;
                self->facingLeft = self->facingLeft ^ 1;
            }
            EfreetEpilogue(self);
            return;
        }
        self->pose = 0;
        self->poseTimer = 0;
        self->step_s = 0;
        self->step = EFREET_STEP_IDLE;
        self->animCurFrame = 0xF;
        break;

    case EFREET_STEP_DEATH: // step 0x14
        switch (self->step_s) {
        case 0:
            self->hitboxState = 0;
            self->velocityX = 0;
            self->velocityY = 0;
            ext->count = 10;
            self->step_s++;
            break;
        case 1:
            ext->count--;
            if (ext->count > 0) {
                break;
            }
            ext->count = 0;
            self->step_s++;
            break;
        case 2:
            if (AnimateEntity(efr_anim_death, self) == 0) {
                self->step_s++;
                ext->count = 0;
                PlaySfxPositional(EFR_SFX_8F2);
            }
            ext->count++;
            if (ext->count == 0x30) {
                (self + 1)->step++;
                (self + 2)->step++;
                EfreetEpilogue(self);
                return;
            }
            break;
        case 3:
            e = AllocEntity(&g_Entities[224], &g_Entities[256]);
            if (e != NULL) {
                CreateEntityFromEntity(E_MOD_EFREET_2A, self, e);
                e->posY.i.hi += 0x20;
                e->posX.i.hi += self->facingLeft == 0 ? SAT_X(6) : SAT_X(2);
                e->zPriority = self->zPriority;
                ext->lastHitBy = (s16)(e - g_Entities);
            } else {
                ext->lastHitBy = -1; // Saturn stores NULL here
            }
            ext->count = 0x1E;
            self->step_s++;
            break;
        case 4:
            ext->count--;
            if (ext->count < 1) {
                self->step_s++;
                ext->count = 0x40;
                EfreetStepColumn(ext);
                self->velocityY = -0x8000;
                self->velocityX = 0;
                EfreetEpilogue(self);
                return;
            }
            break;
        case 5:
            ext->count--;
            if (ext->count < 9) {
                self->step_s++;
                EfreetStepColumn(ext);
                ext->count = 0xE;
                self->velocityY *= 2;
                self->animCurFrame = 0;
            }
            MoveEntity();
            break;
        case 6:
            ext->count--;
            if (ext->count < 1) {
                self->step_s++;
                ext->count = 0;
            }
            MoveEntity();
            break;
        case 7:
            e = AllocEntity(&g_Entities[224], &g_Entities[256]);
            if (e != NULL) {
                CreateEntityFromEntity(E_MOD_EFREET_2B, self, e);
                e->posX.i.hi += self->facingLeft == 0 ? SAT_X(6) : 0;
                e->zPriority = self->zPriority;
            }
            DestroyEntity(self);
            return;
        }
        break;

    default:
        break;
    }

    EfreetEpilogue(self);
}

// Child hitbox from its own frame (Saturn 060E77AC). Horizontal values use
// SAT_X, as in the main body.
static void EfreetChildHitbox(Entity* self) {
    const s8* hb = efr_hitbox[self->animCurFrame];

    self->hitboxWidth = SAT_X(hb[0]);
    self->hitboxHeight = hb[1];
    self->hitboxOffX = SAT_X(-hb[2]);
    self->hitboxOffY = hb[3];
}

// Part 0x24 (Saturn 060E76C4): fixed child, parent is self - 1. Idle anim
// 03DE, death 03E8. Its Y is the parent sprite Y (parent ext spriteY).
void EntityModEfreet24(Entity* self) {
    Entity* parent = self - 1;

    if (parent->entityId != E_MOD_EFREET) {
        DestroyEntity(self);
        return;
    }
    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitModEfreetPart); // TekiInit 0x2C
        self->zPriority = parent->zPriority - 1;
        self->step = 1;
        self->flags =
            (self->flags | 0x2000) & 0x7FFFFFFF; // Saturn mask 7FFFFFFF
        self->attack = E_MOD_EFREET;             // Saturn 0x23
        // falls into the idle
    case 1:
        AnimateEntity(efr_anim_e24_idle, self);
        break;
    case 2:
        self->pose = 0;
        self->poseTimer = 0;
        self->step++;
        // falls into the death
    case 3:
        if (AnimateEntity(efr_anim_e24_die, self) == 0) {
            DestroyEntity(self);
            return;
        }
        break;
    }

    // Saturn sync (060E77AC)
    EfreetChildHitbox(self);
    self->facingLeft = parent->facingLeft;
    self->hitboxState = parent->hitboxState;
    self->posX.i.hi = parent->posX.i.hi;
    self->posY.val =
        parent->posY.val - 0xC0000; // Saturn: spriteY (no bob, see the header)
    if (self->facingLeft == 0) {
        self->posX.i.hi -= efr_child_x24[parent->animCurFrame];
    } else {
        self->posX.i.hi += efr_child_x24[parent->animCurFrame];
    }
}

// Part 0x25 (Saturn 060E78B4): fixed child, parent is self - 2. Idle anim
// 03F2, death 03FC. Its Y is the parent sprite Y minus 0x150000.
void EntityModEfreet25(Entity* self) {
    Entity* parent = self - 2;

    if (parent->entityId != E_MOD_EFREET) {
        DestroyEntity(self);
        return;
    }
    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitModEfreetPart); // TekiInit 0x2C
        self->flags = (self->flags & 0x7FFFFFFF) | 0x2000;
        self->zPriority = parent->zPriority - 1;
        self->step = 1;
        // falls into the idle
    case 1:
        AnimateEntity(efr_anim_e25_idle, self);
        break;
    case 2:
        self->pose = 0;
        self->poseTimer = 0;
        self->step++;
        // falls into the death
    case 3:
        if (AnimateEntity(efr_anim_e25_die, self) == 0) {
            DestroyEntity(self);
            return;
        }
        break;
    }

    // Saturn sync after the switch (060E78B4)
    self->facingLeft = parent->facingLeft;
    self->hitboxState = parent->hitboxState;
    self->posX.i.hi = parent->posX.i.hi;
    self->posY.val =
        parent->posY.val - 0xC0000 - 0x150000; // Saturn: spriteY (no bob)
    if (self->facingLeft == 0) {
        self->posX.i.hi =
            self->posX.i.hi + 1 - efr_child_x25[parent->animCurFrame];
    } else {
        self->posX.i.hi =
            self->posX.i.hi - 1 + efr_child_x25[parent->animCurFrame];
    }
    self->posY.i.hi += efr_child_y25[parent->animCurFrame];
    EfreetChildHitbox(self);
}

// Trail (Saturn 0x26, func 060E7AC4): a fading copy of the body. Spawned
// every 6 frames by the main body. Enemy 5.
void EntityModEfreet26(Entity* self) {
    EfreetExt* ext = EFREET_EXT(self);

    if (self->step == 0) {
        InitializeEntity(g_EInitModEfreetTrail);
        self->step = 1;
        self->drawFlags = ENTITY_OPACITY | ENTITY_MASK_R; // Saturn 0x18
        self->opacity = 0x60;
        ext->counter = 0;
        self->zPriority = self->parent->zPriority - 2;
        return;
    }
    if (self->step == 1) {
        ext->counter++;
        if ((ext->counter & 3) == 0) {
            self->zPriority--;
        }
        if (ext->counter < 0x19) {
            self->opacity = (u8)(0x60 - ext->counter * 4);
        } else {
            self->step++;
        }
        // Saturn at counter 8 sets a sprite flag 0x80 (not ported)
        if (ext->counter == 0x10) {
            self->drawFlags |= ENTITY_BLINK;
        }
        return;
    }
    if (self->step == 2) {
        DestroyEntity(self);
    }
}

// Projectile (Saturn 0x27, funcs 060E7C3C and 060E7D60). Sprite bank
// 0x06046BF8 (a common bank, not the Efreet bank; MOD_EFREET_PUFF_ANIMSET);
// the frames 0x01..0x0D with a 3-frame duration.
void EntityModEfreet27(Entity* self) {
    EfreetExt* ext = EFREET_EXT(self);
    s8 v;

    if (self->step == 0) {
        InitializeEntity(g_EInitModEfreetPart); // TekiInit 0x2C
        self->animSet = MOD_EFREET_PUFF_ANIMSET;
        self->step = 1;
        // Saturn flags |= 0x2000 (not ported)
        self->pose = 0;
        self->poseTimer = 0;
        self->drawFlags = ENTITY_OPACITY; // Saturn 8
        self->opacity = 0x80;
        ext->counter = Random() & 3;
        self->velocityY =
            *(s32*)&self->ext * -0x1000 + 0x30000 + (Random() & 0x3FFF);
        self->velocityX =
            self->facingLeft == 0 ? -SAT_X(0x40000) : SAT_X(0x40000);
        // Saturn sprite clut += 0x10: palette 1 of the bank, set in the
        // conversion (rstage15_entities.py)
        ext->scale = 0x10;
        self->hitboxWidth = 0;
        self->hitboxHeight = 0;
        v = (s8)rsin((s16)(g_Timer << 7));
        self->opacity = (u8)(v - 0x40);
        return;
    }

    self->posX.val -= SAT_X(0x1800);
    MoveEntity();
    ext->scale += 3;
    if (ext->counter < 0x20) {
        self->hitboxWidth = SAT_X(ext->scale / 5);
        self->hitboxHeight = ext->scale / 5;
    }
    if (AnimateEntity(efr_anim_e27_fly, self) == 0) {
        DestroyEntity(self);
        return;
    }
    ext->counter++;
    if (ext->counter == 0x20) {
        self->hitboxWidth = 0;
        self->hitboxHeight = 0;
        self->hitboxState = 0;
    }
}

// Flame shot (Saturn 0x28, func 060E7E60). Two G4 prims form the flame.
// The hitbox and the timing are kept.
void EntityModEfreet28(Entity* self) {
    EfreetExt* ext = EFREET_EXT(self);

    if (self->step == 0) {
        InitializeEntity(g_EInitModEfreetPart); // TekiInit 0x2C
        self->attack *= 2;
        self->step = 1;
        self->pose = 0;
        self->poseTimer = 0;
        self->velocityX =
            self->facingLeft == 0 ? -SAT_X(0x60000) : SAT_X(0x60000);
        self->hitboxWidth = SAT_X(5);
        self->hitboxHeight = 0xC;
        ext->counter90 = 0; // Saturn: not set here, zero assumed
        return;
    }
    if (self->step == 1) {
        AnimateEntity(efr_anim_e28_fly, self);
        MoveEntity();
        if (GetDistanceToPlayerX() > SAT_X(7)) {
            return;
        }
        self->step = 2;
        return;
    }
    if (self->step == 2) {
        // Saturn case 2: two prims for the flame halves, sound 0x8F5
        Primitive* p0;
        Primitive* p1;
        s32 primIdx = g_api.AllocPrimitives(PRIM_G4, 2);

        if (primIdx == -1) {
            DestroyEntity(self); // Saturn caseD_4 on allocation failure
            return;
        }
        self->flags |= FLAG_HAS_PRIMS | FLAG_UNK_2000;
        self->primIndex = primIdx;
        p0 = &g_PrimBuf[primIdx];
        p1 = p0->next;
        EfreetSetColor(p0, 0xE0, 0x60, 0x20); // upper half, stand-in colour
        EfreetSetColor(p1, 0xFF, 0xC0, 0x40); // lower half, stand-in colour
        p0->drawMode = DRAW_TRANSP;
        p1->drawMode = DRAW_TRANSP;
        p0->priority = self->zPriority;
        p1->priority = self->zPriority;
        ext->counter90 = 0;
        self->animCurFrame = 0; // Saturn also sets anim 060F0840 (not ported)
        self->step = 3;
        PlaySfxPositional(EFR_SFX_8F5);
    }
    if (self->step == 3) {
        // Saturn case 3: the prims follow the counter. The width is the low
        // byte of the counter in Saturn (field_0x91); here SAT_X of it.
        Primitive* p0 = &g_PrimBuf[self->primIndex];
        Primitive* p1 = p0->next;
        s16 c = ext->counter90;
        s16 px = self->posX.i.hi;
        s16 py = self->posY.i.hi;
        s16 k;

        if (c < 0x11) {
            EfreetRectX(p0, px - c, px + c);
            EfreetRectX(p1, px - c, px + c);
            self->hitboxWidth = SAT_X(c);
        }
        if (c < 0x20) {
            EfreetRectY(p0, py - c * 8, py);
            EfreetRectY(p1, py + c * 8, py);
            self->hitboxHeight = (u8)(c * 7);
        } else if (c == 0x20) {
            self->hitboxWidth = 0;
            self->hitboxHeight = 0;
            self->hitboxState = 0;
        } else {
            k = 0x28 - c;
            EfreetRectX(p0, px - k * 2, px + k * 2);
            EfreetRectX(p1, px - k * 2, px + k * 2);
        }
        ext->counter90++;
        if (ext->counter90 < 0x28) {
            return;
        }
        self->step = 4;
        return;
    }
    if (self->step == 4) {
        DestroyEntity(self);
    }
}

// Part 0x29 (Saturn 060E823C): flame particles of the fire attack. The
// parent is self->parent (the Efreet). Sprite prims (PRIM_TILE, FakePrim
// layout). Fixed point values are kept in the float slots (exact below
// 2^24). Stand-ins: sprite size 2x2 and the colour ramp.
void EntityModEfreet29(Entity* self) {
    Entity* parent = self->parent;
    EfreetExt* ext = EFREET_EXT(self);
    FakePrim* fp;
    s32 primIdx;
    s32 fx;
    s32 fy;
    s32 green;

    if ((parent->flags & 0x100) || parent->entityId != E_MOD_EFREET) {
        DestroyEntity(self); // Saturn FUN_060E850E
        return;
    }

    if (self->step == 0) {
        primIdx = g_api.AllocPrimitives(PRIM_TILE, 10);
        if (primIdx == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS | FLAG_UNK_2000;
        self->primIndex = primIdx;
        for (fp = (FakePrim*)&g_PrimBuf[primIdx]; fp != NULL;
             fp = (FakePrim*)fp->next) {
            fp->priority = (u16)(parent->zPriority - 2);
            if (parent->facingLeft == 0) {
                fx = self->posX.i.hi - ((Random() & 0xFF) >> 1) - 8;
            } else {
                fx = self->posX.i.hi + ((Random() & 0xFF) >> 1) + 8;
            }
            fp->posX.val = fx * 0x10000;
            fy = self->posY.i.hi + ((Random() & 0xFF) >> 1) - 0x40;
            fp->posY.val = fy * 0x10000;
            fp->drawMode = 0;
            fp->w = 2; // stand-in size
            fp->h = 2;
        }
        self->posX.i.hi -= 0x10;
        self->velocityX = 0x4000;
        ext->partCounter = 0;
        self->step = 1;
    } else if (self->step == 2) {
        DestroyEntity(self); // Saturn FUN_060E850E
        return;
    } else if (self->step > 2) {
        return; // Saturn FUN_060E8518: no update
    }

    self->posX.val = parent->posX.val;
    self->posY.val = EFREET_EXT(parent)->spriteY - 0x120000;
    MoveEntity();

    green = 0xC0 - (ext->partCounter >> 1) * 2;
    if (green < 0) {
        green = 0;
    }
    for (fp = (FakePrim*)&g_PrimBuf[self->primIndex]; fp != NULL;
         fp = (FakePrim*)fp->next) {
        fx = fp->posX.val;
        fx += (self->posX.val - fx) / 0x14; // Saturn sdiv by 0x14
        fp->posX.val = fx;
        fy = fp->posY.val;
        fy += (self->posY.val - fy) / 0x10; // Saturn sdiv by 0x10
        fp->posY.val = fy;

        fp->x0 = (s16)(fx >> 16);
        fp->y0 = (s16)(fy >> 16);
        fp->x2 = fp->x0;
        fp->y2 = (s16)(fp->y0 + (ext->partCounter >= 0x21 ? 1 : 0));

        fp->r0 = 0xFF; // stand-in colour
        fp->g0 = (u8)green;
        fp->b0 = 0x20;
        if (self->params == 0) {
            if ((ext->partCounter & 1) == 0) {
                fp->drawMode |= DRAW_HIDE;
            } else {
                fp->drawMode = 0;
            }
        } else {
            if ((ext->partCounter & 1) == 0) {
                fp->drawMode = 0;
            } else {
                fp->drawMode |= DRAW_HIDE;
            }
        }
    }

    ext->partCounter++;
    if (ext->partCounter > 0x3F) {
        self->step++; // destroyed on the next frame
    }
}

// Part 0x2A (Saturn 060E854C): the death rectangles. Two G4 prims. The
// steps move on when the death of the Efreet advances them (main body).
void EntityModEfreet2A(Entity* self) {
    EfreetExt* ext = EFREET_EXT(self);
    Primitive* p0;
    Primitive* p1;
    s32 primIdx;

    if (self->step == 0) {
        primIdx = g_api.AllocPrimitives(PRIM_G4, 2);
        if (primIdx == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS | FLAG_UNK_2000;
        self->primIndex = primIdx;
        p0 = &g_PrimBuf[primIdx];
        p1 = p0->next;
        EfreetSetColor(p0, 0x60, 0x30, 0x08); // stand-in colour
        EfreetSetColor(p1, 0x80, 0x70, 0x40);
        // a light column: additive (a stand-in; the Saturn draw mode is not
        // decoded)
        p0->drawMode = DRAW_TPAGE | DRAW_TPAGE2 | DRAW_TRANSP;
        p1->drawMode = DRAW_TPAGE | DRAW_TPAGE2 | DRAW_TRANSP;
        EfreetRectX(p0, self->posX.i.hi - 0x30, self->posX.i.hi + 0x30);
        EfreetRectY(p0, self->posY.i.hi, self->posY.i.hi);
        EfreetRectX(p1, self->posX.i.hi - 0x10, self->posX.i.hi + 0x10);
        EfreetRectY(p1, self->posY.i.hi, self->posY.i.hi);
        ext->counter90 = 0;
        self->step = 1;
    } else {
        p0 = &g_PrimBuf[self->primIndex];
        p1 = p0->next;
        switch (self->step) {
        case 1:
            if (ext->counter90 > 0x1F) {
                break;
            }
            p0->y0 -= 0xC;
            p0->y1 -= 0xC;
            ext->counter90++;
            break;
        case 2:
            ext->counter90 = 0x40;
            self->step++;
            // falls into case 3
        case 3:
            if (ext->counter90 > 0) {
                if (ext->counter90 & 1) {
                    EfreetRectX(p0, p0->x0 + 1, p0->x1 - 1);
                }
                ext->counter90--;
            }
            break;
        case 4:
            ext->counter90 = 0;
            self->step++;
            // falls into case 5
        case 5:
            if (ext->counter90 < 0x10) {
                p0->y2 -= 0x18;
                p0->y3 -= 0x18;
                p1->y2 -= 0x18;
                p1->y3 -= 0x18;
                ext->counter90++;
                break;
            }
            self->step++;
            // falls into case 6
        case 6:
            if (EfreetRectShrink(p0, p1)) {
                DestroyEntity(self);
                return;
            }
            break;
        }
    }

    // Saturn z update after the switch (the prims alternate by timer parity)
    if ((g_Timer & 1) == 0) {
        p0->priority = self->zPriority - 1;
        p1->priority = self->zPriority - 1;
        p0->drawMode &= 0xFEFF;
        p1->drawMode &= 0xFEFF;
    } else {
        p0->priority = self->zPriority + 3;
        p1->priority = self->zPriority + 3;
        p0->drawMode |= 0x100;
        p1->drawMode |= 0x100;
    }
}

// Part 0x2B (Saturn 060E88A0): sparks that rise at the death. 32 sprite
// prims (PRIM_TILE). The fixed point Y is kept in the float slot.
void EntityModEfreet2B(Entity* self) {
    FakePrim* fp;
    s32 primIdx;
    s32 rnd;
    s32 fy;

    if (self->step == 0) {
        primIdx = g_api.AllocPrimitives(PRIM_TILE, 0x20);
        if (primIdx == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIdx;
        // Random() order as in Saturn: colour, x, y, velocity, counter
        for (fp = (FakePrim*)&g_PrimBuf[primIdx]; fp != NULL;
             fp = (FakePrim*)fp->next) {
            fp->priority = (u16)(self->zPriority - 1);
            rnd = Random() & 7;
            fp->r0 = 0xFF; // stand-in colour
            fp->g0 = (u8)(0x80 + rnd * 8);
            fp->b0 = 0x20;
            fp->drawMode = DRAW_UNK02;
            fp->x0 = (s16)(self->posX.i.hi + (Random() & 0xF) - 8);
            fp->x2 = fp->x0;
            fy = self->posY.i.hi + (Random() & 0x3F) - 0x20;
            fp->y0 = (s16)fy;
            fp->y2 = (s16)(fy + 1);
            fp->posY.val = fy * 0x10000;
            fp->velocityY.val = ((Random() & 7) + 1) * 0x8000;
            fp->timer = (s16)(Random() & 0x1F);
        }
        self->step = 1;
        return;
    }
    if (self->step != 1) {
        return;
    }

    for (fp = (FakePrim*)&g_PrimBuf[self->primIndex]; fp != NULL;
         fp = (FakePrim*)fp->next) {
        fp->timer++;
        if (fp->timer > 0x46) {
            if ((fp->timer & 1) == 0) {
                fp->drawMode = DRAW_UNK02;
            } else {
                fp->drawMode |= DRAW_HIDE;
            }
        }
        fy = fp->posY.val - fp->velocityY.val;
        fp->posY.val = fy;
        fp->y0 = (s16)(fy >> 16);
        fp->x2 = fp->x0;
        fp->y2 = (s16)(fp->y0 + 1);
        if (fp->timer > 0x78) {
            DestroyEntity(self);
            return;
        }
    }
}
