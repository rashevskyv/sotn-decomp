// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rstage16.h"
#include <sfx.h>

extern EInit g_EInitBreakable;
void CreateEntityFromCurrentEntity(u16 entityId, Entity* entity);
void ReplaceBreakableWithItemDrop(Entity*);
void PreventEntityFromRespawning(Entity* entity);
Entity* AllocEntity(Entity* start, Entity* end);

// The inverted Prison's breakables: the Prison's code and config table
// (RSTAGE16 0x060E99C8, STAGE_16 0x060EA164: only the pointers differ).
// Saturn entity 0x01, type = params >> 12. They
// use bank 16 (ANIMSET_OVL(2) frames 1-12, gdn_entities.py RSTAGE16): type 0
// the ram-head sconce (frames 6-9), type 1 the brazier (frames 1-4), type 2
// the lantern (static frame 12). The hitbox is 8 x 8 (enemy 0), raised by
// 16, 40 and 16. Types 0 and 1 leave their broken base (entity 0x12, frame
// 10 or 5) and drop a random prize; type 2 breaks like glass, drops the
// persistent prize params & 0x1FF and does not come back (the PS1's NO4
// breakable does the same).
static u8 anim_0[] = {5, 6, 5, 7, 5, 8, 5, 9, 0, 0};
static u8 anim_1[] = {5, 1, 5, 2, 5, 3, 5, 4, 0, 0};
static u8 anim_2[] = {0x10, 12, -1, -1};
static u8* anims[] = {anim_0, anim_1, anim_2};
static s16 hitbox_offy[] = {-16, -40, -16};

void EntityBreakable(Entity* self) {
    Entity* entity;
    u16 type = self->params >> 12;

    if (!self->step) {
        InitializeEntity(g_EInitBreakable);
        self->zPriority = g_unkGraphicsStruct.g_zEntityCenter - 0x14;
        self->animSet = MOD_CANDLE_ANIMSET;
        self->hitboxWidth = 8;
        self->hitboxHeight = 8;
        self->hitboxOffY = hitbox_offy[type];
    }
    AnimateEntity(anims[type], self);
    if (!self->hitParams) {
        return;
    }
    entity = AllocEntity(&g_Entities[224], &g_Entities[256]);
    if (entity != NULL) {
        CreateEntityFromCurrentEntity(E_EXPLOSION, entity);
        entity->posY.i.hi += hitbox_offy[type];
        entity->params = 0;
    }
    if (type == 2) {
        g_api.PlaySfx(SFX_GLASS_BREAK_E);
        entity = AllocEntity(&g_Entities[160], &g_Entities[192]);
        if (entity != NULL) {
            CreateEntityFromEntity(E_PERSISTENT_ITEM_DROP, self, entity);
            entity->posY.i.hi += hitbox_offy[type];
            entity->params = self->params & 0x1FF;
        }
        PreventEntityFromRespawning(self);
        DestroyEntity(self);
        return;
    }
    entity = AllocEntity(&g_Entities[224], &g_Entities[256]);
    if (entity != NULL) {
        CreateEntityFromCurrentEntity(E_ID_12, entity);
        entity->params = type;
    }
    self->posY.i.hi += hitbox_offy[type];
    g_api.PlaySfx(SFX_CANDLE_HIT);
    ReplaceBreakableWithItemDrop(self);
}

// Saturn 0x12 (func_060DC418): the broken sconce or brazier base
void EntityModBreakableBase(Entity* self) {
    if (!self->step) {
        InitializeEntity(g_EInitInteractable);
        self->animSet = MOD_CANDLE_ANIMSET;
        self->animCurFrame = self->params ? 5 : 10;
        self->zPriority = 0x6A;
        self->step = 0x100;
    }
}
