// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * The Cursed Prison's area name banner (Saturn STAGE_16 0x09, func_060E102C).
 * It is the PS1 prototype's "drum" popup (e_stage_name_jp.h,
 * STAGE_NAME_RENDER_DRUM) with Saturn art: a box opens, the name rolls down
 * onto a drum in eight strips, holds, spins away and the box closes. The
 * name is bank 18's two 128x48 halves, "呪われた牢獄 / Cursed Prison"
 * (gdn_entities.py: MOD_BANNER_PARTS). Saturn values: x around 148 (scaled
 * by 4/5 around 128 here), y around 181 (176 here), drum radius 24, depth
 * 0x414, castle flag 0xF8 set when it starts.
 */
#include "stage16.h"

#define BANNER_FLAG 0xF8
#define BANNER_CX 128
#define BANNER_CY 176
#define BANNER_RADIUS 24
#define BANNER_HALF_W 102 // 128 Saturn pixels narrowed to 4/5
#define BANNER_BOX_HALF_W 104
#define BANNER_BOX_HALF_H 28
#define BANNER_ROWS 4

static const s16 s_BannerParts[][6] = {MOD_BANNER_PARTS};

// one drum edge: the left, middle and right screen points of the text line
// at angle a (the Saturn's TransformAndProjectPoints with (±140, 0, -24))
static void BannerEdge(s32 a, s32 depth, s16* x, s16* y, bool flat) {
    s32 z = depth - (BANNER_RADIUS * rcos(a) >> 12);
    s32 h = 0x414 - BANNER_RADIUS; // the flat text's scale is 1
    s32 dy = BANNER_RADIUS * rsin(a) >> 12;

    if (flat) {
        z = h;
    }
    x[0] = BANNER_CX - BANNER_HALF_W * h / z;
    x[1] = BANNER_CX;
    x[2] = BANNER_CX + BANNER_HALF_W * h / z;
    *y = BANNER_CY + dy * h / z;
}

static void BannerDrum(Entity* self, bool flat) {
    Primitive* prim = self->ext.stpopupj.firstRollingTextPrim;
    s16 x[BANNER_ROWS + 1][3];
    s16 y[BANNER_ROWS + 1];
    s32 i, half;

    for (i = 0; i <= BANNER_ROWS; i++) {
        BannerEdge(self->ext.stpopupj.rotationSlices[i],
                   self->ext.stpopupj.depth, x[i], &y[i], flat);
    }
    for (half = 0; half < 2; half++) {
        for (i = 0; i < BANNER_ROWS; i++) {
            prim->x0 = x[i][half];
            prim->x1 = x[i][half + 1];
            prim->x2 = x[i + 1][half];
            prim->x3 = x[i + 1][half + 1];
            prim->y0 = prim->y1 = y[i];
            prim->y2 = prim->y3 = y[i + 1];
            prim = prim->next;
        }
    }
}

static void BannerShowText(Entity* self, u8 drawMode) {
    Primitive* prim = self->ext.stpopupj.firstRollingTextPrim;

    while (prim) {
        prim->drawMode = drawMode;
        prim = prim->next;
    }
}

void EntityModBanner(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    s32 i, half;
    s16* slice;
    bool busy;

    switch (self->step_s) {
    case 0:
        if (g_CastleFlags[BANNER_FLAG]) {
            DestroyEntity(self);
            return;
        }
        g_CastleFlags[BANNER_FLAG]++;
        InitializeEntity(g_EInitInteractable);
        primIndex = g_api.AllocPrimitives(PRIM_GT4, 1 + 2 * BANNER_ROWS);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->primIndex = primIndex;
        self->flags |= FLAG_HAS_PRIMS;
        prim = &g_PrimBuf[primIndex];
        // the box: the Saturn's texture is the PS1 box's gradient (palette
        // 0x060EA718: #102800 at the top to #080038 at the bottom)
        self->ext.stpopupj.boxPrim = prim;
        prim->type = PRIM_G4;
        prim->tpage = 26;
        prim->x0 = prim->x1 = prim->x2 = prim->x3 = BANNER_CX;
        prim->y0 = prim->y1 = prim->y2 = prim->y3 = BANNER_CY;
        prim->r0 = prim->r1 = 16;
        prim->g0 = prim->g1 = 40;
        prim->b0 = prim->b1 = 0;
        prim->r2 = prim->r3 = 8;
        prim->g2 = prim->g3 = 0;
        prim->b2 = prim->b3 = 56;
        prim->priority = 0xB0;
        prim->drawMode = DRAW_TRANSP | DRAW_TPAGE | DRAW_COLORS;
        prim = prim->next;
        self->ext.stpopupj.firstRollingTextPrim = prim;
        for (half = 0; half < 2; half++) {
            for (i = 0; i < BANNER_ROWS; i++) {
                prim->tpage = s_BannerParts[half][0];
                prim->clut = s_BannerParts[half][1];
                prim->u0 = prim->u2 = s_BannerParts[half][2];
                prim->u1 = prim->u3 =
                    s_BannerParts[half][2] + s_BannerParts[half][4];
                prim->v0 = prim->v1 = s_BannerParts[half][3] + i * 12;
                prim->v2 = prim->v3 = prim->v0 + 12;
                prim->r0 = prim->g0 = prim->b0 = 128;
                LOW(prim->r1) = LOW(prim->r0);
                LOW(prim->r2) = LOW(prim->r0);
                LOW(prim->r3) = LOW(prim->r0);
                prim->priority = 0xC0;
                prim->drawMode = DRAW_HIDE;
                prim = prim->next;
            }
        }
        self->ext.stpopupj.depth = 0x414;
        for (i = 0; i <= BANNER_ROWS; i++) {
            self->ext.stpopupj.rotationSlices[i] = ROT(-270);
        }
        self->step_s++;
        break;
    case 1: // the box opens
        busy = false;
        prim = self->ext.stpopupj.boxPrim;
        if (prim->y0 > BANNER_CY - BANNER_BOX_HALF_H) {
            prim->y0 = prim->y1 -= 4;
            prim->y2 = prim->y3 += 4;
            busy = true;
        }
        if (prim->x0 > BANNER_CX - BANNER_BOX_HALF_W) {
            prim->x0 = prim->x2 -= 8;
            prim->x1 = prim->x3 += 8;
            busy = true;
        }
        if (!busy) {
            BannerShowText(self, DRAW_COLORS);
            self->step_s++;
        }
        break;
    case 2: // the name rolls down onto the drum
        slice = self->ext.stpopupj.rotationSlices;
        slice[4] = MIN(slice[4] + 64, ROT(90));
        if (slice[4] > ROT(-225)) {
            slice[3] = MIN(slice[3] + 62, 341);
        }
        if (slice[3] > ROT(-225)) {
            slice[2] = MIN(slice[2] + 60, 0);
        }
        if (slice[2] > ROT(-225)) {
            slice[1] = MIN(slice[1] + 58, -341);
        }
        if (slice[1] > ROT(-225)) {
            if (slice[0] < ROT(-90)) {
                slice[0] += 56;
            } else {
                slice[0] = ROT(-90);
                self->ext.stpopupj.timer = 64;
                self->step_s++;
            }
        }
        BannerDrum(self, false);
        break;
    case 3: // held flat
        BannerDrum(self, true);
        if (!--self->ext.stpopupj.timer) {
            for (i = 0; i <= BANNER_ROWS; i++) {
                self->ext.stpopupj.rotationSlices[i] -= ROT(720);
            }
            self->step_s++;
        }
        break;
    case 4: // the drum spins away
        for (i = 0; i <= BANNER_ROWS; i++) {
            self->ext.stpopupj.rotationSlices[i] += 80;
        }
        self->ext.stpopupj.depth += 64;
        BannerDrum(self, false);
        if (self->ext.stpopupj.rotationSlices[4] > 0) {
            BannerShowText(self, DRAW_HIDE);
            self->step_s++;
        }
        break;
    case 5: // the box closes
        prim = self->ext.stpopupj.boxPrim;
        prim->y0 = prim->y1 += 2;
        prim->y2 = prim->y3 -= 2;
        prim->x0 = prim->x2 += 2;
        prim->x1 = prim->x3 -= 2;
        if (prim->y0 > prim->y2) {
            DestroyEntity(self);
        }
        break;
    }
}
