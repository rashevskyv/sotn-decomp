// SPDX-License-Identifier: AGPL-3.0-or-later
#include "nz1.h"

static u8 D_us_80180FB0[] = {
    6, 1, 6, 2, 6, 3, 0, 0,
};
extern EInit g_EInitInteractable;

// Wide view: the grid is 3 columns of 128 px, starting in -128..-1. It gets
// GEARS_LEAD() columns more on the left and (WIDE_X + 127) >> 7 more on the
// right, to reach -WIDE_X and 0x100 + WIDE_X. 3 columns on PS1 and in 4:3.
#define GEARS_LEAD() ((WIDE_X + 126) >> 7)
#define GEARS_COLS() (3 + GEARS_LEAD() + ((WIDE_X + 127) >> 7))

void EntityBackgroundGears(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    s32 i, j;
    s32 posX;
    s32 posY;
    s32 u;
    s32 v;
#ifdef PLATFORM_PSYZ
    s32 cols, lead;
#else
#define cols 3
#define lead 0
#endif

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        self->animSet = 0;
        self->posX.i.hi = 0;
        self->posY.i.hi = 0;
        self->unk68 = 0x80;
        primIndex =
            g_api.AllocPrimitives(PRIM_GT4, WIDE_POOL(9, 3 * GEARS_COLS()));
#ifdef PLATFORM_PSYZ
        self->step_s = GEARS_COLS(); // the columns, as spawned
        if (primIndex == -1) {
            self->step_s = 3; // 4:3 only
            primIndex = g_api.AllocPrimitives(PRIM_GT4, 9);
        }
#endif
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        prim = &g_PrimBuf[primIndex];
        self->ext.prim = prim;
        while (prim != NULL) {
            prim->tpage = 0xF;
            prim->clut = 8;
            prim->priority = 0x20;
            prim->drawMode = DRAW_UNK02;
            prim = prim->next;
        }
        // fallthrough

    case 1:
        AnimateEntity(D_us_80180FB0, self);

        posX = self->posX.i.hi;
        posX &= 0x7F;
        posX -= 128;
        posY = self->posY.i.hi;
        posY &= 0x7F;
        posY -= 128;
        u = 0;
        v = 0;
        if (self->animCurFrame == 2) {
            u = 128;
        }
        if (self->animCurFrame == 3) {
            v = 128;
        }

        prim = self->ext.prim;
#ifdef PLATFORM_PSYZ
        cols = self->step_s;
        lead = (cols - 3) / 2; // GEARS_COLS() is 3 + 2 * lead (+ 1)
#endif
        for (i = 0; i < 3; i++) {
            for (j = -lead; j < cols - lead; j++) {
                prim->x0 = prim->x2 = posX + (j * 128);
                prim->x1 = prim->x3 = posX + (j * 128) + 128;
                prim->y0 = prim->y1 = posY + (i * 128);
                prim->y2 = prim->y3 = posY + (i * 128) + 128;
                prim->u0 = prim->u2 = u;
                prim->u1 = prim->u3 = u + 127;
                prim->v0 = prim->v1 = v;
                prim->v2 = prim->v3 = v + 127;
                prim->drawMode = DRAW_DEFAULT;
                prim = prim->next;
            }
        }
        while (prim != NULL) {
            prim->drawMode = DRAW_HIDE;
            prim = prim->next;
        }
        break;
    }
}

#undef cols
#undef lead
