// SPDX-License-Identifier: AGPL-3.0-or-later
// The Garden's water (room 0, layouts 1). Saturn 0x17 (func_060DC838) is the
// PS1's EntityAlucardWaterEffect, and 0x18-0x1C are its splashes and drops:
// the PS1 code of src/st/water_effects.h with the Saturn water table.
// Saturn 0x15 (func_060DE2F4) is the blue tint over the water.
#include "stage15.h"

u16 g_WaterSounds[] = {SFX_WATER_SPLASH_JUMP, SFX_WATER_SPLASH_MOVE};
// Saturn g_Stage15WaterRegions (0x060F2044): x, width, slope, top, bottom
s16 g_WaterXTbl[] = {1608, 532, 0, 441, 512, 7, 7, 0};

#include "../water_effects.h"

// The layout puts 0x15 and 0x17 at both room edges (x 128 and 2176): the copy
// at the door the player comes in by spawns, and it must live on to the pool.
// The other copy, met at the far edge, goes away.
static bool IsSecondWater(Entity* self) {
    Entity* e;

    for (e = g_Entities; e < &g_Entities[TOTAL_ENTITY_COUNT]; e++) {
        if (e != self && e->entityId == self->entityId && e->step) {
            DestroyEntity(self);
            return true;
        }
    }
    return false;
}

// Saturn 0x17
void EntityModWaterEffect(Entity* self) {
    if (self->step == 0 && IsSecondWater(self)) {
        return;
    }
    EntityAlucardWaterEffect(self); // its spawner init keeps it off camera
}

// Saturn 0x15: one meshed (half see-through) polygon of RGB555 (6, 8, 12)
// over the water region params, in front of the player (Saturn z 0x9A)
void EntityModWaterTint(Entity* self) {
    s16* r = &g_WaterXTbl[self->params * 8];
    Primitive* prim;
    s32 x, y0, y1;

    if (self->step == 0) {
        if (IsSecondWater(self)) {
            return;
        }
        InitializeEntity(g_EInitCommon);
        self->flags &= ~FLAG_DESTROY_IF_OUT_OF_CAMERA;
        self->primIndex = g_api.AllocPrimitives(PRIM_G4, 1);
        if (self->primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        prim = &g_PrimBuf[self->primIndex];
        PRED(prim) = 6 << 3;
        PGRN(prim) = 8 << 3;
        PBLU(prim) = 12 << 3;
        prim->priority = 0x9A;
    }
    prim = &g_PrimBuf[self->primIndex];
    x = r[0] - g_Tilemap.scrollX.i.hi;
    y0 = r[3] - g_Tilemap.scrollY.i.hi;
    y1 = r[4] - g_Tilemap.scrollY.i.hi;
    if (x + r[1] <= -0x80 || x >= 0x180 || y1 <= 0 || y0 >= 0xF0) {
        prim->drawMode = DRAW_HIDE;
        return;
    }
    prim->x0 = prim->x2 = x;
    prim->x1 = prim->x3 = x + r[1];
    prim->y0 = prim->y1 = y0 < 0 ? 0 : y0;
    prim->y2 = prim->y3 = y1 > 0xF0 ? 0xF0 : y1;
    prim->drawMode = DRAW_TRANSP;
}
