// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * The inverted Cursed Prison (Saturn RSTAGE16) Wraith, translated from the
 * SH-2 (Saturn port mod):
 *   EntityModWraith    Saturn 0x18, func 060E3608, enemy 424
 *   EntityModWraith19  Saturn 0x19, func 060E4664 (the afterimage trail)
 *   EntityModWraith1A  Saturn 0x1A, func 060E4804 (the drain particles)
 *   EntityModWraith1B  Saturn 0x1B, func 060E4AFC (the soul particles)
 * The sprites are the converted Saturn bank 0x060ECFA0 (MOD_WRAITH_ANIMSET);
 * the anim tables are the Saturn's (0x060ECECC..), frames 1:1. The frames
 * have no colour calculation: the Wraith is opaque, shaded by opacity.
 */
#include "rstage16.h"

// Saturn screen is 320 px wide, PS1 is 256: horizontal quantities * 4/5
#define SAT_X(v) ((v) * 4 / 5)
#define SAT_X_INV(v) ((v) * 5 / 4)

// Saturn-only sounds (not in the PS1 sound banks): PS1 stand-ins
#define MOD_SFX_WRAITH_GRAB SFX_WEAPON_SWISH_B         // Saturn 0x904
#define MOD_SFX_WRAITH_DRAIN SFX_MAGIC_NOISE_SWEEP     // Saturn 0x905
#define MOD_SFX_WRAITH_DEATH SFX_GHOST_ENEMY_HOWL      // Saturn 0x906
#define MOD_SFX_WRAITH_SOUL SFX_MAGIC_WEAPON_APPEAR_B  // Saturn 0x907

// Saturn g_Player.status & 0x010401A3: the player cannot be grabbed
#define WRAITH_NO_GRAB_STATUS                                                  \
    (PLAYER_STATUS_AXEARMOR | PLAYER_STATUS_DEAD | PLAYER_STATUS_INVINCIBLE |  \
     PLAYER_STATUS_STONE | PLAYER_STATUS_CROUCH | PLAYER_STATUS_MIST_FORM |    \
     PLAYER_STATUS_BAT_FORM)
#define WRAITH_GRAB_DAMAGE 35

static EInit g_EInitModWraith = {MOD_WRAITH_ANIMSET, 1, 0, PAL_NONE, 424};
static EInit g_EInitModWraithFx = {MOD_WRAITH_ANIMSET, 0, 0, PAL_NONE, 5};

typedef struct {
    /* Saturn 0x80 */ s16 timer;
    /* Saturn 0x88 */ s16 timer2;
    /* Saturn 0x8C */ s16 bob;
    /* Saturn 0x98 */ s32 bobY; // the body: drawn 32 px below, hit 4 px below
    /* Saturn 0xA4 */ u16 hitboxSave;
    /* Saturn 0xA8 */ s32 targetVX;
    /* Saturn 0xAC */ s32 targetVY;
    // posY holds the drawn place between updates, dy the offset from the path
    s32 dy;
} ET_ModWraith;
#define WRAITH_EXT(e) ((ET_ModWraith*)&(e)->ext)

typedef struct {
    s16 timer; // Saturn 0x9C (trail), 0x90 (particles)
} ET_ModWraithFx;
#define WRAITH_FX_EXT(e) ((ET_ModWraithFx*)&(e)->ext)

enum {
    WRAITH_INIT = 0,
    WRAITH_IDLE = 1,
    WRAITH_GRAB = 2,
    WRAITH_DASH = 3,
    WRAITH_RECOVER = 4,
    WRAITH_RETREAT = 5,
    WRAITH_APPEAR = 6,
    WRAITH_HIT = 0x12,
    WRAITH_DEATH = 0x14,
};

// shared by every Wraith, as on the Saturn (0x060EFCDC)
static s16 s_WraithAccel;

static u8 wraith_anim_appear[] = {
    0x18, 1, 3, 2, 3, 3, 3, 4, 3, 5, 3, 6, 6, 7, 7, 8, 5, 9, 6, 10, 0x10, 11,
    2, 12, 2, 13, 2, 14, 2, 15, 2, 16, 2, 17, 4, 18, 6, 19, 0xFF, 0xFF};
static u8 wraith_anim_idle[] = {4, 19, 4, 20, 4, 21, 4, 22, 0, 0};
static u8 wraith_anim_grab_start[] = {
    2, 19, 2, 18, 2, 17, 2, 16, 2, 15, 2, 14, 2, 13, 2, 12, 2, 11, 4, 10,
    8, 11, 3, 12, 2, 13, 2, 23, 3, 24, 3, 25, 0xFF, 0xFF};
static u8 wraith_anim_grab_hold[] = {6, 28, 6, 29, 0, 0};
static u8 wraith_anim_grab_end[] = {
    4, 28, 2, 27, 2, 26, 2, 25, 2, 24, 1, 23, 1, 12, 3, 11, 7, 10, 0x18, 11,
    0xFF, 0xFF};
static u8 wraith_anim_reach[] = {5, 41, 5, 42, 5, 43, 5, 44, 0, 0};
static u8 wraith_anim_grab2[] = {5, 27, 5, 28, 0xFF, 0xFF};
static u8 wraith_anim_hit[] = {
    4, 30, 4, 38, 4, 39, 0x10, 40, 4, 39, 4, 38, 4, 30, 4, 19, 0xFF, 0xFF};
static u8 wraith_anim_death[] = {
    0x10, 19, 4, 30, 4, 31, 5, 32, 5, 33, 0x18, 34, 2, 35, 1, 36, 0xFF, 0xFF};
static u8 wraith_anim_death2[] = {0x18, 34, 2, 35, 1, 36, 0xFF, 0xFF};
static u8 wraith_anim_recover0[] = {
    3, 12, 3, 13, 3, 14, 3, 15, 3, 16, 3, 17, 3, 18, 8, 19, 0xFF, 0xFF};
static u8 wraith_anim_recover1[] = {
    3, 25, 3, 24, 3, 23, 3, 13, 3, 12, 3, 11, 3, 12, 3, 13, 3, 14, 3, 15,
    3, 16, 3, 17, 3, 18, 8, 19, 0xFF, 0xFF};

static void WraithFace(Entity* self) {
    self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
}

static void WraithResetAnim(Entity* self) {
    self->pose = 0;
    self->poseTimer = 0;
}

static void WraithApproach(s32* v, s32 target) {
    if (*v < target) {
        *v += s_WraithAccel;
    } else if (*v > target) {
        *v -= s_WraithAccel;
    }
    if ((target >> 11) == (*v >> 11)) {
        *v = target;
    }
}

static Entity* WraithSpawn(Entity* self, u16 id) {
    Entity* e = AllocEntity(&g_Entities[160], &g_Entities[192]);

    if (e != NULL) {
        DestroyEntity(e);
        CreateEntityFromEntity(id, self, e);
        e->facingLeft = self->facingLeft;
        e->zPriority = self->zPriority;
    }
    return e;
}

void EntityModWraith(Entity* self) {
    ET_ModWraith* ext = WRAITH_EXT(self);
    Entity* e;
    s16 dx, dy, r, step;
    s32 v, d;

    // back on the path
    self->posY.val -= ext->dy;
    if (self->hitParams && (self->step & 1)) {
        WraithResetAnim(self);
        self->step = WRAITH_HIT;
        self->step_s = 0;
    }
    if ((self->flags & FLAG_DEAD) && self->step != WRAITH_DEATH) {
        SetStep(WRAITH_DEATH);
        self->hitboxState = 0;
        self->hitboxWidth = self->hitboxHeight = 0;
    }

    switch (self->step) {
    case WRAITH_INIT:
        InitializeEntity(g_EInitModWraith);
        self->drawFlags = ENTITY_OPACITY | ENTITY_MASK_G;
        self->opacity = 0x90;
        self->hitboxWidth = SAT_X(8);
        self->hitboxHeight = 24;
        self->hitboxOffX = 0;
        // the Saturn sprite's z is 0x80 + 32
        self->zPriority += 32;
        WraithFace(self);
        self->step = WRAITH_APPEAR;
        self->step_s = 0;
        self->animCurFrame = 0;
        break;

    case WRAITH_APPEAR:
        switch (self->step_s) {
        case 0:
            ext->hitboxSave = self->hitboxState;
            self->step_s++;
            // fallthrough
        case 1:
            if (GetDistanceToPlayerY() <= 95 &&
                GetDistanceToPlayerX() <= SAT_X(127)) {
                self->step_s = 2;
            }
            break;
        case 2:
            self->drawFlags |= ENTITY_BLINK;
            ext->timer = 24;
            self->step_s++;
            // fallthrough
        case 3:
            if (!AnimateEntity(wraith_anim_appear, self)) {
                self->hitboxState = ext->hitboxSave;
                SetStep(WRAITH_IDLE);
            }
            if (--ext->timer == 0) {
                self->drawFlags &= ~ENTITY_BLINK;
            }
            break;
        }
        break;

    case WRAITH_IDLE:
        dx = GetDistanceToPlayerX();
        dy = GetDistanceToPlayerY();
        step = WRAITH_IDLE;
        if (dx <= SAT_X(47)) {
            step = WRAITH_RETREAT;
        } else if (dx <= SAT_X(95) && dy <= 47) {
            r = (Random() & 0xFF) % 60;
            if (r < 2) {
                step = WRAITH_GRAB;
            } else if (r == 2) {
                step = WRAITH_RETREAT;
            }
        } else if (dx <= SAT_X(159) && dy <= 79) {
            if ((Random() & 0xFF) % 60 < 2) {
                step = WRAITH_GRAB;
            }
        } else if (dx <= SAT_X(127)) {
            if (!((Random() & 0xFF) % 60)) {
                step = WRAITH_RETREAT;
            }
        } else if (!(Random() & 3)) {
            step = WRAITH_DASH;
        }
        if (step != WRAITH_IDLE) {
            self->step = step;
            self->step_s = 0;
            WraithResetAnim(self);
        } else {
            AnimateEntity(wraith_anim_idle, self);
        }
        // a new drift every 64 frames
        if (!(++ext->timer2 & 63)) {
            ext->targetVX =
                SAT_X((((Random() << 8) | Random()) & 0x3FFF) - 0x2000) << 4;
            ext->targetVY = ((((Random() << 8) | Random()) & 0x3FFF) - 0x2000)
                            << 3;
            s_WraithAccel = (((Random() << 8) | Random()) & 0x1FF) + 0x700;
            WraithFace(self);
        }
        WraithApproach(&self->velocityX, ext->targetVX);
        WraithApproach(&self->velocityY, ext->targetVY);
        MoveEntity();
        break;

    case WRAITH_DASH:
        if (self->step_s == 0) {
            WraithFace(self);
            self->velocityX = self->facingLeft ? SAT_X(FIX(4)) : SAT_X(FIX(-4));
            self->velocityY = 0;
            ext->timer2 = 90;
            self->step_s++;
        }
        self->velocityX -= self->velocityX / 64;
        MoveEntity();
        self->posY.val += (PLAYER.posY.val - self->posY.val) / 64;
        if (--ext->timer2 < 0 || GetDistanceToPlayerX() <= SAT_X(127)) {
            ext->timer2 = 0;
            SetStep(WRAITH_IDLE);
        }
        break;

    case WRAITH_RETREAT:
    case WRAITH_HIT:
        if (self->step_s == 0) {
            WraithFace(self);
            self->velocityX = self->facingLeft ? SAT_X(FIX(-4)) : SAT_X(FIX(4));
            self->velocityY = 0;
            ext->timer2 = 60;
            self->step_s++;
        }
        self->velocityX -= self->velocityX / 64;
        AnimateEntity(wraith_anim_hit, self);
        self->posY.val += (PLAYER.posY.val - self->posY.val) / 64;
        MoveEntity();
        if (--ext->timer2 < 0) {
            SetStep(WRAITH_IDLE);
        }
        break;

    case WRAITH_GRAB:
        switch (self->step_s) {
        case 0:
            if (!AnimateEntity(wraith_anim_grab_start, self)) {
                WraithResetAnim(self);
                if (GetDistanceToPlayerX() <= SAT_X(47) ||
                    GetDistanceToPlayerY() > 96) {
                    self->step = WRAITH_RECOVER;
                    self->step_s = 0;
                } else {
                    ext->timer = 120;
                    self->step_s = 1;
                }
            }
            break;
        case 1:
            // the reach: faster as the time runs out
            if (!(ext->timer % 30)) {
                WraithFace(self);
            }
            // ponytail: the SH-2 divides by 0 below 8 frames; taken as 1
            d = ext->timer / 8;
            v = SAT_X(FIX(1) + FIX(4) / (d > 0 ? d : 1));
            self->posX.val += self->facingLeft ? v : -v;
            self->posY.val += (PLAYER.posY.val - self->posY.val) / 6;
            dx = PLAYER.posX.i.hi - self->posX.i.hi;
            if (!self->facingLeft) {
                dx = -dx;
            }
            dy = PLAYER.posY.i.hi - self->posY.i.hi;
            if (!(g_Player.status & WRAITH_NO_GRAB_STATUS) && dy >= -31 &&
                dy <= 7 && dx >= SAT_X(33) && dx <= SAT_X(63)) {
                g_Player.grabState = GRAB_START;
                g_Player.grabDamage = 0;
                g_Player.grabReactionEffect = GRAB_REACTION_NORMAL;
                self->hitboxState = 0;
                self->velocityX = self->velocityY = 0;
                self->step_s = 2;
            } else if (g_Player.grabState == GRAB_NONE && --ext->timer == 0) {
                self->step = WRAITH_RECOVER;
                self->step_s = 1;
            }
            AnimateEntity(wraith_anim_reach, self);
            break;
        case 2:
            self->posX.i.hi = PLAYER.posX.i.hi +
                              (self->facingLeft ? SAT_X(-56) : SAT_X(56));
            self->posY.i.hi = PLAYER.posY.i.hi;
            WraithResetAnim(self);
            self->step_s++;
            PlaySfxPositional(MOD_SFX_WRAITH_GRAB);
            break;
        case 3:
            if (!AnimateEntity(wraith_anim_grab2, self)) {
                // the player's death knock-back (func_80113F7C) reads the
                // last entity that touched it: NULL if nothing did yet
                // (the PS1 reads address 0, the PC crashes)
                PLAYER.unkB8 = self;
                g_Player.grabDamage = WRAITH_GRAB_DAMAGE;
                g_Player.grabState = GRAB_DAMAGE;
                ext->timer = 90;
                WraithResetAnim(self);
                self->step_s++;
            }
            break;
        case 4:
            // the drain: a particle stream from the player every 16 frames
            AnimateEntity(wraith_anim_grab_hold, self);
            if (--ext->timer == 0) {
                self->step_s++;
            } else if (!(ext->timer & 15)) {
                e = WraithSpawn(self, E_MOD_WRAITH_1A);
                if (e != NULL) {
                    e->posY.val = ext->bobY;
                }
                PlaySfxPositional(MOD_SFX_WRAITH_DRAIN);
            }
            break;
        case 5:
            if (!AnimateEntity(wraith_anim_grab_end, self)) {
                self->step_s++;
            }
            break;
        case 6:
            g_Player.grabState = GRAB_NONE;
            self->hitboxState = 3;
            self->velocityX = self->facingLeft ? SAT_X(FIX(-3)) : SAT_X(FIX(3));
            self->velocityY = FIX(-2);
            ext->timer = 38;
            self->step_s++;
            break;
        case 7:
            MoveEntity();
            self->velocityX -= self->velocityX / 64;
            self->velocityY -= self->velocityY / 32;
            if (--ext->timer == 0) {
                SetStep(WRAITH_IDLE);
            }
            break;
        }
        break;

    case WRAITH_RECOVER:
        if (!AnimateEntity(self->step_s ? wraith_anim_recover1
                                        : wraith_anim_recover0,
                           self)) {
            SetStep(WRAITH_IDLE);
        }
        self->velocityX -= self->velocityX / 64;
        self->velocityY -= self->velocityY / 64;
        MoveEntity();
        break;

    case WRAITH_DEATH:
        switch (self->step_s) {
        case 0:
            if (g_Player.grabState) {
                g_Player.grabState = GRAB_NONE;
            }
            WraithFace(self);
            self->velocityX = self->facingLeft ? SAT_X(FIX(-4)) : SAT_X(FIX(4));
            self->velocityY = 0;
            ext->timer2 = 90;
            self->step_s++;
            PlaySfxPositional(MOD_SFX_WRAITH_DEATH);
            break;
        case 1:
            self->velocityX -= self->velocityX / 16;
            MoveEntity();
            if (!AnimateEntity(wraith_anim_death, self)) {
                self->step_s++;
                self->velocityX = self->velocityY = 0;
                WraithResetAnim(self);
                ext->timer2 = 224;
            }
            break;
        case 2:
            ext->timer2--;
            if (!AnimateEntity(wraith_anim_death2, self)) {
                PlaySfxPositional(MOD_SFX_WRAITH_SOUL);
                self->step_s++;
                // the soul (frame 45) rises out of the body
                self->animCurFrame = 45;
                self->posX.i.hi += self->facingLeft ? SAT_X(-5) : SAT_X(5);
                self->posX.i.hi += self->facingLeft ? SAT_X(4) : SAT_X(-4);
                self->posY.i.hi -= 30;
            }
            break;
        case 3:
            // it shrinks and spins away
            if (--ext->timer2 <= 63) {
                DestroyEntity(self);
                return;
            }
            if (ext->timer2 == 84) {
                self->drawFlags |= ENTITY_BLINK;
            }
            self->drawFlags |= ENTITY_SCALEX | ENTITY_SCALEY | ENTITY_ROTATE;
            self->scaleX = self->scaleY = (ext->timer2 / 3) << 2;
            // ponytail: the Saturn angle is taken as 0x10000 per turn
            self->rotate = (-ext->timer2 * ext->timer2) >> 4;
            break;
        }
        if (self->step_s && ext->timer2 > 170 && !(ext->timer2 & 7)) {
            e = WraithSpawn(self, E_MOD_WRAITH_1B);
            if (e != NULL) {
                e->posY.val = ext->bobY;
                if (self->animCurFrame == 45) {
                    e->posY.i.hi += 31;
                    e->posX.i.hi += self->facingLeft ? SAT_X(4) : SAT_X(-4);
                } else {
                    e->posY.i.hi += 1;
                }
            }
        }
        break;
    }

    // the bob: the body floats up to 8 px around 12 px above the path
    ext->bobY = self->posY.val + (rsin(ext->bob << 5) << 7) - FIX(12);
    if (!((self->step == WRAITH_GRAB && self->step_s > 1) ||
          self->step == WRAITH_DEATH || self->step == WRAITH_APPEAR)) {
        ext->bob++;
    }
    v = ext->bobY + FIX(32);
    ext->dy = v - self->posY.val;
    self->posY.val = v;
    self->hitboxOffY = 4 - 32;
    if (self->step != WRAITH_DEATH && self->step != WRAITH_APPEAR &&
        !(g_Timer % 6)) {
        e = WraithSpawn(self, E_MOD_WRAITH_19);
        if (e != NULL) {
            e->animCurFrame = self->animCurFrame;
            e->posY.val = v;
            e->zPriority = self->zPriority - 2;
        }
    }
}

// the afterimage: drifts back and down, fades to red
void EntityModWraith19(Entity* self) {
    ET_ModWraithFx* ext = WRAITH_FX_EXT(self);
    s16 frame;

    switch (self->step) {
    case 0:
        frame = self->animCurFrame;
        InitializeEntity(g_EInitModWraithFx);
        self->animCurFrame = frame;
        self->drawFlags = ENTITY_OPACITY | ENTITY_MASK_R;
        self->opacity = 0x80;
        ext->timer = 0;
        break;
    case 1:
        if (!(++ext->timer & 3)) {
            self->zPriority--;
        }
        self->posX.val += self->facingLeft ? SAT_X(-0x2000) : SAT_X(0x2000);
        self->posY.val += FIX(0.5);
        if (ext->timer > 24) {
            self->step++;
        } else {
            // ponytail: the Saturn adds the mesh (checker) at 8; opacity only
            self->opacity = 128 - ext->timer * 4;
        }
        if (ext->timer == 16) {
            self->drawFlags |= ENTITY_BLINK;
        }
        break;
    default:
        DestroyEntity(self);
        break;
    }
}

// The particles: a drawn TILE and a hidden data prim per particle (as the
// Saturn's pairs). The data prim keeps the offset from the entity and the
// speed in 16.16.
#define P_X(d) (((s32)(d)->x0 << 16) | (u16)(d)->y0)
#define P_Y(d) (((s32)(d)->x1 << 16) | (u16)(d)->y1)
#define P_VX(d) (((s32)(d)->x2 << 16) | (u16)(d)->y2)
#define P_VY(d) (((s32)(d)->x3 << 16) | (u16)(d)->y3)
#define P_SET(hi, lo, v) ((hi) = (s16)((v) >> 16), (lo) = (s16)(v))

static void ParticleSet(Primitive* d, s32 x, s32 y, s32 vx, s32 vy) {
    P_SET(d->x0, d->y0, x);
    P_SET(d->x1, d->y1, y);
    P_SET(d->x2, d->y2, vx);
    P_SET(d->x3, d->y3, vy);
}

static bool ParticleInit(Entity* self, s32 count) {
    s32 primIndex;
    Primitive* prim;

    InitializeEntity(g_EInitModWraithFx);
    self->animSet = 0;
    self->animCurFrame = 0;
    primIndex = g_api.AllocPrimitives(PRIM_TILE, count * 2);
    if (primIndex == -1) {
        DestroyEntity(self);
        return false;
    }
    self->flags |= FLAG_HAS_PRIMS;
    self->primIndex = primIndex;
    for (prim = &g_PrimBuf[primIndex]; prim != NULL; prim = prim->next) {
        prim->drawMode = DRAW_HIDE;
    }
    WRAITH_FX_EXT(self)->timer = 0;
    return true;
}

static void ParticleColor(Entity* self, s32 r, s32 g, s32 b) {
    Primitive* prim;

    // (CLAMP has no outer parentheses)
    r = (CLAMP(r, 0, 31)) << 3;
    g = (CLAMP(g, 0, 31)) << 3;
    b = (CLAMP(b, 0, 31)) << 3;
    for (prim = &g_PrimBuf[self->primIndex]; prim != NULL;
         prim = prim->next->next) {
        prim->r0 = r;
        prim->g0 = g;
        prim->b0 = b;
    }
}

static s32 ParticleRand(void) { return (Random() << 8) | Random(); }

// the drain: drops fly up from the player and home in on the Wraith
void EntityModWraith1A(Entity* self) {
    ET_ModWraithFx* ext = WRAITH_FX_EXT(self);
    Primitive* prim;
    Primitive* d;
    s32 speed, angle, x, y, vx, vy, k;

    if (!self->step) {
        if (!ParticleInit(self, 32)) {
            return;
        }
        x = self->facingLeft ? SAT_X(0x3D0000) : SAT_X(-0x400000);
        for (prim = &g_PrimBuf[self->primIndex]; prim != NULL;
             prim = d->next) {
            d = prim->next;
            speed = (ParticleRand() & 0x7FFF) + 0x8000;
            angle = ParticleRand() % 0xFFF;
            ParticleSet(d, x, 0, SAT_X((rcos(angle) * speed) >> 13),
                        ((rsin(angle) * speed) >> 12) - 0x12000);
            prim->u0 = prim->v0 = 2;
            prim->priority = self->zPriority - 32;
            prim->drawMode = DRAW_TPAGE2 | DRAW_TPAGE | DRAW_TRANSP;
        }
    }
    k = ++ext->timer;
    // the blue-green glow brightens, then turns white
    if (k / 2 + 16 >= 31) {
        ParticleColor(self, k / 2, k / 3 + 16, 31);
    } else {
        ParticleColor(self, 0, k / 3 + 16, k / 2 + 16);
    }
    k /= 5;
    for (prim = &g_PrimBuf[self->primIndex]; prim != NULL; prim = d->next) {
        d = prim->next;
        if (prim->drawMode == DRAW_HIDE) {
            continue;
        }
        x = P_X(d);
        y = P_Y(d);
        if (x >= -15 && x <= 15 && y >= -15 && y <= 15) {
            prim->drawMode = DRAW_HIDE;
            continue;
        }
        angle = ratan2(-y, SAT_X_INV(-x));
        vx = P_VX(d) + SAT_X(rcos(angle) * k / 8);
        vy = P_VY(d) + rsin(angle) * k / 4;
        x += vx;
        y += vy;
        ParticleSet(d, x, y, vx, vy);
        prim->x0 = self->posX.i.hi + (x >> 16);
        prim->y0 = self->posY.i.hi + (y >> 16);
    }
    if (ext->timer > 58) {
        DestroyEntity(self);
    }
}

// the soul: sparks burst out and spiral back into the soul
void EntityModWraith1B(Entity* self) {
    ET_ModWraithFx* ext = WRAITH_FX_EXT(self);
    Primitive* prim;
    Primitive* d;
    s32 speed, angle, x, y, vx, vy, t, dx, dy;

    if (!self->step) {
        if (!ParticleInit(self, 12)) {
            return;
        }
        for (prim = &g_PrimBuf[self->primIndex]; prim != NULL;
             prim = d->next) {
            d = prim->next;
            speed = (ParticleRand() & 0x7FFF) + 0x8000;
            angle = ParticleRand() & 0xFFF;
            ParticleSet(d, SAT_X(rcos(angle) << 7), rsin(angle) << 7,
                        SAT_X(((rcos(angle) * speed) / 6) >> 7),
                        ((rsin(angle) * speed) / 6) >> 7);
            prim->u0 = prim->v0 = 3;
            prim->priority = self->zPriority - 1;
            prim->drawMode = DRAW_TPAGE2 | DRAW_TPAGE | DRAW_TRANSP;
        }
    }
    t = ++ext->timer;
    ParticleColor(self, 31 - t / 2, 31 - t, 47 - t / 2);
    for (prim = &g_PrimBuf[self->primIndex]; prim != NULL; prim = d->next) {
        d = prim->next;
        if (prim->drawMode == DRAW_HIDE) {
            continue;
        }
        x = P_X(d);
        y = P_Y(d);
        dx = -(x >> 16);
        dy = -(y >> 16);
        if (dx * dx + dy * dy <= 15) {
            prim->drawMode = DRAW_HIDE;
            continue;
        }
        angle = ratan2(dy, SAT_X_INV(dx)) +
                (self->facingLeft ? 0x280 : -0x280);
        x += SAT_X(rcos(angle) * t);
        y += rsin(angle) * t;
        angle = ratan2(-(y >> 16), SAT_X_INV(-(x >> 16)));
        vx = P_VX(d) + SAT_X(rcos(angle));
        vy = P_VY(d) + rsin(angle);
        x += vx;
        y += vy;
        ParticleSet(d, x, y, vx, vy);
        prim->x0 = self->posX.i.hi + (x >> 16) - 1;
        prim->y0 = self->posY.i.hi + (y >> 16) - 1;
    }
    if (ext->timer > 95) {
        DestroyEntity(self);
    }
}
