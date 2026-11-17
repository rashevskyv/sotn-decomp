// SPDX-License-Identifier: AGPL-3.0-or-later
// PC: the Saturn's New Game "Player select", after the name entry. Port of
// sotn-ultimate 22a9c6d mods/sotn-ultimate/src/menu/2C048_mod_player_select.h
// without its version panel. Included by select.c.
//
// Left/Right pick a card, confirm or START starts the game, back returns to
// the name entry. On Maria's card Up/Down pick her version. The cursor
// starts on the character the name entry picked (func_801AD66C: the retail
// RICHTER name, the CHEATS -> CHARACTER row). The pick is the player and the
// save owner of this game only: the CHARACTER row keeps its value and still
// acts on a save load.
//
// Art: tools/saturn/charsel_gfx.py makes charsel_gfx.h in the build dir: the
// frame and the arrows (drawn in code), the portraits (the owner's art, else
// the Saturn's; CHARSEL_PORTRAITS 0: the blue fill only), the Saturn header
// and gothic names (CHARSEL_SATURN 0: the names in the menu font). All of it
// is drawn 1:1 in the 384x240 frame.
// VRAM x 896+, y < 256, which the menu never samples: the portraits 8bpp at
// y 0-91, their CLUTs at y 92-94, the 4bpp CLUTs at y 95, the 4bpp art from
// y 96 (the frame and the arrows at u 0, the header and the names at u 104).
#include <stdio.h>
#include "charsel_gfx.h"

// 1 when Maria's player code is in the build: her card is then offered.
#ifdef USE_PLAYER_MARIA
#define CHARSEL_MARIA 1
#else
#define CHARSEL_MARIA 0
#endif

typedef struct {
    s32 player;
    const char* name; // 8x8 cells; language = uk shows the pack's name
#if CHARSEL_PORTRAITS
    const u16* pix;
    const u16* clut;
#endif
#if CHARSEL_SATURN
    const u8* art; // the gothic name's cell map
#endif
} Charsel;

#if CHARSEL_PORTRAITS
#define CS_PIC(n) , g_CharselPix_##n, g_CharselClut_##n
#else
#define CS_PIC(n)
#endif
#if CHARSEL_SATURN
#define CS_ART(n) , g_CharselMap_name_##n
#else
#define CS_ART(n)
#endif
// The Saturn's cursor order: Alucard (middle), Richter (left), Maria
// (right). CHEAT_CHAR_* values, never g_Status.D_80097C40.
static const Charsel g_Charsel[] = {
    {PLAYER_ALUCARD, _S("Alucard") CS_PIC(alucard) CS_ART(alucard)},
    {PLAYER_RICHTER, _S("Richter") CS_PIC(richter) CS_ART(richter)},
#if CHARSEL_MARIA
    {PLAYER_MARIA, _S("Maria") CS_PIC(maria) CS_ART(maria)},
#endif
};
#define CS_N ((s32)LEN(g_Charsel))

// Maria's versions, Up/Down on her card: PSP is this port's player, PSX is
// the classic art of the PSX/Saturn Maria. Roadmap C2 (the Saturn Maria's
// player) starts the game for PSX in CharselLocked; until then it refuses.
#if CHARSEL_MARIA
enum { CHARSEL_MARIA_PSP, CHARSEL_MARIA_PSX };
static s32 s_CharselMariaVariant; // kept while the cursor is elsewhere
#endif

#define CS_VRAM_X 896
#define CS_CENTRE_X 192
#define CS_SPACING 112
#define CS_FRAME_Y 72
#define CS_NAME_Y (CS_FRAME_Y + CHARSEL_FRAME_H)
#define CS_HEADER_X 30
#define CS_HEADER_Y 36

#define CS_CLUT_Y CHARSEL_WIN_H
#define CS_CLUT4_Y (CS_CLUT_Y + 3)
#define CS_FRAME_CLUT_X (CS_VRAM_X + 48) // after the 3 Saturn CLUTs
#define CS_ART_Y 96
#define CS_ART_TPAGE (CS_VRAM_X >> 6) // 4bpp, y 0
#define CS_ARROW_V (CS_ART_Y + CHARSEL_FRAME_H)
#define CS_SIDE_U 104
#define CS_NAME_V (CS_ART_Y + CHARSEL_HEADER_H * 8)
#define CS_NAME_H (CHARSEL_NAME_RICHTER_H * 8)

static s32 s_CharselCursor;
static u32 s_CharselTimer;

void func_801AEA8C(s32 arg0);

// one textured quad, w x h on screen from a tw x th texture rectangle, the
// texture times shade / 0x80
static void CharselQuad(s32 x, s32 y, s32 w, s32 h, s32 u, s32 v, s32 tw,
                        s32 th, u16 tpage, u16 clut, s32 shade, s32 otIdx) {
    POLY_GT4* poly = &g_CurrentBuffer->polyGT4[g_GpuUsage.gt4];

    setPolyGT4(poly);
    setSemiTrans(poly, 0);
    setShadeTex(poly, 0);
    setXY4(poly, x, y, x + w, y, x, y + h, x + w, y + h);
    setUV4(poly, u, v, u + tw, v, u, v + th, u + tw, v + th);
    setRGB0(poly, shade, shade, shade);
    setRGB1(poly, shade, shade, shade);
    setRGB2(poly, shade, shade, shade);
    setRGB3(poly, shade, shade, shade);
    poly->tpage = tpage;
    poly->clut = clut;
    AddPrim(&g_CurrentBuffer->ot[otIdx], poly);
    g_GpuUsage.gt4++;
}

static void CharselUpload(s32 x, s32 y, s32 w, s32 h, const void* data) {
    RECT rect;

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    LoadImage(&rect, (u_long*)data);
    DrawSync(0);
}

// a card's portrait into its slot: Maria's in the chosen version
static void CharselUploadPortrait(s32 id) {
#if CHARSEL_PORTRAITS
    const u16* pix = g_Charsel[id].pix;
    const u16* clut = g_Charsel[id].clut;

#if CHARSEL_MARIA
    if (g_Charsel[id].player == PLAYER_MARIA &&
        s_CharselMariaVariant == CHARSEL_MARIA_PSX) {
        pix = g_CharselPix_maria_psx;
        clut = g_CharselClut_maria_psx;
    }
#endif
    CharselUpload(CS_VRAM_X + id * (CHARSEL_WIN_W / 2), 0, CHARSEL_WIN_W / 2,
                  CHARSEL_WIN_H, pix);
    CharselUpload(CS_VRAM_X, CS_CLUT_Y + id, CHARSEL_CLUT_LEN, 1, clut);
#endif
}

#if CHARSEL_SATURN
// one 4bpp image from its cell map, a row of cells at a time
static void CharselUploadCells(const u8* map, s32 w, s32 h, s32 u, s32 v) {
    u32 buf[CHARSEL_HEADER_W * 8];
    const u32* t;
    s32 x, y, r;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            t = map[y * w + x] ? &g_CharselTiles[(map[y * w + x] - 1) * 8]
                               : NULL;
            for (r = 0; r < 8; r++) {
                buf[r * w + x] = t ? t[r] : 0;
            }
        }
        CharselUpload(CS_VRAM_X + u / 4, v + y * 8, w * 2, 8, buf);
    }
}
#endif

static void CharselInit(void) {
    s32 i;

    for (i = 0; i < CS_N; i++) {
        CharselUploadPortrait(i);
#if CHARSEL_SATURN
        CharselUploadCells(g_Charsel[i].art, CHARSEL_NAME_RICHTER_W,
                           CHARSEL_NAME_RICHTER_H, CS_SIDE_U,
                           CS_NAME_V + i * CS_NAME_H);
#endif
    }
#if CHARSEL_SATURN
    for (i = 0; i < (s32)LEN(g_CharselClut4); i++) {
        CharselUpload(CS_VRAM_X + i * 16, CS_CLUT4_Y, 16, 1, g_CharselClut4[i]);
    }
    CharselUploadCells(g_CharselMap_header, CHARSEL_HEADER_W, CHARSEL_HEADER_H,
                       CS_SIDE_U, CS_ART_Y);
#endif
    for (i = 0; i < 2; i++) {
        CharselUpload(
            CS_FRAME_CLUT_X + i * 16, CS_CLUT4_Y, 16, 1, g_CharselFrameClut[i]);
    }
    CharselUpload(CS_VRAM_X, CS_ART_Y, CHARSEL_FRAME_W / 4, CHARSEL_FRAME_H,
                  g_CharselFrame);
    CharselUpload(CS_VRAM_X, CS_ARROW_V, CHARSEL_ARROW_W / 4, CHARSEL_ARROW_H,
                  g_CharselArrow_up);
    CharselUpload(CS_VRAM_X, CS_ARROW_V + 8, CHARSEL_ARROW_W / 4,
                  CHARSEL_ARROW_H, g_CharselArrow_down);
    s_CharselCursor = 0;
    for (i = 0; i < CS_N; i++) {
        if (g_Charsel[i].player == g_PlayableCharacter) {
            s_CharselCursor = i;
        }
    }
    MenuHideAllGfx();
}

// the name in the 8x8 menu font, centred under the card (pack name on uk)
static void CharselName(s32 id, s32 x, s32 sel) {
    const u8* s = (const u8*)LANG_CELL(g_Charsel[id].name);
    s32 n;

    for (n = 0; s[n] != 0xFF; n++) {
    }
    x += (CHARSEL_FRAME_W - n * 8) / 2;
    for (; *s != 0xFF; s++, x += 8) {
        DrawGlyph(x, CS_NAME_Y + 8, 8, 8, (*s & 0xF) * 8, (*s & 0xF0) / 2,
                  0x196, 0x1E, 0, sel ? 0x80 : 0x50);
    }
}

#if CHARSEL_MARIA
// Maria's Up/Down arrows above and under her card; they bob on her
static void CharselArrows(s32 x, s32 sel) {
    u16 clut = getClut(CS_FRAME_CLUT_X + (sel ? 16 : 0), CS_CLUT4_Y);
    s32 bob = sel && (s_CharselTimer & 0x10) ? 1 : 0;

    x += (CHARSEL_FRAME_W - CHARSEL_ARROW_W) / 2;
    CharselQuad(x, CS_FRAME_Y - CHARSEL_ARROW_H - 2 - bob, CHARSEL_ARROW_W,
                CHARSEL_ARROW_H, 0, CS_ARROW_V, CHARSEL_ARROW_W,
                CHARSEL_ARROW_H, CS_ART_TPAGE, clut, 0x80, 0x20);
    CharselQuad(x, CS_NAME_Y + 26 + bob, CHARSEL_ARROW_W, CHARSEL_ARROW_H, 0,
                CS_ARROW_V + 8, CHARSEL_ARROW_W, CHARSEL_ARROW_H, CS_ART_TPAGE,
                clut, 0x80, 0x20);
}
#endif

// One card: the blue fill, the portrait on it, the frame and the name on
// top. The chosen card has the bright frame and the portrait at full light.
static void CharselDrawCard(s32 id, s32 x, s32 sel) {
    s32 wx = x + CHARSEL_WIN_X;
    s32 wy = CS_FRAME_Y + CHARSEL_WIN_Y;
    s32 ww = CHARSEL_WIN_W;
    s32 wh = CHARSEL_WIN_H;
    const u8* t = g_CharselFill[0];
    const u8* b = g_CharselFill[1];
    POLY_G4* poly = &g_CurrentBuffer->polyG4[g_GpuUsage.g4];

    setPolyG4(poly);
    setSemiTrans(poly, 0);
    setXY4(poly, wx, wy, wx + ww, wy, wx, wy + wh, wx + ww, wy + wh);
    setRGB0(poly, t[0], t[1], t[2]);
    setRGB1(poly, t[0], t[1], t[2]);
    setRGB2(poly, b[0], b[1], b[2]);
    setRGB3(poly, b[0], b[1], b[2]);
    AddPrim(&g_CurrentBuffer->ot[0x1E], poly);
    g_GpuUsage.g4++;

#if CHARSEL_PORTRAITS
    CharselQuad(wx, wy, ww, wh, id * ww, 0, ww, wh, 0x80 | (CS_VRAM_X >> 6),
                getClut(CS_VRAM_X, CS_CLUT_Y + id), sel ? 0x80 : 0x60, 0x1F);
#endif
    CharselQuad(x, CS_FRAME_Y, CHARSEL_FRAME_W, CHARSEL_FRAME_H, 0, CS_ART_Y,
                CHARSEL_FRAME_W, CHARSEL_FRAME_H, CS_ART_TPAGE,
                getClut(CS_FRAME_CLUT_X + (sel ? 16 : 0), CS_CLUT4_Y), 0x80,
                0x20);
#if CHARSEL_MARIA
    if (g_Charsel[id].player == PLAYER_MARIA) {
        CharselArrows(x, sel);
    }
#endif
#if CHARSEL_SATURN
    // the gothic name is English art: uk shows the pack's name instead
    if (!g_Lang || LANG_CELL(g_Charsel[id].name) == g_Charsel[id].name) {
        CharselQuad(x + (CHARSEL_FRAME_W - CHARSEL_NAME_RICHTER_W * 8) / 2,
                    CS_NAME_Y, CHARSEL_NAME_RICHTER_W * 8, CS_NAME_H,
                    CS_SIDE_U, CS_NAME_V + id * CS_NAME_H,
                    CHARSEL_NAME_RICHTER_W * 8, CS_NAME_H, CS_ART_TPAGE,
                    getClut(CS_VRAM_X + (sel ? 32 : 16), CS_CLUT4_Y), 0x80,
                    0x20);
        return;
    }
#endif
    CharselName(id, x, sel);
}

// the card's left edge: the Saturn's places, the two cards centred
static s32 CharselCardX(s32 id) {
    s32 x = CS_CENTRE_X;

    if (CS_N == 2) {
        x += id ? -CS_SPACING / 2 : CS_SPACING / 2;
    } else if (id == 1) {
        x -= CS_SPACING;
    } else if (id == 2) {
        x += CS_SPACING;
    }
    return x - CHARSEL_FRAME_W / 2;
}

static void CharselDraw(void) {
    s32 i;

#if CHARSEL_SATURN
    if (!g_Lang) { // the header is English art
        CharselQuad(CS_HEADER_X, CS_HEADER_Y, CHARSEL_HEADER_W * 8,
                    CHARSEL_HEADER_H * 8, CS_SIDE_U, CS_ART_Y,
                    CHARSEL_HEADER_W * 8, CHARSEL_HEADER_H * 8, CS_ART_TPAGE,
                    getClut(CS_VRAM_X, CS_CLUT4_Y), 0x80, 0x20);
    }
#endif
    for (i = 0; i < CS_N; i++) {
        CharselDrawCard(i, CharselCardX(i), i == s_CharselCursor);
    }
}

// 1 when the card under the cursor cannot start a game yet
static s32 CharselLocked(void) {
#if CHARSEL_MARIA
    // C2: the Saturn Maria's player goes here
    return g_Charsel[s_CharselCursor].player == PLAYER_MARIA &&
           s_CharselMariaVariant == CHARSEL_MARIA_PSX;
#else
    return 0;
#endif
}

// the retail name entry's last steps (Upd_Eng_17) for the chosen character
static void CharselStartGame(void) {
    if (g_PlayableCharacter == PLAYER_ALUCARD) {
        g_StageId = STAGE_ST0;
    } else {
        g_StageId = STAGE_NO3;
    }
    D_8003C730 = 0;
    D_8006C378 = -1;
    if (D_801BAF10) {
        D_80097924 = -1;
    } else {
        D_80097924 = D_801D6B04 / 15;
    }
    g_api.PlaySfx(SET_UNK_80);
    g_GameEngineStep = Upd_Eng_18;
}

static void CharselUpdate(void) {
    s_CharselTimer++;
    // the Saturn: Left = cursor + 1, Right = cursor - 1, wrapping
    if (g_pads[0].tapped & (PAD_LEFT | PAD_RIGHT)) {
        g_api.PlaySfx(SFX_UI_MOVE);
        s_CharselCursor += g_pads[0].tapped & PAD_LEFT ? 1 : CS_N - 1;
        s_CharselCursor %= CS_N;
    }
#if CHARSEL_MARIA
    if ((g_pads[0].tapped & (PAD_UP | PAD_DOWN)) &&
        g_Charsel[s_CharselCursor].player == PLAYER_MARIA) {
        g_api.PlaySfx(SFX_UI_MOVE);
        s_CharselMariaVariant ^= 1; // two versions: Up and Down both switch
        CharselUploadPortrait(s_CharselCursor);
    }
#endif
    if (g_pads[0].tapped & (PAD_MENU_CONFIRM | PAD_START)) {
        if (CharselLocked()) {
            g_api.PlaySfx(SFX_UI_ERROR);
        } else {
            g_api.PlaySfx(SFX_UI_CONFIRM);
            g_PlayableCharacter = g_Charsel[s_CharselCursor].player;
            g_CheatSaveChar = g_PlayableCharacter; // save_mgr.h: save owner
            fprintf(stderr, "[charsel] start: player %d\n", g_PlayableCharacter);
            CharselStartGame();
        }
    } else if (g_pads[0].tapped & PAD_MENU_BACK) {
        g_api.PlaySfx(SFX_UI_ERROR);
        func_801AEA8C(0);
        g_GameEngineStep = Upd_Eng_17;
    }
    CharselDraw();
}
