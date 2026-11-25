// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rnz0.h"

extern EInit g_EInitInteractable;

// Wide view: the grid is 3 columns of 0x80 px, starting in -0x80..-1. It gets
// BRICK_LEAD() columns more on the left and (WIDE_X + 127) >> 7 more on the
// right, to reach -WIDE_X and 0x100 + WIDE_X. 3 columns on PS1 and in 4:3.
#define BRICK_LEAD() ((WIDE_X + 126) >> 7)
#define BRICK_COLS() (3 + BRICK_LEAD() + ((WIDE_X + 127) >> 7))

// A purplish-red brick background that scrolls behind the foreground layer
void EntityPurpleBrickScrollingBackground(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    s32 tempPosX;
    s32 tempPosY;
    s32 x, y;
#ifdef PLATFORM_PSYZ
    s32 cols, lead;
#else
#define cols 3
#define lead 0
#endif

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        self->posX.i.hi = 0;
        self->posY.i.hi = 0;
        self->unk68 = 0x80;
        // Composed of 15 primitives
        primIndex =
            g_api.AllocPrimitives(PRIM_GT4, WIDE_POOL(15, 5 * BRICK_COLS()));
#ifdef PLATFORM_PSYZ
        self->step_s = BRICK_COLS(); // the columns, as spawned
        if (primIndex == -1) {
            self->step_s = 3; // 4:3 only
            primIndex = g_api.AllocPrimitives(PRIM_GT4, 15);
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
            prim->clut = 4;
            prim->u0 = prim->u2 = 0x80;
            prim->u1 = prim->u3 = 0xFF;
            prim->v0 = prim->v1 = 0x80;
            prim->v2 = prim->v3 = 0xBF;
            prim->priority = 0x20;
            prim->drawMode = DRAW_UNK02;
            prim = prim->next;
        }

    case 1:
        // Add a scrolling effect
        tempPosX = self->posX.i.hi;
        tempPosX = tempPosX & 0x7F;
        tempPosX = (tempPosX)-0x80;
        tempPosY = self->posY.i.hi;
        tempPosY = tempPosY & 0x3F;
        tempPosY = tempPosY - 0x40;
        prim = self->ext.prim;
#ifdef PLATFORM_PSYZ
        cols = self->step_s;
        lead = (cols - 3) / 2; // BRICK_COLS() is 3 + 2 * lead (+ 1)
#endif
        // Primitives are laid out in a 5-tall by 3-wide grid
        for (y = 0; y < 5; y++) {
            for (x = -lead; x < cols - lead; x++) {
                prim->x0 = prim->x2 = tempPosX + (x * 0x80);
                prim->x1 = prim->x3 = tempPosX + (x * 0x80) + 0x80;
                prim->y0 = prim->y1 = tempPosY + (y * 0x40);
                prim->y2 = prim->y3 = tempPosY + (y * 0x40) + 0x40;
                prim->drawMode = DRAW_DEFAULT;
                prim = prim->next;
            }
        }

        while (prim != NULL) {
            prim->drawMode = DRAW_HIDE;
            prim = prim->next;
        }
    }
}

#undef cols
#undef lead
