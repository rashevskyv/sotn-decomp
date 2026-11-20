// SPDX-License-Identifier: AGPL-3.0-or-later
#include "st0.h"
#include "disk.h"

#if TARGET == TARGET_PSP
#define GET_CLUT(x, y) getClut(x, y)
#else
#define GET_CLUT(x, y) GetClut(x, y)
#endif

static ProloguePrimitive D_80181568[] = {
    {0x01, 0x80, 0xFF, 0x80, 1, 112, 0x9C},
    {0x00, 0x80, 0xFF, 0x80, 256, 112, 0x9E},
    {0x01, 0x00, 0xFF, 0x80, 1, -16, 0x9C},
    {0x00, 0x00, 0xFF, 0x80, 256, -16, 0x9E},
    {0x01, 0x80, 0xFF, 0x80, 1, -144, 0x98},
    {0x00, 0x80, 0xFF, 0x80, 256, -144, 0x9A},
    {0x01, 0x00, 0xFF, 0x80, 1, -272, 0x98},
    {0x00, 0x00, 0xFF, 0x80, 256, -272, 0x9A},
    {0x00, 0x00, 0x88, 0x70, 112, -384, 0x8C},
#if TARGET == TARGET_PSP
    {0x00, 0x71, 0xA0, 0x8F, 248, -415, 0x8C},
#else
    {0x00, 0x70, 0xA0, 0x90, 248, -416, 0x8C},
#endif
};

// bss
u16 D_801BEDFC;
STATIC_PAD_BSS(2);
u16 D_801BEE00;
STATIC_PAD_BSS(2);
u16 D_801BEE04;
STATIC_PAD_BSS(2);
s16 D_801BEE08;
STATIC_PAD_BSS(2);
s16 D_801BEE0C;
STATIC_PAD_BSS(2);

#ifdef PLATFORM_PSYZ
#include <psyz/video.h>
// The wide view shows the whole display around the 512 frame (src/pc/wide.c).
// HD side pieces continue the castle column: loose PNG files keyed on one
// 16-bit VRAM column of the stock picture (1x256 at the column's edge), so
// the stock game, or a pack without them, draws nothing there. Above the
// sky, slow clouds keyed on a column of the moon picture. The fade tiles
// grow over all of it (Wide_Tile).
#define WIDE_PIECES 3
#define WIDE_CLOUDS 5
#define CLOUD_W 512   // frame pixels of one cloud picture, it repeats
#define CLOUD_Y 8     // its top row and height
#define CLOUD_H 176
#define CLOUD_STEP 6  // frames for one pixel of drift
static s16 wide_prims = -1;
static u16 wide_clock;

static void WideQuad(Primitive* p, s32 x, s32 y, s32 w, s32 h, s32 vram_x,
                     s32 vram_y, s32 rows) {
    s32 u = vram_x & 0x3F;
    p->tpage = 0x100 | ((vram_y >> 8) << 4) | (vram_x >> 6); // 16-bit texels
    p->x0 = p->x2 = x;
    p->x1 = p->x3 = x + w;
    p->y0 = p->y1 = y;
    p->y2 = p->y3 = y + h;
    p->u0 = p->u2 = u;
    p->u1 = p->u3 = u + 1;
    p->v0 = p->v1 = vram_y & 0xFF;
    p->v2 = p->v3 = (vram_y & 0xFF) + rows - 1;
    p->drawMode = DRAW_DEFAULT;
}

static void PrologueWide(void) {
    // piece: VRAM column of the stock rect, the column prims it sits next to
    // (top half, bottom half), and its side
    static const s16 piece[WIDE_PIECES][4] = {
        {768, 4, 2, 0},  // bridge, left
        {767, 9, 7, 1},  // cliff, right
        {1023, 5, 3, 1}, // bridge, right
    };
    PsyzRect a = Psyz_VideoGetDrawArea();
    Primitive* col[16];
    Primitive* p;
    s32 i, n, x, tw, th;

    if (g_GameStep == 0) { // not made yet
        wide_prims = -1;
    }
    if (wide_prims < 0) {
        return;
    }
    p = &g_PrimBuf[D_801BEE08];
    for (n = 0; p && n < 16; p = p->next) {
        col[n++] = p;
    }
    p = &g_PrimBuf[wide_prims];
    for (i = 0; i < WIDE_PIECES; i++, p = p->next) {
        const s16* k = piece[i];
        p->drawMode = DRAW_HIDE;
        if (n < 12 || ((col[k[1]]->drawMode & DRAW_HIDE) &&
                       (col[k[2]]->drawMode & DRAW_HIDE)) ||
            !Psyz_HdTexSize(k[0], 256, 1, 256, 16, &tw, &th)) {
            continue;
        }
        // the stock column covers x 1..510: the pieces meet it there
        x = k[3] ? 511 : 1 - 432;
        WideQuad(p, x, col[k[1]]->y0, 432, 256, k[0], 256, 256);
        p->priority = col[k[1]]->priority;
    }
    // clouds: behind the column (priority 0, the moon's), while the moon shows
    wide_clock++;
    x = a.sx - (s32)(wide_clock / CLOUD_STEP) % CLOUD_W;
    n = n >= 12 && !(col[0]->drawMode & DRAW_HIDE) &&
        Psyz_HdTexSize(848, 0, 1, CLOUD_H, 16, &tw, &th);
    for (i = 0; i < WIDE_CLOUDS; i++, p = p->next, x += CLOUD_W) {
        p->drawMode = DRAW_HIDE;
        if (n) { // 5 x 512 covers the widest canvas (1364 at 32:9)
            WideQuad(p, x, CLOUD_Y, CLOUD_W, CLOUD_H, 848, 0, CLOUD_H);
            p->priority = 0;
        }
    }
}
#endif

static void SetGameState(GameState gameState) {
    g_GameState = gameState;
    g_GameStep = 0;
    g_cameraOffsetX = 0;
    g_cameraOffsetY = 0;
}

static void InitClutIndices(void) {
    s32 clutId;
    s32 i;
    s32 j;

    clutId = 0;
    for (i = 0xF0; i < 0x100; i++) {
        for (j = 0x200; j < 0x300; j += 0x10) {
            g_ClutIds[clutId] = GET_CLUT(j, i);
            clutId++;
        }
    }
    for (i = 0xF0; i < 0x100; i++) {
        for (j = 0; j < 0x100; j += 0x10) {
            g_ClutIds[clutId] = GET_CLUT(j, i);
            clutId++;
        }
    }
    for (i = 0xF0; i < 0x100; i++) {
        for (j = 0x100; j < 0x200; j += 0x10) {
            g_ClutIds[clutId] = GET_CLUT(j, i);
            clutId++;
        }
    }
}

static void ClearScreen(void) {
    RECT rect;

    rect.x = 0;
    rect.y = 0;
    rect.w = 512;
    rect.h = 512;
    ClearImage(&rect, 0, 0, 0);
}

static void SetDisplayBufferColorsToBlack(void) {
    g_GpuBuffers[0].draw.r0 = 0;
    g_GpuBuffers[0].draw.g0 = 0;
    g_GpuBuffers[0].draw.b0 = 0;
    g_GpuBuffers[1].draw.r0 = 0;
    g_GpuBuffers[1].draw.g0 = 0;
    g_GpuBuffers[1].draw.b0 = 0;
}

static void SetStageCinematicBorders(s32 bufferId) {
    g_GpuBuffers[0].draw.clip.y = 0x0014;
    g_GpuBuffers[0].draw.clip.h = 0x00CF;
    if (!bufferId) {
        g_GpuBuffers[1].draw.clip.y = 0x0014;
    } else {
        g_GpuBuffers[1].draw.clip.y = 0x0014 + DISP_STAGE_NEXT_X;
    }
    g_GpuBuffers[1].draw.clip.h = 0x00CF;
    g_GpuBuffers[0].draw.isbg = g_GpuBuffers[1].draw.isbg = 1;
    SetDisplayBufferColorsToBlack();
    g_GpuBuffers[0].disp.isrgb24 = g_GpuBuffers[1].disp.isrgb24 = 0;
}

static void SetStageDisplayBuffer(void) {
    SetDefDrawEnv(&g_GpuBuffers[0].draw, 0, 0, DISP_STAGE_W, DISP_STAGE_H);
    SetDefDrawEnv(&g_GpuBuffers[1].draw, DISP_STAGE_NEXT_X, 0, DISP_STAGE_W,
                  DISP_STAGE_H);
    SetDefDispEnv(&g_GpuBuffers[0].disp, DISP_STAGE_NEXT_X, 0, DISP_STAGE_W,
                  DISP_STAGE_H);
    SetDefDispEnv(&g_GpuBuffers[1].disp, 0, 0, DISP_STAGE_W, DISP_STAGE_H);
    SetStageCinematicBorders(0);
}

static void SetTitleDisplayBuffer(void) {
    SetDefDrawEnv(&g_GpuBuffers[0].draw, 0, 0, DISP_TITLE_W, DISP_TITLE_H);
    SetDefDrawEnv(&g_GpuBuffers[1].draw, 0, DISP_STAGE_NEXT_X, DISP_TITLE_W,
                  DISP_TITLE_H);
    SetDefDispEnv(&g_GpuBuffers[0].disp, 0, DISP_STAGE_NEXT_X, DISP_TITLE_W,
                  DISP_TITLE_H);
    SetDefDispEnv(&g_GpuBuffers[1].disp, 0, 0, DISP_TITLE_W, DISP_TITLE_H);
    g_GpuBuffers[0].draw.clip.y = 0;
#if TARGET == TARGET_PSP
    g_GpuBuffers[1].draw.clip.y = 0;
#else
    g_GpuBuffers[1].draw.clip.y = DISP_TITLE_W / 2;
#endif
    g_GpuBuffers[0].draw.clip.h = g_GpuBuffers[1].draw.clip.h = DISP_TITLE_H;
    g_GpuBuffers[0].draw.isbg = g_GpuBuffers[1].draw.isbg = 1;
    SetDisplayBufferColorsToBlack();
    g_GpuBuffers[0].disp.isrgb24 = g_GpuBuffers[1].disp.isrgb24 = 0;
}

static s32 IsSkipRequested(void) {
    if (g_pads[0].tapped & PAD_START &&
        ((g_Settings.D_8003CB04 & 2) || g_GameClearFlag)) {
        return 1;
    }
    return 0;
}

// Post-Dracula fight prologue scroll. This is responsible for scrolling the
// prologue text and displaying the Castlevania logo.
void PrologueScroll(void) {
    Primitive* prim;
    s32 i;
    ProloguePrimitive* textPrim;
    s32 skip;
    Entity* entity;
    s32 primIndex;

#if TARGET == TARGET_PSP
    const int EndTimer = 0x14C0; // approximately 110 seconds
    entity = &g_Entities[STAGE_ENTITY_START];
    DisableAutoPowerOff();
#else
    const int EndTimer = 0x14B0; // approximately 110 seconds
    entity = &g_Entities[STAGE_ENTITY_START];
#endif
    switch (g_GameStep) {
    case 0:
        if (g_UseDisk) {
            if (g_IsUsingCd) {
                break;
            }
            g_CdStep = CdStep_LoadInit;
            g_LoadFile = CdFile_Prologue;
        }
        D_801BEE08 = g_api.AllocPrimitives(PRIM_SPRT, 0x10);
        prim = &g_PrimBuf[D_801BEE08];
        for (i = 0; i < 2; i++, prim = prim->next) {
            prim->x0 = (i * 0xb0) + 0x50;
            prim->y0 = 0x15;
            if (i) {
                prim->u0 = 0x50;
            } else {
                prim->u0 = 0x20;
            }
            prim->v0 = 0;
            prim->u1 = 0xB0;
            prim->v1 = 0xB0;
            prim->tpage = i + 0x8D;
            prim->clut = 0x210;
            prim->priority = 0;
            prim->drawMode = DRAW_HIDE;
        }

        textPrim = (ProloguePrimitive*)D_80181568;
        for (i = 0; i < 10; i++, prim = prim->next) {
            prim->u0 = textPrim->u0;
            prim->v0 = textPrim->v0;
            prim->u1 = textPrim->u1;
            prim->v1 = textPrim->v1;
            prim->x0 = textPrim->x;
            prim->y0 = textPrim->y;
            prim->tpage = textPrim->tpage;
            prim->clut = 0x200;
            prim->priority = 1;
            prim->drawMode = DRAW_HIDE;
            textPrim++;
        }

        for (i = 0; i < 4; i++, prim = prim->next) {
            prim->type = PRIM_GT4;
            prim->u0 = prim->u2 = 0;
            prim->u1 = prim->u3 = 0x80;
            if (i == 3) {
                prim->u1 = prim->u3 = 0x40;
            }
            prim->v0 = prim->v1 = i * 0x20;
            prim->v2 = prim->v3 = prim->v0 + 0x20;
            prim->x0 = prim->x2 = (i * 0x80) + 0x20;
            prim->x1 = prim->x3 = prim->x0 + 0x80;
            if (i == 3) {
                prim->x1 = prim->x3 = prim->x0 + 0x40;
            }
            prim->y0 = prim->y1 = 0x68;
            prim->y2 = prim->y3 = 0x88;
            prim->r0 = prim->r1 = prim->r2 = prim->r3 = 0;
            prim->g0 = prim->g1 = prim->g2 = prim->g3 = 0;
            prim->b0 = prim->b1 = prim->b2 = prim->b3 = 0;
            prim->tpage = 0xB;
            prim->clut = 0x220;
            prim->priority = 2;
            prim->drawMode = DRAW_HIDE;
        }
        D_801BEE0C = g_api.AllocPrimitives(PRIM_TILE, 3);
        prim = &g_PrimBuf[D_801BEE0C];
        for (i = 0; prim; prim = prim->next, i++) {
            prim->x0 = i * 0xC0;
            prim->y0 = 0;
            prim->u0 = 0xC0;
            if (i == 2) {
                prim->u0 = 0x80;
            }
            prim->v0 = 0xFF;
            prim->r0 = prim->b0 = prim->g0 = 0xFC;
            prim->priority = 4;
            prim->drawMode =
                DRAW_TPAGE2 | DRAW_TPAGE | DRAW_COLORS | DRAW_TRANSP;
        }
#ifdef PLATFORM_PSYZ
        wide_prims = g_api.AllocPrimitives(PRIM_GT4, WIDE_PIECES + WIDE_CLOUDS);
        wide_clock = 0;
#endif
        g_GameStep++;
        break;
    case 1:
        break;
    case 2: // 0x801B07DC
        // transition to black
        SetTitleDisplayBuffer();
        prim = &g_PrimBuf[D_801BEE08];
        for (i = 0; i < 12; i++) {
            if (prim->y0 >= -0x10 && prim->y0 < 0xF0) {
                prim->drawMode = DRAW_DEFAULT;
            }
            prim = prim->next;
        }
        DestroyEntity(entity);
        D_801BEDFC = D_801BEE00 = D_801BEE04 = 0;
        g_GpuBuffers[0].draw.r0 = 0;
        g_GpuBuffers[0].draw.g0 = 0;
        g_GpuBuffers[0].draw.b0 = 0x18;
        g_GpuBuffers[1].draw.r0 = 0;
        g_GpuBuffers[1].draw.g0 = 0;
        g_GpuBuffers[1].draw.b0 = 0x18;
#if TARGET == TARGET_PSP
        func_psp_0891CEB8(0x100, 0xF0);
        func_psp_0891CEB8(0x100, 0xF1);
#endif
        g_GameStep++;
        break;
    case 3: // 0x801B08A4
        // moon appears
        prim = &g_PrimBuf[D_801BEE0C];
        for (i = 0; prim; prim = prim->next, i++) {
            prim->r0 = prim->b0 = prim->g0 -= 4;
            if (i == 0 && !prim->r0) {
                g_api.FreePrimitives(D_801BEE0C);
                g_GameStep++;
                break;
            }
        }
        break;
    case 4: // 0x801B091C
        if (!g_api.func_80131F68()) {
            g_api.PlaySfx(MU_MOONLIGHT_NOCTURNE);
            g_GameStep++;
        }
        break;
    case 5: // 0x801B0940
        if (
#if TARGET == TARGET_PSP
            1
#else
            g_api.func_80131F68()
#endif
        ) {
            g_GameStep++;
        }
        break;
    case 6: // 0x801B096C
        // prologue scroll
        func_801B1298(entity);
        D_801BEDFC++;
        D_801BEE04++;
        if (D_801BEDFC == 0xB && D_801BEE00 < 0x1C0) {

            D_801BEDFC = 0;
            D_801BEE00++;
            prim = &g_PrimBuf[D_801BEE08];
            for (i = 0; i < 12; i++) {
                if (i >= 2) {
                    prim->y0++;
                }

                if (prim->y0 < -0x90 || prim->y0 > 0xF0) {
                    prim->drawMode = DRAW_HIDE;
                } else {
                    prim->drawMode = DRAW_DEFAULT;
                }
                prim = prim->next;
            }
        }
        // 110 seconds?
        if (D_801BEE04 > EndTimer) {
            prim = &g_PrimBuf[D_801BEE08];
            for (i = 0; i < 12; i++) {
                prim = prim->next;
            }
            while (prim) {
                prim->drawMode =
                    DRAW_TPAGE | DRAW_TPAGE2 | DRAW_COLORS | DRAW_TRANSP;
                prim = prim->next;
            }
            DestroyEntity(entity);
            D_801BEE00 = 0;
            g_GameStep++;
#if TARGET == TARGET_PSP
            func_pspeu_09246B88();
#endif
        }
        skip = IsSkipRequested();
        if (skip) {
            g_GameStep = Play_16;
        }
        break;
    case 7: // 0x801B0AE8
        // scroll finished, fade in castlevania
        prim = &g_PrimBuf[D_801BEE08];
        i = 0;
        while (i < 12) {
            prim = prim->next;
            i++;
        }
        while (prim) {
            prim->r0 = prim->r1 = prim->r2 = prim->r3++;
            prim->g0 = prim->g1 = prim->g2 = prim->g3++;
            prim->b0 = prim->b1 = prim->b2 = prim->b3++;
            prim = prim->next;
        }
        D_801BEE00++;
        if (D_801BEE00 > 0xC0) {
            D_801BEE00 = 0;
            g_GameStep++;
        }
        skip = IsSkipRequested();
        if (skip) {
            g_GameStep = Play_16;
        }
        break;
    case 8:
        prim = &g_PrimBuf[D_801BEE08];
        for (i = 0; i < 12; i++) {
            prim = prim->next;
        }
        while (prim) {
            prim->r0 = prim->r1 = prim->r2 = prim->r3--;
            prim->g0 = prim->g1 = prim->g2 = prim->g3--;
            prim->b0 = prim->b1 = prim->b2 = prim->b3--;
            prim->drawMode = DRAW_COLORS;
            prim = prim->next;
        }
        D_801BEE00++;
        if (D_801BEE00 > 0x40) {
            D_801BEE00 = 0;
            g_GameStep++;
        }
        skip = IsSkipRequested();
        if (skip) {
            g_GameStep = Play_16;
        }
        break;
    case 9:
        D_801BEE00++;
        if (D_801BEE00 > 0x230) {
            g_GameStep = Play_16;
            break;
        }
        skip = IsSkipRequested();
        if (skip) {
            g_GameStep = Play_16;
        }
        break;
    case 16:
        g_api.PlaySfx(SET_UNK_82);
        primIndex = g_api.AllocPrimitives(PRIM_TILE, 3);
        prim = &g_PrimBuf[D_801BEE08];
        i = 0;
        while (prim) {
            if (!prim->next) {
                prim->next = &g_PrimBuf[primIndex];
                break;
            }
            prim = prim->next;
        }
        prim = &g_PrimBuf[primIndex];
        i = 0;
        while (prim) {
            prim->x0 = i * 0xC0;
            prim->y0 = 0;
            prim->u0 = 0xC0;
            if (i == 2) {
                prim->u0 = 0x80;
            }
            prim->v0 = 0xFF;
            prim->r0 = prim->b0 = prim->g0 = 0;
            prim->priority = 4;
            prim->drawMode =
                DRAW_UNK_40 | DRAW_TPAGE | DRAW_COLORS | DRAW_TRANSP;
            prim = prim->next;
            i++;
        };

        D_801BEE00 = 0;
        g_GameStep++;
        break;
    case 17:
        prim = &g_PrimBuf[D_801BEE08];
        for (i = 0; i < 16; i++) {
            prim = prim->next;
        }
        while (prim) {
            if (prim->r0 < 0xF8) {
                prim->r0 = prim->b0 = prim->g0 += 8;
            }
            prim = prim->next;
        }
        D_801BEE00++;
        if (D_801BEE00 > 0x20) {
            g_GpuBuffers[0].draw.r0 = 0;
            g_GpuBuffers[0].draw.g0 = 0;
            g_GpuBuffers[0].draw.b0 = 0;
            g_GpuBuffers[1].draw.r0 = 0;
            g_GpuBuffers[1].draw.g0 = 0;
            g_GpuBuffers[1].draw.b0 = 0;
            g_GameStep++;
        }
        break;
    case 18:
        g_Settings.D_8003CB04 |= 2;
        InitClutIndices();
        g_api.FreePrimitives(D_801BEE08);
#ifdef PLATFORM_PSYZ
        if (wide_prims >= 0) {
            g_api.FreePrimitives(wide_prims);
            wide_prims = -1;
        }
#endif
        DestroyEntitiesFromIndex(0);
        g_GameStep++;
        break;
    case 19:
        if (!g_api.func_80131F68()) {
            SetStageDisplayBuffer();
            ClearScreen();
            if (g_StageId == STAGE_ST0) {
                g_StageId = STAGE_NO3;
                SetGameState(Game_NowLoading);
                g_GameStep = 2;
            } else {
                SetGameState(Game_Title);
            }
        }
        break;
    }
#ifdef PLATFORM_PSYZ
    PrologueWide();
#endif
}
