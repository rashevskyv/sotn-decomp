// SPDX-License-Identifier: AGPL-3.0-or-later
// The Prison's room 0 floor switch and stone pillar (Saturn STAGE_16):
// 0x16 the switch (func_060DC894), 0x17 the pillar (func_060DC9E0), 0x15
// the pillar's debris (func_060DC60C / func_060DC734). Both draw bank 19
// (ANIMSET_OVL(13): frame 1 the pillar, frame 2 the switch). Stepping on the
// switch presses it down and sets castle flag 0xF9 (Saturn 0x0605CB49 =
// g_CastleFlags + 0xF9, kept in the save); the pillar then sinks into the
// floor with dust and debris and stays down.
#include "stage16.h"
#include <sfx.h>

#define MOD_PILLAR_FLAG 0xF9
#define SAT_X(v) ((v) * 4 / 5)

u8 GetPlayerCollisionWith(Entity* self, u16 w, u16 h, u16 flags);
void CreateEntityFromCurrentEntity(u16 entityId, Entity* entity);
Entity* AllocEntity(Entity* start, Entity* end);

static void PillarInit(Entity* self, s16 frame) {
    InitializeEntity(g_EInitInteractable);
    self->animSet = MOD_PILLAR_ANIMSET;
    self->animCurFrame = frame;
    self->zPriority = 0x60;
}

void EntityModPillarSwitch(Entity* self) {
    u8 collided = 0;

    if (self->step == 1) {
        collided = GetPlayerCollisionWith(
            self, self->hitboxWidth, self->hitboxHeight, 4);
    }
    switch (self->step) {
    case 0:
        PillarInit(self, 2);
        self->hitboxWidth = 8;
        self->hitboxHeight = 6;
        if (g_CastleFlags[MOD_PILLAR_FLAG]) {
            self->posY.i.hi = 0x2A2 - g_Tilemap.scrollY.i.hi;
            self->step = 2;
        }
        break;
    case 1:
        if (collided) {
            PLAYER.posY.i.hi++;
            self->posY.val += FIX(0.25);
            if (self->posY.i.hi + g_Tilemap.scrollY.i.hi > 0x2A2) {
                self->posY.i.hi = 0x2A2 - g_Tilemap.scrollY.i.hi;
                g_CastleFlags[MOD_PILLAR_FLAG] = 1;
                g_api.PlaySfx(SFX_SWITCH_CLICK);
                self->step++;
            }
        }
        break;
    }
}

void EntityModPillar(Entity* self) {
    Entity* e;
    u8 collided = GetPlayerCollisionWith(self, 10, 0x38, 5);

    switch (self->step) {
    case 0:
        PillarInit(self, 1);
        self->ext.timer.t = 0;
        if (g_CastleFlags[MOD_PILLAR_FLAG]) {
            self->posY.i.hi = 0x2D8 - g_Tilemap.scrollY.i.hi;
            self->step = 0x80;
        }
        break;
    case 1:
        if (g_CastleFlags[MOD_PILLAR_FLAG]) {
            g_api.PlaySfx(SFX_STONE_MOVE_A);
            CreateEntityFromEntity(E_MOD_PILLAR_DEBRIS, self, self + 1);
            self->step++;
        }
        break;
    case 2:
        self->posY.val += FIX(0.375);
        self->ext.timer.t++;
        self->posX.i.hi += self->ext.timer.t & 1 ? 1 : -1;
        if (!(self->ext.timer.t & 0xF)) {
            g_api.PlaySfx(SFX_STONE_MOVE_A);
        }
        // the player rides it down
        if (collided & 4) {
            PLAYER.posY.i.hi++;
        }
        if (self->posY.i.hi + g_Tilemap.scrollY.i.hi > 0x2D7) {
            self->posY.i.hi = 0x2D8 - g_Tilemap.scrollY.i.hi;
            DestroyEntity(self + 1);
            self->step++;
        }
        if (!(g_Timer & 0xF)) {
            e = AllocEntity(&g_Entities[224], &g_Entities[256]);
            if (e != NULL) {
                CreateEntityFromEntity(E_INTENSE_EXPLOSION, self, e);
                e->posY.i.hi = 0x2A0 - g_Tilemap.scrollY.i.hi;
                e->posX.i.hi += SAT_X((s32)(Random() & 0xF) - 8);
                e->params = 0x10;
                e->drawFlags |= ENTITY_SCALEX | ENTITY_SCALEY;
                e->scaleX = e->scaleY = 0xC0;
            }
        }
        break;
    }
}

// 64 one-pixel stone chips, one every other frame, thrown up from the floor
// at the pillar's foot; each falls until its speed passes 6 px a frame.
// ponytail: one static chip table, as there is one pillar in the stage
static struct {
    s32 x, y, vx, vy;
    u8 state;
} s_Chips[64];

void EntityModPillarDebris(Entity* self) {
    Primitive* prim;
    s32 primIndex, i, dx;

    if (!self->step) {
        InitializeEntity(g_EInitInteractable);
        self->animSet = 0;
        primIndex = g_api.AllocPrimitives(PRIM_TILE, 0x40);
        if (primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        self->primIndex = primIndex;
        for (prim = &g_PrimBuf[primIndex], i = 0; prim != NULL;
             prim = prim->next, i++) {
            // Saturn colour 0xB208
            prim->r0 = 8 << 3;
            prim->g0 = 16 << 3;
            prim->b0 = 12 << 3;
            prim->u0 = prim->v0 = 1;
            prim->priority = 0x97;
            prim->drawMode = DRAW_HIDE;
            s_Chips[i].state = 0;
        }
    } else if (!(g_Timer & 1)) {
        for (i = 0; i < 64 && s_Chips[i].state; i++) {
        }
        if (i < 64) {
            s_Chips[i].state = 1;
        }
    }
    for (prim = &g_PrimBuf[self->primIndex], i = 0; prim != NULL;
         prim = prim->next, i++) {
        switch (s_Chips[i].state) {
        case 1:
            dx = (Random() & 0xF) - 8;
            s_Chips[i].x = (self->posX.i.hi + SAT_X(dx)) << 16;
            s_Chips[i].y = (0x2A0 - g_Tilemap.scrollY.i.hi) << 16;
            s_Chips[i].vx = SAT_X(dx < 1 ? -(s32)(Random() & 0xFF) * 0x50
                                         : (s32)(Random() & 0xFF) * 0x50);
            s_Chips[i].vy = (Random() & 0xFF) * 0x100 - 0x40000;
            prim->drawMode = DRAW_UNK02;
            s_Chips[i].state = 2;
        case 2:
            s_Chips[i].x += s_Chips[i].vx;
            s_Chips[i].y += s_Chips[i].vy;
            prim->x0 = s_Chips[i].x >> 16;
            prim->y0 = s_Chips[i].y >> 16;
            s_Chips[i].vy += 0x4000;
            if (s_Chips[i].vy > 0x60000) {
                // free for the next throw (Saturn state 3 -> 0)
                prim->drawMode = DRAW_HIDE;
                s_Chips[i].state = 0;
            }
            break;
        }
    }
}
