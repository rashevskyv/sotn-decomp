// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rlib.h"

extern EInit g_EInitInteractable;

// Wide view: a parallax row of strips starts at or left of -WIDE_X and runs
// past 0x100 + WIDE_X. A strip is `pitch` px wide and the first one starts in
// (-WIDE_X - pitch, -WIDE_X], so the row needs STRIPS(pitch) strips (4 for the
// 4:3 view, the original count). WIDE_X is 0 on PS1.
// The margin that the entity may use: the width it was spawned with (its
// prim chain covers no more), at most the current one. 0 on PS1.
#define SCENERY_MARGIN(self) WIDE_POOL(0, MIN(WIDE_X, (s32)(self)->step_s))

#define STRIPS(pitch) ((255 + 2 * WIDE_X + (pitch)) / (pitch) + 1)

static SVECTOR D_us_80180DC0 = {0, 0, 512};
static SVECTOR D_us_80180DC8 = {0};

// parallax book cases
void EntityBackgroundBookcase(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    long p, flag;
    s16 x;
    long sxy;
    SVECTOR svector;
    VECTOR vector;
    MATRIX matrix;

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        primIndex = g_api.AllocPrimitives(PRIM_GT4, WIDE_POOL(4, STRIPS(124)));
#ifdef PLATFORM_PSYZ
        self->step_s = WIDE_X; // the margin the chain covers
        if (primIndex == -1) {
            self->step_s = 0;
            primIndex = g_api.AllocPrimitives(PRIM_GT4, 4); // 4:3 only
        }
#endif
        if (primIndex != -1) {
            self->flags |= FLAG_HAS_PRIMS;
            self->primIndex = primIndex;
            prim = &g_PrimBuf[primIndex];
            self->ext.prim = prim;
            while (prim != NULL) {
                prim->tpage = 0xF;
                prim->clut = 0x35;
                prim->u0 = prim->u2 = 130;
                prim->u1 = prim->u3 = 253;
                prim->v0 = prim->v1 = 50;
                prim->v2 = prim->v3 = 125;
                prim->priority = 0x40;
                prim->drawMode = DRAW_UNK02;
                prim = prim->next;
            }
        } else {
            DestroyEntity(self);
            return;
        }
        g_GpuBuffers[0].draw.r0 = 16;
        g_GpuBuffers[0].draw.g0 = 16;
        g_GpuBuffers[0].draw.b0 = 16;
        g_GpuBuffers[1].draw.r0 = 16;
        g_GpuBuffers[1].draw.g0 = 16;
        g_GpuBuffers[1].draw.b0 = 16;
        // fallthrough
    case 1:
        SetGeomScreen(1024);
        SetGeomOffset(128, self->posY.i.hi);
        RotMatrix(&D_us_80180DC8, &matrix);
        vector.vx = self->posX.i.hi - 128;
        vector.vy = 0;
        vector.vz = 1024;
        TransMatrix(&matrix, &vector);
        SetRotMatrix(&matrix);
        SetTransMatrix(&matrix);
        RotTransPers(&D_us_80180DC0, &sxy, &p, &flag);

        x = sxy & 0xFFFF;
        if (x > 0) {
            while (x > -64) {
                x -= 124;
            }
        } else {
            while (x < -128) {
                x += 124;
            }
        }
#ifdef PLATFORM_PSYZ
        while (x > -SCENERY_MARGIN(self)) {
            x -= 124;
        }
#endif

        prim = self->ext.prim;
        while (prim != NULL) {
            prim->x0 = prim->x2 = x;
            x += 124;
            prim->x1 = prim->x3 = x;
            prim->y0 = prim->y1 = self->posY.i.hi + 76;
            prim->y2 = prim->y3 = self->posY.i.hi;
            prim->drawMode = DRAW_UNK02;

            prim = prim->next;
            if (x > 256 + SCENERY_MARGIN(self)) {
                break;
            }
        }

        while (prim != NULL) {
            prim->drawMode = DRAW_HIDE;
            prim = prim->next;
        }
        break;
    }
}

static SVECTOR D_us_80180DD0 = {0, 0, 256};
static SVECTOR D_us_80180DD8 = {0};

void EntityBackgroundFurniture(Entity* self) {
    Primitive* prim;
    s32 primIndex;
    long p, flag;
    s16 x;
    long sxy;
    SVECTOR svector;
    VECTOR vector;
    MATRIX matrix;

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        primIndex = g_api.AllocPrimitives(PRIM_GT4, WIDE_POOL(4, STRIPS(120)));
#ifdef PLATFORM_PSYZ
        self->step_s = WIDE_X; // the margin the chain covers
        if (primIndex == -1) {
            self->step_s = 0;
            primIndex = g_api.AllocPrimitives(PRIM_GT4, 4); // 4:3 only
        }
#endif
        if (primIndex != -1) {
            self->flags |= FLAG_HAS_PRIMS;
            self->primIndex = primIndex;
            prim = &g_PrimBuf[primIndex];
            self->ext.prim = prim;
            while (prim != NULL) {
                prim->tpage = 0xF;
                prim->clut = 0x34;
                prim->u0 = prim->u2 = 128;
                prim->u1 = prim->u3 = 248;
                prim->v0 = prim->v1 = 0;
                prim->v2 = prim->v3 = 28;
                prim->priority = 0x50;
                prim->drawMode = DRAW_UNK02;
                prim = prim->next;
            }
        } else {
            DestroyEntity(self);
            return;
        }
        // fallthrough
    case 1:
        SetGeomScreen(1024);
        SetGeomOffset(128, self->posY.i.hi);
        RotMatrix(&D_us_80180DD8, &matrix);
        vector.vx = self->posX.i.hi - 128;
        vector.vy = 0;
        vector.vz = 1024;
        TransMatrix(&matrix, &vector);
        SetRotMatrix(&matrix);
        SetTransMatrix(&matrix);
        RotTransPers(&D_us_80180DD0, &sxy, &p, &flag);

        x = sxy & 0xFFFF;
        if (x > 0) {
            while (x > -64) {
                x -= 120;
            }
        } else {
            while (x < -128) {
                x += 120;
            }
        }
#ifdef PLATFORM_PSYZ
        while (x > -SCENERY_MARGIN(self)) {
            x -= 120;
        }
#endif

        prim = self->ext.prim;
        while (prim != NULL) {
            prim->x0 = prim->x2 = x;
            x += 120;
            prim->x1 = prim->x3 = x;
            prim->y0 = prim->y1 = self->posY.i.hi + 28;
            prim->y2 = prim->y3 = self->posY.i.hi;
            prim->drawMode = DRAW_UNK02;

            prim = prim->next;
            if (x > 256 + SCENERY_MARGIN(self)) {
                break;
            }
        }

        while (prim != NULL) {
            prim->drawMode = DRAW_HIDE;
            prim = prim->next;
        }
        break;
    }
}

static SVECTOR D_us_80180DE0 = {0, 0, 448};
static SVECTOR D_us_80180DE8 = {0};

// Wide view: the prims are the shadow tiles (24 px wide, every 56 px), then
// the band pieces for the margins (a tile is at most 255 px wide, the band
// of the last prim covers 0..255), then the last prim. 4:3: 9 tiles, no
// pieces, as the original 10 prims.
#define SHADOW_TILES(w) WIDE_POOL(9, 9 + (2 * (w) + 55) / 56)
// left piece -w..0 and right piece 0xFF..0x100 + w (the centre band is 0..254)
#define SHADOW_PIECES(w)                                                       \
    WIDE_POOL(0, ((w) + 254) / 255 + ((w) > 0 ? ((w) + 255) / 255 : 0))

#ifdef PLATFORM_PSYZ
// Black pieces of the band from px to end, 255 px at most each, side by side.
// Returns the next free prim. Stops at the last prim.
static Primitive* ShadowBand(
    Primitive* prim, Primitive* last, s32 px, s32 end, s32 y) {
    for (; px < end && prim != NULL && prim != last; px += 255) {
        prim->x0 = px;
        prim->y0 = y;
        prim->u0 = end - px < 255 ? end - px : 255;
        prim->v0 = 48;
        prim->r0 = prim->g0 = prim->b0 = 0;
        prim->priority = 0x60;
        prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;
        prim = prim->next;
    }
    return prim;
}
#endif

// parallax column shadow
void EntityBackgroundShadow(Entity* self) {
    Primitive *prim, *last;
    s32 primIndex;
    long p, flag;
    s16 x;
    long sxy;
    SVECTOR svector;
    VECTOR vector;
    MATRIX matrix;
#ifdef PLATFORM_PSYZ
    s32 i;
#endif

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        primIndex = g_api.func_800EDB58(
            PRIM_TILE_ALT, WIDE_POOL(10, SHADOW_TILES(WIDE_X) + SHADOW_PIECES(WIDE_X) + 1));
#ifdef PLATFORM_PSYZ
        self->step_s = WIDE_X; // the margin the chain covers
        if (primIndex == -1) {
            primIndex = g_api.func_800EDB58(PRIM_TILE_ALT, 10); // 4:3 only
            self->step_s = 0;
        }
#endif
        if (primIndex != -1) {
            self->flags |= FLAG_HAS_PRIMS;
            self->primIndex = primIndex;
            prim = &g_PrimBuf[primIndex];
            self->ext.libraryShadow.prim = prim;
            while (prim != NULL) {
                prim->u0 = 24;
                prim->v0 = 64;
                prim->r0 = prim->g0 = prim->b0 = 0;
                prim->priority = 0x60;
                prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;
                last = prim;
                prim = prim->next;
            }
            prim = last;
            self->ext.libraryShadow.lastPrim = prim;
            prim->u0 = 255;
            prim->v0 = 48;
            prim->x0 = 0;
            prim->y0 = self->posY.i.hi + 112;
            prim->r0 = prim->g0 = prim->b0 = 0;
            prim->priority = 0x60;
            prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;
        } else {
            DestroyEntity(self);
            return;
        }
        // fallthrough
    case 1:
        SetGeomScreen(1024);
        SetGeomOffset(128, self->posY.i.hi);
        RotMatrix(&D_us_80180DE8, &matrix);
        vector.vx = self->posX.i.hi - 128;
        vector.vy = 0;
        vector.vz = 1024;
        TransMatrix(&matrix, &vector);
        SetRotMatrix(&matrix);
        SetTransMatrix(&matrix);
        RotTransPers(&D_us_80180DE0, &sxy, &p, &flag);

        x = sxy & 0xFFFF;
        if (x > 0) {
            while (x > -64) {
                x -= 56;
            }
        } else {
            while (x < -128) {
                x += 56;
            }
        }
#ifdef PLATFORM_PSYZ
        while (x > -SCENERY_MARGIN(self)) {
            x -= 56;
        }
#endif

#ifdef PLATFORM_PSYZ
        last = self->ext.libraryShadow.lastPrim;
        prim = self->ext.libraryShadow.prim;
        for (i = 0;
             prim != NULL && prim != last && i < SHADOW_TILES(self->step_s);
             i++) {
            prim->x0 = x;
            x += 56;
            prim->y0 = self->posY.i.hi;
            prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;

            prim = prim->next;
            if (x > 256 + SCENERY_MARGIN(self)) {
                break;
            }
        }
#else
        prim = self->ext.libraryShadow.prim;
        while (prim != NULL) {
            prim->x0 = x;
            x += 56;
            prim->y0 = self->posY.i.hi;
            prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;

            prim = prim->next;
            if (x > 256) {
                break;
            }
        }
#endif

        while (prim != NULL) {
            prim->drawMode = DRAW_HIDE;
            prim = prim->next;
        }

#ifdef PLATFORM_PSYZ
        // the band of the last prim (0..255) goes on over the margins in
        // pieces that do not overlap: -WIDE_X..0 and 0x100..0x100 + WIDE_X
        prim = self->ext.libraryShadow.prim;
        for (i = 0; i < SHADOW_TILES(self->step_s) && prim != NULL; i++) {
            prim = prim->next;
        }
        prim = ShadowBand(prim, self->ext.libraryShadow.lastPrim,
                          -SCENERY_MARGIN(self), 0, self->posY.i.hi + 64);
        if (SCENERY_MARGIN(self) > 0) {
            ShadowBand(prim, self->ext.libraryShadow.lastPrim, 0xFF,
                       0x100 + SCENERY_MARGIN(self), self->posY.i.hi + 64);
        }
#endif
        prim = self->ext.libraryShadow.lastPrim;
        prim->u0 = 255;
        prim->v0 = 48;
        prim->x0 = 0;
        prim->y0 = self->posY.i.hi + 64;
        prim->r0 = prim->g0 = prim->b0 = 0;
        prim->priority = 0x60;
        prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;
        break;
    }
}

static SVECTOR D_us_80180DF0 = {0, 0, 0x400};
// clang-format off
static u8 D_us_80180DF8[] = {
    0x00, 0xF0, 0xF0, 0xF0, 0x00,
    0x00, 0xC0, 0xF0, 0xC0, 0x00,
    0x00, 0x40, 0x80, 0x40, 0x00,
    0x00, 0x20, 0x20, 0x20, 0x00,
    0x00, 0x20, 0x20, 0x20, 0x00};
static u8 D_us_80180E14[] = {
    0x40, 0x20, 0x60, 0xA0, 0x20,
    0x50, 0x30, 0x70, 0xB0, 0x30,
    0x60, 0x40, 0x80, 0xC0, 0x40,
    0x70, 0x50, 0x90, 0xD0, 0x50,
    0x80, 0x60, 0xA0, 0xE0, 0x60};
static u8 D_us_80180E30[] = {
    0x40, 0x20, 0x60, 0xA0, 0x20,
    0x50, 0x30, 0x70, 0xB0, 0x30,
    0x60, 0x40, 0x80, 0xC0, 0x40,
    0x70, 0x50, 0x90, 0xD0, 0x50,
    0x80, 0x60, 0xA0, 0xE0, 0x60};
static u8 D_us_80180E4C[] = {
    0x00, 0xC0, 0xF0, 0xC0, 0x00,
    0x00, 0x60, 0x60, 0x60, 0x00,
    0x00, 0x40, 0x80, 0x40, 0x00,
    0x00, 0x20, 0x20, 0x20, 0x00,
    0x00, 0x20, 0x20, 0x20, 0x00};
// clang-format on
static u8* D_us_80180E68[] = {
    D_us_80180DF8, D_us_80180E14, D_us_80180E30, D_us_80180E4C};

static Point16 D_us_80180E78[] = {
    {0x000, 0x000}, {0x420, 0x1E0}, {0x0B0, 0x280}, {0x080, 0x1A0}};
static SVECTOR D_us_80180E88 = {0};

// In two rooms, draws books deep into the background with a bit of
// a parallax effect
void EntityDistantBookshelves(Entity* self) {
    long p, flag;
    u8 pad[4];
    long sxy;
    VECTOR trans;
    MATRIX m;
    Primitive* prim;
    s32 primIndex;
    s32 i, j;
    u8* ptr;
    s16 posX, posY;

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitInteractable);
        self->posX.i.hi =
            D_us_80180E78[self->params].x - g_Tilemap.scrollX.i.hi;
        self->posY.i.hi =
            D_us_80180E78[self->params].y - g_Tilemap.scrollY.i.hi;
        primIndex = g_api.func_800EDB58(PRIM_GT4, 0x10);
        if (primIndex != -1) {
            self->flags |= FLAG_HAS_PRIMS;
            self->primIndex = primIndex;
            prim = &g_PrimBuf[primIndex];
            self->ext.prim = prim;
            for (i = 0; prim != NULL; i++) {
                prim->tpage = 0xF;
                prim->clut = 0x2F;
                if (i % 2) {
                    prim->u0 = prim->u2 = 0x88;
                    prim->u1 = prim->u3 = 0xFE;
                } else {
                    prim->u0 = prim->u2 = 0xFE;
                    prim->u1 = prim->u3 = 0x88;
                }
                prim->v0 = prim->v1 = 0xFE;
                prim->v2 = prim->v3 = 0x80;
                prim->priority = 0x20;
                prim->drawMode = DRAW_UNK02;
                prim = prim->next;
            }
        } else {
            DestroyEntity(self);
            return;
        }
        g_GpuBuffers[0].draw.r0 = 0x40;
        g_GpuBuffers[0].draw.g0 = 0x38;
        g_GpuBuffers[0].draw.b0 = 0x28;
        g_GpuBuffers[1].draw.r0 = 0x40;
        g_GpuBuffers[1].draw.g0 = 0x38;
        g_GpuBuffers[1].draw.b0 = 0x28;
    case 1:
        SetGeomScreen(0x400);
        SetGeomOffset(0x80, 0x80);
        RotMatrix(&D_us_80180E88, &m);
        trans.vx = self->posX.i.hi - 0x80;
        trans.vy = self->posY.i.hi - 0x80;
        trans.vz = 0x400;
        TransMatrix(&m, &trans);
        SetRotMatrix(&m);
        SetTransMatrix(&m);
        RotTransPers(&D_us_80180DF0, &sxy, &p, &flag);
        posX = sxy & 0xFFFF;
        posY = sxy >> 0x10;
        prim = self->ext.prim;
        ptr = D_us_80180E68[self->params];
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                prim->drawMode = DRAW_DITHERING | DRAW_COLORS;
                prim->x0 = prim->x2 = posX + (j * 0x78) - 0xF0;
                // wide view: a quad shows until it leaves the wide screen. The
                // 4x4 grid is one patch with its own shading at the ends, so
                // it gets no more columns.
                if (prim->x0 > 0x100 + WIDE_X) {
                    prim->drawMode = DRAW_HIDE;
                }
                prim->x1 = prim->x3 = posX + (j * 0x78) - 0x78;
                if (prim->x1 < -WIDE_X) {
                    prim->drawMode = DRAW_HIDE;
                }
                prim->y0 = prim->y1 = posY + (i * 0x80) - 0x100;
                if (prim->y0 > 0x100) {
                    prim->drawMode = DRAW_HIDE;
                }
                prim->y2 = prim->y3 = posY + (i * 0x80) - 0x80;
                if (prim->y2 < 0) {
                    prim->drawMode = DRAW_HIDE;
                }
                PGREY(prim, 0) = *(ptr + i * 5 + j + 0);
                PGREY(prim, 1) = *(ptr + i * 5 + j + 1);
                PGREY(prim, 2) = *(ptr + (i + 1) * 5 + j + 0);
                PGREY(prim, 3) = *(ptr + (i + 1) * 5 + j + 1);
                prim = prim->next;
            }
        }
    }
}
