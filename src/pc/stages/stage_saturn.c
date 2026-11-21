// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SATURN_STAGE_ROOM_TEST
#include <psyz/cd.h>
#include <psyz/gpu.h>
#include <psyz/module.h>
#include "pc.h"
#include "music/xa_config.h"
#endif
#include "overlay.h"
#include "../spawn_point.h"

#ifndef SATURN_STAGE_ASSET_DIR
#define SATURN_STAGE_ASSET_DIR "stage15_assets"
#endif

#include "saturn_stage_data.h"

#if defined(SATURN_STAGE_GDN) && defined(SATURN_STAGE_ROOM_TEST)
#undef SATURN_STAGE_GDN
#endif

static SpriteParts* s_SpriteBanks[0x14];
#ifdef SATURN_STAGE_GDN
// stage15 and stage16: the PS1 stage framework (src/st/stage15, stage16) runs
// the layout entities, with the Saturn enemies and their converted sprites
// (gdn_entities.py)
#ifdef SATURN_STAGE_PRISON
#include "../../st/stage16/stage16.h"
#elif defined(SATURN_STAGE_RPRISON)
#include "../../st/rstage16/rstage16.h"
#else
#include "../../st/stage15/stage15.h"
#endif
#include "gdn_entities.h"
static u_long* s_GdnPalDef[] = {
    MAKE_PAL_OP(PAL_BULK_COPY, 0),
    PAL_BULK_(GDN_SPRITE_CLUT_BASE * COLORS_PER_PAL, s_GdnSpritePals,
              GDN_SPRITE_CLUT_COUNT * COLORS_PER_PAL),
    PAL_TERMINATE(),
};
static u_long* s_Cluts[] = {(u_long*)s_GdnPalDef};
static GfxBank* s_GfxBanks[] = {GDN_ROOM_GFX_BANKS};
#else
static u_long s_EmptyClut[] = {(u_long)-1};
static u_long* s_Cluts[] = {s_EmptyClut};
static GfxBank s_EmptyGfxBank = {.kind = GFX_BANK_NONE};
static GfxBank* s_GfxBanks[] = {&s_EmptyGfxBank};
#endif
static RoomDef* s_TileLayers;
static LayoutEntity* s_CurrentLayout;
static size_t s_CurrentLayoutCount;

static void Noop(void) {}

#ifndef SATURN_STAGE_ROOM_TEST
#define SATURN_STAGE_BACKDROP_ENTITY (TOTAL_ENTITY_COUNT - 1)
#define SATURN_STAGE_MASONRY_CLUT_SLOT 0x1FF
// stage15's EntityUpdates (src/st/stage15/e_init.c) holds these two
#ifdef SATURN_STAGE_GDN
#define SATURN_STAGE_ENTITY_FN
#else
#define SATURN_STAGE_ENTITY_FN static
#endif
static void EntitySaturnStageBackdrop(Entity* self);
SATURN_STAGE_ENTITY_FN void EntitySaturnStageMasonry(Entity* self);

#define SATURN_STAGE_MASONRY_PIXEL_BYTES (24 * 16 / 2)
#define SATURN_STAGE_MASONRY_PALETTE_WORDS 16
static u8 s_SaturnStageMasonryPixels[SATURN_STAGE_MASONRY_PIXEL_BYTES]
    __attribute__((aligned(4)));
static u16 s_SaturnStageMasonryPalette[SATURN_STAGE_MASONRY_PALETTE_WORDS];
static u16 s_SaturnStageMasonryTpage;
static bool s_SaturnStageMasonryReady;

static bool SaturnStageLoadMasonry(void) {
    s32 length;

    length = FileReadToBuf(
        SATURN_STAGE_ASSET_DIR "/masonry.chars.bin", s_SaturnStageMasonryPixels,
        0, sizeof(s_SaturnStageMasonryPixels));
    if (length != sizeof(s_SaturnStageMasonryPixels)) {
        return false;
    }
    length = FileReadToBuf(
        SATURN_STAGE_ASSET_DIR "/masonry.pal.bin", s_SaturnStageMasonryPalette,
        0, sizeof(s_SaturnStageMasonryPalette));
    return length == sizeof(s_SaturnStageMasonryPalette);
}

#ifndef SATURN_STAGE_ROOM_TEST
extern s32 g_CurCdPos;
extern XaMusicConfig g_XaMusicConfigs[];

enum {
    SATURN_STAGE_MUSIC = 0x533,
    SATURN_STAGE_MUSIC_LOOP_POINT,
};

static void SaturnStageConfigureMusic(void) {
    XaMusicConfig* start = &g_XaMusicConfigs[SATURN_STAGE_MUSIC - 0x300];
    XaMusicConfig* loop =
        &g_XaMusicConfigs[SATURN_STAGE_MUSIC_LOOP_POINT - 0x300];
    u32 base = 0U - (u32)g_CurCdPos;

    Psyz_CdSetDiskPath(SATURN_STAGE_ASSET_DIR "/music/audio.cue");
    *start = (XaMusicConfig){
        base,
        SATURN_XA_INTRO_VSYNC,
        SATURN_XA_FILE_NUMBER,
        SATURN_XA_CHANNEL,
        80,
        0,
        1};
    *loop = (XaMusicConfig){
        base + SATURN_XA_LOOP_SECTOR,
        SATURN_XA_LOOP_VSYNC,
        SATURN_XA_FILE_NUMBER,
        SATURN_XA_CHANNEL,
        80,
        0,
        0};
}
#endif

#ifdef SATURN_STAGE_ENTITY_LABELS
static void SaturnStageDrawEntityLabels(void) {
    LayoutEntity* layout;
    DR_MODE* drawMode;
    SPRT* sprite;
    TILE* tile;
    char label[24];
    const char* character;
    s32 characterIndex;
    s32 labelLength;
    s32 layoutIndex;
    s32 screenX;
    s32 screenY;
    s32 tpage;

    if (s_CurrentLayout == NULL || g_GpuUsage.drawModes >= MAX_DRAW_MODES) {
        return;
    }

    tpage = getTPage(0, 0, 0x380, 0x100);
    drawMode = &g_CurrentBuffer->drawModes[g_GpuUsage.drawModes];
    SetDrawMode(drawMode, 0, 0, tpage, NULL);

    for (layoutIndex = 0; layoutIndex < s_CurrentLayoutCount; layoutIndex++) {
        layout = &s_CurrentLayout[layoutIndex];
        screenX = layout->posX - g_Tilemap.scrollX.i.hi;
        screenY = layout->posY - g_Tilemap.scrollY.i.hi;
        if (screenX < -32 || screenX >= DISP_STAGE_W + 32 || screenY < -16 ||
            screenY >= DISP_STAGE_H + 16) {
            continue;
        }

        labelLength = snprintf(label, sizeof(label), "%d:%d",
                               layout->entityId & 0x3FF, layout->params);
        if (labelLength <= 0) {
            continue;
        }

        if (g_GpuUsage.tile + 3 <= MAX_TILE_COUNT) {
            tile = &g_CurrentBuffer->tiles[g_GpuUsage.tile++];
            SetTile(tile);
            SetSemiTrans(tile, false);
            setRGB0(tile, 0, 0, 0);
            setXY0(tile, screenX + g_cameraOffsetX + 3,
                   screenY + g_cameraOffsetY - 5);
            setWH(tile, labelLength * 8 + 2, 10);
            AddPrim(&g_CurrentBuffer->ot[0x1EF], tile);

            tile = &g_CurrentBuffer->tiles[g_GpuUsage.tile++];
            SetTile(tile);
            SetSemiTrans(tile, false);
            setRGB0(tile, 255, 64, 64);
            setXY0(
                tile, screenX + g_cameraOffsetX - 3, screenY + g_cameraOffsetY);
            setWH(tile, 7, 1);
            AddPrim(&g_CurrentBuffer->ot[0x1F0], tile);

            tile = &g_CurrentBuffer->tiles[g_GpuUsage.tile++];
            SetTile(tile);
            SetSemiTrans(tile, false);
            setRGB0(tile, 255, 64, 64);
            setXY0(
                tile, screenX + g_cameraOffsetX, screenY + g_cameraOffsetY - 3);
            setWH(tile, 1, 7);
            AddPrim(&g_CurrentBuffer->ot[0x1F0], tile);
        }

        for (character = label; *character != '\0'; character++) {
            if (g_GpuUsage.sp >= MAX_SPRT_COUNT) {
                break;
            }
            characterIndex = (u8)*character - 0x20;
            sprite = &g_CurrentBuffer->sprite[g_GpuUsage.sp++];
            SetSprt8(sprite);
            SetSemiTrans(sprite, false);
            SetShadeTex(sprite, true);
            setRGB0(sprite, 128, 128, 128);
            sprite->x0 =
                screenX + 4 + (character - label) * 8 + g_cameraOffsetX;
            sprite->y0 = screenY - 4 + g_cameraOffsetY;
            sprite->u0 = (characterIndex & 0xF) * 8;
            sprite->v0 = (characterIndex >> 4) * 8;
            sprite->clut = getClut(0x380, 0x180);
            AddPrim(&g_CurrentBuffer->ot[0x1F0], sprite);
        }
    }

    AddPrim(&g_CurrentBuffer->ot[0x1F0], drawMode);
    g_GpuUsage.drawModes++;
}
#endif

static s32 SaturnStageCurrentRoom(void) {
    s32 room;

    for (room = 0; room < LEN(s_SaturnStageRoomBounds); room++) {
        if (g_Tilemap.left == s_SaturnStageRoomBounds[room][0] &&
            g_Tilemap.top == s_SaturnStageRoomBounds[room][1] &&
            g_Tilemap.right == s_SaturnStageRoomBounds[room][2] &&
            g_Tilemap.bottom == s_SaturnStageRoomBounds[room][3]) {
            return room;
        }
    }
    return -1;
}

#ifndef SATURN_STAGE_GDN
enum Palettes { PAL_NONE };

// clang-format off
EInit g_EInitCommon = {ANIMSET_DRA(0), 0x00, 0x00, PAL_NONE, ENEMY_COMMON};
// clang-format on
#endif
u16 g_RedDoorTiles[2][8];
// The door's tile writes land here, not in the native-layer stage. With
// hSize 0 the index is (screen x >> 4): an s16 screen x gives -2048..2047
// (a door far off screen, as in the inverted castle's right-hand doors).
static u16 s_SaturnStageRedDoorTiles[4096];
static Tilemap s_SaturnStageRedDoorTilemap = {
    .fg = &s_SaturnStageRedDoorTiles[2048],
    .hSize = 0,
};

static u8 SaturnStageDoorSideToPlayer(Entity* self) {
    u8 side = 0;
    if (self->posX.i.hi > PLAYER.posX.i.hi) {
        side |= 1;
    }
    if (self->posY.i.hi > PLAYER.posY.i.hi) {
        side |= 2;
    }
    return side;
}

static void SaturnStageInitializeDoor(Entity* self) {
    u16* init = g_EInitCommon;
    EnemyDef* enemy;

    self->animSet = *init++;
    self->animCurFrame = *init++;
    self->unk5A = *init++;
    self->palette = *init++;
    self->enemyId = *init++;
    enemy = &g_api.enemyDefs[self->enemyId];
    self->hitPoints = enemy->hitPoints;
    self->attack = enemy->attack;
    self->attackElement = enemy->attackElement;
    self->hitboxState = enemy->hitboxState;
    self->hitboxWidth = enemy->hitboxWidth;
    self->hitboxHeight = enemy->hitboxHeight;
    self->flags = enemy->flags;
    self->hitboxOffX = 0;
    self->hitboxOffY = 0;
    self->step++;
    self->step_s = 0;
    if (!self->zPriority) {
        self->zPriority = g_unkGraphicsStruct.g_zEntityCenter - 0xC;
    }
}

#define InitializeEntity(init) SaturnStageInitializeDoor(self)
#define GetSideToPlayer() SaturnStageDoorSideToPlayer(self)
#define g_Tilemap s_SaturnStageRedDoorTilemap
#include "../../st/e_red_door.h"
#undef g_Tilemap
#undef GetSideToPlayer
#undef InitializeEntity

static bool SaturnStageBackdropIsActive(void) {
    return g_Entities[SATURN_STAGE_BACKDROP_ENTITY].pfnUpdate ==
           EntitySaturnStageBackdrop;
}
#endif

#ifdef SATURN_STAGE_NATIVE_8X8
#define SATURN_STAGE_NATIVE_LAYERS 3
#define SATURN_STAGE_NATIVE_CHARS_SIZE                                         \
    (SATURN_STAGE_MAX_LAYER_PAGES * 256 * 256 / 2)
#define SATURN_STAGE_NATIVE_MAP_WORDS (360 * 64 + 2)
#define SATURN_STAGE_NATIVE_PAL_WORDS (16 * 16)
#define SATURN_STAGE_CHAR_SIZE 8
#define SATURN_STAGE_ATLAS_COLUMNS 31
#define SATURN_STAGE_PAGE_CHARS                                                \
    (SATURN_STAGE_ATLAS_COLUMNS * SATURN_STAGE_ATLAS_COLUMNS)
#define SATURN_STAGE_PAGE_BYTES (256 * 256 / 2)
#define SATURN_STAGE_MAX_LAYER_PAGES 2
#define SATURN_STAGE_MAX_ROOM_PAGES 4
#define SATURN_STAGE_PAGE_VRAM_X 0x200
#define SATURN_STAGE_PAGE_VRAM_W 0x40
#define SATURN_STAGE_SATURN_W 320
#define SATURN_STAGE_TO_SATURN_X(x) ((x) * SATURN_STAGE_SATURN_W / DISP_STAGE_W)
// Wide view (src/pc/wide.c): the margins past the 256x240 frame. x is in
// Saturn px, rounded up, so the last column reaches the screen edge.
#define SATURN_STAGE_WIDE_X                                                    \
    ((WIDE_X * SATURN_STAGE_SATURN_W + DISP_STAGE_W - 1) / DISP_STAGE_W)
#define SATURN_STAGE_WIDE_Y WIDE_Y

static s32 SaturnStageFloorDiv(s32 value, s32 divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

static s32 SaturnStageWrap(s32 value, s32 size) {
    value %= size;
    return value < 0 ? value + size : value;
}

typedef struct {
    u_long chars[SATURN_STAGE_NATIVE_CHARS_SIZE / sizeof(u_long)];
    u16 map[SATURN_STAGE_NATIVE_MAP_WORDS];
    u16 palettes[SATURN_STAGE_NATIVE_PAL_WORDS];
    u16 cluts[16];
    u16 tpages[SATURN_STAGE_MAX_LAYER_PAGES];
    s32 pageCount;
    u8 characterVisible[SATURN_STAGE_MAX_LAYER_PAGES * SATURN_STAGE_PAGE_CHARS];
    s32 paletteCount;
    u16 width;
    u16 height;
} SaturnStageNativeLayer;

static SaturnStageNativeLayer s_NativeLayers[SATURN_STAGE_NATIVE_LAYERS];
static s32 s_NativeRoom = -1;
static bool s_NativeReady;
static bool s_GameOverDisk;

static bool SaturnStageLoadNativeLayer(s32 room, s32 layer) {
    SaturnStageNativeLayer* native = &s_NativeLayers[layer];
    char path[256];
    s32 length;
    s32 entryCount;

    snprintf(path, sizeof(path),
             SATURN_STAGE_ASSET_DIR "/native_room%d_layer%d.chars.bin", room,
             layer);
    length = FileReadToBuf(path, native->chars, 0, sizeof(native->chars));
    if (length <= 0 || length % SATURN_STAGE_PAGE_BYTES) {
        return false;
    }
    native->pageCount = length / SATURN_STAGE_PAGE_BYTES;

    snprintf(path, sizeof(path),
             SATURN_STAGE_ASSET_DIR "/native_room%d_layer%d.pal.bin", room,
             layer);
    length = FileReadToBuf(path, native->palettes, 0, sizeof(native->palettes));
    if (length <= 0 || (length & 0x1)) {
        return false;
    }
    native->paletteCount = length / (16 * sizeof(u16));
    if (native->paletteCount > 16) {
        native->paletteCount = 16;
    }
    {
        const u8* pixels = (const u8*)native->chars;
        s32 character;
        s32 row;
        s32 byte;
        for (character = 0; character < LEN(native->characterVisible);
             character++) {
            native->characterVisible[character] = 0;
            for (row = 0; row < 8 && !native->characterVisible[character];
                 row++) {
                s32 index = character % SATURN_STAGE_PAGE_CHARS;
                s32 offset =
                    character / SATURN_STAGE_PAGE_CHARS *
                        SATURN_STAGE_PAGE_BYTES +
                    (index / SATURN_STAGE_ATLAS_COLUMNS * 8 + row) * 128 +
                    index % SATURN_STAGE_ATLAS_COLUMNS * 4;
                for (byte = 0; byte < 4; byte++) {
                    if (pixels[offset + byte] != 0) {
                        native->characterVisible[character] = 1;
                        break;
                    }
                }
            }
        }
    }
    snprintf(path, sizeof(path),
             SATURN_STAGE_ASSET_DIR "/native_room%d_layer%d.map.bin", room,
             layer);
    length = FileReadToBuf(path, native->map, 0, sizeof(native->map));
    if (length < 4 || (length & 0x1)) {
        return false;
    }
    native->width = native->map[0];
    native->height = native->map[1];
    entryCount = native->width * native->height;
    if (native->width == 0 || native->height == 0 ||
        entryCount > SATURN_STAGE_NATIVE_MAP_WORDS - 2 ||
        length < (entryCount + 2) * sizeof(u16)) {
        return false;
    }
    return true;
}

static bool SaturnStageLoadNativeRoom(s32 room) {
    RECT paletteRect;
    SaturnStageNativeLayer* native;
    s32 layer;
    s32 palette;
    s32 paletteSlot;
    s32 page;
    s32 vramPage = 0;

    s_NativeReady = false;
    s_NativeRoom = room;
    for (layer = 0; layer < SATURN_STAGE_NATIVE_LAYERS; layer++) {
        if (!SaturnStageLoadNativeLayer(room, layer)) {
            return false;
        }
    }

    for (layer = 0; layer < SATURN_STAGE_NATIVE_LAYERS; layer++) {
        native = &s_NativeLayers[layer];
        for (page = 0; page < native->pageCount; page++, vramPage++) {
            if (vramPage >= SATURN_STAGE_MAX_ROOM_PAGES) {
                return false;
            }
            native->tpages[page] = LoadTPage(
                &native->chars[page * SATURN_STAGE_PAGE_BYTES / sizeof(u_long)],
                0, 0,
                SATURN_STAGE_PAGE_VRAM_X + vramPage * SATURN_STAGE_PAGE_VRAM_W,
                0, 0x100, 0x100);
        }
    }
    for (layer = 0; layer < SATURN_STAGE_NATIVE_LAYERS; layer++) {
        native = &s_NativeLayers[layer];
        for (palette = 0; palette < native->paletteCount; palette++) {
            paletteSlot = layer * 16 + palette;
            paletteRect.x = 0x300 + (paletteSlot & 0xF) * 16;
            paletteRect.y = 0xF0 + (paletteSlot >> 4);
            paletteRect.w = 16;
            paletteRect.h = 1;
            native->cluts[palette] =
                LoadClut2((u_long*)&native->palettes[palette * 16],
                          paletteRect.x, paletteRect.y);
        }
    }
    s_NativeReady = true;
    return true;
}

static void SaturnStageDrawNativeBlock(
    SaturnStageNativeLayer* native, s32 order, s32 mapX, s32 mapY,
    s32 blockWidth, s32 blockHeight, s32 x0, s32 y0, s32 x1, s32 y1) {
    POLY_GT4* poly; // a GT4 slot stays GT4: menu and map code reuse it
    u16 pnd;
    u16 character;
    u16 palette;
    s32 index;
    u8 u0;
    u8 u1;
    u8 v0;
    u8 v1;
    bool blockVisible = false;
    s32 dx;
    s32 dy;

    if (x1 <= -WIDE_X || x0 >= DISP_STAGE_W + WIDE_X ||
        y0 <= -blockHeight * 8 - SATURN_STAGE_WIDE_Y ||
        y0 >= DISP_STAGE_H + SATURN_STAGE_WIDE_Y ||
        g_GpuUsage.gt4 >= MAX_POLY_GT4_COUNT) {
        return;
    }
    pnd = native->map[2 + mapY * native->width + mapX];
    character = pnd & 0xFFF;
    palette = pnd >> 12;
    if (palette >= native->paletteCount) {
        return;
    }
    for (dy = 0; dy < blockHeight && !blockVisible; dy++) {
        for (dx = 0; dx < blockWidth; dx++) {
            u16 member =
                native->map[2 + (mapY + dy) * native->width + mapX + dx];
            if (native->characterVisible[member & 0xFFF]) {
                blockVisible = true;
                break;
            }
        }
    }
    if (!blockVisible) {
        return;
    }

    if (character >= native->pageCount * SATURN_STAGE_PAGE_CHARS) {
        return;
    }
    index = character % SATURN_STAGE_PAGE_CHARS;
    u0 = index % SATURN_STAGE_ATLAS_COLUMNS * SATURN_STAGE_CHAR_SIZE;
    v0 = index / SATURN_STAGE_ATLAS_COLUMNS * SATURN_STAGE_CHAR_SIZE;
    u1 = u0 + blockWidth * SATURN_STAGE_CHAR_SIZE;
    v1 = v0 + blockHeight * SATURN_STAGE_CHAR_SIZE;

    poly = &g_CurrentBuffer->polyGT4[g_GpuUsage.gt4++];
    setPolyGT4(poly);
    setShadeTex(poly, true);
    setSemiTrans(poly, false);
    setRGB0(poly, 0x80, 0x80, 0x80);
    setRGB1(poly, 0x80, 0x80, 0x80);
    setRGB2(poly, 0x80, 0x80, 0x80);
    setRGB3(poly, 0x80, 0x80, 0x80);
    poly->x0 = poly->x2 = x0 + g_cameraOffsetX;
    poly->x1 = poly->x3 = x1 + g_cameraOffsetX;
    poly->y0 = poly->y1 = y0 + g_cameraOffsetY;
    poly->y2 = poly->y3 = y1 + g_cameraOffsetY;
    poly->u0 = poly->u2 = u0;
    poly->u1 = poly->u3 = u1;
    poly->v0 = poly->v1 = v0;
    poly->v2 = poly->v3 = v1;
    poly->tpage = native->tpages[character / SATURN_STAGE_PAGE_CHARS];
    poly->clut = native->cluts[palette];
    AddPrim(&g_CurrentBuffer->ot[order], poly);
}

// Draws a layer of 8x8 Saturn cells in game px: x is the Saturn x * 256/320.
static void SaturnStageDrawNativeLayer(s32 layer, s32 order, bool wrap) {
    SaturnStageNativeLayer* native = &s_NativeLayers[layer];
    s32 scrollGame = layer == 1 ? 0 : g_Tilemap.scrollX.i.hi;
    s32 vw = DISP_STAGE_W + 2 * WIDE_X;
    s32 scrollX = SATURN_STAGE_TO_SATURN_X(g_Tilemap.scrollX.i.hi);
    s32 scrollY = g_Tilemap.scrollY.i.hi;
    s32 startX;
    s32 startY;
    s32 endX;
    s32 endY;
    s32 logicalX;
    s32 logicalY;
    s32 mapX;
    s32 mapY;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;
    // the sky (layer 1) is one Saturn screen: in the wide view stretch it
    // over the view instead of a repeat with seams at game x 0 and 256
    bool stretchX = layer == 1 && SATURN_STAGE_WIDE_X &&
                    native->width * SATURN_STAGE_CHAR_SIZE <= SATURN_STAGE_SATURN_W;
    bool stretchY = layer == 1 && SATURN_STAGE_WIDE_Y &&
                    native->height * SATURN_STAGE_CHAR_SIZE <= 256;

    if (layer == 1) {
        scrollX = 0;
        scrollY = 10;
    }
    if (wrap) {
        scrollX =
            SaturnStageWrap(scrollX, native->width * SATURN_STAGE_CHAR_SIZE);
        scrollY =
            SaturnStageWrap(scrollY, native->height * SATURN_STAGE_CHAR_SIZE);
    }
    startX = SaturnStageFloorDiv(
                 scrollX - SATURN_STAGE_WIDE_X, SATURN_STAGE_CHAR_SIZE) -
             1;
    startY = SaturnStageFloorDiv(
        scrollY - SATURN_STAGE_WIDE_Y, SATURN_STAGE_CHAR_SIZE);
    endX = startX +
           (SATURN_STAGE_SATURN_W + 2 * SATURN_STAGE_WIDE_X) /
               SATURN_STAGE_CHAR_SIZE +
           3;
    endY = startY +
           (DISP_STAGE_H + 2 * SATURN_STAGE_WIDE_Y) / SATURN_STAGE_CHAR_SIZE +
           (SATURN_STAGE_WIDE_Y ? 1 : 0);
    if (stretchX) {
        startX = 0;
        endX = native->width - 1;
    }
    if (stretchY) {
        startY = 0;
        endY = native->height - 1;
    }
    for (logicalY = startY; logicalY <= endY; logicalY++) {
        mapY = wrap ? SaturnStageWrap(logicalY, native->height) : logicalY;
        if (mapY < 0 || mapY >= native->height) {
            continue;
        }
        y0 = logicalY * SATURN_STAGE_CHAR_SIZE - scrollY;
        y1 = y0 + SATURN_STAGE_CHAR_SIZE;
        if (stretchY) {
            // rows -scrollY..240-scrollY of the 256 px sky fill the view
            y0 = y0 * (DISP_STAGE_H + 2 * SATURN_STAGE_WIDE_Y) / DISP_STAGE_H -
                 SATURN_STAGE_WIDE_Y;
            y1 = y1 * (DISP_STAGE_H + 2 * SATURN_STAGE_WIDE_Y) / DISP_STAGE_H -
                 SATURN_STAGE_WIDE_Y;
        }
        for (logicalX = startX; logicalX <= endX; logicalX++) {
            mapX = wrap ? SaturnStageWrap(logicalX, native->width) : logicalX;
            if (mapX < 0 || mapX >= native->width) {
                continue;
            }
            if (stretchX) {
                x0 = (logicalX * SATURN_STAGE_CHAR_SIZE - scrollX) * vw /
                         (native->width * SATURN_STAGE_CHAR_SIZE) -
                     WIDE_X;
                x1 = ((logicalX + 1) * SATURN_STAGE_CHAR_SIZE - scrollX) * vw /
                         (native->width * SATURN_STAGE_CHAR_SIZE) -
                     WIDE_X;
            } else {
                // Cell edges on the map in game px (x * 256 / 320, floor),
                // moved by the camera in whole game px: a cell keeps its
                // width (6 or 7 px) at every scroll. A grid applied after the
                // scroll gave each cell a width that changed with the scroll
                // phase, so thin art flickered while the camera moved.
                x0 = SaturnStageFloorDiv(
                         logicalX * SATURN_STAGE_CHAR_SIZE * DISP_STAGE_W,
                         SATURN_STAGE_SATURN_W) -
                     scrollGame;
                x1 = SaturnStageFloorDiv(
                         (logicalX + 1) * SATURN_STAGE_CHAR_SIZE * DISP_STAGE_W,
                         SATURN_STAGE_SATURN_W) -
                     scrollGame;
            }
            SaturnStageDrawNativeBlock(
                native, order, mapX, mapY, 1, 1, x0, y0, x1, y1);
        }
    }
}

static void SaturnStageDrawBackScreen(void) {
    enum {
        BACKDROP_PRIORITY = 0x10,
        BACKDROP_RED = 0x31,
        BACKDROP_GREEN = 0x00,
        BACKDROP_BLUE = 0x29,
    };
    TILE* tile;

    if (g_GpuUsage.tile >= MAX_TILE_COUNT) {
        return;
    }
    tile = &g_CurrentBuffer->tiles[g_GpuUsage.tile++];
    SetTile(tile);
    SetSemiTrans(tile, false);
    setRGB0(tile, BACKDROP_RED, BACKDROP_GREEN, BACKDROP_BLUE);
    setXY0(tile, g_cameraOffsetX, g_cameraOffsetY);
    setWH(tile, DISP_STAGE_W, DISP_STAGE_H);
    Wide_Tile(tile);
    AddPrim(&g_CurrentBuffer->ot[BACKDROP_PRIORITY], tile);
}

static bool SaturnStageRenderNativeLayers(void) {
    s32 room = SaturnStageCurrentRoom();

#ifndef SATURN_STAGE_ROOM_TEST
    // game over hides the layers (HideAllBackgroundLayers) and loads its own
    // picture into the VRAM pages of this room. Its music and the title's
    // are PS1 tracks: give the CD the game's disk again.
    if (g_GameState == Game_GameOver) {
        if (!s_GameOverDisk) {
            Psyz_CdSetDiskPath(g_GameParams.diskPath);
            s_GameOverDisk = true;
        }
        return true;
    }
#endif
    if (room < 0) {
        return false;
    }
    if (room != s_NativeRoom && !SaturnStageLoadNativeRoom(room)) {
        return false;
    }
    if (!s_NativeReady) {
        return false;
    }
    if (g_Tilemap.hideTimer > 0) {
        g_Tilemap.hideTimer--;
        return true;
    }

    if (room < SATURN_STAGE_ORDINARY_ROOMS && SaturnStageBackdropIsActive()) {
#if SATURN_STAGE_HAS_BACK_SCREEN
        SaturnStageDrawBackScreen();
#endif
        SaturnStageDrawNativeLayer(1, 0x20, true);
    }
    SaturnStageDrawNativeLayer(2, 0x40, false);
    SaturnStageDrawNativeLayer(
        0, room >= SATURN_STAGE_ORDINARY_ROOMS ? 0xA0 : 0x60, false);
#ifdef SATURN_STAGE_ENTITY_LABELS
    SaturnStageDrawEntityLabels();
#endif
    return true;
}

// g_PcStageNativeLayer: the planes SaturnStageRenderNativeLayers draws
static const PcNativeLayer* SaturnStageNativePlane(int layer) {
    static PcNativeLayer out;
    SaturnStageNativeLayer* native;

    if (layer < 0 || layer >= SATURN_STAGE_NATIVE_LAYERS || !s_NativeReady ||
        s_NativeRoom != SaturnStageCurrentRoom()) {
        return NULL;
    }
    native = &s_NativeLayers[layer];
    out = (PcNativeLayer){
        .map = &native->map[2],
        .chars = (const u8*)native->chars,
        .palettes = native->palettes,
        .tpages = native->tpages,
        .cluts = native->cluts,
        .w = native->width,
        .h = native->height,
        .pages = native->pageCount,
        .paletteCount = native->paletteCount,
    };
    return &out;
}
#endif

#ifndef SATURN_STAGE_ROOM_TEST
static void EntitySaturnStageBackdrop(Entity* self) {
    LayerDef* def;
    BgLayer* bg = &g_BgLayers[1];

    (void)self;
    if (s_TileLayers == NULL) {
        return;
    }
    def = s_TileLayers[SATURN_STAGE_BACKDROP_LAYER].bg;
    if (def == NULL || def->tileDef == NULL) {
        bg->flags = 0;
        return;
    }
    bg->layout = def->layout;
    bg->tileDef = def->tileDef;
    bg->scrollX.i.hi = 0;
    bg->scrollY.i.hi = 10;
    bg->order = def->zPriority;
    bg->flags = def->flags;
    bg->w = def->rect.right - def->rect.left + 1;
    bg->h = def->rect.bottom - def->rect.top + 1;
    bg->scrollKind = def->rect.params;
    bg->hideTimer = 1;
}

static void EntitySaturnStageDummy(Entity* self) { (void)self; }

SATURN_STAGE_ENTITY_FN void EntitySaturnStageMasonry(Entity* self) {
    static const s8 offsetsX[3] = {-14, 2, 18};
    Primitive* prim;
    s32 column;
    s32 primIndex;
    s32 row;
    s32 rows;

    if (!s_SaturnStageMasonryReady) {
        return;
    }
    rows = (self->params & 2) ? 6 : 2;
    g_ClutIds[SATURN_STAGE_MASONRY_CLUT_SLOT] = getClut(0x3F0, 0xF3);
    if (self->step == 0) {
        primIndex = g_api.AllocPrimitives(PRIM_GT4, rows * 3);
        if (primIndex == -1) {
            return;
        }
        self->primIndex = primIndex;
        self->flags |= FLAG_HAS_PRIMS;
        self->step = 1;
    }

    prim = &g_PrimBuf[self->primIndex];
    for (row = 0; row < rows; row++) {
        for (column = 0; column < 3; column++, prim = prim->next) {
            s32 centerX = self->posX.i.hi + offsetsX[column];
            s32 centerY = self->posY.i.hi - 8 - row * 16;

            prim->tpage = s_SaturnStageMasonryTpage;
            prim->clut = SATURN_STAGE_MASONRY_CLUT_SLOT;
            prim->u0 = prim->u2 = 0;
            prim->u1 = prim->u3 = 24;
            prim->v0 = prim->v1 = 0;
            prim->v2 = prim->v3 = 16;
            prim->x0 = prim->x2 = centerX - 10;
            prim->x1 = prim->x3 = centerX + 9;
            prim->y0 = prim->y1 = centerY - 8;
            prim->y2 = prim->y3 = centerY + 8;
            prim->priority = 0x80;
            prim->drawMode = DRAW_UNK02;
        }
    }
}

SATURN_STAGE_ENTITY_FN void EntitySaturnStageSpecialController(Entity* self) {
    self->step = 1;
}

static void SaturnStageInitBackdrop(void) {
    Entity* backdrop = &g_Entities[SATURN_STAGE_BACKDROP_ENTITY];

    if (backdrop->pfnUpdate != EntitySaturnStageBackdrop) {
        DestroyEntity(backdrop);
        backdrop->entityId = 1;
        backdrop->pfnUpdate = EntitySaturnStageBackdrop;
    }
    EntitySaturnStageBackdrop(backdrop);
}

static void SaturnStageSetCurrentLayout(s32 layoutId) {
    s_CurrentLayout = layoutId >= 0 && layoutId < LEN(s_SaturnStageObjLayouts)
                          ? s_SaturnStageObjLayouts[layoutId]
                          : NULL;
    s_CurrentLayoutCount =
        layoutId >= 0 && layoutId < LEN(s_SaturnStageObjLayoutCounts)
            ? s_SaturnStageObjLayoutCounts[layoutId]
            : 0;
}

#ifdef SATURN_STAGE_GDN
extern PfnEntityUpdate EntityUpdates[];
extern PfnEntityUpdate* PfnEntityUpdates;
void Update(void);
void HitDetection(void);
void UpdateRoomPosition(void);
void InitRoomEntities(s32 objLayoutId);
void UpdateStageEntities(void);

u16 g_GdnSolidTile;

#define GDN_LAYOUTS LEN(s_SaturnStageObjLayouts)
// the head, the entries and the tail (the Prison's room 0 has 68 entries)
#define GDN_LAYOUT_MAX 80
static LayoutEntity s_GdnLayoutX[GDN_LAYOUTS][GDN_LAYOUT_MAX];
static LayoutEntity s_GdnLayoutY[GDN_LAYOUTS][GDN_LAYOUT_MAX];
// create_entity.h reads these by name on the PC (pfn_entity_update.h)
LayoutEntity* entityLayoutHorizontal[GDN_LAYOUTS];
LayoutEntity* entityLayoutVertical[GDN_LAYOUTS];

#ifdef SATURN_STAGE_PRISON
// Saturn STAGE_16 entity id -> stage16.h EntityID. The ids without a port
// stay idle.
static u16 SaturnGdnEntityId(u16 saturnId, bool saveRoom) {
    switch (saturnId) {
    case 0x01:
        return E_BREAKABLE;
    case 0x05:
        return E_RED_DOOR;
    case 0x09:
        return E_STAGE_NAME_POPUP;
    case 0x0F:
        return saveRoom ? E_SATURN_MASONRY : E_DUMMY_0F;
    case 0x10:
        return E_SATURN_CONTROLLER;
    case 0x11:
        return E_ID_11;
    case 0x18:
        return E_BONE_ARCHER;
    case 0x1A:
        return E_SPEAR_GUARD;
    case 0x1D:
        return E_SLINGER;
    case 0x20:
        return E_MOD_SPECTER;
    case 0x21:
        return E_MOD_GARGOYLE;
    case 0x24: // the Garden's 0x40 code; no Prison layout places it
        return E_MOD_SKBREEDER;
    case 0x28:
        return E_MOD_WISP_SPAWNER;
    case 0x16:
        return E_MOD_PILLAR_SWITCH;
    case 0x17:
        return E_MOD_PILLAR;
    default:
        return E_DUMMY_0F;
    }
}
#elif defined(SATURN_STAGE_RPRISON)
// Saturn RSTAGE16 entity id -> rstage16.h EntityID. The helpers (0x16, 0x17,
// 0x19-0x1B, 0x1D-0x1F, 0x21, 0x22, 0x24, 0x25, 0x27-0x2A) are spawned by
// their enemies; no layout places them.
static u16 SaturnGdnEntityId(u16 saturnId, bool saveRoom) {
    switch (saturnId) {
    case 0x01:
        return E_BREAKABLE;
    case 0x05:
        return E_RED_DOOR;
    case 0x0F:
        return saveRoom ? E_SATURN_MASONRY : E_DUMMY_0F;
    case 0x10:
        return E_SATURN_CONTROLLER;
    case 0x11:
        return E_ID_11;
    case 0x15:
        return E_MOD_WIGHT;
    case 0x18:
        return E_MOD_WRAITH;
    case 0x1C:
        return E_FLYING_ZOMBIE;
    case 0x20:
        return E_MOD_GARGOYLE;
    case 0x23:
        return E_JACK_O_BONES;
    case 0x26:
        return E_NOVA_SKELETON;
    default:
        return E_DUMMY_0F;
    }
}
#else
// Saturn STAGE_15 entity id -> stage15.h EntityID. The ids without a port
// (water, CD room foreground) stay idle.
static u16 SaturnGdnEntityId(u16 saturnId, bool saveRoom) {
    switch (saturnId) {
    case 0x01:
        return E_BREAKABLE;
    case 0x05:
        return E_RED_DOOR;
    case 0x09:
        return E_STAGE_NAME_POPUP;
    case 0x0F:
        return saveRoom ? E_SATURN_MASONRY : E_DUMMY_0F;
    case 0x10:
        return E_SATURN_CONTROLLER;
    case 0x11:
        return E_ID_11;
    case 0x1E:
        return E_MOD_DRIP_SPAWNER;
    case 0x20:
        return E_SLINGER;
    case 0x23:
        return E_MOD_GARGOYLE;
    case 0x2E:
        return E_MOD_TREE;
    case 0x35:
        return E_MOD_LEAPER_SPAWNER;
    case 0x36: // params 0 Thornweed, 1 Corpseweed
        return E_THORNWEED;
    case 0x3B:
        return E_MOD_MANEATER;
    case 0x3D:
        return E_MOD_GARDENER;
    case 0x40:
        return E_MOD_SKBREEDER;
    case 0x44:
        return E_MOD_BOSS_CONTROLLER;
    default:
        return E_DUMMY_0F;
    }
}
#endif

// insertion sort: stable, so equal positions keep the Saturn order
static void SaturnGdnSortLayout(LayoutEntity* list, s32 count, bool byY) {
    LayoutEntity item;
    s32 i, j;

    for (i = 1; i < count; i++) {
        item = list[i];
        for (j = i; j > 0 && (byY ? list[j - 1].posY > item.posY
                                  : list[j - 1].posX > item.posX);
             j--) {
            list[j] = list[j - 1];
        }
        list[j] = item;
    }
}

// create_entity.h wants each layout twice, sorted by x and by y, between a
// 0xFFFE head and a 0xFFFF tail; the converter writes x order alone
static void SaturnGdnBuildLayouts(void) {
    const LayoutEntity head = {0xFFFE, 0xFFFE, 0, 0, 0};
    const LayoutEntity tail = {0xFFFF, 0xFFFF, 0, 0, 0};
    s32 saveLayout =
        s_SaturnStageRooms[SATURN_STAGE_SAVE_ROOM].load.objLayoutId;
    LayoutEntity* src;
    LayoutEntity* x;
    s32 count;
    s32 id;
    s32 i;

    for (id = 0; id < GDN_LAYOUTS; id++) {
        src = s_SaturnStageObjLayouts[id];
        x = s_GdnLayoutX[id];
        count = 0;
        for (i = 0; src && i < s_SaturnStageObjLayoutCounts[id] &&
                    src[i].posX != 0xFFFF && count < GDN_LAYOUT_MAX - 2;
             i++) {
            x[1 + count] = src[i];
            x[1 + count].entityId =
                (src[i].entityId & ~0x3FF) |
                SaturnGdnEntityId(src[i].entityId & 0x3FF, id == saveLayout);
            count++;
        }
#ifdef SATURN_STAGE_PRISON
        // the test spawn point "prison-breeder": a Skeleton Breeder on room
        // 0's fourth floor (y 545), in free slots 0x5C-0x5E (it uses self + 1
        // and + 2); the Saturn layouts place none
        if (id == 1 && count < GDN_LAYOUT_MAX - 2 && SpawnPoint_Name() &&
            !strcmp(SpawnPoint_Name(), "prison-breeder")) {
            x[1 + count++] = (LayoutEntity){
                200, 545 - 32, SaturnGdnEntityId(0x24, false), 0x5C5C, 0};
        }
#endif
        SaturnGdnSortLayout(&x[1], count, false);
        x[0] = head;
        x[1 + count] = tail;
        memcpy(s_GdnLayoutY[id], x, (count + 2) * sizeof(LayoutEntity));
        SaturnGdnSortLayout(&s_GdnLayoutY[id][1], count, true);
        entityLayoutHorizontal[id] = x;
        entityLayoutVertical[id] = s_GdnLayoutY[id];
    }
}

static void SaturnGdnStart(void) {
    TileDefinition* tileDef = &s_SaturnStageTileDef;
    s32 room;
    s32 i;

    SaturnGdnBuildLayouts();
    PfnEntityUpdates = EntityUpdates;
#define X(n, bank) s_SpriteBanks[n] = (SpriteParts*)(bank);
    GDN_SPRITE_BANKS
#undef X
    // the Saturn-only enemy records 400-425 (GAME.PRG, gdn_entities.py)
    memcpy(&g_api.enemyDefs[GDN_ENEMY_FIRST], s_GdnEnemyDefs,
           sizeof(s_GdnEnemyDefs));
    // each ordinary room loads its sprite pages (gfxBanks[room]); the pause
    // menu reloads the same bank (D_80097904)
    for (i = 0; i < LEN(s_SaturnStageRooms); i++) {
        for (room = 0; room < GDN_ROOM_GFX_COUNT; room++) {
            if (s_SaturnStageRooms[i].left == s_SaturnStageRoomBounds[room][0] &&
                s_SaturnStageRooms[i].top == s_SaturnStageRoomBounds[room][1] &&
                s_SaturnStageRooms[i].right ==
                    s_SaturnStageRoomBounds[room][2] &&
                s_SaturnStageRooms[i].bottom ==
                    s_SaturnStageRoomBounds[room][3] &&
                s_SaturnStageRooms[i].load.tilesetId != 0xFF) {
                s_SaturnStageRooms[i].load.objGfxId = room + 1;
            }
        }
    }
    // the boss room's door: a solid tile (its art is not drawn: the native
    // renderer draws the Saturn planes)
    for (i = 1; i < 0x1000; i++) {
        if (tileDef->collision[i] == 3) {
            g_GdnSolidTile = i;
            break;
        }
    }
}

// [ent] log: the live stage entities and the enemies (slot:id
// hp/step@screen x,y), the player's screen x,y, the breakables (id 1), and in
// the Prison the pillar and its switch too, once a second and when an entity
// comes or goes or an hp or a step changes
#ifdef SATURN_STAGE_PRISON
#define SATURN_LOGGED(e)                                                       \
    (SATURN_LOGGED_COMMON(e) || (e)->entityId == E_MOD_PILLAR ||               \
     (e)->entityId == E_MOD_PILLAR_SWITCH)
#else
#define SATURN_LOGGED(e) SATURN_LOGGED_COMMON(e)
#endif
// the enemies, the breakables and the drops (their params: the item)
#define SATURN_LOGGED_COMMON(e)                                                \
    ((e)->enemyId || (e)->entityId == E_BREAKABLE ||                           \
     (e)->entityId == E_PRIZE_DROP || (e)->entityId == E_EQUIP_ITEM_DROP)
static void SaturnGdnUpdate(void) {
    static u32 s_LastSum;
    static s32 s_Frame;
    char line[1024];
    size_t len;
    Entity* e;
    s32 count = 0;
    u32 sum = 0;

    Update();
    len = snprintf(line, sizeof(line), "[ent] room=%d php=%d exp=%d pl=%d,%d",
                   SaturnStageCurrentRoom(), g_Status.hp, g_Status.exp,
                   PLAYER.posX.i.hi, PLAYER.posY.i.hi);
    for (e = &g_Entities[STAGE_ENTITY_START];
         e < &g_Entities[TOTAL_ENTITY_COUNT]; e++) {
        if (!e->entityId || !e->pfnUpdate) {
            continue;
        }
        count++;
        if (SATURN_LOGGED(e) && len < sizeof(line) - 48) {
            len += snprintf(line + len, sizeof(line) - len, " %d:%d/%d/%d@%d,%d",
                            (s32)(e - g_Entities), e->entityId, e->hitPoints,
                            e->step, e->posX.i.hi, e->posY.i.hi);
            if (e->entityId == E_PRIZE_DROP ||
                e->entityId == E_EQUIP_ITEM_DROP) {
                len += snprintf(line + len, sizeof(line) - len, "#%X",
                                e->params);
            }
            sum = sum * 31 + (e - g_Entities) * 7 + e->hitPoints * 131 +
                  e->step + e->params;
        }
    }
    sum += g_Status.hp * 17 + g_Status.exp * 3;
    if (++s_Frame >= 60 || sum != s_LastSum) {
        fprintf(stderr, "%s n=%d\n", line, count);
        s_Frame = 0;
        s_LastSum = sum;
    }
}

static void SaturnStageGdnInitRoomEntities(s32 layoutId) {
    SaturnStageSetCurrentLayout(layoutId);
    if (layoutId < 0 || layoutId >= GDN_LAYOUTS) {
        layoutId = 0;
    }
    InitRoomEntities(layoutId);
    SaturnStageInitBackdrop();
}
#else
static void SaturnStageSyncRoomEntities(void) {
    LayoutEntity* layout;
    Entity* entity;
    size_t layoutIndex;
    s32 entityId;
    s32 index;
    s32 visible;

    if (s_CurrentLayout == NULL) {
        return;
    }
    for (layoutIndex = 0; layoutIndex < s_CurrentLayoutCount; layoutIndex++) {
        layout = &s_CurrentLayout[layoutIndex];
        index = STAGE_ENTITY_START + (layout->entityRoomIndex & 0xFF);
        if (index < STAGE_ENTITY_START || index >= TOTAL_ENTITY_COUNT) {
            continue;
        }
        entityId = layout->entityId & 0x3FF;
        visible = layout->posX >= g_Tilemap.scrollX.i.hi - 64 - WIDE_X &&
                  layout->posX <= g_Tilemap.scrollX.i.hi + 320 + WIDE_X &&
                  layout->posY >= g_Tilemap.scrollY.i.hi - 64 - WIDE_Y &&
                  layout->posY <= g_Tilemap.scrollY.i.hi + 288 + WIDE_Y;
        entity = &g_Entities[index];
        if (!visible) {
            if (entity->pfnUpdate == EntitySaturnStageDummy ||
                entity->pfnUpdate == EntitySaturnStageMasonry ||
                entity->pfnUpdate == EntityRedDoor ||
                entity->pfnUpdate == EntitySaturnStageSpecialController) {
                DestroyEntity(entity);
            }
            continue;
        }
        if (entity->entityId != entityId ||
            (entity->pfnUpdate != EntitySaturnStageDummy &&
             entity->pfnUpdate != EntitySaturnStageMasonry &&
             entity->pfnUpdate != EntityRedDoor &&
             entity->pfnUpdate != EntitySaturnStageSpecialController)) {
            DestroyEntity(entity);
            entity->entityId = entityId;
            entity->params = layout->params;
            entity->entityRoomIndex = layout->entityRoomIndex >> 8;
            if (entityId == 5) {
                entity->pfnUpdate = EntityRedDoor;
            } else if (entityId == 15 &&
                       SaturnStageCurrentRoom() == SATURN_STAGE_SAVE_ROOM) {
                entity->pfnUpdate = EntitySaturnStageMasonry;
            } else if (entityId == 16) {
                entity->pfnUpdate = EntitySaturnStageSpecialController;
            } else {
                entity->pfnUpdate = EntitySaturnStageDummy;
            }
        }
        entity->posX.i.hi =
            (s16)layout->posX - g_Tilemap.scrollX.i.hi +
            (entity->pfnUpdate == EntityRedDoor && entity->step != 0
                 ? entity->ext.redDoor.xOffset
                 : 0);
        entity->posY.i.hi =
            (s16)layout->posY - g_Tilemap.scrollY.i.hi +
            (entity->pfnUpdate == EntityRedDoor && entity->step != 0 ? 0x1F
                                                                     : 0);
    }
}

static void SaturnStageUpdate(void) {
    Entity* entity;

    SaturnStageSyncRoomEntities();
    for (entity = &g_Entities[STAGE_ENTITY_START];
         entity < &g_Entities[TOTAL_ENTITY_COUNT]; entity++) {
        if (entity->pfnUpdate == EntitySaturnStageDummy ||
            entity->pfnUpdate == EntitySaturnStageMasonry ||
            entity->pfnUpdate == EntityRedDoor ||
            entity->pfnUpdate == EntitySaturnStageSpecialController ||
            entity->pfnUpdate == EntitySaturnStageBackdrop) {
            entity->pfnUpdate(entity);
        }
    }
}

static void SaturnStageInitRoomEntities(s32 layoutId) {
    Entity* entity;
    Entity* backdrop;

    for (entity = &g_Entities[STAGE_ENTITY_START];
         entity < &g_Entities[TOTAL_ENTITY_COUNT]; entity++) {
        if (entity->pfnUpdate == EntitySaturnStageDummy ||
            entity->pfnUpdate == EntitySaturnStageMasonry ||
            entity->pfnUpdate == EntityRedDoor ||
            entity->pfnUpdate == EntitySaturnStageSpecialController) {
            DestroyEntity(entity);
        }
    }
    s_CurrentLayout = layoutId >= 0 && layoutId < LEN(s_SaturnStageObjLayouts)
                          ? s_SaturnStageObjLayouts[layoutId]
                          : NULL;
    s_CurrentLayoutCount =
        layoutId >= 0 && layoutId < LEN(s_SaturnStageObjLayoutCounts)
            ? s_SaturnStageObjLayoutCounts[layoutId]
            : 0;
    backdrop = &g_Entities[SATURN_STAGE_BACKDROP_ENTITY];
    if (backdrop->pfnUpdate != EntitySaturnStageBackdrop) {
        DestroyEntity(backdrop);
        backdrop->entityId = 1;
        backdrop->pfnUpdate = EntitySaturnStageBackdrop;
    }
    SaturnStageSyncRoomEntities();
    EntitySaturnStageBackdrop(backdrop);
}
#endif // SATURN_STAGE_GDN
#else
static void SaturnStageUpdate(void) {}
static void SaturnStageInitRoomEntities(s32 layoutId) { (void)layoutId; }
#endif

Overlay g_Overlay = {
#ifdef SATURN_STAGE_GDN
    // the PS1 stage framework (src/st/stage15): layouts, hits, enemies
    .Update = SaturnGdnUpdate,
    .HitDetection = HitDetection,
    .UpdateRoomPosition = UpdateRoomPosition,
    .InitRoomEntities = SaturnStageGdnInitRoomEntities,
    .UpdateStageEntities = UpdateStageEntities,
#else
    .Update = SaturnStageUpdate,
    .HitDetection = Noop,
    .UpdateRoomPosition = Noop,
    .InitRoomEntities = SaturnStageInitRoomEntities,
    .UpdateStageEntities = Noop,
#endif
    .rooms = s_SaturnStageRooms,
    .spriteBanks = s_SpriteBanks,
    .cluts = s_Cluts,
    .objLayoutHorizontal = s_SaturnStageObjLayouts,
    .tileLayers = s_SaturnStageTileLayers,
    .gfxBanks = s_GfxBanks,
};

void Psyz_ModuleStart(void* param) {
    Overlay* o = param;

    s_CurrentLayout = NULL;
    s_CurrentLayoutCount = 0;
    s_TileLayers = g_Overlay.tileLayers;
#ifndef SATURN_STAGE_ROOM_TEST
    if (!PcLoadStageChr(SATURN_STAGE_ASSET_DIR "/F_STAGE.BIN")) {
        ERRORF("could not load Stage 15 graphics");
    }
    SaturnStageConfigureMusic();
    // the engine starts it as stage music: at the stage start and when
    // the player leaves a save room (5087C.c), as on the PS1
    g_StageMusicId = SATURN_STAGE_MUSIC;
    // 224 art rows in each 256-row screen: 16 empty rows at the room top
    // and bottom (native_room*_layer*.map.bin)
    g_WideBandTop = g_WideBandBottom = 16;
    s_SaturnStageMasonryReady = SaturnStageLoadMasonry();
    if (s_SaturnStageMasonryReady) {
        s_SaturnStageMasonryTpage = LoadTPage(
            (u_long*)s_SaturnStageMasonryPixels, 0, 0, 0x300, 0, 24, 16);
        g_ClutIds[SATURN_STAGE_MASONRY_CLUT_SLOT] =
            LoadClut2((u_long*)s_SaturnStageMasonryPalette, 0x3F0, 0xF3);
    }
#ifdef SATURN_STAGE_GDN
    SaturnGdnStart();
#endif
#endif

    memcpy(o, &g_Overlay, sizeof(Overlay));
#ifdef SATURN_STAGE_NATIVE_8X8
    s_NativeRoom = -1;
    s_NativeReady = false;
    s_GameOverDisk = false;
    g_PcStageLayerRenderer = SaturnStageRenderNativeLayers;
    g_PcStageNativeLayer = SaturnStageNativePlane;
#endif
}

void Psyz_ModuleStop(void) {
#ifndef SATURN_STAGE_ROOM_TEST
    Psyz_CdSetDiskPath(g_GameParams.diskPath);
#endif
}
