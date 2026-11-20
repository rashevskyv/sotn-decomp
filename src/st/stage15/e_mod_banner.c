// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * The Underground Garden's area name banner (Saturn STAGE_15 0x09,
 * func_060E4064). It is the PS1 banner (e_stage_name_jp.h, the "shutter"
 * version) with Saturn art: a box opens, a star sweeps the name in from the
 * right as a two-row band, the band opens up and down, the name holds and
 * the box wipes it away to the left. The name is bank 18's three 64x48
 * thirds, "地下庭園 / Underground Garden", and image 6 is the star
 * (gdn_entities.py: MOD_BANNER_PARTS). Saturn values, x scaled by 4/5
 * around 128 and y moved 5 up: box 20..300 x 153..209, text 64..256 x
 * 157..203, sweep 10 per frame, wipe 5 per frame, hold 32, castle flag 0xF0
 * set when the hold ends.
 */
#include "stage15.h"

#define BANNER_FLAG 0xF0
#define BANNER_CX 128
#define BANNER_CY 176
#define BANNER_TEXT_X 51 // 64 Saturn
#define BANNER_TEXT_TOP 152
#define BANNER_TEXT_BOTTOM 198
#define BANNER_BOX_LEFT 16
#define BANNER_TOP_LIMIT 150 // the box's top stops at 148
#define BANNER_PARTS 3

static const s16 s_BannerParts[][6] = {MOD_BANNER_PARTS};

// the name's prims show the part of the text inside x0..x1, y0..y1
static void BannerClip(Entity* self, s32 x0, s32 x1, s32 y0, s32 y1) {
    Primitive* prim = self->ext.stpopupj.firstTextPrim;
    const s16* p;
    s32 px, l, r, i;

    for (i = 0, px = BANNER_TEXT_X; i < BANNER_PARTS; i++, px += p[4]) {
        p = s_BannerParts[i];
        l = MAX(x0, px);
        r = MIN(x1, px + p[4]);
        if (l >= r || y0 >= y1) {
            prim->drawMode = DRAW_HIDE;
        } else {
            prim->drawMode = DRAW_COLORS;
            prim->x0 = prim->x2 = l;
            prim->x1 = prim->x3 = r;
            prim->y0 = prim->y1 = y0;
            prim->y2 = prim->y3 = y1;
            prim->u0 = prim->u2 = p[2] + l - px;
            prim->u1 = prim->u3 = p[2] + r - px;
            prim->v0 = prim->v1 = p[3] + y0 - BANNER_TEXT_TOP;
            prim->v2 = prim->v3 = p[3] + y1 - BANNER_TEXT_TOP;
        }
        prim = prim->next;
    }
}

void EntityModBanner(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    s32 i;
    bool busy;
    s16* top = &self->ext.stpopupj.rotationSlices[0];
    s16* bottom = &self->ext.stpopupj.rotationSlices[1];
    s16* front = &self->ext.stpopupj.rotationSlices[2];

    switch (self->step) {
    case 0:
        if (g_CastleFlags[BANNER_FLAG]) {
            DestroyEntity(self);
            return;
        }
        InitializeEntity(g_EInitInteractable);
        primIndex = g_api.AllocPrimitives(PRIM_GT4, BANNER_PARTS + 2);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->primIndex = primIndex;
        self->flags |= FLAG_HAS_PRIMS;
        prim = &g_PrimBuf[primIndex];
        self->ext.stpopupj.firstTextPrim = prim;
        for (i = 0; i <= BANNER_PARTS; i++) {
            prim->tpage = s_BannerParts[i][0];
            prim->clut = s_BannerParts[i][1];
            prim->r0 = prim->g0 = prim->b0 = 128;
            LOW(prim->r1) = LOW(prim->r0);
            LOW(prim->r2) = LOW(prim->r0);
            LOW(prim->r3) = LOW(prim->r0);
            prim->priority = 0xC0;
            prim->drawMode = DRAW_HIDE;
            if (i == BANNER_PARTS) {
                // the star: image 6, drawn added
                self->ext.stpopupj.firstStarPrim = prim;
                prim->u0 = prim->u2 = s_BannerParts[i][2];
                prim->u1 = prim->u3 = prim->u0 + s_BannerParts[i][4];
                prim->v0 = prim->v1 = s_BannerParts[i][3];
                prim->v2 = prim->v3 = prim->v0 + s_BannerParts[i][5];
                prim->priority = 0xC1;
            }
            prim = prim->next;
        }
        // the box: the PS1 banner's gradient
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
        *top = BANNER_CY;
        *bottom = BANNER_CY + 2;
        *front = BANNER_TEXT_X + s_BannerParts[0][4] + s_BannerParts[1][4] +
                 s_BannerParts[2][4];
        break;
    case 1:
        switch (self->step_s) {
        case 0: // the box opens
            busy = false;
            prim = self->ext.stpopupj.boxPrim;
            if (prim->y0 > BANNER_TOP_LIMIT) {
                prim->y0 = prim->y1 -= 4;
                prim->y2 = prim->y3 += 4;
                busy = true;
            }
            if (prim->x0 > BANNER_BOX_LEFT) {
                prim->x0 = prim->x2 -= 8;
                prim->x1 = prim->x3 += 8;
                busy = true;
            }
            if (!busy) {
                self->step_s++;
            }
            break;
        case 1: // the star sweeps the band in from the right
            *front -= 8;
            prim = self->ext.stpopupj.firstStarPrim;
            prim->drawMode =
                DRAW_TRANSP | DRAW_TPAGE | DRAW_TPAGE2 | DRAW_COLORS;
            prim->x0 = prim->x2 = *front - s_BannerParts[3][4] / 2;
            prim->x1 = prim->x3 = prim->x0 + s_BannerParts[3][4];
            prim->y0 = prim->y1 = BANNER_CY + 1 - s_BannerParts[3][5] / 2;
            prim->y2 = prim->y3 = prim->y0 + s_BannerParts[3][5];
            if (*front <= BANNER_TEXT_X) {
                self->step_s++;
            }
            BannerClip(self, *front, 256, *top, *bottom);
            break;
        case 2: // the band opens up and down, the star fades
            prim = self->ext.stpopupj.firstStarPrim;
            if (prim->r0 > 4) {
                prim->r0 = prim->g0 = prim->b0 -= 4;
                LOW(prim->r1) = LOW(prim->r0);
                LOW(prim->r2) = LOW(prim->r0);
                LOW(prim->r3) = LOW(prim->r0);
            } else {
                prim->drawMode = DRAW_HIDE;
            }
            if (!(g_Timer & 1)) {
                if (*top > BANNER_TEXT_TOP) {
                    (*top)--;
                } else {
                    self->step_s++;
                }
                if (*bottom <= BANNER_TEXT_BOTTOM) {
                    (*bottom)++;
                }
            }
            BannerClip(self, BANNER_TEXT_X, 256, *top, *bottom);
            break;
        case 3:
            self->step = 32;
            self->ext.stpopupj.timer = 32;
            break;
        }
        break;
    case 32: // held
        if (!--self->ext.stpopupj.timer) {
            g_CastleFlags[BANNER_FLAG] = 1;
            self->ext.stpopupj.firstStarPrim->drawMode = DRAW_HIDE;
            self->step_s = 0;
            self->step = 18;
        }
        break;
    case 18: // the box wipes the name away to the left
        prim = self->ext.stpopupj.boxPrim;
        prim->x1 = prim->x3 -= 4;
        BannerClip(self, BANNER_TEXT_X, prim->x1, *top, *bottom);
        if (prim->x1 < prim->x0) {
            DestroyEntity(self);
        }
        break;
    }
}
