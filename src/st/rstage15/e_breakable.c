// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rstage15.h"
#include <sfx.h>

extern EInit g_EInitBreakable;
void CreateEntityFromCurrentEntity(u16 entityId, Entity* entity);
void ReplaceBreakableWithItemDrop(Entity*);
Entity* AllocEntity(Entity* start, Entity* end);

// The Reverse Garden's sconces: Saturn entity 0x01 (func_060DC040, on a hit
// func_060DC1A8), config g_RStage15BreakableConfigs (0x060EE440) type 0
// alone: bank 16 frames 0x26-0x29 at 5 ticks (ANIMSET_OVL(2) frames 1-4,
// rstage15_entities.py), a hitbox 8 x 8 raised by 16. The hit leaves the
// broken base (entity 0x12, frame 0x2A) and drops the prize params & 0xFFF.
static u8 anim_0[] = {5, 1, 5, 2, 5, 3, 5, 4, 0, 0};

void EntityBreakable(Entity* self) {
    Entity* entity;

    if (!self->step) {
        InitializeEntity(g_EInitBreakable);
        self->zPriority = g_unkGraphicsStruct.g_zEntityCenter - 0x14;
        self->animSet = MOD_CANDLE_ANIMSET;
        self->hitboxWidth = 8;
        self->hitboxHeight = 8;
        self->hitboxOffY = -16;
    }
    AnimateEntity(anim_0, self);
    if (!self->hitParams) {
        return;
    }
    entity = AllocEntity(&g_Entities[224], &g_Entities[256]);
    if (entity != NULL) {
        CreateEntityFromCurrentEntity(E_EXPLOSION, entity);
        entity->posY.i.hi -= 16;
        entity->params = 0;
    }
    entity = AllocEntity(&g_Entities[224], &g_Entities[256]);
    if (entity != NULL) {
        CreateEntityFromCurrentEntity(E_ID_12, entity);
        entity->params = 0;
    }
    self->posY.i.hi -= 16;
    g_api.PlaySfx(SFX_CANDLE_HIT);
    ReplaceBreakableWithItemDrop(self);
}

// Saturn 0x12 (func_060DC3A4): the broken sconce base, bank 16 frame 0x2A
void EntityModBreakableBase(Entity* self) {
    if (!self->step) {
        InitializeEntity(g_EInitInteractable);
        self->animSet = MOD_CANDLE_ANIMSET;
        self->animCurFrame = 5;
        self->zPriority = 0x6A;
        self->step = 0x100;
    }
}
