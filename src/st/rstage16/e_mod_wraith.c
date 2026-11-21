// SPDX-License-Identifier: AGPL-3.0-or-later
// placeholder: the Wraith stands still (the SH-2 port follows)
#include "rstage16.h"

static EInit g_EInitModWraith = {MOD_WRAITH_ANIMSET, 1, 0, PAL_NONE, 424};

void EntityModWraith(Entity* self) {
    if (!self->step) {
        InitializeEntity(g_EInitModWraith);
        self->animCurFrame = 1;
    }
    if (self->flags & FLAG_DEAD) {
        DestroyEntity(self);
    }
}

void EntityModWraith19(Entity* self) { DestroyEntity(self); }
void EntityModWraith1A(Entity* self) { DestroyEntity(self); }
void EntityModWraith1B(Entity* self) { DestroyEntity(self); }
