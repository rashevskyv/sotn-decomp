// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * Copper Armor (Saturn RSTAGE15 entities 0x21 and 0x22), translated from the
 * SH-2 (Saturn port mod):
 *   EntityModCopper    Saturn 0x21, func_060E5804, E_MOD_COPPER.
 *                      TekiInit 0x191 (enemy 401). Its role sits in ext 0x78:
 *                        0      the armor (the main body, placed in the room)
 *                        other  a debris piece, made when the armor dies
 *   EntityModCopper22  Saturn 0x22, func_060E6750, E_MOD_COPPER_22.
 *                      TekiInit 0x18D (enemy 397). The weapon hitbox, self + 1.
 *
 * Saturn ids spawned: 0x21 (debris), 0x22 (weapon, from the armor), 0x02
 * (E_EXPLOSION, from a debris piece). Layout: the armor makes its weapon at
 * self + 1. The debris pieces take AllocEntity slots 160..192.
 *
 * Required from the including stage: MOD_COPPER_ANIMSET, enum entries
 * E_MOD_COPPER and E_MOD_COPPER_22, E_EXPLOSION.
 *
 * Sprite: the converted Saturn bank (MOD_COPPER_ANIMSET, frames 1:1). The
 * epilogue writes the sprite at the entity position with no offset (Saturn
 * FUN_06079BB4 copies posX and posY). So dy = 0 for every frame. The Saturn
 * sprite flip (flag 0x10) is dropped: facingLeft does the flip. No palette
 * changes at run time.
 *
 * Collision helpers, Saturn to PC:
 *   FUN_06079DEC -> UnkCollisionFunc2 (floor and wall sensors)
 *   FUN_06079C04 -> UnkCollisionFunc3 (four sensors, falls)
 *   FUN_0607A118 -> CheckColliderOffsets
 *   FUN_06079BE4 -> FallEntity (not called here, the function above falls)
 *
 * Saturn-space rules: X values and widths use SAT_X. The Saturn hitbox and
 * child offsets are multiplied by 5/4 first (table * 5 >> 2), so their PC
 * values use the same rule (CopperScale54).
 *
 * Simplified or uncertain:
 *  - The armor's role after case 1 is _DAT_06099810 ^ facingLeft. That address
 *    is g_Entities[0].facingLeft (the player's facing). Its low byte is the
 *    flip flag of the recover sub-steps.
 *  - SUB_06030E4C (called before the opacity) is skipped. It keeps the speed.
 *  - The death sound 0x8F0 is Saturn-only: SFX_SKELETON_DEATH_C stands in (not
 *    positional). Check g_api.PlaySfx is the right call.
 *  - Frame 0x35 + 7 = 60 (params 7 of a debris piece) is past the 60-frame bank.
 */

#include "rstage15.h"

// Saturn screen is 320 px wide, PS1 is 256: horizontal quantities * 4/5.
#define SAT_X(v) ((v) * 4 / 5)

// Sounds (include/sfx.h): the Saturn id equals the PS1 id.
#define MOD_COPPER_SFX_SWING SFX_WEAPON_SWISH_C // 0x60C
#define MOD_COPPER_SFX_LUNGE SFX_WEAPON_SWISH_B // 0x60B
#define MOD_COPPER_SFX_DEATH SFX_SKELETON_DEATH_C // Saturn-only 0x8F0

// The collision helpers (st_common.c, e_misc.c).
s32 UnkCollisionFunc2(s16* sensors);
s32 UnkCollisionFunc3(s16* sensors);
u8 CheckColliderOffsets(s16* sensors, u8 facing);
void EntityExplosionVariantsSpawner(
    Entity* self, u8 count, u8 params, s16 x, s16 y, u8 index, s16 xGap);
void CreateEntityFromCurrentEntity(u16 entityId, Entity* entity);
void CreateEntityFromEntity(u16 entityId, Entity* source, Entity* entity);

static EInit g_EInitModCopper = {MOD_COPPER_ANIMSET, 0, 0, PAL_NONE, 0x191};
static EInit g_EInitModCopper22 = {0, 0, 0, PAL_NONE, 0x18D};

// Ext fields, accessed through COPPER_EXT() (no change to the Ext union).
typedef struct {
    /* Saturn 0x78 */ s16 role; // 0 = armor; other = debris; armor: flip flag
    u8 pad[0x1A];
    /* Saturn 0x94 */ s16 timer; // walk and pause timer
} ET_ModCopper;
#define COPPER_EXT(e) ((ET_ModCopper*)&(e)->ext)

enum {
    COPPER_INIT = 0,
    COPPER_IDLE = 1,
    COPPER_LAND = 2,       // airborne after a jump, then the recover sub-steps
    COPPER_WALK = 3,       // walks at the player for a while
    COPPER_LUNGE = 4,      // short lunge
    COPPER_LUNGE_LONG = 6, // long lunge
    COPPER_COMBO_A = 8,    // three-part attack
    COPPER_COMBO_B = 10,   // three-part attack, wide
    COPPER_HOP = 0xC,      // hop, then a swipe
    COPPER_HOP_B = 0xE,    // hop, then a wide swipe
    COPPER_HIT = 0x10,     // hit reaction
    COPPER_DEATH = 0x12,   // spawns eight debris pieces, then destroys itself
    COPPER_DEBRIS = 0x14,  // a debris piece, rises and fades
};

// Saturn animation scripts, frames 1:1. Pairs of (duration, frame). 0x00 0x00
// loops. 0xFF 0x00 ends (AnimateEntity returns 0).
static u8 copper_anim_328[] = {// 0x060EF328, armor IDLE (loops)
                               0x17, 0x10, 0x0D, 0x11, 0x0D, 0x12,
                               0x09, 0x13, 0x0D, 0x14, 0x00, 0x00};
static u8 copper_anim_31C[] = {// 0x060EF31C, armor WALK
                               0x12, 0x0C, 0x06, 0x0D, 0x10, 0x0E,
                               0x05, 0x0F, 0x00, 0x00};
static u8 copper_anim_304[] = {// 0x060EF304, LUNGE and HOP swipe
                               0x02, 0x01, 0x02, 0x02, 0x02, 0x03, 0x08, 0x04,
                               0x10, 0x05, 0x18, 0x06, 0x08, 0x07, 0x03, 0x08,
                               0x03, 0x09, 0x03, 0x0A, 0x1E, 0x0B, 0x00, 0x00};
static u8 copper_anim_334[] = {// 0x060EF334, LAND (stand up, sub-steps 0-2)
                               0x02, 0x15, 0x03, 0x16, 0x03, 0x17, 0x03, 0x18,
                               0x03, 0x19, 0x03, 0x1A, 0xFF, 0x00};
static u8 copper_anim_344[] = {// 0x060EF344, LAND recover (sub-step 3)
                               0x04, 0x1B, 0x04, 0x1C, 0x06, 0x1D,
                               0x08, 0x1E, 0x18, 0x1F, 0xFF, 0x00};
static u8 copper_anim_350[] = {// 0x060EF350, HIT
                               0x06, 0x20, 0x0A, 0x21, 0x14, 0x22, 0x16, 0x23,
                               0x07, 0x25, 0x18, 0x26, 0x00, 0x00};
static u8 copper_anim_360[] = {// 0x060EF360, LUNGE_LONG and HOP_B swipe
                               0x02, 0x27, 0x03, 0x28, 0x04, 0x29, 0x06, 0x2A,
                               0x1C, 0x2B, 0x06, 0x2C, 0x03, 0x2D, 0x01, 0x2E,
                               0x18, 0x2F, 0x03, 0x30, 0x03, 0x31, 0x04, 0x32,
                               0x08, 0x33, 0x0A, 0x34, 0x00, 0x00};
static u8 copper_anim_380[] = {// 0x060EF380, COMBO first part
                               0x02, 0x01, 0x02, 0x02, 0x04, 0x03, 0x02, 0x04,
                               0x10, 0x05, 0x20, 0x06, 0x04, 0x07, 0x02, 0x08,
                               0x02, 0x09, 0x02, 0x0A, 0x02, 0x0B, 0x00, 0x00};
static u8 copper_anim_398[] = {// 0x060EF398, COMBO second part
                               0x02, 0x04, 0x04, 0x05, 0x04, 0x06, 0x04, 0x07,
                               0x04, 0x08, 0x04, 0x09, 0x04, 0x0A, 0x04, 0x0B,
                               0x00, 0x00};
static u8 copper_anim_3AC[] = {// 0x060EF3AC, COMBO A third part
                               0x02, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07,
                               0x04, 0x08, 0x04, 0x09, 0x06, 0x0A, 0x18, 0x0B,
                               0x00, 0x00};
static u8 copper_anim_3D4[] = {// 0x060EF3D4, COMBO B third part
                               0x02, 0x27, 0x03, 0x28, 0x04, 0x29, 0x06, 0x2A,
                               0x1C, 0x2B, 0x06, 0x2C, 0x03, 0x2D, 0x01, 0x2E,
                               0x30, 0x2F, 0x03, 0x30, 0x03, 0x31, 0x04, 0x32,
                               0x08, 0x33, 0x0A, 0x34, 0x00, 0x00};
static u8 copper_anim_3C0[] = {// 0x060EF3C0, HOP walk back (loops)
                               0x02, 0x04, 0x02, 0x05, 0x02, 0x06, 0x02, 0x07,
                               0x02, 0x08, 0x02, 0x09, 0x02, 0x0A, 0x02, 0x0B,
                               0x00, 0x00};

// Collision sensors. X is scaled (SAT_X), Y as is.
// 0x060EF2CA: UnkCollisionFunc2 (floor, then wall).
static s16 copper_sensors_2ca[] = {0, 0, SAT_X(8), 0};
// 0x060EF2D2: UnkCollisionFunc3 (four pairs).
static s16 copper_sensors_2d2[] = {0, 0, 0, -28, SAT_X(12), -36, SAT_X(-24), -32};
// 0x060EF2B8: CheckColliderOffsets, one point, facing 0 (0xFF ends it).
static s16 copper_wall_2b8[] = {SAT_X(0), 8, 0xFF};
// 0x060EF2BE: CheckColliderOffsets, one point (0xFF ends it).
static s16 copper_wall_2be[] = {SAT_X(24), -4, 0xFF};

// 0x060EF2E2: the debris piece's offsets, by params 0..7. X (SAT, scaled as
// CopperScale54) and Y (plain). The Y is added to posY - 0x20.
static s16 copper_child_x[8] = {8, -4, 0, 8, 0, -16, 0, 0};
static s16 copper_child_y[8] = {24, 24, 8, -4, 0, -2, -4, -16};

// 0x060EF410 (+ 0x060EF4E4): the weapon hitbox for armor frames 0..52. Each
// row: {offX, offY, width, height}, Saturn space. Frames above 52 use row 0.
static s8 copper_hitbox_22[53][4] = {
    {0, 0, 0, 0},     {-17, 1, 0, 0},   {-22, 24, 0, 0},  {-14, 22, 0, 0},
    {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},     {-42, 1, 16, 4},
    {-40, 4, 8, 1},   {-37, 2, 8, 1},   {-32, 2, 0, 0},   {-16, -8, 0, 0},
    {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {-17, -4, 0, 0},  {-18, -3, 0, 0},  {-18, -2, 0, 0},  {-18, -3, 0, 0},
    {-18, -3, 0, 0},  {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {-16, 24, 3, 6},  {-19, 21, 1, 8},  {-25, 4, 8, 1},   {-24, 2, 8, 1},
    {-17, 0, 5, 4},   {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {0, 0, 0, 0},     {-12, 15, 3, 8},  {-6, 13, 3, 8},   {-10, 21, 3, 8},
    {-19, 17, 1, 10}, {-53, -8, 8, 1},  {-55, -6, 8, 1},  {-19, 18, 0, 0},
    {-11, 20, 0, 0},  {-7, 13, 0, 0},   {0, 0, 0, 0},     {0, 0, 0, 0},
};

// Saturn (v * 5) >> 2, rounded toward zero for negative v, then to PC space.
static s32 CopperScale54(s32 v) {
    v *= 5;
    if (v < 0) {
        v += 3;
    }
    return SAT_X(v >> 2);
}

// One dust puff at the feet. Saturn SpawnDestructAnim(self, 1, 1, x, 0, n, 0).
static void CopperDust(Entity* self, s32 x) {
    EntityExplosionVariantsSpawner(self, 1, 1, SAT_X(x), 0,
                                   (Random() & 3) + 1, 0);
}

// The epilogue (Saturn 060E6608): opacity from speed, and dust.
static void CopperEpilogue(Entity* self) {
    s32 speed = self->velocityX;

    if (speed < 0) {
        speed = -speed;
    }
    if (speed > 0x40) {
        speed = 0x40;
    }
    self->opacity = speed + 0x70;

    if ((speed >> 4) != 0 && self->step != COPPER_LAND && (g_Timer & 1)) {
        if (self->facingLeft == 0) {
            CopperDust(self, -16 - (Random() & 3));
            CopperDust(self, 24 - (Random() & 7));
        } else {
            CopperDust(self, 16 + (Random() & 3));
            CopperDust(self, -24 + (Random() & 7));
        }
    }
}

// The common tail of the attack steps (Saturn LAB_060E6406): floor collision,
// then the epilogue.
static void CopperTail(Entity* self) {
    UnkCollisionFunc2(copper_sensors_2ca);
    CopperEpilogue(self);
}

// Saturn FUN_060E6400: the attack is over. Back to IDLE, then the tail.
static void CopperToIdle(Entity* self) {
    self->step = COPPER_IDLE;
    CopperTail(self);
}

// Saturn FUN_060E56F4: the landing sub-steps of step 2. Returns 1 when done.
// flip is the low byte of the ext role (1 = flip the speed).
static s32 CopperLandSub(Entity* self, u8 flip) {
    switch (self->step_s) {
    case 0: // turn the speed around if needed
        if (self->facingLeft) {
            self->velocityX = -self->velocityX;
        }
        if (flip) {
            self->velocityX = -self->velocityX;
        }
        self->step_s++;
        AnimateEntity(copper_anim_334, self);
        return 0;
    case 1: // stand up until frame 0x18
        AnimateEntity(copper_anim_334, self);
        if (self->animCurFrame == 0x18) {
            self->step_s++;
        }
        return 0;
    case 2: // wait for the landing
        AnimateEntity(copper_anim_334, self);
        if (self->velocityY >= 0 &&
            CheckColliderOffsets(copper_wall_2b8, 0)) {
            self->pose = 0;
            self->poseTimer = 0;
            self->step_s++;
        }
        return (u8)UnkCollisionFunc3(copper_sensors_2d2);
    case 3: // the landing recovery
        UnkCollisionFunc3(copper_sensors_2d2);
        return AnimateEntity(copper_anim_344, self) == 0;
    }
    return 0;
}

void EntityModCopper(Entity* self) {
    ET_ModCopper* ext = COPPER_EXT(self);
    Entity* e;
    s32 dist;
    s32 r;
    s32 next;
    s32 i;
    s16 old;

    if (self->hitParams != 0 && (self->step & 1)) {
        self->pose = 0;
        self->poseTimer = 0;
        self->step = COPPER_HIT;
    }
    if (self->flags & FLAG_DEAD) {
        self->step = COPPER_DEATH;
    }

    switch (self->step) {
    case COPPER_INIT:
        InitializeEntity(g_EInitModCopper);
        self->drawFlags = 0x48; // opacity and blue mask
        if (ext->role == 0) {
            // the armor: its weapon in the next slot, then it lands
            self->hitboxOffY = -0x1C;
            DestroyEntity(self + 1);
            CreateEntityFromCurrentEntity(E_MOD_COPPER_22, self + 1);
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            self->step = COPPER_LAND;
        } else {
            // a debris piece: its frame, then it rises
            self->animCurFrame = self->params + 0x35;
            self->hitboxState = 0;
            self->step = COPPER_DEBRIS;
            self->hitboxOffY = -0x1C;
        }
        CopperEpilogue(self);
        return;

    case COPPER_IDLE:
        self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        dist = GetDistanceToPlayerX();
        next = 0;
        if (dist < SAT_X(0x20) || (dist < SAT_X(0x70) && (Random() & 0x3F) == 0)) {
            // too close: jump back
            self->velocityX = SAT_X(-0x20000);
            self->velocityY = -0x48000;
            self->step_s = 0;
            ext->role = (s16)(g_Entities[0].facingLeft ^ self->facingLeft);
            next = COPPER_LAND;
        } else if (dist > SAT_X(0x4F)) {
            if (dist < SAT_X(0x60)) {
                r = (Random() & 0xFF) % 0x2D;
                if (r == 0) {
                    next = COPPER_LUNGE;
                } else if (r <= 2) {
                    next = COPPER_LUNGE_LONG;
                }
            } else if (dist < SAT_X(0x70)) {
                r = (Random() & 0xFF) % 0x5A;
                if (r == 0) {
                    next = COPPER_COMBO_A;
                } else if (r == 1) {
                    next = COPPER_COMBO_B;
                } else if (r == 2) {
                    next = COPPER_HOP;
                } else if (r == 3) {
                    next = COPPER_HOP_B;
                }
            } else {
                if ((Random() & 7) == 0) {
                    self->velocityX = SAT_X(-0x30000);
                    self->velocityY = -0x48000;
                    self->step_s = 0;
                    ext->role = 0;
                    next = COPPER_LAND;
                } else {
                    self->velocityX =
                        self->facingLeft ? SAT_X(0x6400) : SAT_X(-0x6400);
                    ext->timer = 0x1E;
                    next = COPPER_WALK;
                }
            }
        } else {
            r = (Random() & 0xFF) % 0x2D;
            if (r < 2) {
                next = COPPER_LUNGE;
            } else if (r == 2) {
                next = COPPER_LUNGE_LONG;
            }
        }
        if (next != 0) {
            self->step = next;
        }
        if (self->step != COPPER_IDLE) {
            self->pose = 0;
            self->poseTimer = 0;
            CopperEpilogue(self);
            return;
        }
        AnimateEntity(copper_anim_328, self);
        CopperEpilogue(self);
        return;

    case COPPER_LAND:
        if (!(u8)CopperLandSub(self, (u8)ext->role)) {
            CopperEpilogue(self);
            return;
        }
        self->pose = 0;
        self->poseTimer = 0;
        self->step_s = 0;
        ext->role = 0;
        self->step = COPPER_IDLE;
        CopperEpilogue(self);
        return;

    case COPPER_WALK:
        AnimateEntity(copper_anim_31C, self);
        UnkCollisionFunc2(copper_sensors_2ca);
        if (CheckColliderOffsets(copper_wall_2be, (u8)self->facingLeft)) {
            self->pose = 0;
            self->poseTimer = 0;
            self->velocityX = SAT_X(-0x20000);
            self->velocityY = -0x48000;
            self->step_s = 0;
            ext->role = 0;
            self->step = COPPER_LAND;
            // Saturn code runs on after this: the timer and the range check.
        }
        if (ext->timer != 0) {
            ext->timer--;
            CopperEpilogue(self);
            return;
        }
        dist = GetDistanceToPlayerX();
        if (dist > SAT_X(0x90)) {
            CopperEpilogue(self);
            return;
        }
        self->pose = 0;
        self->poseTimer = 0;
        self->step = COPPER_IDLE;
        CopperEpilogue(self);
        return;

    case COPPER_LUNGE:
        if (self->pose == 6 && self->poseTimer == 0) {
            PlaySfxPositional(MOD_COPPER_SFX_SWING);
        }
        if (self->animCurFrame == 7) {
            self->velocityX = self->facingLeft ? SAT_X(0x40000) : SAT_X(-0x40000);
        }
        self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
        if (AnimateEntity(copper_anim_304, self) == 0) {
            self->pose = 0;
            self->poseTimer = 0;
            self->velocityX = 0;
            CopperToIdle(self);
            return;
        }
        CopperTail(self);
        return;

    case COPPER_LUNGE_LONG:
        if (self->pose == 8 && self->poseTimer == 0) {
            PlaySfxPositional(MOD_COPPER_SFX_LUNGE);
        }
        if (self->animCurFrame == 0x2E) {
            self->velocityX = self->facingLeft ? SAT_X(0x80000) : SAT_X(-0x80000);
        }
        self->velocityX = (self->velocityX * 6) / 7;
        if (AnimateEntity(copper_anim_360, self) == 0) {
            self->pose = 0;
            self->poseTimer = 0;
            self->velocityX = 0;
            CopperToIdle(self);
            return;
        }
        CopperTail(self);
        return;

    case COPPER_COMBO_A:
        switch (self->step_s) {
        case 0:
            self->step_s++;
            // fallthrough
        case 1:
            if (self->pose == 6 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_SWING);
            }
            if (self->animCurFrame == 7) {
                self->velocityX = self->facingLeft ? SAT_X(0x40000) : SAT_X(-0x40000);
            }
            self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
            if (AnimateEntity(copper_anim_380, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s++;
            }
            break;
        case 2:
            if (self->pose == 3 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_SWING);
            }
            if (self->animCurFrame == 7) {
                self->velocityX = self->facingLeft ? SAT_X(0x40000) : SAT_X(-0x40000);
            }
            self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
            if (AnimateEntity(copper_anim_398, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s++;
            }
            break;
        case 3:
            if (self->pose == 3 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_SWING);
            }
            if (self->animCurFrame == 4) {
                self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            } else if (self->animCurFrame == 7) {
                self->velocityX = self->facingLeft ? SAT_X(0x60000) : SAT_X(-0x60000);
            }
            self->velocityX = (self->velocityX * 4) / 5;
            if (AnimateEntity(copper_anim_3AC, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->velocityX = 0;
                self->step_s = 0;
                self->velocityX = 0;
                CopperToIdle(self);
                return;
            }
            break;
        default:
            CopperEpilogue(self);
            return;
        }
        CopperTail(self);
        return;

    case COPPER_COMBO_B:
        switch (self->step_s) {
        case 0:
            self->step_s++;
            // fallthrough
        case 1:
            if (self->pose == 6 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_SWING);
            }
            if (self->animCurFrame == 7) {
                self->velocityX = self->facingLeft ? SAT_X(0x40000) : SAT_X(-0x40000);
            }
            self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
            if (AnimateEntity(copper_anim_380, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s++;
            }
            break;
        case 2:
            if (self->pose == 3 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_SWING);
            }
            if (self->animCurFrame == 7) {
                self->velocityX = self->facingLeft ? SAT_X(0x40000) : SAT_X(-0x40000);
            }
            self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
            if (AnimateEntity(copper_anim_398, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s++;
            }
            break;
        case 3:
            if (self->pose == 8 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_LUNGE);
            }
            if (self->animCurFrame == 0x2A) {
                self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            } else if (self->animCurFrame == 0x2E) {
                self->velocityX = self->facingLeft ? SAT_X(0x100000) : SAT_X(-0x100000);
            }
            self->velocityX = (self->velocityX * 5) / 6;
            if (AnimateEntity(copper_anim_3D4, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->velocityX = 0;
                self->step_s = 0;
                self->velocityX = 0;
                CopperToIdle(self);
                return;
            }
            break;
        default:
            CopperEpilogue(self);
            return;
        }
        CopperTail(self);
        return;

    case COPPER_HOP:
        if (self->step_s > 2) {
            CopperEpilogue(self);
            return;
        }
        if (self->step_s == 2) {
            if (self->pose == 6 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_SWING);
            }
            if (self->animCurFrame == 4) {
                self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            } else if (self->animCurFrame == 7) {
                self->velocityX = self->facingLeft ? SAT_X(0x40000) : SAT_X(-0x40000);
            }
            self->velocityX = (self->velocityX * 5) / 6;
            if (AnimateEntity(copper_anim_304, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->velocityX = 0;
                self->step_s = 0;
                self->velocityX = 0;
                CopperToIdle(self);
                return;
            }
            CopperTail(self);
            return;
        }
        // step_s 0 or 1: the hop back
        self->step_s = 1;
        if (self->pose == 3 && self->poseTimer == 0) {
            PlaySfxPositional(MOD_COPPER_SFX_SWING);
        }
        if (self->animCurFrame == 4) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        }
        if (self->animCurFrame == 7) {
            self->velocityX = self->facingLeft ? SAT_X(0x10000) : SAT_X(-0x10000);
        }
        self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
        if (AnimateEntity(copper_anim_3C0, self) != 0 || (Random() & 3) != 0) {
            CopperTail(self);
            return;
        }
        self->pose = 0;
        self->poseTimer = 0;
        self->step_s++;
        CopperTail(self);
        return;

    case COPPER_HOP_B:
        if (self->step_s > 2) {
            CopperEpilogue(self);
            return;
        }
        if (self->step_s == 2) {
            if (self->pose == 8 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_COPPER_SFX_LUNGE);
            }
            if (self->animCurFrame == 0x2A) {
                self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            } else if (self->animCurFrame == 0x2E) {
                self->velocityX = self->facingLeft ? SAT_X(0x80000) : SAT_X(-0x80000);
            }
            self->velocityX = (self->velocityX * 5) / 6;
            if (AnimateEntity(copper_anim_360, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                self->velocityX = 0;
                self->step_s = 0;
                self->step = COPPER_IDLE;
            }
            CopperTail(self);
            return;
        }
        // step_s 0 or 1
        self->step_s = 1;
        if (self->pose == 3 && self->poseTimer == 0) {
            PlaySfxPositional(MOD_COPPER_SFX_SWING);
        }
        if (self->animCurFrame == 4) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        }
        if (self->animCurFrame == 7) {
            self->velocityX = self->facingLeft ? SAT_X(0x18000) : SAT_X(-0x18000);
        }
        self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
        if (AnimateEntity(copper_anim_3C0, self) == 0 && (Random() & 3) == 0) {
            self->pose = 0;
            self->poseTimer = 0;
            self->step_s++;
        }
        CopperTail(self);
        return;

    case COPPER_HIT:
        if (AnimateEntity(copper_anim_350, self) == 0) {
            self->poseTimer = 0;
            self->pose = 0;
            self->step = COPPER_IDLE;
        }
        UnkCollisionFunc2(copper_sensors_2ca);
        self->velocityX = self->facingLeft ? SAT_X(-0xA000) : SAT_X(0xA000);
        if (self->animCurFrame > 0x22) {
            self->velocityX = 0;
        }
        CopperEpilogue(self);
        return;

    case COPPER_DEATH:
        // Saturn 060E6xxx: eight debris pieces, then the armor is gone.
        g_api.PlaySfx(MOD_COPPER_SFX_DEATH);
        for (i = 0; i < 8; i++) {
            e = AllocEntity(&g_Entities[160], &g_Entities[192]);
            if (e != NULL) {
                DestroyEntity(e);
                CreateEntityFromEntity(E_MOD_COPPER, self, e);
                e->params = i;
                // the role: 16 * i, or 16 + 16 * i for i % 4 == 0; plus 12
                COPPER_EXT(e)->role = (i & 3) == 0 ? 0x10 + 0x10 * i : 0x10 * i;
                COPPER_EXT(e)->role += 0xC;
                e->facingLeft = self->facingLeft;
            }
        }
        DestroyEntity(self);
        return;

    case COPPER_DEBRIS:
        if ((ext->role & 3) == 0) {
            self->posY.i.hi += 1;
        }
        if (ext->role == 10) {
            e = AllocEntity(&g_Entities[160], &g_Entities[192]);
            if (e != NULL) {
                DestroyEntity(e);
                CreateEntityFromEntity(E_EXPLOSION, self, e);
                e->params = 2;
                e->posX.i.hi += (s16)CopperScale54(copper_child_x[self->params]);
                e->posY.i.hi += -0x20 + copper_child_y[self->params];
            }
        }
        old = ext->role;
        ext->role = old - 1;
        if (old < 0) {
            DestroyEntity(self);
            return;
        }
        if (ext->role < 0x10) {
            self->drawFlags |= 0x80; // ENTITY_BLINK
        }
        CopperEpilogue(self);
        return;

    default:
        CopperEpilogue(self);
        return;
    }
}

// Saturn func_060E6750: the armor's weapon. Its hitbox follows the armor's frame.
void EntityModCopper22(Entity* self) {
    Entity* parent = self - 1;
    s32 idx;
    s32 w;

    if (self->step == 0) {
        InitializeEntity(g_EInitModCopper22); // no sprite, as the breeder's weapon
        self->attack = 0x37;
        self->step = 1;
    }

    idx = (u8)parent->animCurFrame;
    if (idx > 0x34) {
        idx = 0;
    }
    self->hitboxOffX = (s16)CopperScale54(copper_hitbox_22[idx][0]);
    self->hitboxOffY = -copper_hitbox_22[idx][1] - 0x20;
    w = copper_hitbox_22[idx][2];
    self->hitboxWidth = SAT_X((u8)((w * 5) >> 2));
    self->hitboxHeight = copper_hitbox_22[idx][3];

    self->facingLeft = parent->facingLeft;
    self->hitboxState = parent->hitboxState;
    self->posX.i.hi = parent->posX.i.hi;
    self->posY.i.hi = parent->posY.i.hi;

    if (parent->entityId != E_MOD_COPPER) {
        DestroyEntity(self);
    }
}
