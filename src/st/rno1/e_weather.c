// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rno1.h"

static s16 fogAngles[] = {
    0xBC0, 0xBB0, 0xBA0, 0xB90, 0xB80, 0xB70, 0xB60, 0xB50};
static s16 fogScales[] = {24, 28, 32, 40, 48, 56, 64, 72};
static AnimationFrame rain_anim[] = {
    4, 0x3C, 4, 0xF8, 4, 0xF9, 4, 0xF8, 0, 0x00};
static AnimationFrame clear_anim[] = {
    4, 0xFF, 4, 0xFA, 4, 0xFB, 4, 0xFA, 0, 0x00};
typedef enum { WEATHER_RAIN, WEATHER_CLEAR, WEATHER_FOG } weatherModes;
static weatherModes weatherMode = 0; // doesn't make sense to init to rain
static u8 D_us_80180EF0[] = {0x60, 0x80, 0xC0, 0x80, 0x60};
static AnimParam animParams[] = {
    {ANIMSET_OVL(1), rain_anim, 44, {.r = 8, .g = 8, .b = 24}},
    {ANIMSET_OVL(2), clear_anim, 44, {.r = 16, .g = 8, .b = 72}},
    {ANIMSET_OVL(2), rain_anim, 5, {.r = 8, .g = 8, .b = 24}}};
static s16 fogMovement[] = {4, 5, 6, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2};
STATIC_PAD_DATA(1);

// bss
static s16 D_us_801D6328[6];
static s16 D_us_801D6334[6];
static s16 D_us_801D6340[14];
static s16 D_us_801D635C[14];
static s32 g_DebugWaitInfoTimer;

// Likely copied out of DRA, unused in RNO1
static void DebugShowWaitInfo(const char* msg) {
    g_CurrentBuffer = g_CurrentBuffer->next;
    FntPrint(msg);
    if (g_DebugWaitInfoTimer++ & 4) {
        FntPrint("\no\n");
    }
    DrawSync(0);
    VSync(0);
    PutDrawEnv(&g_CurrentBuffer->draw);
    PutDispEnv(&g_CurrentBuffer->disp);
    FntFlush(-1);
}

static void DebugInputWait(const char* msg) {
    while (PadRead(0))
        DebugShowWaitInfo(msg);
    while (!PadRead(0))
        DebugShowWaitInfo(msg);
}

#ifdef PLATFORM_PSYZ
// Wide view. The far background is drawn with prims fixed to the 4:3 screen:
// the mountains tile every 0x80 px, the clouds every 0x7F px and the fog every
// 0x90 px. Extra prims cover the side margins. The prim count is fixed at the
// spawn, so the margin of the spawn (WIDE_COLS, in 16 px) is kept in step_s.
#define BG_ROWS 13
// w = the margin in px; mountain tiles k = -1.. and 2.., cloud groups beyond
// the 3 of the 4:3 view (they span -16..366)
static s32 BgMtnL(s32 w) { return (w + 127) >> 7; }
static s32 BgMtnR(s32 w) { return ((255 + w) >> 7) - 1; }
static s32 BgCloudsL(s32 w) { return w > 13 ? (w - 13 + 126) / 127 : 0; }
static s32 BgCloudsR(s32 w) {
    return 256 + w > 363 ? (256 + w - 363 + 126) / 127 : 0;
}
// the prims of the background: the 4:3 ones, the extra cloud groups, then
// the extra mountain tiles (they are set up as prim 2)
static s32 BgTotal(s32 weather, s32 count, s32 q) {
    s32 w = q * 16;
    s32 n = count + BgMtnL(w) + BgMtnR(w);

    if (weather != WEATHER_FOG) {
        n += (BgCloudsL(w) + BgCloudsR(w)) * BG_ROWS;
    }
    return n;
}

// The fog grid has 9 rows. It has 4 columns of 0x90 px in the 4:3 view. A wide
// view needs `lead` columns more on the left and more on the right.
#define FOG_ROWS 9
static s32 BgFogLead(s32 q) { return (q * 16 + 0x8F) / 0x90; }
static s32 BgFogCols(s32 q) {
    s32 cols = 1 + BgFogLead(q) + (256 + q * 16 + 16 + 0x8F) / 0x90;

    return cols < 4 ? 4 : cols;
}
// a rain drop starts further out on both sides of the wide view
static s32 BgRainX(void) {
    if (WIDE_X > 0) {
        // one x over the whole wide view; 0x50 px more on the left for the
        // drift of the drops to the right
        return rand() % (0x100 + 2 * WIDE_X + 0x50) - WIDE_X - 0x50;
    }
    return (rand() & 0xFF) - (rand() & 0x3F) - 0x10;
}
#define BG_RAIN_X() BgRainX()
#define BG_COUNT(q) BgTotal(weatherMode, aP->count, q)
#define BG_MTN_EXTRA(i, q)                                                     \
    ((i) >= BG_COUNT(q) - BgMtnL((q) * 16) - BgMtnR((q) * 16))
#define BG_CASE(i, q) (BG_MTN_EXTRA(i, q) ? 2 : (i))
#define FOG_COLS(self) BgFogCols((self)->step_s)
#define FOG_ROW(i, self) ((i) / FOG_COLS(self))
#define FOG_COL(i, self) ((i) % FOG_COLS(self))
#define FOG_LEAD(self) BgFogLead((self)->step_s)
#else
#define BG_COUNT(q) (aP->count)
#define BG_CASE(i, q) (i)
#define BG_RAIN_X() ((rand() & 0xFF) - (rand() & 0x3F) - 0x10)
#define FOG_ROW(i, self) ((i) >> 2)
#define FOG_COL(i, self) ((i) & 3)
#endif
// the sky and the gradient run to the edges of the wide view
#define BG_XL(q) WIDE_POOL(0, -(q) * 16)
#define BG_XR(q) WIDE_POOL(0xFF, (q) ? 0x100 + (q) * 16 : 0xFF)

void EntityFog(Entity* self) {
    Primitive* prim;
    AnimParam* aP;
    s16 x0, x1;
    s16 xOffset;
    s16 xMin, xMax;
    s32 i, j;
    s16 temp;
#ifdef PLATFORM_PSYZ
    s32 q, m, k;
#endif

    aP = &animParams[weatherMode];
    if (self->step == 0) {
        InitializeEntity(g_EInitSpawner);
        weatherMode = g_CastleFlags[NO1_WEATHER] & 0x7F;
        g_CastleFlags[NO1_WEATHER] |= 0x80;
        aP = &animParams[weatherMode];
        g_api.func_800EA5E4(aP->animSet);
        self->anim = aP->anim;
        for (i = 0; i < LEN(D_us_801D6340); i++) {
            D_us_801D6340[i] = (i << 0xC) / 7;
        }
#ifdef PLATFORM_PSYZ
        q = WIDE_COLS;
        self->primIndex = g_api.AllocPrimitives(PRIM_GT4, BG_COUNT(q));
        if (self->primIndex == -1 && q) {
            q = 0; // no room for the margins: the 4:3 view only
            self->primIndex = g_api.AllocPrimitives(PRIM_GT4, aP->count);
        }
        self->step_s = q;
#else
        self->primIndex = g_api.AllocPrimitives(PRIM_GT4, aP->count);
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
                if (weatherMode == WEATHER_CLEAR) {
                    prim->y0 = WIDE_POOL(0xE3, WIDE_X ? DISP_STAGE_H : 0xE3);
                    prim->y2 = 0xA8;
                    prim->clut = 0x45;
                    prim->u0 = prim->u2 = 0xF1;
                    prim->u3 = prim->u1 = 0xF6;
                    prim->v0 = 0xCA;
                    prim->v2 = 0xFC;
                } else {
                    prim->y0 = 0x6C;
                    prim->y2 = 0x46;
                    prim->clut = 0x46;
                    prim->u2 = prim->u0 = 0xF9;
                    prim->u3 = prim->u1 = 0xFD;
                    prim->v0 = 0xD9;
                    prim->v2 = 0xFE;
                }
                prim->x0 = prim->x2 = BG_XL(q);
                prim->x1 = prim->x3 = BG_XR(q);
                prim->priority = 0x40;
                prim->drawMode = DRAW_DEFAULT;
                prim->tpage = 0xE;
                break;
            case 1:
                PRED(prim) = 0x30;
                PGRN(prim) = 8;
                PBLU(prim) = 0x88;
                prim->y0 = 0x46;
                prim->y2 = 0x3E;
                prim->type = PRIM_G4;
                prim->priority = 0x40;
                prim->x0 = prim->x2 = BG_XL(q);
                prim->x1 = prim->x3 = BG_XR(q);
                prim->drawMode = DRAW_COLORS;
                if (weatherMode == WEATHER_CLEAR) {
                    prim->drawMode = DRAW_HIDE;
                }
                break;
            case 2:
            case 3:
                prim->y2 = WIDE_POOL(0x14, WIDE_X ? 0 : 0x14);
                prim->y0 = 0x42;
                prim->v0 = prim->v1 = 0;
                prim->v2 = prim->v3 = 0x2F;
                prim->tpage = 0xF;
                prim->priority = 0x42;
                prim->drawMode = DRAW_DEFAULT;
                prim->u0 = prim->u2 = 0;
                prim->u1 = prim->u3 = 0x7F;
                if (i == 2) {
                    prim->x0 = prim->x2 = 0;
                    prim->x1 = prim->x3 = 0x80;
#ifdef PLATFORM_PSYZ
                } else if (i != 3) { // an extra tile, left or right
                    m = i - (BG_COUNT(q) - BgMtnL(q * 16) - BgMtnR(q * 16));
                    k = m < BgMtnL(q * 16) ? -1 - m : 2 + m - BgMtnL(q * 16);
                    prim->x0 = prim->x2 = k * 0x80;
                    prim->x1 = prim->x3 = k * 0x80 + 0x80;
#endif
                } else {
                    prim->x0 = prim->x2 = 0x80;
                    prim->x1 = prim->x3 = WIDE_POOL(0xFF, q ? 0x100 : 0xFF);
                }
                break;
            case 4:
                prim->y0 = 0xE3;
                prim->y2 = 0x83;
                prim->v0 = 0;
                prim->v2 = 0x5F;
                prim->tpage = 0xF;
                prim->clut = 0x3A;
                prim->priority = 0x44;
                prim->drawMode = DRAW_COLORS;
                PGREY(prim, 2) = PGREY(prim, 3) = 0x80;
                prim->x3 = prim->x1 = 8;
                prim->x2 = prim->x0 = 0x60;
                prim->u3 = prim->u1 = 0xFF;
                prim->u2 = prim->u0 = 0xA8;
                if (weatherMode != WEATHER_CLEAR) {
                    prim->drawMode = DRAW_HIDE;
                }
                break;
            default:
                if (weatherMode == WEATHER_RAIN) {
                    temp = 0xF5;
                    prim->clut = 0xFE;
                }
                if (weatherMode == WEATHER_CLEAR) {
                    temp = 0xA5;
                    prim->clut = 0x3B;
                }

                prim->y0 = temp - j % 13 * 8;
                prim->y2 = prim->y0 - 8;
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
    for (i = 0; i < 5; i++) {
        if (i == 2 || i == 3) {
            prim->clut = self->animCurFrame & 0xFF;
        }
        if (i == 4) {
            PGREY(prim, 0) = PGREY(prim, 1) = ((g_GameTimer & 1) << 4) + 0x30;
        }
        prim = prim->next;
    }

    if (weatherMode != WEATHER_FOG) {
        for (i = 0; i < LEN(D_us_801D6340); i++) {
            D_us_801D6340[i] += 0x10;
            x0 = rsin(D_us_801D6340[i]) >> 10;
            x0 *= fogMovement[i];
            D_us_801D635C[i] = x0 >> 3;
        }
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
            prim = prim->next;
        }
    }
#ifdef PLATFORM_PSYZ
    // prim is now the first of the extra prims (after the 4:3 ones)
    if (weatherMode != WEATHER_FOG) {
        // extra cloud groups: nL to the left, then nR to the right
        s32 nL = BgCloudsL(q * 16), nR = BgCloudsR(q * 16);

        for (m = 0; m < nL + nR; m++) {
            if (m < nL) {
                xOffset = -0x10 - (m + 1) * 0x7F;
            } else {
                xOffset = 0xEE + (m - nL + 1) * 0x7F;
            }
            for (k = 0; k < BG_ROWS; k++, prim = prim->next) {
                prim->u0 = prim->u2 = 0x80;
                prim->u1 = prim->u3 = 0xFF;
                prim->x0 = D_us_801D635C[k] + xOffset;
                prim->x2 = D_us_801D635C[k + 1] + xOffset;
                prim->x1 = prim->x0 + 0x80;
                prim->x3 = prim->x2 + 0x80;
            }
        }
    }
    // extra mountain tiles
    for (m = BgMtnL(q * 16) + BgMtnR(q * 16); m > 0; m--) {
        prim->clut = self->animCurFrame & 0xFF;
        prim = prim->next;
    }
#endif
    g_GpuBuffers[0].draw.r0 = aP->color.r;
    g_GpuBuffers[0].draw.g0 = aP->color.g;
    g_GpuBuffers[0].draw.b0 = aP->color.b;
    g_GpuBuffers[1].draw.r0 = aP->color.r;
    g_GpuBuffers[1].draw.g0 = aP->color.g;
    g_GpuBuffers[1].draw.b0 = aP->color.b;
    FntPrint("scr_y:%02x\n", g_Tilemap.scrollY.i.hi);
    FntPrint("scr_x:%02x\n", g_Tilemap.scrollX.i.hi);
    // Tenki is Japanese for "weather"
    FntPrint("tenki_w:%02x\n", weatherMode);
    FntPrint("flags_tenki:%2x\n", g_CastleFlags[NO1_WEATHER]);
}

#define PrimLine(x) ((PrimLineG2*)(x))

void EntityRain(Entity* self) {
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

    if (weatherMode != WEATHER_CLEAR) {
        t = rsin((s16)g_Status.timerMinutes * 66) >> 10;
        t += 4;
        angle = fogAngles[t];
        xLenUnscaled = rcos(angle) >> 8;
        yLenUnscaled = -(rsin(angle) >> 8);
        xVelUnscaled = rcos(angle) * 16;
        yVelUnscaled = -(rsin(angle) * 16);
        lenScale = fogScales[t];
        speed = 0x30;
        if (self->step == 0) {
            InitializeEntity(g_EInitSpawner);
            if (weatherMode == WEATHER_RAIN) {
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
                    PrimLine(prim)->xLength = -xLen;
                    PrimLine(prim)->yLength = -yLen;
                    PrimLine(prim)->preciseX.i.hi = PrimLine(prim)->x1 =
                        PrimLine(prim)->xLength + PrimLine(prim)->x0;
                    PrimLine(prim)->preciseY.i.hi = PrimLine(prim)->y1 =
                        PrimLine(prim)->yLength + PrimLine(prim)->y0;
                    PrimLine(prim)->velocityX.val = -xVel;
                    PrimLine(prim)->velocityY.val = -yVel;
                    PrimLine(prim)->priority = 0x48;
                    PrimLine(prim)->drawMode = DRAW_DEFAULT;

                    i++;

                    if (PrimLine(prim)->next == NULL) {
                        self->ext.et_801B7188.unk80 = prim;
                    }
                    prim = (Primitive*)PrimLine(prim)->next;
                }
            }
            if (weatherMode == WEATHER_FOG) {
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
                D_us_801D6334[3] = D_us_801D6328[0] = ROT(360);
                D_us_801D6334[4] = D_us_801D6328[1] = ROT(360 * 0.8);
                D_us_801D6334[0] = D_us_801D6328[2] = ROT(360 * 0.6);
                D_us_801D6334[1] = D_us_801D6328[3] = ROT(360 * 0.4);
                D_us_801D6334[2] = D_us_801D6328[4] = ROT(360 * 0.2);
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
                    x0 = (rsin(D_us_801D6328[(FOG_ROW(i, self) + 0) % 5]) >>
                          9) -
                         8 + posX;
                    x1 = (rsin(D_us_801D6328[(FOG_ROW(i, self) + 1) % 5]) >>
                          9) -
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
                    prim->u0 = prim->u2 = 0x7E;
                    prim->v0 = prim->v1 = 0x78;
                    prim->u1 = prim->u3 = 2;
                    prim->v2 = prim->v3 = 0x39;
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
        if (weatherMode == WEATHER_RAIN) {
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
                    if (PrimLine(prim)->y0 < 0) {
                        PrimLine(prim)->drawMode = DRAW_HIDE;
                        PrimLine(prim)->x0 = BG_RAIN_X();
                        PrimLine(prim)->y0 += 0x120;
                        PrimLine(prim)->xLength = -xLen;
                        PrimLine(prim)->yLength = -yLen;
                        PrimLine(prim)->velocityX.val = -xVel;
                        PrimLine(prim)->velocityY.val = -yVel;
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
        if (weatherMode == WEATHER_FOG) {
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

// One of these exists in every room adjacent to the main room.
// Seems like this makes the rain sound fade out as you walk
// into the new room (out of the rainy room)?
// If it is not raining, this has no impact, but still runs.
void EntityFadeRainSound(Entity* self) {
    if (self->step == 0) {
        g_api.PlaySfx(SET_RELEASE_RATE_HIGH_20_21);
        self->step++;
    }
    DestroyEntity(self);
}
