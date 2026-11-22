// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dra.h"
#include "pc.h"

static RoomTeleport s_SaturnDoorTeleports[] = {
    {16, 131, 0x0000, STAGE_NP3, STAGE_ID_SATURN_15},
    {16, 131, 0x0000, STAGE_NP3, STAGE_ID_SATURN_15},
    {16, 136, 0x0008, STAGE_NO0, STAGE_ID_SATURN_16},
    {752, 136, 0x0000, STAGE_NO4, STAGE_ID_SATURN_16},
    {16, 136, 0x0008, STAGE_NO0, STAGE_ID_SATURN_16},
    {752, 136, 0x0000, STAGE_NO4, STAGE_ID_SATURN_16},
    {240, 904, 0x0008, STAGE_ID_SATURN_15, STAGE_NP3},
    {1520, 648, 0x0000, STAGE_ID_SATURN_16, STAGE_NO0},
    {16, 392, 0x0028, STAGE_ID_SATURN_16, STAGE_NO4},
    // 140: Saturn teleport 69, the STAGE_15 CD room at (2,45) to the sixth
    // warp room (WARP.PRG room 5, 0x1E in the Saturn table: 0x130 x 4/5)
    {240, 136, 0x0028, STAGE_ID_SATURN_15, STAGE_ID_SATURN_WARP},
    // 141: Saturn teleport 71, the exit of that room to STAGE_15 room 0
    {16, 392, 0x0000, STAGE_ID_SATURN_WARP, STAGE_ID_SATURN_15},
    // 142: the pad of the sixth room to the Entrance warp room (the PS1 list
    // is Castle Keep, Olrox, Outer Wall, Entrance, Catacombs). The Saturn
    // pad goes to the next unlocked room, and the first (bit 0) always is.
    {128, 132, 0x0018, STAGE_ID_SATURN_WARP, STAGE_WRP},
    // 143: the pad of the Olrox room (src/st/wrp/warp.c, the last one of the
    // PS1 cycle) to the sixth room, once the player has been there
    {128, 132, 0x0028, STAGE_WRP, STAGE_ID_SATURN_WARP},
    // 144..147: the pad of the sixth room to the other warp rooms picked on the
    // map (warp_map): Castle Keep, Olrox, Outer Wall, Catacombs (rooms 0,1,2,4)
    {128, 132, 0x0000, STAGE_ID_SATURN_WARP, STAGE_WRP},
    {128, 132, 0x0008, STAGE_ID_SATURN_WARP, STAGE_WRP},
    {128, 132, 0x0010, STAGE_ID_SATURN_WARP, STAGE_WRP},
    {128, 132, 0x0020, STAGE_ID_SATURN_WARP, STAGE_WRP},
};

RoomTeleport* PC_GetRoomTeleport(int id) {
    if (id < RETAIL_ROOM_TELEPORT_COUNT) {
        return &D_800A245C[id];
    }
    return &s_SaturnDoorTeleports[id - RETAIL_ROOM_TELEPORT_COUNT];
}
