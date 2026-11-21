// SPDX-License-Identifier: AGPL-3.0-or-later
// placeholder: the Wight stands still (the SH-2 port follows)
#include "rstage16.h"

static EInit g_EInitModWight = {MOD_WIGHT_ANIMSET, 1, 0, PAL_NONE, 423};

void EntityModWight(Entity* self) {
    if (!self->step) {
        InitializeEntity(g_EInitModWight);
        self->animCurFrame = 1;
    }
    if (self->flags & FLAG_DEAD) {
        DestroyEntity(self);
    }
}

void EntityModWightAttack(Entity* self) { DestroyEntity(self); }
void EntityModWightPart(Entity* self) { DestroyEntity(self); }
