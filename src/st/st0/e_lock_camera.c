// SPDX-License-Identifier: AGPL-3.0-or-later
#include "st0.h"

#include "../player_is_within_hitbox.h"
#ifdef PLATFORM_PSYZ
#include "../../pc/wide.h"
#endif

extern EInit g_EInitLockCamera;
static u8 D_8018065C[] = {32, 0};
static u8 D_80180660[] = {0, 0};
static u16 D_80180664[] = {
    0x0000, 0x0000, 0x0500, 0x0200, // 0
    0x0000, 0x0000, 0x0500, 0x0100, // 1
};

void EntityLockCamera(Entity* self) {
    u16* rect16;
    u16 params;
    u16 facingLeft;

    params = self->params & 0xFF;
    if (!self->step) {
        InitializeEntity(g_EInitLockCamera);
        self->hitboxState = 1;

        facingLeft = self->ext.lockCamera.unk7C = D_80180660[params];
        if (facingLeft) {
            self->hitboxWidth = D_8018065C[params];
            self->hitboxHeight = 20;
        } else {
            self->hitboxWidth = 20;
            self->hitboxHeight = D_8018065C[params];
        }
#ifdef PLATFORM_PSYZ
        self->ext.lockCamera.unk80 = self->posX.i.hi + g_Tilemap.scrollX.i.hi;
        self->ext.lockCamera.unk82 = self->posY.i.hi + g_Tilemap.scrollY.i.hi;
#endif
    }

    if (self->params & 0x100) {
#ifdef PLATFORM_PSYZ
        // The wide view creates this one-shot lock early, while the player
        // is still on the prologue stairs. Wait for the PS1 window.
        if (!Wide_InPsxSpawnWindow(
                self->ext.lockCamera.unk80 - g_Tilemap.scrollX.i.hi,
                self->ext.lockCamera.unk82 - g_Tilemap.scrollY.i.hi)) {
            return;
        }
#endif
        params = params * 4;
        rect16 = &D_80180664[params];
        g_Tilemap.x = *rect16++;
        g_Tilemap.y = *rect16++;
        g_Tilemap.width = *rect16++;
        g_Tilemap.height = *rect16;
        DestroyEntity(self);
        return;
    }

    if (PlayerIsWithinHitbox(self)) {
        facingLeft = GetSideToPlayer();
        if (self->ext.lockCamera.unk7C) {
            facingLeft &= 2;
            facingLeft *= 2;
        } else {
            facingLeft &= 1;
            facingLeft *= 4;
        }
        params = (params << 3) + facingLeft;
        rect16 = &D_80180664[params];
        g_Tilemap.x = *rect16++;
        g_Tilemap.y = *rect16++;
        g_Tilemap.width = *rect16++;
        g_Tilemap.height = *rect16;
    }
}
