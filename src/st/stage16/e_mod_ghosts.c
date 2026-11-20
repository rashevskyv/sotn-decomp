// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * Cursed Prison (Saturn STAGE_16) Specter and Will-o'-Wisp, translated from
 * the SH-2 (Saturn port mod):
 *   EntityModSpecter      Saturn 0x20, func 060E55B0, enemy 417
 *   EntityModWispSpawner  Saturn 0x28, func 060E7ED0
 *   EntityModWisp         Saturn 0x29, func 060E7FC4, enemy 418
 *   EntityModWispFlame    Saturn 0x2A, func 060E86BC, enemy 44 (attack / 4)
 *   EntityModWispTrail    Saturn 0x2B, func 060E8AC0
 *   EntityModWispBurst    Saturn 0x2C, func 060E887C
 * The sprites are the converted Saturn banks (gdn_entities.py STAGE_16:
 * ANIMSET_OVL(11) and (12) with their palette blocks); the anim tables are
 * the Saturn's (0x060ECA8C.., 0x060EEBE2..), frames 1:1.
 *
 * The Saturn draws both with colour calculation on (sprite type 5, ratio
 * register 5) while CCMD is 1 ("add as is": ResetLayerColorCalc,
 * src/saturn/zero_0.c), so the PS1 draws them additive.
 */
#include "stage16.h"

// Saturn screen is 320 px wide, PS1 is 256: horizontal quantities * 4/5
#define SAT_X(v) ((v) * 4 / 5)
#define SAT_X_INV(v) ((v) * 5 / 4)

// Saturn-only sounds (not in the PS1 sound banks): PS1 stand-ins
#define MOD_SFX_SPECTER_CRY SFX_MAGIC_NOISE_SWEEP          // Saturn 0x901
#define MOD_SFX_SPECTER_VANISH SFX_MAGIC_WEAPON_APPEAR_B   // Saturn 0x900
#define MOD_SFX_WISP_BURST SFX_FIRE_SHOT                   // Saturn 0x903
// the PS1's own numbers (the Saturn keeps the PS1 table below 0x900)
#define MOD_SFX_WISP_FLAME SFX_ELECTRICITY                 // Saturn 0x614

EInit g_EInitModSpecter = {MOD_SPECTER_ANIMSET, 1, 0, PAL_NONE, 417};
EInit g_EInitModWisp = {MOD_WISP_ANIMSET, 0, 0, PAL_NONE, 418};
EInit g_EInitModWispFlame = {MOD_WISP_ANIMSET, 0, 0, PAL_NONE, 44};
EInit g_EInitModWispFx = {MOD_WISP_ANIMSET, 0, 0, PAL_NONE, 5};

// ---------------------------------------------------------------- Specter

typedef struct {
    /* Saturn 0x92 */ s16 attacking;
    /* Saturn 0x96 */ s16 counter; // death: hold, then the vanish
    /* Saturn 0x98 */ s16 hitTimer;
    /* Saturn 0xA4 */ u16 hitboxSave;
    /* Saturn 0xA8 */ s32 targetVX;
    /* Saturn 0xAC */ s32 targetVY;
    /* the CMDCOLR palette offset (the PS1 hit flash owns entity->palette) */
    s16 pal;
} ET_ModSpecter;
#define SPECTER_EXT(e) ((ET_ModSpecter*)&(e)->ext)

enum {
    SPECTER_INIT = 0,
    SPECTER_FLOAT = 1,
    SPECTER_WAKE = 5,
    SPECTER_CHASE = 7,
    SPECTER_HIT = 0x10,
    SPECTER_DEATH = 0x12,
};

// shared by every Specter, as on the Saturn (0x060EF11C-0x060EF12A; the
// Saturn's up/down pairs always hold the same value)
static s16 s_SpecterAccel;
static s32 s_SpecterMaxVY;
static s16 s_SpecterTopY;
static s16 s_SpecterBottomY;

static u8 specter_anim_wake[] = {6, 1, 5, 2, 4, 3, 4, 4, 0xFF, 0xFF};
static u8 specter_anim_chase[] = {4, 4, 4, 5, 4, 6, 4, 7, 0, 0};
static u8 specter_anim_hit[] = {8, 7, 8, 8, 0xFF, 0xFF};
static u8 specter_anim_hurt[] = {8, 9, 8, 10, 8, 11, 8, 12, 0, 0};
static u8 specter_anim_attack[] = {
    6, 13, 6, 14, 6, 15, 6, 16, 6, 17, 6, 18, 6, 19, 6, 20,
    6, 21, 6, 22, 6, 23, 6, 24, 0xFF, 0xFF};
// the one part of frames 1-24: its Saturn centre (cx, cy) (0x060ECE10)
static s8 specter_part_centre[25][2] = {
    {0, 0},    {3, -29}, {2, -29}, {1, -28}, {1, -28}, {2, -29}, {5, -29},
    {5, -28},  {1, -28}, {2, -29}, {5, -29}, {5, -28}, {1, -28}, {5, -27},
    {4, -27},  {4, -27}, {5, -27}, {6, -27}, {7, -27}, {6, -27}, {5, -27},
    {5, -27},  {4, -27}, {0, -27}, {5, -27},
};

static s32 SpecterHalf(s32 v) { return v / 2; } // the SH-2's signed v / 2

// the Saturn's distorted sprite: the frame's corners, the skew by speed,
// the hitbox that follows it and the vanish
static void SpecterDraw(Entity* self) {
    ET_ModSpecter* ext = SPECTER_EXT(self);
    Primitive* prim = &g_PrimBuf[self->primIndex];
    s16** frames = (s16**)g_api.o.spriteBanks[MOD_SPECTER_ANIMSET & 0x7FFF];
    s16 f = self->animCurFrame;
    s16* part;
    s16 x = self->posX.i.hi;
    s16 y = self->posY.i.hi;
    s16 xl, xr, yt, yb, cx, cy, s, t, wx, wy;
    u8 u0, u1, v0, v1;

    if (f < 1 || f > 24 || !frames || !frames[f]) {
        prim->drawMode = DRAW_HIDE;
        return;
    }
    part = frames[f] + 1;
    cx = specter_part_centre[f][0];
    cy = specter_part_centre[f][1];
    if (!self->facingLeft) {
        xl = x + part[1];
        xr = xl + part[3] + SAT_X(cx);
    } else {
        xl = x - part[1] - part[3];
        xr = xl + part[3] - SAT_X(cx);
    }
    yt = y + part[2] + 0x20;
    yb = yt + part[4] + cy + 0x20;
    prim->x0 = prim->x2 = xl;
    prim->x1 = prim->x3 = xr;
    prim->y0 = prim->y1 = yt;
    prim->y2 = prim->y3 = yb;

    // the skew: the top edge leans with the speed
    s = (s16)(self->velocityX >> 13);
    if (self->velocityX > 0) {
        prim->x0 += s;
        prim->x1 += s * 2;
        self->hitboxWidth = SAT_X(16) + SpecterHalf(s);
        self->hitboxOffX = -s;
    } else {
        prim->x0 += s * 2;
        prim->x1 += s;
        self->hitboxWidth = SAT_X(16) - SpecterHalf(s);
        self->hitboxOffX = s;
    }
    s = (s16)(self->velocityY >> 13);
    if (self->velocityY < 0) {
        prim->y0 += s * 2;
        prim->y1 += s * 2;
        self->hitboxHeight = 12 - SpecterHalf(s);
        self->hitboxOffY = SpecterHalf(s);
    } else {
        prim->y0 += s;
        prim->y1 += s;
        prim->y2 += s;
        prim->y3 += s;
        self->hitboxHeight = 12;
    }

    // the texture, mirrored by CMDCTRL when facing right
    u0 = part[7];
    v0 = part[8];
    u1 = part[9];
    v1 = part[10];
    if (part[6] & 1) {
        u0 += 0x80;
        u1 += 0x80;
    }
    if (part[6] & 2) {
        v0 += 0x80;
        v1 += 0x80;
    }
    if (self->facingLeft) {
        prim->u0 = prim->u2 = u1 - 1;
        prim->u1 = prim->u3 = u0 - 1;
    } else {
        prim->u0 = prim->u2 = u0;
        prim->u1 = prim->u3 = u1;
    }
    prim->v0 = prim->v1 = v0;
    prim->v2 = prim->v3 = v1;
    prim->tpage = part[6] >> 2;
    prim->clut = part[5] + ext->pal;
    prim->priority = self->zPriority;
    prim->drawMode = DRAW_TPAGE2 | DRAW_TPAGE | DRAW_TRANSP;

    if (self->step != SPECTER_DEATH || self->step_s != 3) {
        return;
    }
    // the vanish: wider and flatter, then a thin vertical streak
    t = (s16)(ext->counter << 7);
    if (t < 0x800) {
        wx = rsin(t) >> 6;
        wy = rsin(t >> 1) / 192;
    } else if (t > 0xBFF) {
        DestroyEntity(self);
        return;
    } else {
        wx = (rsin(t) * 2 / 5) >> 6;
        wy = (rsin(t >> 1) >> 4) - 232;
    }
    wx = SAT_X(wx);
    prim->x0 -= wx;
    prim->x1 += wx;
    prim->x2 -= wx;
    prim->x3 += wx;
    prim->y0 += wy;
    prim->y1 += wy;
    prim->y2 -= wy;
    prim->y3 -= wy;
    if (++ext->counter >= 0xB5) {
        DestroyEntity(self);
    }
}

// the angle to the player in Saturn space; the target speed scaled back
static void SpecterAim(Entity* self) {
    ET_ModSpecter* ext = SPECTER_EXT(self);
    s32 angle = ratan2(PLAYER.posY.i.hi - self->posY.i.hi,
                       SAT_X_INV(PLAYER.posX.i.hi - self->posX.i.hi));

    ext->targetVX = SAT_X(rcos(angle) << 5);
    ext->targetVY = rsin(angle) << 5;
}

static void SpecterApproach(s32* v, s32 target) {
    if (*v < target) {
        *v += s_SpecterAccel;
    } else if (*v > target) {
        *v -= s_SpecterAccel;
    }
    if ((target >> 11) == (*v >> 11)) {
        *v = target;
    }
}

void EntityModSpecter(Entity* self) {
    ET_ModSpecter* ext = SPECTER_EXT(self);
    s16 dx, dy, timer;
    s32 primIndex;

    if (self->hitParams && (self->step & 1)) {
        SetStep(SPECTER_HIT);
    }
    // a holy hit leaves it one hit point
    if (self->step != SPECTER_DEATH && self->nFramesInvincibility == 5) {
        self->hitPoints = 1;
    }
    if (self->step != SPECTER_DEATH && (self->flags & FLAG_DEAD)) {
        SetStep(SPECTER_DEATH);
        self->hitboxState = 0;
    }

    switch (self->step) {
    case SPECTER_INIT:
        InitializeEntity(g_EInitModSpecter);
        self->hitboxWidth = SAT_X(16);
        self->hitboxHeight = 16;
        self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        self->step = SPECTER_FLOAT;
        primIndex = g_api.AllocPrimitives(PRIM_GT4, 1);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        // the primitive draws it, not the entity sprite
        self->animSet = 0;
        self->animCurFrame = 1;
        ext->pal = 0;
        break;

    case SPECTER_FLOAT:
        dx = GetDistanceToPlayerX();
        switch (self->step_s) {
        case 0:
            s_SpecterAccel = (((Random() << 8) | Random()) & 0x1FF) + 0x700;
            s_SpecterMaxVY = (((Random() << 8) | Random()) & 0x3FFF) + 0xE000;
            s_SpecterTopY = (Random() & 15) + 0x48;
            s_SpecterBottomY = (Random() & 15) + 0x68;
            self->step_s++;
            // fallthrough
        case 1:
            if (self->posY.i.hi < s_SpecterTopY) {
                self->velocityY += s_SpecterAccel;
            } else if (self->velocityY > -s_SpecterMaxVY) {
                self->velocityY -= s_SpecterAccel;
            }
            if (self->velocityY > 0) {
                self->velocityY = 0;
                self->step_s = 2;
                s_SpecterBottomY = (Random() & 15) + 0x68;
            }
            break;
        case 2:
            if (self->posY.i.hi > s_SpecterBottomY) {
                self->velocityY -= s_SpecterAccel;
            } else if (self->velocityY < s_SpecterMaxVY) {
                self->velocityY += s_SpecterAccel;
            }
            if (self->velocityY < 0) {
                self->velocityY = 0;
                self->step_s = 1;
                s_SpecterTopY = (Random() & 15) + 0x48;
            }
            break;
        }
        MoveEntity();
        if (dx < SAT_X(0x80) && !(g_Timer % 60)) {
            SetStep(SPECTER_WAKE);
        }
        if (!(g_Timer % 30)) {
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        }
        self->velocityX = self->velocityX * 15 / 16;
        break;

    case SPECTER_WAKE:
        self->velocityY = self->velocityY * 15 / 16;
        if (!AnimateEntity(specter_anim_wake, self)) {
            SetStep(SPECTER_CHASE);
        }
        break;

    case SPECTER_CHASE:
        dx = GetDistanceToPlayerX();
        dy = GetDistanceToPlayerY();
        if (self->step_s == 0) {
            ext->attacking = 0;
            ext->counter = 0;
            SpecterAim(self);
            self->facingLeft = self->velocityX > 0;
            self->step_s++;
        }
        if (!(g_Timer % 30)) {
            SpecterAim(self);
        }
        SpecterApproach(&self->velocityX, ext->targetVX);
        SpecterApproach(&self->velocityY, ext->targetVY);
        self->facingLeft = self->velocityX > 0;
        // kept on the screen
        if (self->posY.i.hi < 0) {
            self->velocityY += s_SpecterAccel;
        } else if (self->posY.i.hi > 0xF0) {
            self->velocityY -= s_SpecterAccel;
        }
        if (self->posX.i.hi < 0) {
            self->velocityX += s_SpecterAccel;
        } else if (self->posX.i.hi > SAT_X(0x140)) {
            self->velocityX -= s_SpecterAccel;
        }
        MoveEntity();
        if (!ext->attacking && dx < SAT_X(0x30) && dy < 0x20 &&
            self->facingLeft == ((GetSideToPlayer() & 1) ^ 1)) {
            self->pose = 0;
            self->poseTimer = 0;
            ext->attacking = 1;
        }
        if (ext->attacking) {
            if (!AnimateEntity(specter_anim_attack, self)) {
                self->pose = 0;
                self->poseTimer = 0;
                ext->attacking = 0;
            }
        } else {
            AnimateEntity(specter_anim_chase, self);
        }
        break;

    case SPECTER_HIT:
        switch (self->step_s) {
        case 0:
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            self->velocityX = self->facingLeft ? SAT_X(FIX(-1.5)) : SAT_X(FIX(1.5));
            ext->hitboxSave = self->hitboxState;
            self->hitboxState = 0;
            ext->hitTimer = 90;
            self->step_s++;
            // fallthrough
        case 1:
            if (!AnimateEntity(specter_anim_hit, self)) {
                self->pose = 0;
                self->poseTimer = 0;
                self->step_s++;
            }
            break;
        case 2:
            AnimateEntity(specter_anim_hurt, self);
            break;
        }
        // the palettes 2-9 fade the flash out
        timer = ext->hitTimer;
        if (timer >= 80) {
            ext->pal = 2;
        } else if (timer >= 70) {
            ext->pal = 3;
        } else if (timer >= 60) {
            ext->pal = 4;
        } else if (timer >= 50) {
            ext->pal = 5;
        } else if (timer >= 40) {
            ext->pal = 6;
        } else if (timer >= 30) {
            ext->pal = 7;
        } else if (timer >= 20) {
            ext->pal = 8;
        } else if (timer >= 10) {
            ext->pal = 9;
        } else {
            ext->pal = timer < 2 ? 0 : timer;
        }
        if (--ext->hitTimer < 0) {
            SetStep(SPECTER_FLOAT);
            self->hitboxState = ext->hitboxSave;
        }
        self->velocityX -= self->velocityX / 60;
        self->velocityY -= self->velocityY / 30;
        MoveEntity();
        break;

    case SPECTER_DEATH:
        switch (self->step_s) {
        case 0:
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            self->velocityX = self->facingLeft ? SAT_X(FIX(-1.5)) : SAT_X(FIX(1.5));
            self->step_s++;
            PlaySfxPositional(MOD_SFX_SPECTER_CRY);
            // fallthrough
        case 1:
            if (!AnimateEntity(specter_anim_hit, self)) {
                self->pose = 0;
                self->poseTimer = 0;
                ext->counter = 0;
                self->step_s++;
            }
            break;
        case 2:
            AnimateEntity(specter_anim_hurt, self);
            if (++ext->counter > 50) {
                ext->counter = 0;
                self->step_s++;
                self->velocityX = 0;
                self->velocityY = 0;
                PlaySfxPositional(MOD_SFX_SPECTER_VANISH);
            }
            break;
        }
        self->velocityX -= self->velocityX / 32;
        self->velocityY -= self->velocityY / 16;
        MoveEntity();
        break;
    }
    // the shimmer between palettes 0 and 1 (0x060ECB22)
    if (self->step != SPECTER_HIT && self->step != SPECTER_DEATH) {
        ext->pal = (g_Timer & 2) ? 1 : 0;
    }
    SpecterDraw(self);
}

// ------------------------------------------------------- Will-o'-Wisp

typedef struct {
    /* Saturn 0x84 */ s16 count;
    /* Saturn 0x9C */ s16 timer;
} ET_ModWispSpawner;

typedef struct {
    /* Saturn 0x94 */ s16 bob;
    /* Saturn 0x9E */ s16 timer;
    /* Saturn 0xA4 */ u16 hitboxSave;
    // The Saturn keeps posY on the path and draws (and hits) at ext 0x98 =
    // posY + the bob; the PS1 draws at posY, so posY holds the drawn place
    // between updates and dy the offset from the path
    s32 dy;
} ET_ModWisp;

typedef struct {
    /* Saturn 0x9C */ s32 timer;
} ET_ModWispFx;

#define WISP_SPAWNER_EXT(e) ((ET_ModWispSpawner*)&(e)->ext)
#define WISP_EXT(e) ((ET_ModWisp*)&(e)->ext)
#define WISP_FX_EXT(e) ((ET_ModWispFx*)&(e)->ext)

enum {
    WISP_INIT = 0,
    WISP_IDLE = 1,
    WISP_APPEAR = 2,
    WISP_SWOOP = 3,
    WISP_HIT = 0x12,
    WISP_DEATH = 0x14,
};

static u8 wisp_anim_idle[] = {5, 1, 5, 2, 5, 3, 5, 4, 5, 5, 5, 6, 0, 0};
static u8 wisp_anim_turn[] = {5, 1, 5, 7, 5, 8, 5, 9, 5, 10, 0xFF, 0xFF};
static u8 wisp_anim_swoop[] = {5, 11, 5, 12, 5, 13, 5, 14, 5, 15, 5, 16, 0, 0};
static u8 wisp_anim_flame[] = {
    3, 17, 3, 18, 2, 19, 3, 20, 3, 21, 3, 22, 2, 23, 0xFF, 0xFF};
static u8 wisp_anim_hit[] = {2, 16, 2, 8, 2, 7, 2, 1, 2, 25, 0xFF, 0xFF};
static u8 wisp_anim_shrink[] = {6, 25, 6, 26, 0, 0};
static u8 wisp_anim_burst[] = {
    2, 26, 3, 27, 4, 28, 4, 29, 4, 30, 5, 31, 5, 32, 5, 33, 0xFF, 0xFF};
static u8 wisp_anim_appear[] = {5, 34, 4, 35, 4, 36, 4, 37, 4, 38, 0xFF, 0xFF};
static u16 wisp_burst_colors[16] = {MOD_WISP_COLORS};

void EntityModWispSpawner(Entity* self) {
    ET_ModWispSpawner* ext = WISP_SPAWNER_EXT(self);
    Entity* wisp = self + 1;

    if (!self->step) {
        InitializeEntity(g_EInitInteractable);
        ext->timer = 1000;
        ext->count = 0;
    }
    if (wisp->entityId != E_MOD_WISP && ++ext->timer > 300 &&
        ++ext->count < 17) {
        CreateEntityFromEntity(E_MOD_WISP, self, wisp);
        wisp->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        ext->timer = 0;
        wisp->posX.i.hi += (Random() & 15) - 8;
        wisp->posY.i.hi += (Random() & 15) - 8;
        wisp->params = ext->count;
    }
}

void EntityModWisp(Entity* self) {
    ET_ModWisp* ext = WISP_EXT(self);
    Entity* e;
    s32 drawY;

    // back on the path
    self->posY.val -= ext->dy;
    if (self->hitParams && (self->step & 1)) {
        SetStep(WISP_HIT);
        self->velocityX = self->velocityY = 0;
        self->hitboxState = 0;
    }
    if ((self->flags & FLAG_DEAD) && self->step != WISP_DEATH) {
        SetStep(WISP_DEATH);
        self->velocityX = self->velocityY = 0;
        self->hitboxState = 0;
    }

    switch (self->step) {
    case WISP_INIT:
        InitializeEntity(g_EInitModWisp);
        self->hitboxWidth = SAT_X(4);
        self->hitboxHeight = 4;
        self->hitboxOffX = 0;
        self->hitboxOffY = 0;
        self->blendMode = BLEND_ADD;
        self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
        self->velocityX = self->velocityY = 0;
        SetStep(WISP_APPEAR);
        ext->hitboxSave = self->hitboxState;
        self->hitboxState = 0;
        self->animCurFrame = 0;
        break;

    case WISP_IDLE:
        if (GetDistanceToPlayerX() < SAT_X(0x80)) {
            self->velocityX = self->velocityY = 0;
            SetStep(WISP_SWOOP);
            self->facingLeft = (GetSideToPlayer() & 1) ^ 1;
            ext->timer = 0;
        }
        AnimateEntity(wisp_anim_idle, self);
        break;

    case WISP_APPEAR:
        if (!AnimateEntity(wisp_anim_appear, self)) {
            self->velocityX = self->velocityY = 0;
            SetStep(WISP_IDLE);
            self->hitboxState = ext->hitboxSave;
        }
        break;

    case WISP_SWOOP:
        switch (self->step_s) {
        case 0:
            ext->timer = 0;
            self->step_s++;
            // fallthrough
        case 1:
            if (!AnimateEntity(wisp_anim_turn, self)) {
                self->step_s++;
                self->pose = 0;
                self->poseTimer = 0;
            }
            break;
        case 2:
            if (GetDistanceToPlayerX() < SAT_X(0x48)) {
                self->step_s++;
                ext->timer = 0;
            }
            break;
        case 3:
            CreateEntityFromEntity(E_MOD_WISP_FLAME, self, self + 1);
            self[1].facingLeft = self->facingLeft;
            ext->timer = 0;
            self->step_s++;
            PlaySfxPositional(MOD_SFX_WISP_FLAME);
            break;
        case 4:
            if (++ext->timer > 0x20) {
                self->step_s++;
            }
            break;
        }
        MoveEntity();
        if (!self->facingLeft) {
            if (self->velocityX > SAT_X(FIX(-2))) {
                self->velocityX -= SAT_X(0x2222);
            }
        } else if (self->velocityX < SAT_X(FIX(2))) {
            self->velocityX += SAT_X(0x2222);
        }
        self->posY.val += FIX(0.25);
        if (self->step_s > 1) {
            AnimateEntity(wisp_anim_swoop, self);
        }
        if ((u16)(self->posX.i.hi + SAT_X(0x20)) > SAT_X(0x170)) {
            DestroyEntity(self);
            return;
        }
        break;

    case WISP_HIT:
    case WISP_DEATH:
        switch (self->step_s) {
        case 0:
            self->velocityX = self->facingLeft ? SAT_X(FIX(-3)) : SAT_X(FIX(3));
            self->velocityY = -self->velocityY;
            self->step_s++;
            // fallthrough
        case 1:
            if (!AnimateEntity(wisp_anim_hit, self)) {
                self->step_s++;
                ext->timer = 0;
                self->pose = 0;
                self->poseTimer = 0;
            }
            break;
        case 2:
            AnimateEntity(wisp_anim_shrink, self);
            if (++ext->timer > 0x12) {
                self->pose = 0;
                self->poseTimer = 0;
                ext->timer = 0;
                self->step_s++;
                PlaySfxPositional(MOD_SFX_WISP_BURST);
            }
            break;
        case 3:
            if (!AnimateEntity(wisp_anim_burst, self)) {
                DestroyEntity(self);
                return;
            }
            switch (self->animCurFrame) {
            case 0x1E:
            case 0x1F:
                self->palette = 2;
                break;
            case 0x20:
                self->palette = 3;
                break;
            case 0x21:
                self->palette = 4;
                break;
            default:
                self->palette = 0;
                break;
            }
            if (++ext->timer == 0x11) {
                e = AllocEntity(&g_Entities[224], &g_Entities[256]);
                if (e != NULL) {
                    CreateEntityFromEntity(E_MOD_WISP_BURST, self, e);
                    e->posY.val = self->posY.val + ext->dy;
                    e->zPriority = self->zPriority;
                }
            }
            break;
        }
        MoveEntity();
        if (!self->facingLeft) {
            if (self->velocityX > 0) {
                self->velocityX -= SAT_X(0x1999);
            }
        } else if (self->velocityX < 0) {
            self->velocityX += SAT_X(0x1999);
        }
        break;
    }

    // the bob: drawn (and hit) up to 4 px around 12 px above the path
    if (self->step == WISP_DEATH) {
        drawY = self->posY.val + rsin(ext->bob << 5) * 0x40 - FIX(12);
    } else {
        drawY = self->posY.val + rsin(ext->bob << 6) * 0x40 - FIX(12);
        ext->bob++;
    }
    ext->dy = drawY - self->posY.val;
    self->posY.val = drawY;
    self->hitboxOffY = 0;
    if (self->step == WISP_SWOOP && !(g_Timer % 6)) {
        e = AllocEntity(&g_Entities[160], &g_Entities[192]);
        if (e != NULL) {
            DestroyEntity(e);
            CreateEntityFromEntity(E_MOD_WISP_TRAIL, self, e);
            e->facingLeft = self->facingLeft;
            e->animCurFrame = self->animCurFrame;
            e->zPriority = self->zPriority - 2;
        }
    }
}

void EntityModWispFlame(Entity* self) {
    Entity* wisp = self - 1;
    s16 pathY;

    // (and when the wisp is gone: the Saturn would follow an empty slot)
    if (wisp->step == WISP_DEATH || wisp->entityId != E_MOD_WISP) {
        DestroyEntity(self);
        return;
    }
    if (!self->step) {
        InitializeEntity(g_EInitModWispFlame);
        self->attack /= 4;
        self->blendMode = BLEND_ADD;
        self->zPriority = wisp->zPriority - 2;
        self->hitboxWidth = SAT_X(16);
        self->hitboxHeight = 16;
        self->hitboxOffX = SAT_X(-12);
    } else {
        if (!AnimateEntity(wisp_anim_flame, self)) {
            DestroyEntity(self);
            return;
        }
        switch (self->animCurFrame) {
        case 0x11:
        case 0x17:
            self->palette = 3;
            break;
        case 0x12:
        case 0x16:
            self->palette = 2;
            break;
        default:
            self->palette = 0;
            break;
        }
    }
    // drawn at the wisp's bob, hit 8 px below the wisp's path
    self->posX.i.hi = wisp->posX.i.hi;
    self->posY.val = wisp->posY.val;
    pathY = (wisp->posY.val - WISP_EXT(wisp)->dy) >> 16;
    self->hitboxOffY = pathY + 8 - self->posY.i.hi;
}

void EntityModWispTrail(Entity* self) {
    ET_ModWispFx* ext = WISP_FX_EXT(self);
    s16 frame;

    if (!self->step) {
        frame = self->animCurFrame;
        InitializeEntity(g_EInitModWispFx);
        self->animCurFrame = frame;
        self->blendMode = BLEND_ADD;
        ext->timer = 0;
        self->palette = 2;
        return;
    }
    if (!(++ext->timer & 3)) {
        self->zPriority--;
    }
    if (ext->timer < 10) {
        self->palette = 2;
    } else if (ext->timer < 20) {
        self->palette = 3;
    } else if (ext->timer < 30) {
        self->palette = 4;
    } else {
        DestroyEntity(self);
    }
}

// 64 two-pixel sparks fly out, through the wisp palette's 16 colours
void EntityModWispBurst(Entity* self) {
    ET_ModWispFx* ext = WISP_FX_EXT(self);
    Primitive* prim;
    s32 primIndex;
    s32 speed;
    s32 angle;
    u16 c;

    if (!self->step) {
        InitializeEntity(g_EInitModWispFx);
        self->animSet = 0;
        primIndex = g_api.AllocPrimitives(PRIM_TILE, 0x40);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        for (prim = &g_PrimBuf[primIndex]; prim != NULL; prim = prim->next) {
            speed = (((Random() << 8) | Random()) % 0x6000) + FIX(2);
            angle = ((Random() << 8) | Random()) % 0xFFF;
            // the speeds in 1/256 px: x = 1.2 * cos, scaled 4/5
            prim->x2 = SAT_X((speed * rcos(angle) >> 12) * 6 / 5) >> 8;
            prim->y2 = (speed * rsin(angle) >> 12) >> 8;
            prim->u0 = 1;
            prim->v0 = 2;
            prim->priority = self->zPriority;
            prim->drawMode = DRAW_TPAGE2 | DRAW_TPAGE | DRAW_TRANSP;
        }
        ext->timer = 0;
    }
    ext->timer++;
    c = wisp_burst_colors[(ext->timer / 5) & 15];
    for (prim = &g_PrimBuf[self->primIndex]; prim != NULL; prim = prim->next) {
        prim->x0 = self->posX.i.hi + ((prim->x2 * ext->timer) >> 8);
        prim->y0 = self->posY.i.hi + ((prim->y2 * ext->timer) >> 8);
        prim->r0 = (c & 0x1F) << 3;
        prim->g0 = ((c >> 5) & 0x1F) << 3;
        prim->b0 = ((c >> 10) & 0x1F) << 3;
    }
    if (ext->timer >= 0x4F) {
        DestroyEntity(self);
    }
}
