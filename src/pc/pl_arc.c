// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include <psyz/log.h>
#include "pc.h"
#include <stdlib.h>
#include <string.h>

static const int BaseAddr = 0x8013C000;
u8* g_PlOvlSpritesheet[256];
u8** g_PlOvlAluBatSpritesheet[1];
static u8* g_batSpritesheetData[256];
static u8* sprite_data = NULL;

// Reads a table of PSX pointers starting at byte offset `tableOffset` and
// rebases each one into `sprite_data`, stopping at the first invalid entry.
static void ParseSpritesheetTable(
    size_t tableOffset, u32 fileLength, u8** dst, int dstLen) {
    u32* ptr = (u32*)(sprite_data + tableOffset);
    for (int i = 0; i < dstLen; i++) {
        u32 psxPtr = *ptr++;
        if (psxPtr < BaseAddr) { // validate PTR correctness
            break;
        }
        size_t dataPos = psxPtr - BaseAddr;
        if (dataPos >= fileLength) { // validate data correctness
            break;
        }
        dst[i] = sprite_data + dataPos;
    }
}

// The cloak colour preview (func_800F82F4) loads the two frames that the
// pointers at 0x8013C200 and 0x8013C270 name: entries 120 and 148 of the
// sprite table at 0x8013C020. The stub (stubs.c) stays when a pointer or
// its frame is not in the file.
extern ImgSrc g_imgUnk8013C200_impl;
extern ImgSrc* g_imgUnk8013C200;
extern ImgSrc* g_imgUnk8013C270;

static ImgSrc* PreviewFrame(u32 tableOffset, u32 fileLength) {
    u32 pos;
    ImgSrc* img;
    if (fileLength < tableOffset + 4) {
        return &g_imgUnk8013C200_impl;
    }
    pos = *(u32*)(sprite_data + tableOffset) - BaseAddr;
    if (pos > fileLength - 4) {
        return &g_imgUnk8013C200_impl;
    }
    img = (ImgSrc*)(sprite_data + pos);
    if (img->width / 2 * img->height > fileLength - 4 - pos) {
        return &g_imgUnk8013C200_impl;
    }
    return img;
}

void InitPlayerArc(const struct FileUseContent* file) {
    if (sprite_data) {
        free(sprite_data);
    }
    // this is not freed anywhere else, technically a memory leak but it's not
    DEBUGF("allocating %d bytes for ARC spritesheet", file->length);
    sprite_data = malloc(file->length);
    memcpy(sprite_data, file->content, file->length);

    // clean-up previously allocated data, only useful for memory sanity checks
    memset(g_PlOvlSpritesheet, 0, sizeof(g_PlOvlSpritesheet));
    memset(g_batSpritesheetData, 0, sizeof(g_batSpritesheetData));

    u32* header = (u32*)sprite_data;
    u32 batTablePtr = header[1];
    if (batTablePtr >= BaseAddr && batTablePtr - BaseAddr < file->length) {
        ParseSpritesheetTable(batTablePtr - BaseAddr, file->length,
                              g_batSpritesheetData, LEN(g_batSpritesheetData));
    }
    g_PlOvlAluBatSpritesheet[0] = g_batSpritesheetData;

    ParseSpritesheetTable(8 * sizeof(u32), file->length, g_PlOvlSpritesheet,
                          LEN(g_PlOvlSpritesheet));
    g_imgUnk8013C200 = PreviewFrame(0x200, file->length);
    g_imgUnk8013C270 = PreviewFrame(0x270, file->length);
}
