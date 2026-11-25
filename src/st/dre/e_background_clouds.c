// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dre.h"

extern EInit g_EInitInteractable;

// clouds in the background ID 0x18
void EntityBackgroundClouds(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    s32 camX, camY;
#ifdef PLATFORM_PSYZ
    s32 wx = WIDE_X; // the margin of this spawn; 0 if the pool is short
#else
#define wx WIDE_X
#endif

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        // wide view: the sky spans the margins and the clouds (95 px apart)
        // start left of -WIDE_X and run past 0x100 + WIDE_X
        primIndex = g_api.AllocPrimitives(
            PRIM_GT4, WIDE_POOL(5, 1 + (445 + 2 * wx) / 95));
#ifdef PLATFORM_PSYZ
        if (primIndex == -1 && wx) {
            wx = 0; // 4:3 only
            primIndex = g_api.AllocPrimitives(PRIM_GT4, 5);
        }
#endif
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->primIndex = primIndex;
        self->flags |= FLAG_HAS_PRIMS;
        prim = &g_PrimBuf[primIndex];
        self->ext.prim = prim;
        prim->type = PRIM_G4;
        prim->x0 = prim->x2 = -wx;
        prim->x1 = prim->x3 = 0x100 + wx;
        prim->y0 = prim->y1 = 0x6E - g_Tilemap.scrollY.i.hi;
        prim->y2 = prim->y3 = 0xF0;
        setRGB0(prim, 16, 16, 16);
        LOW(prim->r1) = LOW(prim->r0);
        LOW(prim->r2) = LOW(prim->r0);
        LOW(prim->r3) = LOW(prim->r0);
        prim->priority = 0x20;
        prim->drawMode = DRAW_DEFAULT;
        prim = prim->next;
        camX = -g_Tilemap.scrollX.i.hi;
#ifdef PLATFORM_PSYZ
        while (camX > -wx) {
            camX -= 95;
        }
        while (camX <= -wx - 95) {
            camX += 95;
        }
#endif
        camY = 32 - g_Tilemap.scrollY.i.hi;
        while (prim != NULL) {
            prim->tpage = 0xF;
            prim->clut = 5;
            prim->u0 = prim->u2 = 0x84;
            prim->u1 = prim->u3 = 0xE3;
            prim->v0 = prim->v1 = 1;
            prim->v2 = prim->v3 = 0x4F;
            prim->x0 = prim->x2 = camX;
            camX += 95;
            prim->x1 = prim->x3 = camX;
            prim->y0 = prim->y1 = camY;
            prim->y2 = prim->y3 = camY + 0x4E;
            prim->priority = 0x20;
            prim->drawMode = DRAW_DEFAULT;
            prim = prim->next;
        }
        break;

    case 255:
#include "../pad2_anim_debug.h"
    }
    g_GpuBuffers[0].draw.r0 = 40;
    g_GpuBuffers[0].draw.g0 = 24;
    g_GpuBuffers[0].draw.b0 = 24;
    g_GpuBuffers[1].draw.r0 = 40;
    g_GpuBuffers[1].draw.g0 = 24;
    g_GpuBuffers[1].draw.b0 = 24;
}

#undef wx
