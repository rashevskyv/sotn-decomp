// SPDX-License-Identifier: AGPL-3.0-or-later
// BO6 (Richter) module glue. Based on artesanalintelligence/sotn-vita 4f0a257
// (src/pc/bosses/boss_bo6.c, src/boss/bo6/bss_pc.c, src/boss/bo6/tiledef.c,
// src/boss/bo6/background_block_init.c).
#include <psyz/module.h>
#include <game.h>
#include <cutscene.h>
#include <string.h>
#include "../stages/overlay.h"
#include "../pc.h"
#include "../boss/bo6/bo6.h"

extern AbbreviatedOverlay2 g_BossOverlay;
extern PfnEntityUpdate EntityUpdates[];
extern LayoutEntity* entityLayoutHorizontal[];
extern LayoutEntity* entityLayoutVertical[];
extern PfnEntityUpdate* PfnEntityUpdates;
extern LayoutEntity** g_pStObjLayoutHorizontal;
extern LayoutEntity** g_pStObjLayoutVertical;

// bss that holds pointers, so it can not live in the PSX-sized bss blob
s32 g_SkipCutscene;
Dialogue g_Dialogue;
u32 g_CutsceneFlags;
PlayerState g_Ric;
s32 g_IsCutsceneDone;

// tile definition, still asm on PSX
#include "../../boss/bo6/gen/bo6_tiledef_bo6.h"

// background block init, still asm on PSX (same as BO4's)
static u8 AnimFrames_801804D8[] = {0x10, 0x0B, 0x00, 0x00};
static u8 AnimFrames_801804DC[] = {
    0x08, 0x0C, 0x08, 0x0D, 0x08, 0x0E, 0x08, 0x0F, 0x08, 0x10, 0x00, 0x00,
};
ObjInit2 BackgroundBlockInit[] = {
    {
        .animSet = ANIMSET_OVL(1),
        .zPriority = 0x69,
        .facingLeft = 0,
        .unk5A = 0,
        .palette = 0,
        .drawFlags = ENTITY_DEFAULT,
        .blendMode = BLEND_NO,
        .flags = 0,
        .animFrames = AnimFrames_801804D8,
    },
    {
        .animSet = ANIMSET_OVL(1),
        .zPriority = 0x69,
        .facingLeft = 0,
        .unk5A = 0,
        .palette = 0,
        .drawFlags = ENTITY_DEFAULT,
        .blendMode = BLEND_TRANSP | BLEND_ADD,
        .flags = 0,
        .animFrames = AnimFrames_801804DC,
    }};

// contiguous copies of the PSX data (generated d_pc_data.c, d_pc_sprites.c)
extern u8 bo6_pc_data_blob[];
extern u8 bo6_pc_sprites_blob[];

static void InitCutscenePc(void) {
    // cutscene scripts, their events and the portraits are in these blocks
    static const CutsceneSymbolRange symbols[] = {
        {bo6_pc_data_blob, 0x80180614, 0x80182F70 - 0x80180614},
        {bo6_pc_sprites_blob, 0x8018303C, 0x801A6320 - 0x8018303C},
    };
    CutscenePcAlloc(symbols, LEN(symbols));
}

void Psyz_ModuleStart(void* param) {
    Overlay* o = param;

    memcpy(o, &g_BossOverlay, sizeof(AbbreviatedOverlay2));
    PfnEntityUpdates = EntityUpdates;
    g_pStObjLayoutHorizontal = entityLayoutHorizontal;
    g_pStObjLayoutVertical = entityLayoutVertical;
    InitCutscenePc();
}

void Psyz_ModuleStop(void) {}
