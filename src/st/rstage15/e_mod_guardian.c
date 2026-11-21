// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * Skeleton Guardian (Saturn RSTAGE15, enemy record 425 = TekiInit 0x1A9),
 * Reverse Garden room 3. Translated from the SH-2 (Saturn port mod):
 *   EntityModGuardian    Saturn 0x38, func 060EB550, enemy 425 (body)
 *   EntityModGuardian39  Saturn 0x39, func 060EC174, enemy 397 (hitbox, self + 1)
 *   EntityModGuardian3A  Saturn 0x3A, func 060EC274, enemy 397 (hitbox, self + 2)
 *   EntityModGuardian3B  Saturn 0x3B, func 060EC374, enemy 5 (death debris)
 *
 * The sprite is the converted Saturn bank MOD_GUARDIAN_ANIMSET (frames 1:1).
 * The Saturn epilogue draws the sprite at entity + (0, 32): frames 1-18 (the
 * body) need +32 px. The debris frames 0x13-0x1A are drawn with dy 0. The
 * body frames and the debris frames do not overlap.
 *
 * Simplified: the Saturn's sprite flip, sprite setup and SyncSpriteObjectPos
 * calls are dropped. The Saturn's FUN_06079DEC (walk, gravity, walls) is
 * UnkCollisionFunc2 with the Saturn's sensors. The debris (0x3B) is the
 * breeder's debris (e_mod_skeleton_breeder.h, the same code with frames
 * 0x13-0x1A). The death debris and its sparks use EntityExplosionVariantsSpawner
 * (Saturn SpawnDestructAnim, the same as the wight port).
 *
 * Sounds (all have the same id on the PS1): 0x60C swing, 0x64A stomp, 0x62A
 * death. No palette changes: PAL_NONE only.
 *
 * Required from the including stage: MOD_GUARDIAN_ANIMSET, E_MOD_GUARDIAN,
 * E_MOD_GUARDIAN_39, E_MOD_GUARDIAN_3A, E_MOD_GUARDIAN_3B, E_EXPLOSION.
 */
#include "rstage15.h"

// Saturn screen is 320 px wide, PS1 is 256: horizontal quantities * 4/5.
// Defined before the breeder header so both use the same macro.
#define SAT_X(v) ((v) * 4 / 5)

// Breeder debris (func 060EFF44) reused for the death debris (Saturn 3B).
#define MOD_SKBREEDER_DEBRIS_ONLY
#define MOD_SKBREEDER_DEBRIS_FRAME 0x13
#define MOD_SKBREEDER_ANIMSET MOD_GUARDIAN_ANIMSET

void SetStep(u8 step);
void MoveEntity(void);
void CreateEntityFromEntity(u16 entityId, Entity* source, Entity* entity);
s32 UnkCollisionFunc2(s16* sensors);
void EntityExplosionVariantsSpawner(
    Entity* self, u8 count, u8 params, s16 x, s16 y, u8 index, s16 xGap);

#include "../stage15/e_mod_skeleton_breeder.h"

#define MOD_GUARDIAN_SFX_SWING SFX_WEAPON_SWISH_C    // 0x60C
#define MOD_GUARDIAN_SFX_STEP SFX_STOMP_HARD_E       // 0x64A
#define MOD_GUARDIAN_SFX_DEATH SFX_SKELETON_DEATH_B  // 0x62A

static EInit g_EInitModGuardian = {MOD_GUARDIAN_ANIMSET, 0, 0, PAL_NONE, 425};
// the two hitbox entities: no sprite, enemy 397 (Saturn TekiInit 0x18D)
static EInit g_EInitModGuardianAttack = {0, 0, 0, PAL_NONE, 397};

enum {
    GUARDIAN_INIT = 0,
    GUARDIAN_WALK = 1,
    GUARDIAN_SWING = 3,   // short swing, sound on pose 1
    GUARDIAN_PREPARE = 6, // wait, then dash (or hit reaction)
    GUARDIAN_DASH = 8,    // windup, run, stop
    GUARDIAN_HIT = 0x10,  // knocked back
    GUARDIAN_DEATH = 0x12,
};

typedef struct {
    /* Saturn 0x8C */ s16 timer;      // dash, hit and stop phases
    /* Saturn 0x8E */ s16 counter;    // prepare: the wait
    /* Saturn 0x92 */ u16 hitboxSave; // hitboxState kept from init
    /* Saturn 0x94 */ u8 opacityTarget;
    /* Saturn 0x98 */ s16 opacityFrac; // fade accumulator, opacity << 8
} ET_ModGuardian;
#define GUARDIAN_EXT(e) ((ET_ModGuardian*)&(e)->ext)

// Saturn 0x060F2DAC: sensors of FUN_06079DEC (move with walls and gravity):
// feet {0, 32}, wall {8, 0}, Saturn space (X scaled).
static s16 guardian_sensors_move[] = {0, 32, SAT_X(8), 0};

// Saturn animations: (duration, frame) pairs, frames 1:1.
// 0x00 0x00 loops, 0xFF 0xFF ends.
static u8 guardian_anim_walk[] = {
    // 0x060F2DB4, walk, loops
    0x0D, 0x01, 0x0D, 0x02, 0x0D, 0x03, 0x0D, 0x02, 0x00, 0x00};
static u8 guardian_anim_swing[] = {
    // 0x060F2DBE, swing
    0x15, 0x04, 0x01, 0x05, 0x02, 0x06, 0x10, 0x07, 0xFF, 0xFF};
static u8 guardian_anim_windup[] = {
    // 0x060F2DC8, dash windup
    0x13, 0x08, 0x02, 0x09, 0x1A, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x03, 0x0D,
    0x03, 0x0C, 0x03, 0x0D, 0x02, 0x0C, 0x02, 0x0D, 0x20, 0x0E, 0xFF, 0xFF};
static u8 guardian_anim_dash[] = {
    // 0x060F2DE0, dash run, loops
    0x07, 0x0F, 0x07, 0x10, 0x07, 0x11, 0x00, 0x00};
static u8 guardian_anim_stop[] = {
    // 0x060F2DE8, dash stop
    0x04, 0x12, 0x05, 0x0E, 0x05, 0x03, 0xFF, 0xFF};
static u8 guardian_anim_hit[] = {
    // 0x060F2DF0, hit reaction
    0x05, 0x03, 0x05, 0x0E, 0x05, 0x12, 0xFF, 0xFF};

// Saturn 0x060F2DF8 / 0x060F2E00: where the 8 debris pieces appear. X is
// mirrored by facing. Saturn space: X is scaled at runtime.
static s8 guardian_debris_off_x[8] = {-3, -4, 8, 7, 0, -22, 8, 12};
static s8 guardian_debris_off_y[8] = {-8, -5, -8, 22, 8, 19, -12, 11};

// Body hitbox {w, h, offX, offY} for animCurFrame 1..18: Saturn 0x060F2E30 +
// frame * 4. Index = frame - 1. Saturn space (w and offX scaled at runtime).
static s8 guardian_hitbox[18][4] = {
    {7, 16, 0, 8}, {7, 16, 0, 8}, {7, 16, 0, 8}, {7, 16, 0, 8},
    {7, 16, 0, 8}, {7, 16, 0, 8}, {7, 16, 0, 8}, {7, 16, 0, 8},
    {7, 16, 0, 8}, {4, 16, 0, 8}, {4, 16, 0, 8}, {4, 16, 0, 8},
    {4, 16, 0, 8}, {4, 16, 0, 8}, {7, 16, 0, 8}, {7, 16, 0, 8},
    {7, 16, 0, 8}, {7, 16, 0, 8},
};

// Attack hitbox of 0x39 (self + 1), indexed by the body's frame - 1.
// Saturn 0x060F2E78 + frame * 4.
static s8 guardian_hitbox_39[18][4] = {
    {6, 6, -24, 21},  {6, 6, -28, 20},  {6, 6, -31, 18},  {6, 6, 4, -34},
    {16, 6, -20, -32}, {8, 12, -38, -14}, {6, 12, -34, 20}, {6, 8, 8, -24},
    {6, 8, 8, -24},   {6, 8, 8, -24},   {8, 8, -16, -18}, {8, 4, -32, 6},
    {8, 4, -32, 4},   {8, 4, -32, 5},   {8, 4, -40, 5},   {8, 4, -41, 3},
    {8, 4, -40, 3},   {8, 4, -32, 4},
};

// Hitbox of 0x3A (self + 2), indexed by the body's frame - 1.
// Saturn 0x060F2EC0 + frame * 4.
static s8 guardian_hitbox_3a[18][4] = {
    {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},     {0, 0, 0, 0},
    {0, 0, -6, 0},    {4, 16, -6, 12},  {4, 16, -6, 12},  {4, 16, -6, 12},
    {4, 16, -6, 12},  {4, 16, -6, 12},  {5, 18, -16, 6},  {5, 18, -16, 6},
    {5, 18, -16, 6},  {4, 16, -8, 12},
};

// Saturn SpawnDestructAnim (func 060E1FAC): the sparks at the feet.
// Random order and arguments match the Saturn: x, index, then the second pair.
static void GuardianSparks(Entity* self) {
    s16 x;
    u8 index;

    x = SAT_X((Random() & 3) | 8);
    index = (Random() & 3) + 1;
    EntityExplosionVariantsSpawner(self, 1, 1, x, 0x20, index, 0);
    x = SAT_X(-8 - (Random() & 3));
    index = (Random() & 3) + 1;
    EntityExplosionVariantsSpawner(self, 1, 1, x, 0x20, index, 0);
}

// Saturn step change: velocity 0, then SetStep (step, step_s, pose).
static void GuardianSetStep(Entity* self, u8 step) {
    self->velocityX = 0;
    self->velocityY = 0;
    SetStep(step);
}

// Saturn epilogue: the opacity fade, then the hitbox from the frame.
// The sprite offset (dy 32) has no PS1 call: it is in the sprite frames.
static void GuardianEpilogue(Entity* self) {
    ET_ModGuardian* ext = GUARDIAN_EXT(self);
    s32 t;
    s32 i;

    if (self->opacity != ext->opacityTarget) {
        t = ext->opacityTarget * 0x100 - (u16)ext->opacityFrac;
        if (t < 0) {
            t += 0x1F;
        }
        t = (t >> 5) + ext->opacityFrac;
        ext->opacityFrac = (s16)t;
        self->opacity = (u8)((u32)t >> 8);
    }

    if ((u16)(self->animCurFrame - 1) < 18) {
        i = self->animCurFrame - 1;
        self->hitboxWidth = SAT_X(guardian_hitbox[i][0]);
        self->hitboxHeight = guardian_hitbox[i][1];
        self->hitboxOffX = SAT_X(guardian_hitbox[i][2]);
        self->hitboxOffY = guardian_hitbox[i][3];
    } else {
        self->hitboxWidth = 0;
        self->hitboxHeight = 0;
        self->hitboxOffX = 0;
        self->hitboxOffY = 0;
    }
}

void EntityModGuardian(Entity* self) {
    ET_ModGuardian* ext = GUARDIAN_EXT(self);
    s32 move = 1; // 0 where the Saturn skips FUN_06079DEC
    s16 dist;
    s32 i;
    Entity* ent;

    if (self->hitParams != 0 && (self->step & 1)) {
        GuardianSetStep(self, GUARDIAN_HIT);
    }
    if ((self->flags & FLAG_DEAD) && self->step != GUARDIAN_DEATH) {
        GuardianSetStep(self, GUARDIAN_DEATH);
        self->opacity = 0x80;
    }

    switch (self->step) {
    case GUARDIAN_INIT:
        InitializeEntity(g_EInitModGuardian);
        self->hitboxWidth = SAT_X(7);
        self->hitboxHeight = 16;
        self->hitboxOffX = 0;
        self->hitboxOffY = 8;
        // Saturn drawFlags 0x68: opacity and the green and blue masks
        self->drawFlags = ENTITY_OPACITY | ENTITY_MASK_G | ENTITY_MASK_B;
        self->opacity = 0x80;
        ext->opacityTarget = 0x80;
        ext->opacityFrac = 0; // Saturn: the fade accumulator starts at 0
        self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        CreateEntityFromEntity(E_MOD_GUARDIAN_39, self, self + 1);
        CreateEntityFromEntity(E_MOD_GUARDIAN_3A, self, self + 2);
        ext->hitboxSave = self->hitboxState;
        GuardianSetStep(self, GUARDIAN_WALK);
        self->animCurFrame = 0;
        move = 0;
        break;

    case GUARDIAN_WALK:
        if (g_Timer % 30 == 0) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        }
        dist = GetDistanceToPlayerX();
        if (dist < SAT_X(0x30)) {
            if ((Random() & 0xFF) % 20 == 0) {
                self->step = GUARDIAN_SWING;
            }
        } else if (dist < SAT_X(0x60)) {
            if ((Random() & 0xFF) % 60 == 0) {
                self->step = GUARDIAN_SWING;
            }
        } else if (dist < SAT_X(0x100) && (Random() & 0xFF) % 60 == 0) {
            self->step = GUARDIAN_DASH;
        }
        if (self->step != GUARDIAN_WALK) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            self->velocityX = 0;
            self->velocityY = 0;
            self->pose = 0;
            self->poseTimer = 0;
            self->step_s = 0;
            move = 0;
            break;
        }
        AnimateEntity(guardian_anim_walk, self);
        if (self->facingLeft == 0) {
            self->velocityX = SAT_X(-0xC000); // Saturn 0.75 px/frame
        } else {
            self->velocityX = SAT_X(0xC000);
        }
        break;

    case GUARDIAN_SWING:
        // pose 1 with timer 0: the next AnimateEntity starts frame 4
        if (self->pose == 1 && self->poseTimer == 0) {
            PlaySfxPositional(MOD_GUARDIAN_SFX_SWING);
        }
        if (AnimateEntity(guardian_anim_swing, self) == 0) {
            self->velocityX = 0;
            self->velocityY = 0;
            self->pose = 0;
            self->poseTimer = 0;
            self->step = GUARDIAN_WALK;
            self->step_s = 0;
        }
        break;

    case GUARDIAN_PREPARE:
        switch (self->step_s) {
        case 0:
            ext->counter = (Random() & 0xFF) % 60 + 150;
            self->step_s++;
            // fallthrough
        case 1:
            ext->counter--;
            if (ext->counter < 0) {
                self->velocityX = 0;
                self->velocityY = 0;
                self->pose = 0;
                self->poseTimer = 0;
                self->step = GUARDIAN_WALK;
                self->step_s = 0;
                ext->opacityTarget = 0x80;
            } else if (self->hitFlags & 3) {
                self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
                self->hitboxState = 0;
                self->step_s++;
            }
            break;
        case 2:
            if (self->facingLeft == 0) {
                self->velocityX = SAT_X(0x30000);
            } else {
                self->velocityX = SAT_X(-0x30000);
            }
            ext->timer = (Random() & 0xFF) % 20 + 50;
            self->step_s++;
            // fallthrough
        case 3:
            self->velocityX = self->velocityX * 32 / 33;
            if ((ext->timer & 3) == 0) {
                GuardianSparks(self);
            }
            ext->timer--;
            if (ext->timer < 0) {
                self->step_s++;
            }
            break;
        case 4:
            self->hitboxState = ext->hitboxSave;
            self->velocityX = 0;
            self->velocityY = 0;
            self->pose = 0;
            self->poseTimer = 0;
            self->step = GUARDIAN_DASH;
            self->step_s = 0;
            if (self->facingLeft == 0) {
                self->velocityX = SAT_X(-0x54000);
            } else {
                self->velocityX = SAT_X(0x54000);
            }
            ext->timer = (Random() & 0xFF) % 20 + 50;
            self->step_s = 1;
            ext->opacityTarget = 0x80;
            break;
        }
        break;

    case GUARDIAN_DASH:
        if (self->step_s == 0) {
            // pose 5 with timer 0: the next AnimateEntity starts frame 0x0C
            if (self->pose == 5 && self->poseTimer == 0) {
                PlaySfxPositional(MOD_GUARDIAN_SFX_SWING);
            }
            if (AnimateEntity(guardian_anim_windup, self) == 0) {
                self->pose = 0;
                self->poseTimer = 0;
                if (self->facingLeft == 0) {
                    self->velocityX = SAT_X(-0x60000);
                } else {
                    self->velocityX = SAT_X(0x60000);
                }
                ext->timer = (Random() & 0xFF) % 20 + 50;
                self->step_s++;
            }
        } else if (self->step_s == 1) {
            self->velocityX = self->velocityX * 32 / 33;
            AnimateEntity(guardian_anim_dash, self);
            if (g_Timer % 7 == 0) {
                PlaySfxPositional(MOD_GUARDIAN_SFX_STEP);
            }
            ext->timer--;
            if (ext->timer < 0) {
                self->pose = 0;
                self->poseTimer = 0;
                ext->timer = 0;
                self->step_s++;
            }
        } else if (self->step_s == 2) {
            // stop: the Saturn's FUN_060EBE8E
            if (g_Timer % 7 == 0) {
                PlaySfxPositional(MOD_GUARDIAN_SFX_STEP);
            }
            self->velocityX = (self->velocityX + (self->velocityX < 0)) >> 1;
            if ((ext->timer & 3) == 0) {
                GuardianSparks(self);
            }
            ext->timer++;
            if (AnimateEntity(guardian_anim_stop, self) == 0) {
                GuardianSetStep(self, GUARDIAN_WALK);
            }
        }
        break;

    case GUARDIAN_HIT:
        if (self->step_s == 0) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            if (self->facingLeft == 0) {
                self->velocityX = SAT_X(0x40000);
            } else {
                self->velocityX = SAT_X(-0x40000);
            }
            self->hitboxState = 0;
            ext->timer = (Random() & 0xFF) % 20 + 50;
            self->step_s++;
            ext->opacityTarget = 0xC0;
        } else if (self->step_s == 1) {
            self->velocityX = self->velocityX * 24 / 25;
            if ((ext->timer & 3) == 0) {
                GuardianSparks(self);
            }
            AnimateEntity(guardian_anim_hit, self);
            ext->timer--;
            if (ext->timer < 0) {
                self->hitboxState = ext->hitboxSave;
                self->velocityX = 0;
                self->velocityY = 0;
                self->pose = 0;
                self->poseTimer = 0;
                self->step = GUARDIAN_PREPARE;
                self->step_s = 0;
            }
        }
        break;

    case GUARDIAN_DEATH:
        if (self->step_s == 0) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            for (i = 0; i < 8; i++) {
                ent = AllocEntity(&g_Entities[224], &g_Entities[256]);
                if (ent == NULL) {
                    break;
                }
                CreateEntityFromEntity(E_MOD_GUARDIAN_3B, self, ent);
                ent->facingLeft = self->facingLeft;
                ent->params = i;
                if (self->facingLeft == 0) {
                    ent->posX.i.hi += SAT_X(guardian_debris_off_x[i]);
                } else {
                    ent->posX.i.hi -= SAT_X(guardian_debris_off_x[i]);
                }
                ent->posY.i.hi += guardian_debris_off_y[i];
                // TODO Z (Saturn: the sprite z of the body)
                ent->zPriority = self->zPriority;
            }
            PlaySfxPositional(MOD_GUARDIAN_SFX_DEATH);
            self->step_s++;
            move = 0;
        } else if (self->step_s == 1) {
            DestroyEntity(self);
            return;
        }
        break;

    default:
        move = 0;
        break;
    }

    if (move) {
        UnkCollisionFunc2(guardian_sensors_move);
    }
    GuardianEpilogue(self);
}

// Saturn func_060EC174: the attack hitbox at self + 1 (parent = self - 1).
void EntityModGuardian39(Entity* self) {
    Entity* parent = self - 1;
    s32 i;

    if (self->step == 0) {
        InitializeEntity(g_EInitModGuardianAttack); // step -> 1
        self->attack = 0x28;
    } else if (self->step != 1) {
        return;
    }

    if ((u16)(parent->animCurFrame - 1) < 18) {
        i = parent->animCurFrame - 1;
        self->hitboxWidth = SAT_X(guardian_hitbox_39[i][0]);
        self->hitboxHeight = guardian_hitbox_39[i][1];
        self->hitboxOffX = SAT_X(guardian_hitbox_39[i][2]);
        self->hitboxOffY = guardian_hitbox_39[i][3];
    } else {
        self->hitboxWidth = 0;
        self->hitboxHeight = 0;
        self->hitboxOffX = 0;
        self->hitboxOffY = 0;
    }
    self->facingLeft = parent->facingLeft;
    self->hitboxState = parent->hitboxState;
    self->posX.i.hi = parent->posX.i.hi;
    self->posY.i.hi = parent->posY.i.hi;
    if (parent->entityId != E_MOD_GUARDIAN) {
        DestroyEntity(self);
    }
}

// Saturn func_060EC274: the second hitbox at self + 2 (parent = self - 2).
void EntityModGuardian3A(Entity* self) {
    Entity* parent = self - 2;
    s32 i;

    if (self->step == 0) {
        InitializeEntity(g_EInitModGuardianAttack); // step -> 1
        self->attack = 0x28;
        self->hitboxState = 2;
    } else if (self->step != 1) {
        return;
    }

    if ((u16)(parent->animCurFrame - 1) < 18) {
        i = parent->animCurFrame - 1;
        self->hitboxWidth = SAT_X(guardian_hitbox_3a[i][0]);
        self->hitboxHeight = guardian_hitbox_3a[i][1];
        self->hitboxOffX = SAT_X(guardian_hitbox_3a[i][2]);
        self->hitboxOffY = guardian_hitbox_3a[i][3];
    } else {
        self->hitboxWidth = 0;
        self->hitboxHeight = 0;
        self->hitboxOffX = 0;
        self->hitboxOffY = 0;
    }
    self->facingLeft = parent->facingLeft;
    self->posX.i.hi = parent->posX.i.hi;
    self->posY.i.hi = parent->posY.i.hi;
    if (parent->entityId != E_MOD_GUARDIAN) {
        DestroyEntity(self);
    }
}

// Saturn func_060EC374: the death debris (Saturn 3B). The breeder's debris
// code is the same, with the frames 0x13-0x1A (set by the defines above).
void EntityModGuardian3B(Entity* self) {
    EntityModSkBreederDebris(self);
}
