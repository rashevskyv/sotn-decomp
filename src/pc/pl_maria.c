// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../maria/maria.h"

// her voice bank, built from sd_mar2.spk in the build dir (CMakeLists.txt)
#define SD_RIH_VH g_MariaVoVh
#define SD_RIH_VH_len g_MariaVoVhLen
#define SD_RIH_VB g_MariaVoVb
#define SD_RIH_VB_len g_MariaVoVbLen
#include "sfx/maria_vo.h"

// sim_pc.c: Richter's bank file 2 (VH) or 3 (VB) while she plays
int MariaReadVab(int body, void* dst, u32 size) {
    const unsigned char* src = body ? g_MariaVoVb : g_MariaVoVh;
    unsigned int len = body ? g_MariaVoVbLen : g_MariaVoVhLen;

    if (len != size) {
        return -1;
    }
    memcpy(dst, src, size);
    fprintf(stderr, "[maria] voice bank %s: %u bytes\n", body ? "VB" : "VH",
            size);
    return 0;
}

// To use Maria as a playable character, add -DUSE_PLAYER_MARIA=1 in cmake

void MARIA_Load();
extern PlayerOvl MARIA_player;

// D_80097C40 of the save owner while the other kind of player plays
static int s_C40Stashed;
static u32 s_C40Owner;
// g_Status.subWeapon of each character while another one plays (her
// animals and their subweapons share the field); s_SubHolder is the
// character whose value g_Status.subWeapon holds, -1: the save owner
static u32 s_Sub[PLAYER_MARIA + 1];
static int s_SubHolder = -1;

static int SaveOwner(void) {
    return g_CheatSaveChar >= 0 ? g_CheatSaveChar : g_PlayableCharacter;
}

void Maria_ResetStash(void) {
    s_C40Stashed = 0;
    s_SubHolder = -1;
    memset(s_Sub, 0, sizeof(s_Sub));
}

static void SyncSub(void) {
    int me = g_PlayableCharacter;

    if (s_SubHolder < 0) {
        s_SubHolder = SaveOwner();
    }
    if (me == s_SubHolder || (unsigned)me > PLAYER_MARIA ||
        (unsigned)s_SubHolder > PLAYER_MARIA) {
        return;
    }
    s_Sub[s_SubHolder] = g_Status.subWeapon;
    g_Status.subWeapon = s_Sub[me]; // 0: none, or her doll
    fprintf(stderr, "[maria] subweapon: player %d keeps %u, %d gets %u\n",
            s_SubHolder, s_Sub[s_SubHolder], me, g_Status.subWeapon);
    s_SubHolder = me;
}

void Maria_SyncStash(void) {
    int mine =
        (SaveOwner() == PLAYER_MARIA) == (g_PlayableCharacter == PLAYER_MARIA);

    if (!mine && !s_C40Stashed) {
        s_C40Owner = g_Status.D_80097C40;
        s_C40Stashed = 1;
        g_Status.D_80097C40 = 0; // her doll, or a new Muramasa counter
    } else if (mine && s_C40Stashed) {
        g_Status.D_80097C40 = s_C40Owner;
        s_C40Stashed = 0;
    }
    SyncSub();
}

void Maria_SaveStash(u32* subWeapon, u32* c40) {
    int owner = SaveOwner();

    if (s_C40Stashed) {
        *c40 = s_C40Owner;
    }
    if (s_SubHolder >= 0 && s_SubHolder != owner &&
        (unsigned)owner <= PLAYER_MARIA) {
        *subWeapon = s_Sub[owner];
    }
}

// Her animals are subweapons 0..4 (0: the doll), in both slots:
// g_Status.subWeapon and g_Status.D_80097C40 (PSP). Richter's 5..9 (APPLY
// NOW) wrap onto 1..4.
static void MariaMain(void) {
    if (g_Status.subWeapon > 4) {
        g_Status.subWeapon = (g_Status.subWeapon - 1) % 4 + 1;
    }
    if (g_Status.D_80097C40 > 4) {
        g_Status.D_80097C40 = 0;
    }
    MARIA_player.D_8013C000();
}

void InitPlayerMaria(void) {
    g_CurrentEntity = &PLAYER;
    Maria_SyncStash();
    MARIA_Load();
    g_PlOvl.D_8013C000 = MariaMain;
}

// the HUD copies of her two slots (5F60C.c)
s32 D_psp_091CDC80;
s32 D_psp_091CDC88;

float func_psp_089260AC(float x) { return sinf(x); }
float func_psp_089260D0(float x) { return cosf(x); }

// pl_main.c swaps her two animals on a new press of PSP Circle: Switch A,
// keyboard d. Circle is set only on the frame it is tapped, so a press
// held from a menu does not swap when the game goes on.
int PadReadPSP(void) {
    return (g_pads[0].pressed & ~PAD_CIRCLE) | (g_pads[0].tapped & PAD_CIRCLE);
}

void LoadPendingGfx();
void ResetPendingGfxLoad();
void func_psp_091040A0(u_long** bank) {
    GfxBank* gfxBank = (GfxBank*)bank;
    for (int i = 0; i < LEN(g_GfxLoad); i++) {
        GfxLoad* gfxLoad = &g_GfxLoad[i];
        if (gfxLoad->kind == GFX_BANK_NONE) {
            gfxLoad->kind = gfxBank->kind;
            gfxLoad->unk6 = 0;
            gfxLoad->unk8 = 0;
            gfxLoad->next = gfxBank->entries;
            return;
        }
    }
    LoadPendingGfx();
    ResetPendingGfxLoad();
}

void func_psp_0892667C(s32 paletteID, u16* paletteData) {
    int x = (paletteID & 15) * 16;
    int y = (paletteID >> 4) & 15;
    RECT rect = {x, 240 + y, 16, 1};
    LoadImage(&rect, (u_long*)paletteData);
}

void func_psp_089285A0(s32 angle, MATRIX* out) {
    short c = (short)rcos(angle);
    short s = (short)rsin(angle);
    out->m[0][0] = c;
    out->m[0][1] = (s16)-s;
    out->m[0][2] = 0;
    out->m[1][0] = s;
    out->m[1][1] = c;
    out->m[1][2] = 0;
    out->m[2][0] = 0;
    out->m[2][1] = 0;
    out->m[2][2] = 4096;
    out->t[2] = 0;
    out->t[1] = 0;
    out->t[0] = 0;
}
