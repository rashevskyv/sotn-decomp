// SPDX-License-Identifier: AGPL-3.0-or-later
#include <psyz/module.h>
#include <game.h>
#include <cutscene.h>
#include <string.h>
#include "../stages/overlay.h"
#include "../pc.h"
#include "../boss/rbo6/rbo6.h"

// overlay bss, still assembly on PSX
s32 g_SkipCutscene;
Dialogue g_Dialogue;
u32 g_CutsceneFlags;
s32 g_IsCutsceneDone;

// PSX 0x80181720..0x80181D32: both cutscene scripts and their events, kept
// contiguous because the scripts address the events by PSX address
u8 cutscene_script[] = {
#include "../../boss/rbo6/gen/cutscene_script_father.h"
#include "../../boss/rbo6/gen/cutscene_script_mother.h"
#include "../../boss/rbo6/gen/cutscene_events_father.h"
#include "../../boss/rbo6/gen/cutscene_events_mother.h"
};
_Static_assert(sizeof(cutscene_script) == 0x80181D32 - 0x80181720,
               "rbo6 cutscene data size");
// cutscene_1 = father script (0x80181720), cutscene_3 = mother (0x801819E7)
__asm__(".globl cutscene_1\n.set cutscene_1, cutscene_script\n"
        ".globl cutscene_3\n.set cutscene_3, cutscene_script + 0x2C7\n");

extern u8 gfx_portrait_alucard[];
extern u8 gfx_portrait_dracula[];

static void InitCutscenePc(void) {
    static const CutsceneSymbolRange symbols[] = {
        {cutscene_script, 0x80181720, 0x80181D32 - 0x80181720},
        {gfx_portrait_alucard, 0x8019113C, 48 * 72},
        {gfx_portrait_dracula, 0x80191EBC, 48 * 72},
    };
    CutscenePcAlloc(symbols, LEN(symbols));
}

extern Overlay g_BossOverlay;
extern PfnEntityUpdate EntityUpdates[];
extern LayoutEntity* entityLayoutHorizontal[];
extern LayoutEntity* entityLayoutVertical[];
extern PfnEntityUpdate* PfnEntityUpdates;
extern LayoutEntity** g_pStObjLayoutHorizontal;
extern LayoutEntity** g_pStObjLayoutVertical;

// PC copy of the raw PSX data at 0x80181484, used by unk_31738.c (from
// artesanalintelligence/sotn-vita 4f0a257, src/boss/rbo6/d_pc_data.c)
s32 D_us_80181484 = 0; // show the crystals of the 3D background
s32 D_us_80181488 = 0; // stronger debris (set by the collapse)

// crystal rotation speed (x, y, z) and starting angle
s16 D_us_8018148C[][4] = {
    {0x0100, 0x0200, 0x0300, -0x0040},
    {0x0000, 0x0000, 0x0000, -0x1200},
    {0x0000, 0x0000, 0x0400, -0x4600},
    {0x0200, 0x0100, -0x200, -0x6900},
};
static SVECTOR D_us_801814AC = {0x0066, -0x36, -0x21, 0};
static SVECTOR D_us_801814B4 = {0x003F, 0x0036, -0x57, 0};
static SVECTOR D_us_801814BC = {0x0000, -0x36, -0x6C, 0};
static SVECTOR D_us_801814C4 = {0x0000, -0x78, 0x0000, 0};
static SVECTOR D_us_801814CC = {0x003F, -0x36, 0x0057, 0};
static SVECTOR D_us_801814D4 = {0x0066, 0x0036, 0x0021, 0};
static SVECTOR D_us_801814DC = {0x0000, 0x0078, 0x0000, 0};
static SVECTOR D_us_801814E4 = {0x0000, 0x0036, 0x006C, 0};
static SVECTOR D_us_801814EC = {-0x66, -0x36, -0x21, 0};
static SVECTOR D_us_801814F4 = {-0x3F, 0x0036, -0x57, 0};
static SVECTOR D_us_801814FC = {-0x3F, -0x36, 0x0057, 0};
static SVECTOR D_us_80181504 = {-0x66, 0x0036, 0x0021, 0};
// triangles of the 3D background
SVECTOR* D_us_8018150C[] = {
    &D_us_801814AC, &D_us_801814BC, &D_us_801814C4, &D_us_801814EC,
    &D_us_801814C4, &D_us_801814BC, &D_us_801814EC, &D_us_801814BC,
    &D_us_801814F4, &D_us_801814EC, &D_us_801814FC, &D_us_801814C4,
    &D_us_801814AC, &D_us_801814C4, &D_us_801814CC, &D_us_801814AC,
    &D_us_801814B4, &D_us_801814BC, &D_us_801814CC, &D_us_801814D4,
    &D_us_801814AC, &D_us_801814FC, &D_us_801814EC, &D_us_80181504,
    &D_us_801814B4, &D_us_801814AC, &D_us_801814D4, &D_us_801814C4,
    &D_us_801814FC, &D_us_801814CC, &D_us_801814BC, &D_us_801814B4,
    &D_us_801814F4, &D_us_801814F4, &D_us_80181504, &D_us_801814EC,
    &D_us_80181504, &D_us_801814F4, &D_us_801814DC, &D_us_801814D4,
    &D_us_801814CC, &D_us_801814E4, &D_us_80181504, &D_us_801814E4,
    &D_us_801814FC, &D_us_801814D4, &D_us_801814DC, &D_us_801814B4,
    &D_us_801814E4, &D_us_801814CC, &D_us_801814FC, &D_us_801814DC,
    &D_us_801814F4, &D_us_801814B4, &D_us_801814D4, &D_us_801814E4,
    &D_us_801814DC, &D_us_80181504, &D_us_801814DC, &D_us_801814E4,
    &D_us_801814FC, &D_us_801814E4, &D_us_801814E4, &D_us_801814B4,
    &D_us_801814DC, &D_us_801814DC, &D_us_801814DC, &D_us_801814E4,
    &D_us_801814E4, &D_us_801814E4, &D_us_801814DC, &D_us_801814DC,
};
// debris spawn position x, y and direction
s16 D_us_8018162C[][3] = {
    {0x0040, 0x0009, 0x0800},
    {0x000C, 0x0064, 0x02AB},
    {0x0074, 0x0064, -0x2AB},
};
u8 D_us_80181648[] = {
    0x02, 0x01, 0x02, 0x02, 0x02, 0x03, 0x02, 0x04, 0x02, 0x05,
    0x02, 0x06, 0x02, 0x07, 0x02, 0x08, 0x02, 0x09, 0x02, 0x0A,
    0x02, 0x0B, 0x02, 0x0C, 0x02, 0x0D, 0xFF, 0x00};

void Psyz_ModuleStart(void* param) {
    Overlay* o = param;

    memcpy(o, &g_BossOverlay, sizeof(Overlay));
    PfnEntityUpdates = EntityUpdates;
    g_pStObjLayoutHorizontal = entityLayoutHorizontal;
    g_pStObjLayoutVertical = entityLayoutVertical;
    InitCutscenePc();
}

void Psyz_ModuleStop(void) {}
