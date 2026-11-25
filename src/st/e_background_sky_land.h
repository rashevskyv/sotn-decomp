// SPDX-License-Identifier: AGPL-3.0-or-later

extern EInit g_EInitInteractable;

// Wide view: the row of 62 px tiles starts SKY_LAND_LEAD() tiles left of 0 and
// has SKY_LAND_COUNT() tiles, to reach -WIDE_X and 0x100 + WIDE_X. 5 tiles from
// 0 on PS1 and in 4:3.
#define SKY_LAND_LEAD() ((WIDE_X + 61) / 62)
#define SKY_LAND_COUNT() (SKY_LAND_LEAD() + (0x100 + WIDE_X + 61) / 62)

// Row of blue "land" tiles at the bottom of the sky background.
void EntityBackgroundSkyLand(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    s32 x;
#ifdef PLATFORM_PSYZ
    s32 lead = SKY_LAND_LEAD();
#else
#define lead 0
#endif

    if (!self->step) {
        InitializeEntity(g_EInitInteractable);
        primIndex = g_api.AllocPrimitives(
            PRIM_GT4, WIDE_POOL(5, SKY_LAND_COUNT()));
#ifdef PLATFORM_PSYZ
        if (primIndex == -1 && lead) {
            lead = 0; // 4:3 only
            primIndex = g_api.AllocPrimitives(PRIM_GT4, 5);
        }
#endif
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        prim = &g_PrimBuf[primIndex];
        for (x = -lead * 62; prim != NULL; prim = prim->next) {
            prim->tpage = 15;
            prim->clut = PAL_BACKGROUND_SKY_LAND;
            prim->u0 = prim->u2 = 65;
            prim->u1 = prim->u3 = 127;
            prim->v0 = prim->v1 = 169;
            prim->v2 = prim->v3 = 198;
            prim->x0 = prim->x2 = x;
            x += 62;
            prim->x1 = prim->x3 = x;
#ifdef STAGE_IS_RDAI
            prim->y0 = prim->y1 = 64;
            prim->y2 = prim->y3 = 18;
#else
            prim->y0 = prim->y1 = 192;
            prim->y2 = prim->y3 = 228;
#endif
            prim->priority = 16;
            prim->drawMode = DRAW_DEFAULT;
        }
    }
}

#undef lead
