// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Section Sizes:
//
//         | Data  | RO Data | Text  | BSS  |
// --------+-------+---------+-------+------+
//  PSX US | TBD   | TBD     | 0xB78 | 0x10 |
//  PSX HD | TBD   | TBD     | 0xB78 | 0x10 |
//

#include <stage.h>

static u16* g_LayoutObjHorizontal;
static u16* g_LayoutObjVertical;
static u8 g_LayoutObjPosHorizontal;
STATIC_PAD_BSS(3);
static u8 g_LayoutObjPosVertical;
STATIC_PAD_BSS(3);

#define LAYOUT_OBJ_START 0xfffe
#define LAYOUT_OBJ_END 0xffff

#define LAYOUT_OBJ_POSITION_FORWARD 0
#define LAYOUT_OBJ_POSITION_BACKWARD 1

// Follows the struct from LayoutEntity
#define LAYOUT_OBJ_POS_X 0
#define LAYOUT_OBJ_POS_Y 1
#define LAYOUT_OBJ_ID 2
#define LAYOUT_OBJ_SLOT 3
#define LAYOUT_OBJ_PARAMS 4

#if defined(PLATFORM_PSYZ) && (defined(STAGE_IS_ARE) || defined(STAGE_IS_NO2))
// The two JP-only familiar cards (sotn.ini jp_cards; notes in
// are/d_prize_drops.c and no2/d_prize_drops.c).
// jp, the JP/PSP spots: the ARE item drop 7 becomes RELIC_JP_1, the NO2
// item drop 12 becomes RELIC_SWORD_CARD, the NO2 RELIC_SWORD_CARD orb
// becomes the other JP card.
// extra: every US pickup stays, each card is an orb DX px from the pickup
// it would replace (ARE drop 7, the NO2 RELIC_SWORD_CARD orb).
#define JP_CARD_DX_ARE 40
#define JP_CARD_DX_NO2 (-40)
void CreateEntityFromEntity(u16 entityId, Entity* source, Entity* entity);
static void JpCards_Extra(Entity* src, s32 relic, s16 dx) {
    Entity* e;
    for (e = &g_Entities[STAGE_ENTITY_START]; e < &g_Entities[TOTAL_ENTITY_COUNT];
         e++) {
        if (e->entityId == E_RELIC_ORB && e->params == relic) {
            return; // the source came back into range: the orb is still there
        }
    }
    e = AllocEntity(&g_Entities[160], &g_Entities[192]);
    if (e) {
        CreateEntityFromEntity(E_RELIC_ORB, src, e);
        e->posX.i.hi += dx;
        e->params = relic;
    }
}

static void JpCards_OnLayout(Entity* entity) {
#if defined(STAGE_IS_ARE)
    if (entity->entityId == E_PERSISTENT_ITEM_DROP && entity->params == 7) {
        if (g_JpCards == JP_CARDS_JP) {
            entity->entityId = E_RELIC_ORB;
            entity->params = RELIC_JP_1;
        } else if (g_JpCards == JP_CARDS_EXTRA) {
            JpCards_Extra(entity, RELIC_JP_1, JP_CARD_DX_ARE);
        }
    }
#else
    if (g_JpCards == JP_CARDS_JP && entity->entityId == E_PERSISTENT_ITEM_DROP &&
        entity->params == 12) {
        entity->entityId = E_RELIC_ORB;
        entity->params = RELIC_SWORD_CARD;
    } else if (entity->entityId == E_RELIC_ORB &&
               entity->params == RELIC_SWORD_CARD) {
        if (g_JpCards == JP_CARDS_JP) {
            entity->params = RELIC_JP_0;
        } else if (g_JpCards == JP_CARDS_EXTRA) {
            JpCards_Extra(entity, RELIC_JP_0, JP_CARD_DX_NO2);
        }
    }
#endif
    entity->pfnUpdate = PfnEntityUpdates[entity->entityId - 1];
}
#endif

#ifdef PLATFORM_PSYZ
// World x/y of the layout entry that made each entity (B02: see
// LayoutSlotAlways).
static u16 g_LayoutOriginX[TOTAL_ENTITY_COUNT];
static u16 g_LayoutOriginY[TOTAL_ENTITY_COUNT];

// Case 0xA000 ("always create") overwrites its slot. The wide spawn window is
// wider than the distance between two entries of one slot, so the overwrite
// can hit a live entity that is still on screen (the left clock glint). Then
// the new entity goes to a free spare slot (searched from 191 down, so it
// meets the 160-up allocations of bones and drops late). The same entry coming
// back resets its own entity, as on PS1, also when that entity is in a spare.
static Entity* LayoutSlotAlways(Entity* slot, LayoutEntity* obj) {
    Entity* e;
    s32 i = slot - g_Entities;
    s32 sx = slot->posX.i.hi;
    s32 sy = slot->posY.i.hi;

    if (WIDE_X <= 0) {
        return slot;
    }
    for (e = &g_Entities[191]; e >= &g_Entities[160]; e--) {
        if (e->entityId == (obj->entityId & 0x3FF) &&
            g_LayoutOriginX[e - g_Entities] == obj->posX &&
            g_LayoutOriginY[e - g_Entities] == obj->posY) {
            return e;
        }
    }
    if (slot->entityId && sx > -WIDE_X - 32 && sx < 256 + WIDE_X + 32 &&
        sy > -WIDE_Y - 32 && sy < 240 + WIDE_Y + 32 &&
        (g_LayoutOriginX[i] != obj->posX || g_LayoutOriginY[i] != obj->posY)) {
        for (e = &g_Entities[191]; e >= &g_Entities[160]; e--) {
            if (!e->entityId) {
                return e;
            }
        }
    }
    return slot;
}
#else
#define LayoutSlotAlways(slot, obj) (slot)
#endif

#if defined(PLATFORM_PSYZ) && defined(STAGE_IS_RNO3)
// Port: the inverted Saturn hatch. The mirror of the NP3 hatch lid (the way to
// the Hell Garden) is baked closed in the tilemap of room 0 (tiles 0x132 and
// 0x129 at column 23, row 5 of 32x48) and no entity opens it, so a player who
// comes down the shaft from the cave above stands on it for good. The
// inverted castle comes after the Death meeting, so the lid is always open:
// the two hatch tiles of NP3 (0x6C8, 0x6C9) in mirrored order.
static void Rno3_OpenHatch(void) {
    u16* fg = g_Tilemap.fg;

    if (g_Tilemap.hSize == 2 && g_Tilemap.vSize == 3 &&
        fg[5 * 32 + 23] == 0x132 && fg[5 * 32 + 24] == 0x129) {
        fg[5 * 32 + 23] = 0x6C9;
        fg[5 * 32 + 24] = 0x6C8;
    }
}
#endif

void CreateEntityFromLayout(Entity* entity, LayoutEntity* initDesc) {
    Tilemap* tilemap;

    tilemap = &g_Tilemap;
#ifdef PLATFORM_PSYZ
    g_LayoutOriginX[entity - g_Entities] = initDesc->posX;
    g_LayoutOriginY[entity - g_Entities] = initDesc->posY;
#endif

    DestroyEntity(entity);
    entity->entityId = initDesc->entityId & 0x3FF;
    entity->pfnUpdate = PfnEntityUpdates[entity->entityId - 1];
    entity->posX.i.hi = (s16)initDesc->posX - tilemap->scrollX.i.hi;
    entity->posY.i.hi = (s16)initDesc->posY - tilemap->scrollY.i.hi;
    entity->params = initDesc->params;
    entity->entityRoomIndex = initDesc->entityRoomIndex >> 8;
    entity->unk68 = (initDesc->entityId >> 0xA) & 7;
#if defined(PLATFORM_PSYZ) && (defined(STAGE_IS_ARE) || defined(STAGE_IS_NO2))
    JpCards_OnLayout(entity);
#endif
#if defined(PLATFORM_PSYZ) && defined(STAGE_IS_RNO3)
    Rno3_OpenHatch();
#endif
}

void CreateEntityWhenInVerticalRange(LayoutEntity* obj) {
    s16 yClose;
    s16 yFar;
    s16 posY;
    Entity* entity;
    Tilemap* tilemap;

    tilemap = &g_Tilemap;
    posY = tilemap->scrollY.i.hi;
    yClose = posY - 0x40 - WIDE_Y;
    yFar = posY + 0x120 + WIDE_Y;
    if (yClose < 0) {
        yClose = 0;
    }

    posY = obj->posY;
    if (posY < yClose || posY > yFar) {
        return;
    }
    switch (obj->entityId & 0xE000) {
    case 0x0:
        entity =
            g_Entities + STAGE_ENTITY_START + (obj->entityRoomIndex & 0xFF);
        if (!entity->entityId) {
            CreateEntityFromLayout(entity, obj);
        }
        break;
    case 0xA000:
        entity =
            g_Entities + STAGE_ENTITY_START + (obj->entityRoomIndex & 0xFF);
        entity = LayoutSlotAlways(entity, obj);
        CreateEntityFromLayout(entity, obj);
        break;
    case 0x8000:
        break;
    }
}

void CreateEntityWhenInHorizontalRange(LayoutEntity* obj) {
    s16 xClose;
    s16 xFar;
    s16 posX;
    Entity* entity;
    Tilemap* tilemap;

    tilemap = &g_Tilemap;
    posX = tilemap->scrollX.i.hi;
    xClose = posX - 0x40 - WIDE_X;
    xFar = posX + 0x140 + WIDE_X;
    if (xClose < 0) {
        xClose = 0;
    }

    posX = obj->posX;
    if (posX < xClose || posX > xFar) {
        return;
    }

    switch (obj->entityId & 0xE000) {
    case 0x0:
        entity =
            g_Entities + STAGE_ENTITY_START + (obj->entityRoomIndex & 0xFF);
        if (!entity->entityId) {
            CreateEntityFromLayout(entity, obj);
        }
        break;
    case 0xA000:
        entity =
            g_Entities + STAGE_ENTITY_START + (obj->entityRoomIndex & 0xFF);
        entity = LayoutSlotAlways(entity, obj);
        CreateEntityFromLayout(entity, obj);
        break;
    case 0x8000:
        break;
    }
}

/*
 * Scans `g_LayoutObjVertical` looking for the next element with
 * an x  position to the right of the provided argument or the end
 * of the list.
 *
 * - Parameters:
 *   - posX - the position to search for
 *
 * - Side Effects:
 *    `g_LayoutObjHorizontal` will be positioned at an element with a
 *    x position larger than `posX` or the tail eleement.
 */
static void FindFirstEntityToTheRight(s16 posX) {
    while (1) {
        u16* layoutEntity = g_LayoutObjHorizontal;
        if (layoutEntity[LAYOUT_OBJ_POS_X] != LAYOUT_OBJ_START &&
            layoutEntity[LAYOUT_OBJ_POS_X] >= posX) {
            break;
        }

        g_LayoutObjHorizontal += sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Scans `g_LayoutObjHorizontal` looking for the next element with an x
 * position below the provided argument or the beginning of the list.
 *
 * - Parameters:
 *   - posX - the position to search for
 *
 * - Side Effects:
 *    `g_LayoutObjVertical` will be positioned at an element with an
 *    x position smaller than `posX` or the head element.
 */
static void FindFirstEntityToTheLeft(s16 posX) {
    while (true) {
        u16* layoutEntity = g_LayoutObjHorizontal;
        if (layoutEntity[LAYOUT_OBJ_POS_X] != LAYOUT_OBJ_END &&
            (layoutEntity[LAYOUT_OBJ_POS_X] <= posX ||
             layoutEntity[LAYOUT_OBJ_POS_X] == LAYOUT_OBJ_START)) {
            break;
        }
        g_LayoutObjHorizontal -= sizeof(LayoutEntity) / sizeof(u16);
    }
}

#if TARGET == TARGET_PSP
#define DECREMENT_AND_CHECK(x) (x)--
#else
#define DECREMENT_AND_CHECK(x) --(x) != (u8) - 1
#endif

/*
 * Creates entities between the previous horizontal position and
 * the passed in x position toward the right.
 *
 * - Parameters:
 *   - posX - the new right extent
 */
static void CreateEntitiesToTheRight(s16 posX) {
    const s32 expected = false;
    u16* layoutObjHorizontal;
    u8 flag;

    // if previously scrolling left, scan right to the
    // previous x delta and start traversal from there
    // otherwise keep going from the current position.
    if (g_LayoutObjPosHorizontal) {
        FindFirstEntityToTheRight(posX - g_ScrollDeltaX);
        g_LayoutObjPosHorizontal = LAYOUT_OBJ_POSITION_FORWARD;
    }

    while (true) {
        layoutObjHorizontal = g_LayoutObjHorizontal;
        if (layoutObjHorizontal[LAYOUT_OBJ_POS_X] == LAYOUT_OBJ_END ||
            posX < layoutObjHorizontal[LAYOUT_OBJ_POS_X]) {
            break;
        }

        flag = (layoutObjHorizontal[LAYOUT_OBJ_SLOT] >> 8);
        if (DECREMENT_AND_CHECK(flag)) {
            if ((g_unkGraphicsStruct.D_80097428[flag >> 5] &
                 (1 << (flag & 0x1F))) == expected) {
                CreateEntityWhenInVerticalRange(
                    (LayoutEntity*)g_LayoutObjHorizontal);
            }
        } else {
            CreateEntityWhenInVerticalRange(
                (LayoutEntity*)g_LayoutObjHorizontal);
        }

        g_LayoutObjHorizontal += sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Creates entities between the previous horizontal position and
 * the passed in x position.
 *
 * - Parameters:
 *   - posX - the new left extent
 */
static void CreateEntitiesToTheLeft(s16 posX) {
    const s32 expected = false;
    u16* layoutObjHorizontal;
    u8 flag;

    if (posX < 0) {
        posX = 0;
    }

    if (!g_LayoutObjPosHorizontal) {
        FindFirstEntityToTheLeft(posX - g_ScrollDeltaX);
        g_LayoutObjPosHorizontal = LAYOUT_OBJ_POSITION_BACKWARD;
    }
// MAD and TEK only run once to create one entity to the left.
#if VERSION >= VER_BETA_HD
    while (true) {
#else
    {
#endif
        layoutObjHorizontal = g_LayoutObjHorizontal;
        if (layoutObjHorizontal[LAYOUT_OBJ_POS_X] == LAYOUT_OBJ_START) {
            return;
        }

        if (posX > layoutObjHorizontal[LAYOUT_OBJ_POS_X]) {
            return;
        }

        flag = (layoutObjHorizontal[LAYOUT_OBJ_SLOT] >> 8);
        if (DECREMENT_AND_CHECK(flag)) {
            if ((g_unkGraphicsStruct.D_80097428[flag >> 5] &
                 (1 << (flag & 0x1F))) == expected) {
                CreateEntityWhenInVerticalRange(
                    (LayoutEntity*)g_LayoutObjHorizontal);
            }
        } else {
            CreateEntityWhenInVerticalRange(
                (LayoutEntity*)g_LayoutObjHorizontal);
        }

        g_LayoutObjHorizontal -= sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Scans `g_LayoutObjVertical` looking for the next element with
 * a y position above the provided argument or the end of the list.
 *
 * - Parameters:
 *   - posY - the position to search for
 *
 * - Side Effects:
 *    `g_LayoutObjVertical` will be positioned at an element with a
 *    y position larger than `posY` or the tail eleement.
 */
static void FindFirstEntityAbove(s16 posY) {
    while (true) {
        u16* ptr = &g_LayoutObjVertical[LAYOUT_OBJ_POS_Y];
        if (*ptr != LAYOUT_OBJ_START && *ptr >= posY) {
            break;
        }
        g_LayoutObjVertical += sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Scans `g_LayoutObjVertical` looking for the next element with a y
 * position below the provided argument or the beginning of the list.
 *
 * - Parameters:
 *   - posY - the position to search for
 *
 * - Side Effects:
 *    `g_LayoutObjVertical` will be positioned at an element with a
 *    y position smaller than `posY` or the head element.
 */
static void FindFirstEntityBelow(s16 posY) {
    while (true) {
        u16* ptr = &g_LayoutObjVertical[LAYOUT_OBJ_POS_Y];
        if (*ptr != LAYOUT_OBJ_END &&
            (*ptr <= posY || *ptr == LAYOUT_OBJ_START)) {
            break;
        }
        g_LayoutObjVertical -= sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Creates entities between the previous vertical position and
 * the passed in y position going up.
 *
 * - Parameters:
 *   - posY - the new top extent
 */
static void CreateEntitiesAbove(s16 posY) {
    const s32 expected = false;
    u16* layout;
    u8 flag;

    if (g_LayoutObjPosVertical) {
        FindFirstEntityAbove(posY - g_ScrollDeltaY);
        g_LayoutObjPosVertical = LAYOUT_OBJ_POSITION_FORWARD;
    }

    while (true) {
        layout = &g_LayoutObjVertical[LAYOUT_OBJ_POS_Y];

        if (layout[LAYOUT_OBJ_POS_X] == LAYOUT_OBJ_END ||
            posY < layout[LAYOUT_OBJ_POS_X]) {
            break;
        }

        flag = layout[LAYOUT_OBJ_ID] >> 8;
        if (DECREMENT_AND_CHECK(flag)) {
            if ((g_unkGraphicsStruct.D_80097428[flag >> 5] &
                 (1 << (flag & 0x1F))) == expected) {
                CreateEntityWhenInHorizontalRange(
                    (LayoutEntity*)g_LayoutObjVertical);
            }
        } else {
            CreateEntityWhenInHorizontalRange(
                (LayoutEntity*)g_LayoutObjVertical);
        }

        g_LayoutObjVertical += sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Creates entities between the previous vertical position and
 * the passed in y position going down.
 *
 * - Parameters:
 *   - posY - the new top extent
 */
static void CreateEntitiesBelow(s16 posY) {
    const s32 expected = false;
    u16* layout;
    u8 flag;

    if (posY < 0) {
        posY = 0;
    }

    if (!g_LayoutObjPosVertical) {
        FindFirstEntityBelow(posY - g_ScrollDeltaY);
        g_LayoutObjPosVertical = LAYOUT_OBJ_POSITION_BACKWARD;
    }

    while (true) {
        layout = &g_LayoutObjVertical[LAYOUT_OBJ_POS_Y];
        if (layout[LAYOUT_OBJ_POS_X] == LAYOUT_OBJ_START ||
            (posY > layout[LAYOUT_OBJ_POS_X])) {
            break;
        }
        flag = layout[LAYOUT_OBJ_ID] >> 8;
        if (DECREMENT_AND_CHECK(flag)) {
            if ((g_unkGraphicsStruct.D_80097428[flag >> 5] &
                 (1 << (flag & 0x1F))) == expected) {
                CreateEntityWhenInHorizontalRange(
                    (LayoutEntity*)g_LayoutObjVertical);
            }
        } else {
            CreateEntityWhenInHorizontalRange(
                (LayoutEntity*)g_LayoutObjVertical);
        }

        g_LayoutObjVertical -= sizeof(LayoutEntity) / sizeof(u16);
    }
}

/*
 * Sets up layout arrays and initializes stage state to
 * initial values.
 *
 * - Parameters:
 *   - objLayoutId - the room to initialize
 */
#ifdef PLATFORM_PSYZ
void RoomStitch_Layouts(u16** horizontal, u16** vertical, u16** h, u16** v);
#endif
void InitRoomEntities(s32 objLayoutId) {
    Tilemap* tilemap = &g_Tilemap;
    s16 rightEdge;
    s16 temp;
    s16 i;

    g_LayoutObjHorizontal = (u16*)OBJ_LAYOUT_HORIZONTAL[objLayoutId];
    g_LayoutObjVertical = (u16*)OBJ_LAYOUT_VERTICAL[objLayoutId];
#ifdef PLATFORM_PSYZ
    // a stitched room takes merged lists (sotn.ini room_stitch)
    RoomStitch_Layouts((u16**)OBJ_LAYOUT_HORIZONTAL,
                       (u16**)OBJ_LAYOUT_VERTICAL, &g_LayoutObjHorizontal,
                       &g_LayoutObjVertical);
#endif

#ifdef PSP_RNZ0_WTF
    if (objLayoutId == 8 && g_LayoutObjHorizontal[15] == 0x78) {
        g_LayoutObjHorizontal[15] += 16;
    }
#endif

    if (*g_LayoutObjHorizontal != LAYOUT_OBJ_START) {
        g_LayoutObjHorizontal++;
        temp = Random() & 0xFF;
        for (i = 0; true; i++) {
            temp -= *g_LayoutObjHorizontal++;
            if (temp < 0) {
                g_LayoutObjHorizontal =
                    (u16*)((g_LayoutObjHorizontal[LAYOUT_OBJ_POS_Y] << 16) +
                           g_LayoutObjHorizontal[LAYOUT_OBJ_POS_X]);
                break;
            }
            g_LayoutObjHorizontal += 2;
        }
        g_LayoutObjVertical += i * 2 + 2;
        g_LayoutObjVertical =
            (u16*)((g_LayoutObjVertical[LAYOUT_OBJ_POS_Y] << 0x10) +
                   g_LayoutObjVertical[LAYOUT_OBJ_POS_X]);
    }
    temp = tilemap->scrollX.i.hi;
    i = temp - 64 - WIDE_X;
    rightEdge = temp + 320 + WIDE_X;

    if (i < 0) {
        i = 0;
    }

    g_LayoutObjPosHorizontal = LAYOUT_OBJ_POSITION_FORWARD;
    g_LayoutObjPosVertical = LAYOUT_OBJ_POSITION_FORWARD;
    FindFirstEntityToTheRight(i);
    CreateEntitiesToTheRight(rightEdge);
    temp = tilemap->scrollY.i.hi;
    i = temp + 288 + WIDE_Y;
    FindFirstEntityAbove(i);
}

/*
 * Creates any new entities that may need to be created
 * based on a change in the scroll delta.
 */
void UpdateRoomPosition(void) {
    Tilemap* tilemap = &g_Tilemap;
    s16 tmp;

    if (g_ScrollDeltaX != 0) {
        tmp = tilemap->scrollX.i.hi;
        if (g_ScrollDeltaX > 0) {
            tmp += 320 + WIDE_X;
            CreateEntitiesToTheRight(tmp);
        } else {
            tmp -= 64 + WIDE_X;
            CreateEntitiesToTheLeft(tmp);
        }
    }

    if (g_ScrollDeltaY != 0) {
        tmp = tilemap->scrollY.i.hi;
        if (g_ScrollDeltaY > 0) {
            tmp += 288 + WIDE_Y;
            CreateEntitiesAbove(tmp);
        } else {
            tmp -= 64 + WIDE_Y;
            CreateEntitiesBelow(tmp);
        }
    }
}

void CreateEntityFromCurrentEntity(u16 entityId, Entity* entity) {
    DestroyEntity(entity);
    entity->entityId = entityId;
    entity->pfnUpdate = PfnEntityUpdates[entityId - 1];
    entity->posX.i.hi = g_CurrentEntity->posX.i.hi;
    entity->posY.i.hi = g_CurrentEntity->posY.i.hi;
}

void CreateEntityFromEntity(u16 entityId, Entity* source, Entity* entity) {
    DestroyEntity(entity);
    entity->entityId = entityId;
    entity->pfnUpdate = PfnEntityUpdates[entityId - 1];
    entity->posX.i.hi = source->posX.i.hi;
    entity->posY.i.hi = source->posY.i.hi;
}
