// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sel.h"

// BSS
static s32 D_801BB010;
static s32 D_801BB014;
#if REGION == REGION_EU
static u8 D_801BB018[0x60];
u16 g_DialoguePalTimer;
static s32 D_eu_801BB178[0x20];
#else
static u8 D_801BB018[0xE0];
#endif
static s32 D_801804D0 = 0;

// DATA
#if REGION == REGION_JP

#if VERSION != VER_JP10
#define NUM_SELECT_STAGES 33
#else
#define NUM_SELECT_STAGES 16
#endif

static u8 D_801804D4[NUM_SELECT_STAGES] = {
    STAGE_MEMORYCARD, STAGE_ST0,  STAGE_NO3,  STAGE_NP3,  STAGE_NZ0,
    STAGE_DAI,        STAGE_NO0,  STAGE_NO1,  STAGE_LIB,  STAGE_NO4,
    STAGE_NZ1,        STAGE_ARE,  STAGE_CAT,  STAGE_CHI,  STAGE_NO2,
    STAGE_TOP,
#if VERSION != VER_JP10
    STAGE_CEN,        STAGE_RNO3, STAGE_RNZ0, STAGE_RDAI, STAGE_RNO0,
    STAGE_RNO1,       STAGE_RLIB, STAGE_RNO4, STAGE_RNZ1, STAGE_RARE,
    STAGE_RCAT,       STAGE_RCHI, STAGE_RNO2, STAGE_RTOP, STAGE_RCEN,
#endif
};
static char* D_801804D8[NUM_SELECT_STAGES] = {
    "メモリーカード　　　　", "過去（リヒター編）　　",
    "城入り口　　　　　　　", "城に戻る　　　　　　　",
    "錬金研究棟　　　　　　", "礼拝堂　　　　　　　　",
    "大理石の廊下　　　　　", "崖側外壁　　　　　　　",
    "蔵書庫　　　　　　　　", "地下水脈　　　　　　　",
    "時計塔　　　　　　　　", "闘技場　　　　　　　　",
    "カタコンベ　　　　　　", "えん道　　　　　　　　",
    "オルロックの間　　　　", "最上部　　　　　　　　",
#if VERSION != VER_JP10
    "中央部　　　　　　　　", "逆　城入り口　　　　　",
    "逆　錬金研究棟　　　　", "逆　礼拝堂　　　　　　",
    "逆　大理石の廊下　　　", "逆　崖側外壁　　　　　",
    "逆　蔵書庫　　　　　　", "逆　地下水脈　　　　　",
    "逆　時計塔　　　　　　", "逆　闘技場　　　　　　",
    "逆　カタコンベ　　　　", "逆　えん道　　　　　　",
    "逆　オルロックの間　　", "逆　最上部　　　　　　",
    "逆　中央部　　　　　　",
#endif
};
#else
static u8 D_801804D4[] = {
    STAGE_MEMORYCARD,
    STAGE_CAT,
};
static char* D_801804D8[] = {
    "NORMAL      ",
    "SPECIAL     ",
};
#endif

#if VERSION >= VER_JP12
static void AnimateTitleLogo(Primitive* prim, s32 i) {
    Primitive* prim15;

    if (i == 15) {
        prim15 = prim;
    }
    switch (prim->p1) {
    case 0:
        if (i == 0xF) {
            if (!(rand() & 0x7F)) {
                prim->p2 = 6;
                prim->tpage = 0x19;
                prim->p1++;
            }
        } else if ((prim15->p1 >= 2) || (prim15->p1 && (prim15->p2 < 3))) {
            prim->p2 = (rand() & 3) + 6;
            prim->tpage = 0x19;
            prim->p1++;
        }
        break;

    case 1:
        if (!--prim->p2) {
            prim->p2 = 6;
            prim->tpage = 0x1A;
            prim->p1++;
        }
        break;

    case 2:
        if (!--prim->p2) {
            prim->p2 = (rand() & 7) + 6;
            prim->tpage = 0x1B;
            prim->p1++;
        }
        break;

    case 3:
        if (!--prim->p2) {
            prim->p2 = 5;
            prim->tpage = 0x1A;
            prim->p1++;
        }
        break;

    case 4:
        if (!--prim->p2) {
            prim->p2 = 5;
            prim->tpage = 0x19;
            prim->p1++;
        }
        break;

    case 5:
        if (!--prim->p2) {
            prim->p2 = 6;
            prim->tpage = 0x18;
            prim->p1++;
        }
        break;

    case 6:
        if (!--prim->p2) {
            if (i == 0x10) {
                prim->p1 = 0;
            } else {
                prim->p1++;
                prim->p2 = 6;
                if (rand() & 3) {
                    prim->p1 = 1;
                    prim->tpage = 0x19;
                }
            }
        }
        break;

    case 7:
        if (!--prim->p2) {
            prim->p1 = 0;
        }
        break;
    }
    if (i == 0x10) {
        if (prim->tpage == 0x18) {
            prim->u1 = 0x78;
        } else {
            prim->u1 = 0x70;
        }
    }
}

// x, y, u, v, w, h of the sound mode option sprites
static s32 soundModeSprites[][6] = {
    {0x90, 0xA0, 0x00, 0x90, 0x58, 0x10},
    {0xF0, 0xA0, 0x00, 0xA0, 0x90, 0x10},
};
#endif

#if REGION == REGION_JP
const char D_801A7B80[] = "ステージセレクト";
#else
const char D_801A7B80[] = "SELECT ！！";
#endif

#ifdef PLATFORM_PSYZ
#include <psyz/video.h>

// The title prims of the step code, then one extra quad per text piece.
#define TITLE_PRIMS (VERSION >= VER_JP12 ? 0x13 : 0x11)
#define TITLE_TEXT 7 // PRESS START: prims 0..2, KONAMI line: prims 3..6
// ponytail: measured on the stock logo; a new logo with other empty rows
// at its bottom needs a new value
#define TITLE_LOGO_BLANK 15 // empty rows under the logo art

// The sprite rectangle of each text piece, read while it is still a sprite.
static struct {
    s16 x, y, w, h;
} title_text[TITLE_TEXT];

static void TitleQuad(Primitive* p, s32 x0, s32 y0, s32 x1, s32 y1, s32 u0,
                      s32 v0, s32 u1, s32 v1) {
    p->type = PRIM_GT4;
    p->x0 = p->x2 = x0;
    p->x1 = p->x3 = x1;
    p->y0 = p->y1 = y0;
    p->y2 = p->y3 = y1;
    p->u0 = p->u2 = u0;
    p->u1 = p->u3 = u1;
    p->v0 = p->v1 = v0;
    p->v2 = p->v3 = v1;
    // a sprite has one colour: give the quad's corners vertex 0's
    p->r1 = p->r2 = p->r3 = p->r0;
    p->g1 = p->g2 = p->g3 = p->g0;
    p->b1 = p->b2 = p->b3 = p->b0;
}

// A 4:3 TV shows the 512x240 title frame, so there a texel is 5/8 as wide as
// it is high. The wide view draws square texels: the text looked flat and
// small. One text line (pieces first..first+n-1 side by side) is drawn sy
// rows per texel row and sx8/8 columns per texel column, centred on the
// canvas (cx2 = twice the centre x) with its top row at y. Any texture in
// the sprites' slot works: the size comes from the sprites, the texture
// coordinates are read on every frame.
static void TitleTextLine(Primitive** p, s32 first, s32 n, s32 cx2, s32 cw,
                          s32 y, s32 sx8, s32 sy) {
    s32 lo = 0x7FFF, hi = -0x7FFF, k, lc2, xs, xm, xe, u, v, h;
    Primitive *P, *E;

    for (k = first; k < first + n; k++) {
        lo = MIN(lo, title_text[k].x);
        hi = MAX(hi, title_text[k].x + title_text[k].w);
    }
    lc2 = lo + hi;
    if ((hi - lo) * sx8 > cw * 8) {
        sx8 = cw * 8 / (hi - lo); // the line must fit the canvas
    }
#define TX(x) ((cx2 + (2 * (x) - lc2) * sx8 / 8) / 2)
    for (k = first; k < first + n; k++) {
        P = p[k];
        E = p[TITLE_PRIMS + k];
        xs = title_text[k].x;
        xe = xs + title_text[k].w;
        h = title_text[k].h;
        u = P->u0;
        v = P->v0;
        // a quad's end u is a byte, so u 0x100 does not exist: the piece
        // ends at texel 0xFF and the extra quad draws that last column
        xm = MIN(xe, xs + 0xFF - u);
        E->drawMode = DRAW_HIDE;
        if (xm < xe) {
            E->r0 = P->r0, E->g0 = P->g0, E->b0 = P->b0;
            E->tpage = P->tpage;
            E->clut = P->clut;
            E->priority = P->priority;
            E->drawMode = P->drawMode;
            TitleQuad(E, TX(xm), y, TX(xe), y + h * sy, 0xFF, v, 0xFF, v + h);
        }
        TitleQuad(P, TX(xs), y, TX(xm), y + h * sy, u, v, u + xm - xs, v + h);
    }
#undef TX
}

// The wide view gives the title a canvas larger than 512x240 (src/pc/wide.c).
// The background stretches to the whole canvas, the logo stays in place, the
// KONAMI line keeps its gap to the bottom and PRESS START sits midway between
// the logo and the KONAMI line. Runs every frame after the step code, so it
// follows dock changes and the colours the step code just set.
static void TitleFillCanvas(void) {
    PsyzRect a = Psyz_VideoGetDrawArea();
    s32 w = a.ex - a.sx, i, n, sy, sx8, ky, py;
    Primitive* prim;
    Primitive* p[TITLE_PRIMS + TITLE_TEXT];

    if (g_GameEngineStep == 0 || g_GameEngineStep == 5 || w <= 0) {
        return;
    }
    prim = &g_PrimBuf[D_801BB010];
    for (n = 0; prim != NULL && n < LEN(p); prim = prim->next, n++) {
        p[n] = prim;
    }
    if (n < LEN(p)) {
        return;
    }
    for (i = 0; i < TITLE_TEXT; i++) {
        if (p[i]->type == PRIM_SPRT) { // just allocated by step 0
            title_text[i].x = p[i]->x0;
            title_text[i].y = p[i]->y0;
            title_text[i].w = p[i]->u1;
            title_text[i].h = p[i]->v1;
        }
    }
    // whole rows: every texel row gets the same height
    sy = MAX(1, (a.ey - a.sy + DISP_TITLE_H / 2) / DISP_TITLE_H);
    // 5/8 of the height, also at sy 1 (a canvas under 360 rows: 1440p, 4K,
    // 21:9), where square texels drew the text 1.6 times too wide
    // ponytail: at internal scale 1 and sy 1 (a window under 360 rows) the
    // nearest present drops columns; a linear present would fix that
    sx8 = sy * 5;
    // KONAMI line (prims 3..6): the stock gap to the bottom
    ky = a.ey - (DISP_TITLE_H - title_text[3].y - title_text[3].h) -
         title_text[3].h * sy;
    TitleTextLine(p, 3, 4, a.sx + a.ex, w, ky, sx8, sy);
    // PRESS START (prims 0..2): midway between the logo art (prim 11) and
    // KONAMI
    py = (p[11]->y0 + p[11]->v1 - TITLE_LOGO_BLANK + ky -
          title_text[0].h * sy) /
         2;
    TitleTextLine(p, 0, 3, a.sx + a.ex, w, py, sx8, sy);

    for (i = 7; i < TITLE_PRIMS; i++) {
        prim = p[i];
        if (i < 11) { // background: four strips over the whole canvas
            TitleQuad(prim, a.sx + (i - 7) * w / 4, a.sy,
                      a.sx + (i - 6) * w / 4, a.ey, 0, 0, 0x80, 0xF0);
        } else if (i == 15) { // leaf shadow, bottom right corner
            prim->x0 = a.ex - 0xFF;
            prim->y0 = a.ey - 0x90;
        } else if (i == 16) { // leaf shadow, top left corner
            prim->x0 = a.sx;
            prim->y0 = a.sy;
        }
#if VERSION >= VER_JP12
        else if (i >= 17) { // MONO / STEREO
            prim->y0 = soundModeSprites[i - 17][1] - a.sy / 2;
        }
#endif
    }
}
#endif

void HandleTitleScreen(void) {
    Primitive* prim;
    Primitive* prim15;
    s32 i;
#if VERSION == VER_USPROTO
    s32 grey;
    s32 done;
#endif
#if VERSION >= VER_JP12
    s32* sprite;
#endif

    func_801B1F34();
    switch (g_GameEngineStep) {
    case 0:
        if (g_UseDisk != 0) {
            g_CdStep = 1;
            g_LoadFile = 0x8003;
            g_LoadOvlIdx = 0xFF;
        }
#ifdef PLATFORM_PSYZ
        // the extra quads of TitleFillCanvas
        D_801BB010 =
            g_api.AllocPrimitives(PRIM_SPRT, TITLE_PRIMS + TITLE_TEXT);
#elif VERSION >= VER_JP12
        D_801BB010 = g_api.AllocPrimitives(PRIM_SPRT, 0x13);
#else
        D_801BB010 = g_api.AllocPrimitives(PRIM_SPRT, 0x11);
#endif
        prim = &g_PrimBuf[D_801BB010];
        for (i = 0; i < 3; i++, prim = prim->next) {
            prim->x0 = i * 0x80 + 0x70;
#if REGION == REGION_JP
            prim->y0 = 0xA0;
#else
            prim->y0 = 0xA8;
#endif
            prim->u0 = 0x80;
            prim->v0 = i * 0x10 + 0xB0;
            prim->u1 = 0x80;
            if (i == 2) {
                prim->u1 = 0x20;
            }
            prim->v1 = 0x10;
            prim->tpage = 0x18;
            prim->clut = 0x20;
            prim->priority = 1;
            prim->drawMode = DRAW_HIDE;
        }
        for (i = 0; i < 4; i++, prim = prim->next) {
            prim->x0 = i * 0x80 + 0xC;
            prim->y0 = 0xD6;
            prim->u0 = 0x80;
            prim->v0 = i * 8 + 0x90;
            prim->u1 = 0x80;
            if (i == 3) {
                prim->u1 = 0x64;
            }
            prim->v1 = 8;
            prim->tpage = 0x18;
            prim->clut = 0x21;
            prim->priority = 1;
            prim->drawMode = DRAW_HIDE;
        }
        for (i = 0; i < 4; i++, prim = prim->next) {
            prim->x0 = i * 0x80;
            prim->u1 = 0x80;
            prim->v1 = 0xF0;
            prim->tpage = i + 0x88;
            prim->clut = 0x10;
            prim->drawMode = DRAW_HIDE;
        }
        for (i = 0; i < 4; i++, prim = prim->next) {
            prim->x0 = i * 0x80;
#if VERSION == VER_JP12
            prim->u1 = 0x80;
            prim->y0 = 0x30;
            prim->v1 = 0x68;
#elif REGION == REGION_JP
            prim->y0 = 0x30;
            prim->u1 = 0x80;
            prim->v1 = 0x68;
#else
            prim->u1 = 0x80;
            prim->y0 = 0x18;
            prim->v1 = 0x88;
#endif
            prim->tpage = i + 0x8C;
            prim->priority = 2;
            prim->drawMode = DRAW_HIDE;
        }
        prim->x0 = 0x101;
        prim->y0 = 0x60;
        prim->u1 = 0xFF;
        prim->v1 = 0x90;
        prim->tpage = 0x18;
        prim->clut = 0x22;
        prim->priority = 1;
        prim->drawMode = DRAW_HIDE;
        prim = prim->next;

        prim->v0 = 0x90;
        prim->u1 = 0x78;
        prim->v1 = 0x40;
        prim->tpage = 0x18;
        prim->clut = 0x22;
        prim->priority = 1;
        prim->drawMode = DRAW_HIDE;
#if VERSION >= VER_JP12
        prim = prim->next;
        sprite = soundModeSprites[0];
        for (i = 0; i < 2; i++, prim = prim->next) {
            prim->x0 = *sprite++;
            prim->y0 = *sprite++;
            prim->u0 = *sprite++;
            prim->v0 = *sprite++;
            prim->u1 = *sprite++;
            prim->v1 = *sprite++;
            prim->tpage = 0x1C;
            prim->clut = 0x20;
            prim->priority = 2;
            prim->drawMode = DRAW_HIDE;
        }
#endif
        func_801B18F4();
        D_801BB014 = 0;
#if VERSION >= VER_JP12
        g_Settings.isSoundMono = 0;
        g_api.ApplySoundMode();
#endif
        g_GameEngineStep++;
        break;

    case 1:
        SetTitleDisplayBuffer();
        prim = &g_PrimBuf[D_801BB010];
        i = 0;
        D_801BB014 += 8;
        while (prim != NULL) {
            prim->drawMode = DRAW_COLORS;
            SetPrimGrey(prim, D_801BB014);
#if IS_VER_INTL
            if (i == 7 || i == 8 || i == 9 || i == 10) {
                prim->r0 = D_801BB014 * 3 / 4;
                prim->g0 = D_801BB014 * 7 / 8;
                prim->b0 = D_801BB014 * 3 / 4;
            }
#endif
            if (i == 15 || i == 16) {
                prim->drawMode = DRAW_TPAGE | DRAW_COLORS | DRAW_TRANSP;
            }
#if VERSION >= VER_JP12
            if (i >= 17) {
                prim->drawMode = DRAW_HIDE;
            }
#endif
            prim = prim->next;
            i++;
        }
        if (D_801BB014 == 0x80) {
            D_801804D0 = 0x800;
            g_GameEngineStep++;
        }
        break;

    case 2:
#if VERSION >= VER_JP12
        prim = &g_PrimBuf[D_801BB010];
        for (i = 0; prim != NULL; prim = prim->next, i++) {
            if (i < 3) {
                prim->drawMode = DRAW_HIDE;
                if (g_Timer & 0x30) {
                    prim->drawMode = DRAW_DEFAULT;
                }
            }
            if (i == 15 || i == 16) {
                AnimateTitleLogo(prim, i);
            }
            if (i >= 17) {
                prim->drawMode = DRAW_HIDE;
            }
        }
        if (g_pads[0].tapped & PAD_START) {
            g_api.PlaySfx(SFX_START_SLAM_B);
            g_GameEngineStep++;
        }
        if (g_pads[0].tapped & PAD_SELECT) {
            g_api.PlaySfx(SFX_UI_MP_FULL);
            g_GameEngineStep = 7;
        }
        break;
#else
        prim = &g_PrimBuf[D_801BB010];
        for (i = 0; prim != NULL; prim = prim->next, i++) {
            if (i < 3) {
                prim->drawMode = DRAW_HIDE;
                if (g_Timer & 0x30) {
                    prim->drawMode = DRAW_DEFAULT;
                }
            }
            if ((i != 15) && (i != 16)) {
                continue;
            }
            if (i == 15) {
                prim15 = prim;
            }
            switch (prim->p1) {
            case 0:
                if (i == 0xF) {
                    if (!(rand() & 0x7F)) {
                        prim->p2 = 6;
                        prim->tpage = 0x19;
                        prim->p1++;
                    }
                } else if (
                    (prim15->p1 >= 2) || (prim15->p1 && (prim15->p2 < 3))) {
                    prim->p2 = (rand() & 3) + 6;
                    prim->tpage = 0x19;
                    prim->p1++;
                }
                break;

            case 1:
                if (!--prim->p2) {
                    prim->p2 = 6;
                    prim->tpage = 0x1A;
                    prim->p1++;
                }
                break;

            case 2:
                if (!--prim->p2) {
                    prim->p2 = (rand() & 7) + 6;
                    prim->tpage = 0x1B;
                    prim->p1++;
                }
                break;

            case 3:
                if (!--prim->p2) {
                    prim->p2 = 5;
                    prim->tpage = 0x1A;
                    prim->p1++;
                }
                break;

            case 4:
                if (!--prim->p2) {
                    prim->p2 = 5;
                    prim->tpage = 0x19;
                    prim->p1++;
                }
                break;

            case 5:
                if (!--prim->p2) {
                    prim->p2 = 6;
                    prim->tpage = 0x18;
                    prim->p1++;
                }
                break;

            case 6:
                if (!--prim->p2) {
                    if (i == 0x10) {
                        prim->p1 = 0;
                    } else {
                        prim->p1++;
                        prim->p2 = 6;
                        if (rand() & 3) {
                            prim->p1 = 1;
                            prim->tpage = 0x19;
                        }
                    }
                }
                break;

            case 7:
                if (!--prim->p2) {
                    prim->p1 = 0;
                }
                break;
            }
            if (i == 0x10) {
                if (prim->tpage == 0x18) {
                    prim->u1 = 0x78;
                } else {
                    prim->u1 = 0x70;
                }
            }
        }
        if (g_pads[0].tapped & PAD_START) {
            g_api.PlaySfx(SFX_START_SLAM_B);
            g_GameEngineStep++;
        }
        break;
#endif

    case 3:
#if VERSION != VER_USPROTO
        g_GameEngineStep = 6;
#else
        i = 0;
        D_8003C730 = 0;
        D_80097924 = -1;
        prim = &g_PrimBuf[D_801BB010];
        done = 0;
        while (prim != NULL) {
            if (i < 3) {
                prim->drawMode = DRAW_DEFAULT;
                if (g_Timer & 4) {
                    prim->drawMode = DRAW_HIDE;
                }
            } else if ((i >= 3 && i < 11) || i == 15 || i == 16) {
                grey = prim->r0 - 2;
                SetPrimGrey(prim, grey);
                if (i == 16 && grey <= 0) {
                    done = 1;
                }
            }
            prim = prim->next;
            i++;
        }
        if (done) {
            g_api.func_800EA5E4(ANIMSET_DRA(0));
            prim = &g_PrimBuf[D_801BB010];
            prim->x0 = 0x90;
            prim->y0 = 0xA0;
            prim->u0 = 0;
            prim->v0 = 0;
            prim->u1 = 0xF0;
            prim->v1 = 0x10;
            prim->tpage = 0x1F;
            prim->clut = PAL_UNK_1A1;
            prim->priority = 0;
            prim->drawMode = DRAW_DEFAULT;
            prim = prim->next;
            prim->x0 = 0xA0;
            prim->y0 = 0xB0;
            prim->u0 = 0;
            prim->v0 = 0x10;
            prim->u1 = 0xF0;
            prim->v1 = 0x10;
            prim->tpage = 0x1F;
            prim->clut = PAL_UNK_1A1;
            prim->priority = 0;
            prim->drawMode = DRAW_DEFAULT;
            prim = prim->next;
            prim->drawMode = DRAW_HIDE;
            func_801B259C(D_801A7B80, 0);
            g_GameEngineStep++;
        }
#endif
        break;

    case 4:
        if (g_pads[0].tapped & (PAD_RIGHT | PAD_DOWN)) {
            g_api.PlaySfx(SFX_DEBUG_SELECT);
            D_800987B4++;
            if (D_800987B4 >= LEN(D_801804D4)) {
                D_800987B4 = 0;
            }
        }
        if (g_pads[0].tapped & (PAD_UP | PAD_LEFT)) {
            g_api.PlaySfx(SFX_DEBUG_SELECT);
            D_800987B4--;
            if (D_800987B4 < 0) {
                D_800987B4 = LEN(D_801804D4) - 1;
            }
        }
#if VERSION >= VER_USPROTO
        prim = &g_PrimBuf[D_801BB010];
        if (g_Timer & 0x1C) {
            prim->drawMode = DRAW_DEFAULT;
        } else {
            prim->drawMode = DRAW_HIDE;
        }
#endif
        g_StageId = D_801804D4[D_800987B4];
        func_801B1F4C(1);
#if REGION == REGION_JP
        func_801B2564(D_801804D8[D_800987B4], 1);
#else
        func_801B259C(D_801804D8[D_800987B4], 1);
#endif
        if (g_pads[0].tapped & (PAD_START | PAD_CIRCLE)) {
            g_api.PlaySfx(SFX_START_SLAM_B);
            g_GameEngineStep++;
        }
        break;

    case 5:
        func_801B18F4();
        func_801B19F4();
        func_801B1DA8();
        g_api.FreePrimitives(D_801BB010);
        strcpy(g_Status.saveName, "alucard");
        if (g_StageId == STAGE_ST0) {
            SetGameState(Game_VideoPlayback);
        } else if (g_StageId == STAGE_MEMORYCARD) {
            SetGameState(Game_MainMenu);
        } else {
            SetGameState(Game_NowLoading);
            g_GameStep = 2;
        }
        break;

    case 6:
        prim = &g_PrimBuf[D_801BB010];
        i = 0;
        D_801BB014 -= 8;
        while (prim != NULL) {
            prim->drawMode = DRAW_COLORS;
#if VERSION >= VER_JP12
            if (i >= 17) {
                prim->drawMode = DRAW_HIDE;
            }
#endif
            SetPrimGrey(prim, D_801BB014);
            if (i == 15 || i == 16) {
                prim->drawMode = DRAW_TPAGE | DRAW_COLORS | DRAW_TRANSP;
            }
            prim = prim->next;
            i++;
        }
        if (D_801BB014 == 0) {
            g_StageId = STAGE_MEMORYCARD;
            g_GameEngineStep = 5;
        }
#ifdef PLATFORM_PSYZ
        TitleFillCanvas();
#endif
        return;
#if VERSION >= VER_JP12
    // allow selecting between MONO and STEREO when pressing SELECT
    case 7:
        prim = &g_PrimBuf[D_801BB010];
        for (i = 0; i < 3; i++) {
            prim->drawMode = DRAW_HIDE;
            prim = prim->next;
        }
        for (i = 13; i >= 0; i--) {
            prim = prim->next;
        }
        for (i = 0; i < 2; i++) {
            prim->drawMode = DRAW_DEFAULT;
            prim = prim->next;
        }
        g_GameEngineStep = 8;
        break;
    case 8:
        prim = &g_PrimBuf[D_801BB010];
        for (i = 0; i < 17; i++) {
            if (i == 15 || i == 16) {
                AnimateTitleLogo(prim, i);
            }
            prim = prim->next;
        }
        prim->drawMode = DRAW_DEFAULT;
        prim = prim->next;
        if (g_Settings.isSoundMono) {
            prim->v0 = 0xC0;
        } else {
            prim->v0 = 0xA0;
        }
        if (g_pads[0].tapped & (PAD_UP | PAD_RIGHT | PAD_DOWN | PAD_LEFT)) {
            g_Settings.isSoundMono ^= 1;
            g_api.ApplySoundMode();
            g_api.PlaySfx(SFX_UI_MOVE);
        }
        if (g_pads[0].tapped & (PAD_START | PAD_SELECT | PAD_CIRCLE)) {
            g_GameEngineStep = 2;
            g_api.PlaySfx(SFX_UI_MP_FULL);
        }
        break;
#endif
    }
#ifdef PLATFORM_PSYZ
    TitleFillCanvas();
#endif
}
