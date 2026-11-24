// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef ROOM_STITCH_H
#define ROOM_STITCH_H
#include <game.h>

extern int g_RoomStitchUsed;
extern int g_RoomStitch; // sotn.ini room_stitch, 0 = off

// after a stage overlay load: merge the table pairs of this stage
void RoomStitch_HookOverlay(const char* name, Overlay* o);
// the layers of a tile layer index: the merged ones of a stitched room
const RoomDef* RoomStitch_TileLayer(s32 index);
// InitRoomEntities (st/create_entity.h): the lists of a stitched room
void RoomStitch_Layouts(u16** horizontal, u16** vertical, u16** h, u16** v);

#endif
