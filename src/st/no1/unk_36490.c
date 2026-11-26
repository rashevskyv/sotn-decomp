// SPDX-License-Identifier: AGPL-3.0-or-later
#include "no1.h"

static s16 D_us_80180E8C[] = {
    0xBC0, 0xBB0, 0xBA0, 0xB90, 0xB80, 0xB70, 0xB60, 0xB50};
static s16 D_us_80180E9C[] = {24, 28, 32, 40, 48, 56, 64, 72};
static AnimationFrame D_us_80180EAC[] = {
    4, 0x3C, 4, 0xF8, 4, 0xF9, 4, 0xF8, 0, 0x00};
static AnimationFrame D_us_80180EC0[] = {
    4, 0xFF, 4, 0xFA, 4, 0xFB, 4, 0xFA, 0, 0x00};
static s16 D_us_80180ED4[] = {0x0110, 0x7000, 0x0210, 0x0108, 0x0210, 0x0108,
                              0x0000, 0x7000, 0x0190, 0x00F8, 0x0100, 0x7000};
static s32 D_us_80180EEC = 0;
static u8 D_us_80180EF0[] = {0x60, 0x80, 0xC0, 0x80, 0x60};
static AnimParam D_us_80180EF8[] = {
    {ANIMSET_OVL(1), D_us_80180EAC, 44, {.r = 8, .g = 8, .b = 24}},
    {ANIMSET_OVL(2), D_us_80180EC0, 44, {.r = 16, .g = 8, .b = 72}},
    {ANIMSET_OVL(2), D_us_80180EAC, 5, {.r = 8, .g = 8, .b = 24}}};
static s16 D_us_80180F28[] = {4, 5, 6, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2};
STATIC_PAD_DATA(1);

// bss
static s16 D_us_801D6328[6];
static s16 D_us_801D6334[6];
static s16 D_us_801D6340[14];
static s16 D_us_801D635C[14];
STATIC_PAD_BSS(1);

#ifdef PLATFORM_PSYZ
// Wide view (B06, B08). The far background is drawn with prims fixed to the
// 4:3 screen: the mountains tile every 0x80 px, the clouds every 0x7F px and
// the fog every 0x90 px. Extra prims cover the side margins. The prim count
// is fixed at the spawn, so the margin of the spawn (WIDE_COLS, in 16 px) is
// kept in step_s.
#define BG_ROWS 13
// w = the margin in px; mountain segments k = -1.. and 2.., cloud groups
// beyond the 3 of the 4:3 view (they span -16..366)
static s32 BgMtnL(s32 w) { return (w + 127) >> 7; }
static s32 BgMtnR(s32 w) { return ((255 + w) >> 7) - 1; }
static s32 BgCloudsL(s32 w) { return w > 13 ? (w - 13 + 126) / 127 : 0; }
static s32 BgCloudsR(s32 w) {
    return 256 + w > 363 ? (256 + w - 363 + 126) / 127 : 0;
}
static s32 BgTotal(s32 weather, s32 count, s32 q) {
    s32 w = q * 16;
    s32 n = count + BgMtnL(w) + BgMtnR(w);

    if (weather != 2) {
        n += (BgCloudsL(w) + BgCloudsR(w)) * BG_ROWS;
    }
    return n;
}

// The 0x80 px segment k of the mountain texture, clipped to xMin..xMax.
static void BgMountain(Primitive* prim, s32 k, s32 xMin, s32 xMax) {
    s32 a = k * 0x80;
    s32 b = a + 0x80;

    if (a < xMin) {
        a = xMin;
    }
    if (b > xMax) {
        b = xMax;
    }
    if (a >= b) {
        prim->drawMode = DRAW_HIDE;
        return;
    }
    prim->x0 = prim->x2 = a;
    prim->x1 = prim->x3 = b;
    prim->u0 = prim->u2 = a - k * 0x80;
    prim->u1 = prim->u3 = MIN(b - k * 0x80, 0x7F);
    prim->drawMode = DRAW_DEFAULT;
}

// The fog grid (weather 2) has 9 rows. It has 4 columns of 0x90 px in the 4:3
// view. A wide view needs `lead` columns more on the left and more on the
// right.
#define FOG_ROWS 9
static s32 BgFogLead(s32 q) { return (q * 16 + 0x8F) / 0x90; }
static s32 BgFogCols(s32 q) {
    s32 cols = 1 + BgFogLead(q) + (256 + q * 16 + 0x8F + 0x10) / 0x90;

    return cols < 4 ? 4 : cols;
}
// a rain drop starts further out on both sides of the wide view
static s32 BgRainX(void) {
    s32 x = (rand() & 0xFF) + (rand() & 0x3F) + 0x10;

    if (WIDE_X > 0) {
        x += rand() % (2 * WIDE_X + 1) - WIDE_X;
    }
    return x;
}
#define BG_RAIN_X() BgRainX()
// the prim count of the background (the 4:3 count plus the margins) and the
// extra mountain prims at its end (they are set up as prim 2)
#define BG_COUNT(q) BgTotal(D_us_80180EEC, animParams->count, q)
#define BG_MTN_EXTRA(i, q)                                                     \
    ((i) >= BG_COUNT(q) - BgMtnL((q) * 16) - BgMtnR((q) * 16))
#define BG_CASE(i, q) (BG_MTN_EXTRA(i, q) ? 2 : (i))
#define FOG_COLS(self) BgFogCols((self)->step_s)
#define FOG_ROW(i, self) ((i) / FOG_COLS(self))
#define FOG_COL(i, self) ((i) % FOG_COLS(self))
#define FOG_LEAD(self) BgFogLead((self)->step_s)
#else
#define BG_COUNT(q) (animParams->count)
#define BG_CASE(i, q) (i)
#define BG_MTN_EXTRA(i, q) 0
#define BG_RAIN_X() ((rand() & 0xFF) + (rand() & 0x3F) + 0x10)
#define FOG_ROW(i, self) ((i) >> 2)
#define FOG_COL(i, self) ((i) & 3)
#endif

void func_us_801B6490(Entity* self) {
    Primitive* prim;
    AnimParam* animParams;
    s16 x0, x1;
    s16 xOffset;
    s16 xMin, xMax;
    s32 i, j;
    s16 temp;
#ifdef PLATFORM_PSYZ
    s32 q;
#endif

    x0 = D_us_80180ED4[self->params * 2 + 0] - g_Tilemap.scrollX.i.hi;
    x1 = D_us_80180ED4[self->params * 2 + 1] - g_Tilemap.scrollX.i.hi;
    if (self->params == 4) {
        if (g_Tilemap.scrollY.i.hi < 0x260) {
            x0 += 0x50;
        }
        if (g_Tilemap.scrollY.i.hi < 0x80) {
            x0 += 0x10;
        }
        if (g_Tilemap.scrollY.i.hi > 0x190) {
            x1 = 0x7000;
        }
    }
    if (self->params == 2) {
        if (g_Tilemap.scrollY.i.hi > 0x1B0) {
            x0 -= 0x210;
            x1 = 0x7000;
        }
    }

    if (x0 < -WIDE_X) {
        x0 = -WIDE_X;
    }
    xMax = 0xFF + WIDE_X;
    if ((x1 + 0x20 > -WIDE_X) && (x1 < 0x100 + WIDE_X)) {
        if (x1 < -WIDE_X) {
            x1 = -WIDE_X;
        }
        if (x0 >= 0x100 + WIDE_X && x1 < 0xE0 + WIDE_X) {
            xMax = x1 + 0x20;
        }
    } else {
        x1 = 0x7000;
    }

    if (x0 > x1) {
        xMin = x1;
    } else {
        xMin = x0;
    }
    animParams = &D_us_80180EF8[D_us_80180EEC];
    if (self->step == 0) {
        InitializeEntity(g_EInitSpawner);
        D_us_80180EEC = g_CastleFlags[NO1_WEATHER] & 0x7F;
        g_CastleFlags[NO1_WEATHER] |= 0x80;
        animParams = &D_us_80180EF8[D_us_80180EEC];
        g_api.func_800EA5E4(animParams->animSet);
        self->anim = animParams->anim;
        for (i = 0; i < LEN(D_us_801D6340); i++) {
            D_us_801D6340[i] = (i << 0xC) / 7;
        }
#ifdef PLATFORM_PSYZ
        q = WIDE_COLS;
        self->primIndex = g_api.AllocPrimitives(PRIM_GT4, BG_COUNT(q));
        if (self->primIndex == -1 && q) {
            q = 0; // no room for the margins: the 4:3 view only
            self->primIndex =
                g_api.AllocPrimitives(PRIM_GT4, animParams->count);
        }
        self->step_s = q;
#else
        self->primIndex = g_api.AllocPrimitives(PRIM_GT4, animParams->count);
#endif
        if (self->primIndex == -1) {
            DestroyEntity(self);
            return;
        }
        self->flags |= FLAG_HAS_PRIMS;
        prim = &g_PrimBuf[self->primIndex];
        for (i = 0, j = 0; i < BG_COUNT(q); i++) {
            switch (BG_CASE(i, q)) {
            case 0:
                if (D_us_80180EEC == 1) {
                    prim->y0 = WIDE_POOL(0x14, WIDE_X ? 0 : 0x14);
                    prim->y2 = 0x4F;
                    prim->clut = 0x45;
                    prim->u0 = prim->u2 = 0xF1;
                    prim->u3 = prim->u1 = 0xF6;
                    prim->v0 = 0xCA;
                    prim->v2 = 0xFC;
                } else {
                    prim->y0 = 0x8E;
                    prim->y2 = 0xB4;
                    prim->clut = 0x46;
                    prim->u2 = prim->u0 = 0xF9;
                    prim->u3 = prim->u1 = 0xFD;
                    prim->v0 = 0xD9;
                    prim->v2 = 0xFE;
                }
                prim->priority = 0x40;
                prim->tpage = 0xE;
                break;
            case 1:
                PRED(prim) = 0x30;
                PGRN(prim) = 8;
                PBLU(prim) = 0x88;
                prim->y0 = 0xB4;
                prim->y2 = 0xBC;
                prim->type = PRIM_G4;
                prim->priority = 0x40;
                break;
            case 2:
            case 3:
                prim->y0 = 0xB5;
                prim->y2 = WIDE_POOL(0xE3, WIDE_X ? DISP_STAGE_H : 0xE3);
                prim->v0 = prim->v1 = 0;
                prim->v2 = prim->v3 = 0x2F;
                prim->tpage = 0xF;
                prim->priority = 0x42;
                break;
            case 4:
                prim->y0 = 0x14;
                prim->y2 = 0x74;
                prim->v0 = 0;
                prim->v2 = 0x5F;
                prim->tpage = 0xF;
                prim->clut = 0x3A;
                prim->priority = 0x44;
                PGREY(prim, 2) = PGREY(prim, 3) = 0x80;
                break;
            default:
                if (D_us_80180EEC == 0) {
                    temp = 0;
                    prim->clut = 0xFE;
                }
                if (D_us_80180EEC == 1) {
                    temp = 0x50;
                    prim->clut = 0x3B;
                }

                prim->y0 = j % 13 * 8 + temp;
                prim->y2 = prim->y0 + 8;
                prim->v0 = j % 13 * 8 + 0x98;
                prim->v2 = prim->v0 + 7;
                prim->tpage = 0xF;
                prim->priority = 0x46;
                j++;
                break;
            }
            prim->y1 = prim->y0;
            prim->y3 = prim->y2;
            prim->v1 = prim->v0;
            prim->v3 = prim->v2;
            prim = prim->next;
        }
    }
    g_api.UpdateAnim(NULL, NULL);

#ifdef PLATFORM_PSYZ
    q = self->step_s;
#endif
    prim = &g_PrimBuf[self->primIndex];
    for (i = 0; i < BG_COUNT(q); i++) {
        if (i == 2 || i == 3 || BG_MTN_EXTRA(i, q)) {
            prim->clut = self->animCurFrame & 0xFF;
        }
        prim->drawMode = DRAW_HIDE;
        prim = prim->next;
    }
    prim = &g_PrimBuf[self->primIndex];
    if (xMin < 0x100 + WIDE_X) {
        prim->x0 = prim->x2 = xMin;
        prim->x1 = prim->x3 = xMax;
        prim->drawMode = DRAW_DEFAULT;
        prim = prim->next;
        if (D_us_80180EEC != 1) {
            prim->x0 = prim->x2 = xMin;
            prim->x1 = prim->x3 = xMax;
            prim->drawMode = DRAW_COLORS;
        }
        prim = prim->next;
    } else {
        prim = prim->next;
        prim = prim->next;
    }
#ifdef PLATFORM_PSYZ
    BgMountain(prim, 0, xMin, xMax);
    prim = prim->next;
    BgMountain(prim, 1, xMin, xMax);
    prim = prim->next;
#else
    if (xMin < 0x80) {
        prim->drawMode = DRAW_DEFAULT;
        prim->x0 = prim->x2 = xMin;
        prim->u0 = prim->u2 = xMin & 0x7F;
        if (xMax < 0x80) {
            prim->x1 = prim->x3 = xMax;
            prim->u1 = prim->u3 = xMax & 0x7F;
            prim = prim->next;
            prim = prim->next;
        } else {
            prim->x1 = prim->x3 = 0x80;
            prim->u1 = prim->u3 = 0x7F;
            prim = prim->next;
            prim->x0 = prim->x2 = 0x80;
            prim->u0 = prim->u2 = 0;
            prim->x1 = prim->x3 = xMax;
            prim->u1 = prim->u3 = xMax & 0x7F;
            prim->drawMode = DRAW_DEFAULT;
            prim = prim->next;
        }
    } else {
        if (xMin < 0x100) {
            prim->x0 = prim->x2 = xMin;
            prim->x1 = prim->x3 = xMax;
            prim->u0 = prim->u2 = xMin & 0x7F;
            prim->u1 = prim->u3 = xMax & 0x7F;
            prim->drawMode = DRAW_DEFAULT;
        }
        prim = prim->next;
        prim = prim->next;
    }
#endif
    if (D_us_80180EEC == 1) {
        x0 = xMin;
        if (g_Tilemap.scrollX.i.hi > 0xC0) {
            x0 = D_us_80180ED4[self->params * 2] - g_Tilemap.scrollX.i.hi;
            if (self->params == 4) {
                if (g_Tilemap.scrollY.i.hi < 0x260) {
                    x0 += 0x50;
                }
                if (g_Tilemap.scrollY.i.hi < 0x80) {
                    x0 += 0x10;
                }
            }
        }
        if (xMax > 0xA0) {
            if (x0 < 0xF9) {
                if (x0 > 0xA0) {
                    prim->x0 = x0;
                    prim->u0 = x0 + 7;
                } else {
                    prim->x0 = 0xA0;
                    prim->u0 = 0xA7;
                }
                if (xMax > 0xF8) {
                    prim->x1 = 0xF8;
                    prim->u1 = 0xFF;
                } else {
                    prim->x1 = xMax;
                    prim->u1 = xMax + 7;
                }
                prim->x3 = prim->x1;
                prim->u3 = prim->u1;
                prim->x2 = prim->x0;
                prim->u2 = prim->u0;
                prim->drawMode = DRAW_COLORS;
                PGREY(prim, 0) = PGREY(prim, 1) =
                    ((g_GameTimer & 1) << 4) + 0x30;
            }
        }
    }
    prim = prim->next;
    if (D_us_80180EEC != 2) {
        for (i = 0; i < LEN(D_us_801D6340); i++) {
            D_us_801D6340[i] += 0x10;
            x0 = rsin(D_us_801D6340[i]) >> 10;
            x0 *= D_us_80180F28[i];
            D_us_801D635C[i] = x0 >> 3;
        }
#ifdef PLATFORM_PSYZ
        {
            s32 w = q * 16, g, r, nL = BgCloudsL(w), nR = BgCloudsR(w);

            for (g = 0; g < 3 + nL + nR; g++) {
                if (g < 3) {
                    xOffset = -0x10 + g * 0x7F;
                } else if (g < 3 + nL) {
                    xOffset = -0x10 - (g - 2) * 0x7F;
                } else {
                    xOffset = 0xEE + (g - 2 - nL) * 0x7F;
                }
                for (r = 0; r < BG_ROWS; r++, prim = prim->next) {
                    prim->u0 = prim->u2 = 0x80;
                    prim->u1 = prim->u3 = 0xFF;
                    prim->x0 = D_us_801D635C[r] + xOffset;
                    prim->x2 = D_us_801D635C[r + 1] + xOffset;
                    prim->x1 = prim->x0 + 0x80;
                    prim->x3 = prim->x2 + 0x80;
                    if (prim->x1 <= xMin || prim->x0 >= xMax) {
                        continue;
                    }
                    prim->drawMode = DRAW_DEFAULT;
                    if (xMin >= prim->x0) {
                        prim->x0 = xMin;
                        prim->u0 = MAX(0xFF - (prim->x1 - prim->x0), 0x80);
                    }
                    if (xMin >= prim->x2) {
                        prim->x2 = xMin;
                        prim->u2 = MAX(0xFF - (prim->x3 - prim->x2), 0x80);
                    }
                    if (prim->x1 > xMax) {
                        prim->u1 = MAX(0xFF - (prim->x1 - xMax), 0x80);
                        prim->x1 = xMax;
                    }
                    if (prim->x3 > xMax) {
                        prim->u3 = MAX(0xFF - (prim->x3 - xMax), 0x80);
                        prim->x3 = xMax;
                    }
                }
            }
        }
#else
        for (i = 0; i < 0x27; i++) {
            if (i < 13) {
                xOffset = -0x10;
            } else if (i < 0x1A) {
                xOffset = 0x6F;
            } else {
                xOffset = 0xEE;
            }
            prim->u0 = prim->u2 = 0x80;
            prim->u1 = prim->u3 = 0xFF;
            prim->x0 = D_us_801D635C[i % 13] + xOffset;
            prim->x2 = D_us_801D635C[i % 13 + 1] + xOffset;
            prim->x1 = prim->x0 + 0x80;
            prim->x3 = prim->x2 + 0x80;
            if (i < 0xD) {
                if (xMin < 0x70) {
                    prim->drawMode = DRAW_DEFAULT;
                    if (xMin >= (prim->x1 - 0x80)) {
                        prim->x0 = xMin;
                        prim->u0 = 0xFF - (prim->x1 - prim->x0);
                        if (prim->u0 < 0x80) {
                            prim->u0 = 0x80;
                        }
                    }
                    if (prim->x1 > xMax) {
                        prim->u1 = 0xFF - (prim->x1 - xMax);
                        if (prim->u1 < 0x80) {
                            prim->u1 = 0x80;
                        }
                        prim->x1 = xMax;
                    }
                    if (xMin >= (prim->x3 - 0x80)) {
                        prim->x2 = xMin;
                        prim->u2 = 0xFF - (prim->x3 - prim->x2);
                        if (prim->u0 < 0x80) {
                            prim->u0 = 0x80;
                        }
                    }
                    if (prim->x3 > xMax) {
                        prim->u3 = 0xFF - (prim->x3 - xMax);
                        if (prim->u3 < 0x80) {
                            prim->u3 = 0x80;
                        }
                        prim->x3 = xMax;
                    }
                }
            } else if (i < 0x1A) {
                if (xMax < 0x70) {
                    break;
                }
                if (xMin < 0xF0) {
                    prim->drawMode = DRAW_DEFAULT;
                    if (xMin >= prim->x1 - 0x80) {
                        prim->x0 = xMin;
                        prim->u0 = 0xFF - (prim->x1 - prim->x0);
                        if (prim->u0 < 0x80) {
                            prim->u0 = 0x80;
                        }
                    }
                    if (prim->x1 > xMax) {
                        prim->u1 = 0xFF - (prim->x1 - xMax);
                        if (prim->u1 < 0x80) {
                            prim->u1 = 0x80;
                        }
                        prim->x1 = xMax;
                    }
                    if (xMin >= prim->x3 - 0x80) {
                        prim->x2 = xMin;
                        prim->u2 = 0xFF - (prim->x3 - prim->x2);
                        if (prim->u2 < 0x80) {
                            prim->u2 = 0x80;
                        }
                    }
                    if (prim->x3 > xMax) {
                        prim->u3 = 0xFF - (prim->x3 - xMax);
                        if (prim->u3 < 0x80) {
                            prim->u3 = 0x80;
                        }
                        prim->x3 = xMax;
                    }
                }
            } else {
                if (xMin < 0x100) {
                    if (xMax < 0xF0) {
                        break;
                    }
                    prim->drawMode = DRAW_DEFAULT;
                    if (xMin >= prim->x1 - 0x80) {
                        prim->x0 = xMin;
                        prim->u0 = 0xFF - (prim->x1 - prim->x0);
                        if (prim->u0 < 0x80) {
                            prim->u0 = 0x80;
                        }
                    }
                    if (xMin >= prim->x3 - 0x80) {
                        prim->x2 = xMin;
                        prim->u2 = 0xFF - (prim->x3 - prim->x2);
                        if (prim->u0 < 0x80) {
                            prim->u0 = 0x80;
                        }
                    }
                }
            }
            prim = prim->next;
        }
#endif
    }
#ifdef PLATFORM_PSYZ
    {
        // the mountain segments past the 4:3 view
        s32 k, w = q * 16;

        for (k = -1; k >= -BgMtnL(w); k--, prim = prim->next) {
            BgMountain(prim, k, xMin, xMax);
        }
        for (k = 2; k <= BgMtnR(w) + 1; k++, prim = prim->next) {
            BgMountain(prim, k, xMin, xMax);
        }
    }
#endif
    g_GpuBuffers[0].draw.r0 = animParams->color.r;
    g_GpuBuffers[0].draw.g0 = animParams->color.g;
    g_GpuBuffers[0].draw.b0 = animParams->color.b;
    g_GpuBuffers[1].draw.r0 = animParams->color.r;
    g_GpuBuffers[1].draw.g0 = animParams->color.g;
    g_GpuBuffers[1].draw.b0 = animParams->color.b;
}

#define PrimLine(x) ((PrimLineG2*)(x))

void func_us_801B7188(Entity* self) {
    s32 blink;
    s16 yLenUnscaled, xLenUnscaled;
    s32 yVelUnscaled, xVelUnscaled;
    s16 lenScale;
    s16 speed;
    s16 t;
    s16 xMin, xMax;
    s32 xVel, yVel;
    s16 xLen, yLen;
    s16 x0, x1;
    s16 y0, y1;
    s16 angle;
    s16 posX, posY;
    s32 i;
    Primitive* prim;

    if (D_us_80180EEC != 1) {
        x0 = D_us_80180ED4[self->params * 2 + 0] - g_Tilemap.scrollX.i.hi;
        x1 = D_us_80180ED4[self->params * 2 + 1] - g_Tilemap.scrollX.i.hi;
        if (self->params == 4) {
            if (g_Tilemap.scrollY.i.hi < 0x260) {
                x0 += 0x50;
            }
            if (g_Tilemap.scrollY.i.hi < 0x80) {
                x0 += 0x10;
            }
            if (g_Tilemap.scrollY.i.hi > 0x190) {
                x1 = 0x7000;
            }
        }
        if (self->params == 2 && g_Tilemap.scrollY.i.hi > 0x1B0) {
            x0 -= 0x210;
            x1 = 0x7000;
        }
        if (x0 < -WIDE_X) {
            x0 = -WIDE_X;
        }
        xMax = 0xFF + WIDE_X;
        if ((x1 + 0x20) > -WIDE_X && x1 < 0x100 + WIDE_X) {
            if (x1 < -WIDE_X) {
                x1 = -WIDE_X;
            }
            if (x0 >= 0x100 + WIDE_X && x1 < 0xE0 + WIDE_X) {
                xMax = x1 + 0x20;
            }
        } else {
            x1 = 0x7000;
        }
        if (x0 > x1) {
            xMin = x1;
        } else {
            xMin = x0;
        }
        t = rsin((s16)g_Status.timerMinutes * 0x42) >> 10;
        t += 4;
        angle = D_us_80180E8C[t];
        xLenUnscaled = rcos(angle) >> 8;
        yLenUnscaled = -(rsin(angle) >> 8);
        xVelUnscaled = rcos(angle) * 16;
        yVelUnscaled = -(rsin(angle) * 16);
        lenScale = D_us_80180E9C[t];
        speed = 0x30;
        if (self->step == 0) {
            InitializeEntity(g_EInitSpawner);
            if (D_us_80180EEC == 0) {
                g_api.PlaySfx(SFX_RAIN_LOOP);
                self->primIndex = g_api.func_800EDB58(
                    PRIM_LINE_G2_ALT,
                    WIDE_POOL(0x80, 0x80 + MIN(WIDE_X, 0x80)));
#ifdef PLATFORM_PSYZ
                if (self->primIndex == -1) { // the 4:3 count of drops
                    self->primIndex =
                        g_api.func_800EDB58(PRIM_LINE_G2_ALT, 0x80);
                }
#endif
                if (self->primIndex == -1) {
                    DestroyEntity(self);
                    return;
                }
                xLen = (xLenUnscaled * lenScale) >> 4;
                yLen = (yLenUnscaled * lenScale) >> 4;
                xVel = xVelUnscaled * speed;
                yVel = yVelUnscaled * speed;
                prim = &g_PrimBuf[self->primIndex];
                i = 0;
                while (PrimLine(prim) != NULL) {
                    PrimLine(prim)->r0 = PrimLine(prim)->g0 = 8;
                    PrimLine(prim)->b0 = 0x4F;
                    PrimLine(prim)->r1 = PrimLine(prim)->g1 =
                        PrimLine(prim)->b1 = 0x4F;
                    PrimLine(prim)->x0 = BG_RAIN_X();
                    PrimLine(prim)->y0 = rand() & 0xFF;
                    PrimLine(prim)->xLength = xLen;
                    PrimLine(prim)->yLength = yLen;
                    PrimLine(prim)->preciseX.i.hi = PrimLine(prim)->x1 =
                        PrimLine(prim)->xLength + PrimLine(prim)->x0;
                    PrimLine(prim)->preciseY.i.hi = PrimLine(prim)->y1 =
                        PrimLine(prim)->yLength + PrimLine(prim)->y0;
                    PrimLine(prim)->velocityX.val = xVel;
                    PrimLine(prim)->velocityY.val = yVel;
                    PrimLine(prim)->priority = 0x48;
                    PrimLine(prim)->drawMode = DRAW_DEFAULT;

                    i++;

                    if (PrimLine(prim)->next == NULL) {
                        self->ext.et_801B7188.unk80 = prim;
                    }
                    prim = (Primitive*)PrimLine(prim)->next;
                }
            }
            if (D_us_80180EEC == 2) {
#ifdef PLATFORM_PSYZ
                self->step_s = WIDE_COLS;
                self->primIndex =
                    g_api.AllocPrimitives(PRIM_GT4, FOG_ROWS * FOG_COLS(self));
                if (self->primIndex == -1) { // the 4:3 grid
                    self->step_s = 0;
                    self->primIndex = g_api.AllocPrimitives(PRIM_GT4, 0x24);
                }
#else
                self->primIndex = g_api.AllocPrimitives(PRIM_GT4, 0x24);
#endif
                if (self->primIndex == -1) {
                    DestroyEntity(self);
                    return;
                }
                D_us_801D6334[3] = D_us_801D6328[0] = 0x1000;
                D_us_801D6334[4] = D_us_801D6328[1] = 0xCCC;
                D_us_801D6334[0] = D_us_801D6328[2] = 0x999;
                D_us_801D6334[1] = D_us_801D6328[3] = 0x666;
                D_us_801D6334[2] = D_us_801D6328[4] = 0x333;
                i = 0;
#ifdef PLATFORM_PSYZ
                posX =
                    -(g_Tilemap.scrollX.i.hi * 5) / 4 - FOG_LEAD(self) * 0x90;
                while (posX < -0x90 - FOG_LEAD(self) * 0x90) {
                    posX += 0x90;
                }
#else
                posX = -(g_Tilemap.scrollX.i.hi * 5) / 4;
                while (posX < -0x90) {
                    posX += 0x90;
                }
#endif

                posY = -(g_Tilemap.scrollY.i.hi * 5) / 4;
                while (posY < -0x14A) {
                    posY += 0x14A;
                }

                prim = &g_PrimBuf[self->primIndex];

                while (prim != NULL) {
                    x0 =
                        (rsin(D_us_801D6328[(FOG_ROW(i, self) + 0) % 5]) >> 9) -
                        8 + posX;
                    x1 =
                        (rsin(D_us_801D6328[(FOG_ROW(i, self) + 1) % 5]) >> 9) -
                        8 + posX;
                    y0 = (rcos(D_us_801D6334[(FOG_ROW(i, self) + 0) % 5]) >>
                          10) -
                         8 + posY;
                    y1 = (rcos(D_us_801D6334[(FOG_ROW(i, self) + 1) % 5]) >>
                          10) -
                         8 + posY;
                    prim->x0 = (FOG_COL(i, self) * 0x90) + x0;
                    prim->x2 = (FOG_COL(i, self) * 0x90) + x1;
                    prim->x1 = prim->x0 + 0x90;
                    prim->x3 = prim->x2 + 0x90;
                    prim->y1 = prim->y0 = (FOG_ROW(i, self) * 0x42) + y0;
                    prim->y3 = prim->y2 = (FOG_ROW(i, self) * 0x42 + 0x42) + y1;
                    prim->u0 = prim->u2 = 2;
                    prim->v0 = prim->v1 = 0x39;
                    prim->u1 = prim->u3 = 0x7E;
                    prim->v2 = prim->v3 = 0x78;
                    prim->tpage = 0xF;
                    prim->clut = 0x40;
                    prim->priority = 0xE0;
                    prim->drawMode = DRAW_TPAGE | DRAW_TRANSP;
                    i++;
                    prim = prim->next;
                }
            }
            self->flags |= FLAG_HAS_PRIMS;
        }
        if (D_us_80180EEC == 0) {
            i = 0;
            blink = g_GameTimer & 3;
            xLen = (xLenUnscaled * lenScale) >> 4;
            yLen = (yLenUnscaled * lenScale) >> 4;
            xVel = xVelUnscaled * speed;
            yVel = yVelUnscaled * speed;

            prim = &g_PrimBuf[self->primIndex];
            while (PrimLine(prim) != NULL) {
                if ((i &= 3) != blink) {
                    PrimLine(prim)->drawMode = DRAW_HIDE;
                } else {
                    PrimLine(prim)->drawMode = DRAW_COLORS;
                    PrimLine(prim)->preciseX.i.hi = PrimLine(prim)->x0;
                    PrimLine(prim)->preciseY.i.hi = PrimLine(prim)->y0;
                    PrimLine(prim)->preciseX.val +=
                        PrimLine(prim)->velocityX.val;
                    PrimLine(prim)->preciseY.val +=
                        PrimLine(prim)->velocityY.val;
                    PrimLine(prim)->x0 = PrimLine(prim)->preciseX.i.hi;
                    PrimLine(prim)->y0 = PrimLine(prim)->preciseY.i.hi;
                    PrimLine(prim)->drawMode = DRAW_COLORS;
                    if (PrimLine(prim)->x0 < xMin) {
                        PrimLine(prim)->drawMode = DRAW_HIDE;
                    }
                    if (PrimLine(prim)->x1 > xMax) {
                        PrimLine(prim)->drawMode = DRAW_HIDE;
                    }
                    if (PrimLine(prim)->y0 > 0x100) {
                        PrimLine(prim)->drawMode = DRAW_HIDE;
                        PrimLine(prim)->x0 = BG_RAIN_X();
                        PrimLine(prim)->y0 -= 0x120;
                        PrimLine(prim)->xLength = xLen;
                        PrimLine(prim)->yLength = yLen;
                        PrimLine(prim)->velocityX.val = xVel;
                        PrimLine(prim)->velocityY.val = yVel;
                    }
                    PrimLine(prim)->preciseX.i.hi = PrimLine(prim)->x1 =
                        PrimLine(prim)->xLength + PrimLine(prim)->x0;
                    PrimLine(prim)->preciseY.i.hi = PrimLine(prim)->y1 =
                        PrimLine(prim)->yLength + PrimLine(prim)->y0;
                }
                i++;
                prim = (Primitive*)PrimLine(prim)->next;
            }
            prim = self->ext.et_801B7188.unk80;
            prim->drawMode = DRAW_DEFAULT;
            prim->x0 = prim->y0 = prim->x1 = prim->y1 = 0;
        }
        if (D_us_80180EEC == 2) {
            for (i = 0; i < 5; i++) {
                D_us_801D6328[i] += 16;
                D_us_801D6334[i] += 16;
            }
#ifdef PLATFORM_PSYZ
            posX = -(g_Tilemap.scrollX.i.hi * 5) / 4 - FOG_LEAD(self) * 0x90;
            while (posX < -0x90 - FOG_LEAD(self) * 0x90) {
                posX += 0x90;
            }
#else
            posX = -(g_Tilemap.scrollX.i.hi * 5) / 4;
            while (posX < -0x90) {
                posX += 0x90;
            }
#endif
            posY = -(g_Tilemap.scrollY.i.hi * 5) / 4;
            while (posY < -0x14A) {
                posY += 0x14A;
            }
            i = 0;
            prim = &g_PrimBuf[self->primIndex];
            while (prim != NULL) {
                angle = (FOG_ROW(i, self) + 0) % 5;
                x0 = (rsin(D_us_801D6328[angle]) >> 9) - 8 + posX;
                y0 = (rcos(D_us_801D6334[angle]) >> 10) - 8 + posY;
                angle = (FOG_ROW(i, self) + 1) % 5;
                x1 = (rsin(D_us_801D6328[angle]) >> 9) - 8 + posX;
                y1 = (rcos(D_us_801D6334[angle]) >> 10) - 8 + posY;
                prim->x0 = (FOG_COL(i, self) * 0x90) + x0;
                prim->x2 = (FOG_COL(i, self) * 0x90) + x1;
                prim->x1 = prim->x0 + 0x90;
                prim->x3 = prim->x2 + 0x90;
                prim->y1 = prim->y0 = (FOG_ROW(i, self) * 0x42) + y0;
                prim->y3 = prim->y2 = (FOG_ROW(i, self) * 0x42 + 0x42) + y1;
                i++;
                prim = prim->next;
            }
        }
    }
}

void func_us_801B7CC4(Entity* self) {
    if (self->step == 0) {
        g_api.PlaySfx(SET_RELEASE_RATE_HIGH_20_21);
        self->step++;
    }
    DestroyEntity(self);
}

void func_us_801B7D24(Entity* self) {}

void func_us_801B7D2C(Entity* self) {}
