// SPDX-License-Identifier: AGPL-3.0-or-later
#include "spawn_point.h"

#include <string.h>

extern u32 CheckAndDoLevelUp(void);

struct SpawnPoint {
    s16 stageId;
    u8 roomIndex;
    s16 x;
    s16 y;
    const char* name;
};

static const struct SpawnPoint s_SpawnPoints[] = {
    {STAGE_NP3, 1, 192, 870, "np3-stage15-door"},
    {STAGE_RNO3, 1, 96, 104, "reverse-trapdoor-underground"},
    {STAGE_NO0, 0, 1440, 688, "marble-soul"},
    {STAGE_NO4, 5, 176, 396, "caverns-soul"},
    {STAGE_RNO0, 0, 64, 132, "reverse-marble-soul"},
    {STAGE_RNO4, 5, 208, 132, "reverse-caverns-soul"},
    // the Saturn stages (room indices of s_SaturnStageRooms)
    {STAGE_ID_SATURN_16, 0, 560, 140, "prison-banner"},
    {STAGE_ID_SATURN_16, 1, 200, 150, "prison-specter"},
    {STAGE_ID_SATURN_16, 1, 640, 150, "prison-wisp"},
    // adds a Skeleton Breeder to room 0 (no Saturn layout places one)
    {STAGE_ID_SATURN_16, 0, 280, 525, "prison-breeder"},
    {STAGE_ID_SATURN_15, 0, 40, 140, "garden-room0"},
    {STAGE_ID_SATURN_15, 0, 600, 360, "garden-slinger"},
    {STAGE_ID_SATURN_15, 1, 40, 140, "garden-room1"},
    {STAGE_ID_SATURN_15, 2, 40, 140, "garden-room2"},
    {STAGE_ID_SATURN_15, 3, 40, 140, "garden-room3"},
    {STAGE_ID_SATURN_15, 3, 400, 140, "garden-thornweed"},
    {STAGE_ID_SATURN_15, 4, 40, 140, "garden-room4"},
    {STAGE_ID_SATURN_15, 5, 40, 140, "garden-room5"},
    {STAGE_ID_SATURN_15, 6, 40, 140, "garden-room6"},
    // the inverted castle's Saturn stages
    {STAGE_ID_REVERSE_SATURN_15, 0, 2200, 392, "hell-garden-room0"},
    {STAGE_ID_REVERSE_SATURN_15, 1, 40, 136, "hell-garden-room1"},
    {STAGE_ID_REVERSE_SATURN_15, 2, 40, 136, "hell-garden-room2"},
    {STAGE_ID_REVERSE_SATURN_15, 3, 40, 136, "hell-garden-room3"},
    {STAGE_ID_REVERSE_SATURN_15, 4, 40, 136, "hell-garden-room4"},
    {STAGE_ID_REVERSE_SATURN_15, 5, 40, 136, "hell-garden-room5"},
    {STAGE_ID_REVERSE_SATURN_15, 6, 40, 136, "hell-garden-room6"},
    // room 0's left end, where the layout spawns the water (Saturn 0x15-0x17)
    {STAGE_ID_REVERSE_SATURN_15, 0, 128, 100, "hell-garden-water"},
    {STAGE_ID_REVERSE_SATURN_16, 0, 60, 648, "soul-prison-room0"},
    {STAGE_ID_REVERSE_SATURN_16, 1, 1200, 136, "soul-prison-room1"},
};

enum SpawnPointStep {
    SpawnPoint_Off,
    SpawnPoint_PickRoom,
    SpawnPoint_PlacePlayer,
    SpawnPoint_ClearLoadState,
};

static enum SpawnPointStep s_Step = SpawnPoint_Off;
static const struct SpawnPoint* s_Spawn = NULL;
static void (*s_InitRoomEntities)(s32 layoutId) = NULL;

static const struct SpawnPoint* FindSpawnPoint(const char* name) {
    s32 i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < LEN(s_SpawnPoints); i++) {
        if (!strcmp(name, s_SpawnPoints[i].name)) {
            return &s_SpawnPoints[i];
        }
    }
    return NULL;
}

const char* SpawnPoint_Name(void) { return s_Spawn ? s_Spawn->name : NULL; }

int SpawnPoint_StageForName(const char* name) {
    const struct SpawnPoint* spawn = FindSpawnPoint(name);
    return spawn == NULL ? -1 : spawn->stageId;
}

void SpawnPoint_Init(const struct InitGameParams* params) {
    s_Step = SpawnPoint_Off;
    s_Spawn = FindSpawnPoint(params->spawnPoint);
    if (s_Spawn != NULL) {
        s_Step = SpawnPoint_PickRoom;
    }
}

void SpawnPoint_OnFrame(void) {
    const RoomHeader* room;

    if (s_Step == SpawnPoint_ClearLoadState) {
        if (g_GameEngineStep == Engine_Normal) {
            D_8003C730 = 0;
            s_Step = SpawnPoint_Off;
        }
        return;
    }
    if (s_Step != SpawnPoint_PickRoom) {
        return;
    }
    if (g_GameState != Game_Play || g_GameStep < Play_Init) {
        return;
    }
    if (g_StageId != s_Spawn->stageId || g_api.o.rooms == NULL) {
        ERRORF("spawn point does not match the loaded stage %02X", g_StageId);
        s_Step = SpawnPoint_Off;
        return;
    }
    if (g_StageId & STAGE_INVERTEDCASTLE_FLAG) {
        CheckAndDoLevelUp();
    }
    room = &g_api.o.rooms[s_Spawn->roomIndex];
    g_Tilemap.left = room->left;
    g_Tilemap.top = room->top;
    D_8003C730 = 1;
    s_Step = SpawnPoint_PlacePlayer;
}

static void SpawnPoint_PlacePlayerNow(void) {
    s32 x, y;

    if (s_Step != SpawnPoint_PlacePlayer) {
        return;
    }
    s_Step = SpawnPoint_ClearLoadState;

    x = s_Spawn->x;
    y = s_Spawn->y;

    g_PlayerX = x;
    g_PlayerY = y;
    PLAYER.posX.i.hi = (u8)x;
    PLAYER.posY.i.hi = (u8)y;
    PLAYER.facingLeft = PLAYER.posX.i.hi >= 0x80;

    D_8009791C.x = g_Tilemap.left + (g_PlayerX >> 8);
    D_8009791C.y = g_Tilemap.top + (g_PlayerY >> 8);
    g_Tilemap.scrollX.i.hi = (D_8009791C.x - g_Tilemap.left) << 8;
    g_Tilemap.scrollY.i.hi = (D_8009791C.y - g_Tilemap.top) << 8;
    g_PrevScrollX = g_Tilemap.scrollX.i.hi;
    g_PrevScrollY = g_Tilemap.scrollY.i.hi;
}

static void SpawnPoint_InitRoomEntities(s32 layoutId) {
    SpawnPoint_PlacePlayerNow();
    s_InitRoomEntities(layoutId);
}

void SpawnPoint_HookOverlay(Overlay* o) {
    if (s_Step == SpawnPoint_Off || o->InitRoomEntities == NULL) {
        return;
    }
    s_InitRoomEntities = o->InitRoomEntities;
    o->InitRoomEntities = SpawnPoint_InitRoomEntities;
}
