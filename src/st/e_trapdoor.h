// SPDX-License-Identifier: AGPL-3.0-or-later
// the lid is open once it was hit (NP3_TRAPDOOR_OPEN, kept in the castle flags
// so a reload and a save keep it) or after Death took the equipment
#define TRAPDOOR_OPEN()                                                        \
    (g_CastleFlags[NP3_TRAPDOOR_OPEN] || g_CastleFlags[IVE_BEEN_ROBBED])

static u8 trapdoor_anim[] = {1, 27, 6, 28, 6, 29, 129, 30, 255};

// trap door that leads to underground garden in saturn version.
// also opens the one leading to the save room
void EntityTrapDoor(Entity* self) {
    Entity* player;

    switch (self->step) {
    case 0:
        InitializeEntity(g_EInitStInteractable);
        self->animCurFrame = 27;
        self->zPriority = 0x6A;
        self->hitboxState = 1;
        self->hitboxWidth = 16;
        self->hitboxHeight = 4;

        if (!TRAPDOOR_OPEN()) {
            player = &PLAYER;
            if (player->posY.val < self->posY.val) {
                g_Tilemap.fg[0xA8E / 2] = 0x129;
                g_Tilemap.fg[0xA90 / 2] = 0x132;
                DestroyEntity(self);
                break;
            }
            g_Tilemap.fg[0xA8E / 2] = 0x6C8;
            g_Tilemap.fg[0xA90 / 2] = 0x6C9;
        } else {
            self->animCurFrame = 30;
            g_Tilemap.fg[0xA8E / 2] = 0x6C8;
            g_Tilemap.fg[0xA90 / 2] = 0x6C9;
            self->step = 128;
        }

    case 1:
        if (self->hitFlags) {
            g_CastleFlags[NP3_TRAPDOOR_OPEN] = 1;
            self->step++;
        }
        break;

    case 2:
        AnimateEntity(trapdoor_anim, self);
        break;
    }
}
