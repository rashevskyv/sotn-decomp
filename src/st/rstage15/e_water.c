// SPDX-License-Identifier: AGPL-3.0-or-later
// Room 0's water (Saturn 0x15-0x1C). 0x17-0x1C are the PS1's water
// effects (water_effects_rev.h, as RNO4). Saturn row 0x060EE52C has the
// PS1 g_WaterXTbl format; its X columns are scaled to the PC room.
#include "rstage15.h"

#define SAT_X(v) ((v) * 4 / 5)

u16 g_WaterSounds[] = {SFX_WATER_SPLASH_JUMP, SFX_WATER_SPLASH_MOVE};

s16 g_WaterXTbl[] = {SAT_X(1608), SAT_X(532), 0, 441, 512, 7, 7, 0};

#include "../water_effects_rev.h"

// Saturn 0x15 (060DE268): one quad over the water box of row `params`,
// colour 0xB106 (RGB555) as a mesh. The PC draws it 50% transparent.
void EntityWaterTint(Entity* self) {
    s16* row = &g_WaterXTbl[self->params * 8];
    Primitive* prim;
    s32 primIndex;
    s16 l, r, t, b;

    if (!self->step) {
        InitializeEntity(g_EInitInteractable); // Saturn TekiInit 5
        primIndex = g_api.AllocPrimitives(PRIM_G4, 1);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        prim = &g_PrimBuf[primIndex];
        prim->r0 = prim->r1 = prim->r2 = prim->r3 = 6 * 8;
        prim->g0 = prim->g1 = prim->g2 = prim->g3 = 8 * 8;
        prim->b0 = prim->b1 = prim->b2 = prim->b3 = 12 * 8;
        prim->priority = 0x9A;
        prim->drawMode = DRAW_HIDE;
        return;
    }
    prim = &g_PrimBuf[self->primIndex];
    r = g_Tilemap.width - row[0] - g_Tilemap.scrollX.i.hi;
    l = r - row[1];
    b = g_Tilemap.height - row[3] - g_Tilemap.scrollY.i.hi;
    t = g_Tilemap.height - row[4] - g_Tilemap.scrollY.i.hi;
    if (r < -WIDE_X || l > DISP_STAGE_W + WIDE_X || t > DISP_STAGE_H || b < 0) {
        prim->drawMode = DRAW_HIDE;
        return;
    }
    l = MAX(l, -WIDE_X);
    r = MIN(r, DISP_STAGE_W + WIDE_X);
    t = MAX(t, 0);
    b = MIN(b, DISP_STAGE_H);
    prim->x0 = prim->x2 = l;
    prim->x1 = prim->x3 = r - 1;
    prim->y0 = prim->y1 = t;
    prim->y2 = prim->y3 = b - 1;
    prim->drawMode = DRAW_TPAGE | DRAW_UNK02 | DRAW_TRANSP;
}

// Saturn 0x16 (060DE40C): the water line under the box of row `params`, 5
// strips of bank 0x060ECEF0 laid right to left from the box's right end,
// each 1 Saturn pixel over the last. The anim (060EE650) steps the strips
// through 3 phases of 4 images (gdn_entities.py: MOD_WATER_LINE_PARTS).
static const s16 s_WaterLineParts[][6] = {MOD_WATER_LINE_PARTS};
// 060EE628: {image, Saturn width}
static const u8 s_WaterLineStrips[][2] = {
    {0, 128}, {1, 128}, {2, 128}, {0, 128}, {3, 20}};
static u8 s_WaterLineAnim[] = {10, 0, 10, 4, 10, 8, 10, 4, 0, 0};

void EntityWaterLine(Entity* self) {
    s16* row = &g_WaterXTbl[self->params * 8];
    const s16* part;
    Primitive* prim;
    s32 primIndex, i, r, x, y, sx;

    if (!self->step) {
        InitializeEntity(g_EInitInteractable); // Saturn TekiInit 5
        primIndex = g_api.AllocPrimitives(PRIM_SPRT, 5);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        for (prim = &g_PrimBuf[primIndex]; prim; prim = prim->next) {
            prim->priority = 0x9C;
            prim->drawMode = DRAW_HIDE;
        }
        return;
    }
    AnimateEntity(s_WaterLineAnim, self);
    r = g_Tilemap.width - row[0] - g_Tilemap.scrollX.i.hi;
    y = g_Tilemap.height - row[3] - g_Tilemap.scrollY.i.hi;
    prim = &g_PrimBuf[self->primIndex];
    for (i = 0, sx = 0; i < 5; i++, prim = prim->next) {
        sx += 1 - s_WaterLineStrips[i][1];
        x = r + SAT_X(sx);
        part = s_WaterLineParts[s_WaterLineStrips[i][0] + self->animCurFrame];
        if (y < 0 || y >= DISP_STAGE_H || x > DISP_STAGE_W + WIDE_X ||
            x + part[4] < -WIDE_X) {
            prim->drawMode = DRAW_HIDE;
            continue;
        }
        prim->tpage = part[0];
        prim->clut = part[1];
        prim->u0 = part[2];
        prim->v0 = part[3];
        prim->u1 = part[4];
        prim->v1 = part[5];
        prim->x0 = x;
        prim->y0 = y;
        prim->drawMode = DRAW_DEFAULT;
    }
}
