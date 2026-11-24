// SPDX-License-Identifier: AGPL-3.0-or-later
#include "no0.h"

static s16 D_us_80181C14[] = {0x50, 0x68, 0x70, 0x68, 0x50, 0x38, 0x30, 0x38};
static s16 D_us_80181C24[] = {0x90, 0x93, 0x9C, 0xA5, 0xA8, 0xA5, 0x9C, 0x93};

void func_us_801CC8F8(Entity*);

// The 4:3 view has 4 columns that wrap after 0x180 px. A wide view is longer
// than that, so the port has 12 columns that wrap after 0x480 px and cover
// the widest view (WIDE_VIEW_MAX 960: 0x160 px more on each side). The columns
// after the fourth are not in the layout slots (those hold other entities of
// the room), they go to the spare range 192..223.
#define COL_COUNT WIDE_POOL(4, 12)
#define COL_PERIOD (COL_COUNT * 0x60)
#define COL_EDGE WIDE_POOL(0, 0x160)

void func_us_801CC750(Entity* self) {
    Entity* entityPtr;
    s16 i;
    s16 k;
    Primitive* prim;
    s32 primIndex;

    if (self->step) {
        return;
    }

    InitializeEntity(D_us_80180A88);
    primIndex = g_api.AllocPrimitives(PRIM_GT4, WIDE_POOL(9, 33));
    if (primIndex != -1) {
        self->primIndex = primIndex;
        prim = &g_PrimBuf[primIndex];
        self->flags |= FLAG_HAS_PRIMS;
        for (i = -0x10 - COL_EDGE; prim != NULL; i += 0x1E) {
            prim->tpage = 0xF;
            prim->clut = 0x2A;
            prim->u0 = prim->u2 = 0xC0;
            prim->u1 = prim->u3 = 0xE0;
            prim->v0 = prim->v1 = 0x80;
            prim->v2 = prim->v3 = 0xB0;
            prim->x0 = prim->x2 = i;
            prim->x1 = prim->x3 = i + 0x20;
            prim->y0 = prim->y1 = 0x91;
            prim->y2 = prim->y3 = 0xC1;
            prim->priority = 0;
            prim->drawMode = DRAW_DEFAULT;
            prim = prim->next;
        }
    }

    entityPtr = self + 1;

#ifdef PLATFORM_PSYZ
    // this spawner can run again in the same room: drop the old extra columns
    for (entityPtr = &g_Entities[192]; entityPtr < &g_Entities[224];
         entityPtr++) {
        if (entityPtr->entityId == E_ID_16) {
            DestroyEntity(entityPtr);
        }
    }
    entityPtr = self + 1;
#endif
    for (k = 0; k < COL_COUNT; k++) {
        i = -0x10 + k * 0x60;
        if (i > 0x140 + COL_EDGE) {
            i -= COL_PERIOD;
        }
#ifdef PLATFORM_PSYZ
        if (k >= 4) {
            entityPtr = AllocEntity(&g_Entities[192], &g_Entities[224]);
            if (entityPtr == NULL) {
                break;
            }
        }
#endif
        DestroyEntity(entityPtr);
        entityPtr->entityId = E_ID_16;
        entityPtr->pfnUpdate = func_us_801CC8F8;
        entityPtr->posY.i.hi = 0x8E;
        entityPtr->posX.i.hi = i;
        entityPtr++;
    }
}

void func_us_801CC8F8(Entity* self) {
    if (!self->step) {
        InitializeEntity(g_EInitCommon);
        self->animSet = ANIMSET_OVL(1);
        self->animCurFrame = 3;
        self->zPriority = g_unkGraphicsStruct.g_zEntityCenter - 0x54;
        self->unk68 = 0xC0;
        self->flags &= ~FLAG_UNK_20000000;
        return;
    }

    if (self->posX.i.hi < -0x40 - COL_EDGE) {
        self->posX.i.hi += COL_PERIOD;
    }

    if (self->posX.i.hi > 0x140 + COL_EDGE) {
        self->posX.i.hi -= COL_PERIOD;
    }
}

// updates entity movement direction
void func_us_801CC9B4(Entity* self) {
    u8 angle;

    if (!self->step) {
        InitializeEntity(g_EInitCommon);
        self->animSet = ANIMSET_OVL(1);
        self->animCurFrame = 5;
        self->zPriority = 1;
        self->flags &= ~FLAG_POS_CAMERA_LOCKED;
        self->ext.et_801CC9B4.currentAngle = 0;
        return;
    }
    if ((g_GameTimer & 0xF) == 0) {
        angle = (Random() & 7);
        self->ext.et_801CC9B4.targetAngle = GetAnglePointToEntityShifted(
            D_us_80181C14[angle], D_us_80181C24[angle]);
    }
    angle = AdjustValueWithinThreshold(8, self->ext.et_801CC9B4.currentAngle,
                                       self->ext.et_801CC9B4.targetAngle);
    SetEntityVelocityFromAngle(angle, 4);
    MoveEntity();
    self->ext.et_801CC9B4.currentAngle = angle;
}
