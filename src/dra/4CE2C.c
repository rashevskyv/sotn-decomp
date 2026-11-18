// SPDX-License-Identifier: AGPL-3.0-or-later
//! PSPO=4,p
#include "dra.h"
#include "dra_bss.h"
#include <scratchpad.h>

void HideAllBackgroundLayers(void) {
    s32 i;

    g_Tilemap.flags = 0;
    for (i = 0; i < MAX_BG_LAYER_COUNT; i++) {
        g_BgLayers[i].flags = 0;
    }
}

// internal structure that holds all the information to render room layers
typedef struct {
    /* 0x00 */ u16 x;
    /* 0x02 */ u16 y;
    /* 0x04 */ u16 flags;
    /* 0x06 */ u16 clutAlt;
    /* 0x08 */ u16 order;
    /* 0x0A */ u16 isSemiTrans;
    /* 0x0C */ u16 tpage;
    /* 0x0E */ u16 roomTileWidth;
    /* 0x10 */ u16 roomTileHeight;
    /* 0x12 */ u16 pad12;
    /* 0x14 */ u16* tiles;
    /* 0x18 */ u8* page;
    /* 0x1C */ u8* gfx;
    /* 0x20 */ u8* clut;
    /* 0x24 */ OT_TYPE* ot;
    /* 0x28 */ SPRT_16* sp16;
    /* 0x2C */ DR_MODE* dr;
    /* 0x30 */ RECT rect;
} TilemapRenderer;

#ifdef PLATFORM_PSYZ
#include "../pc/wide.h"
#include <math.h>
#include <psyz/gpu.h>
#include <psyz/video.h>

static s32 ClampTile(s32 t, s32 n) { return t < 0 ? 0 : t >= n ? n - 1 : t; }

// The draw mode of a page slot with the rectangle flip of the later GPUs
// (GP0(E1h) bits 12, 13; psyz reads the texture backwards).
static void FlipMode(TilemapRenderer* r, s32 page, s32 flip) {
    u16 tpage = page + 8 + r->tpage;

    if (r->flags & LAYER_TPAGE_ALT) {
        tpage += 0x80;
    }
    SetDrawMode(r->dr, 0, 0, tpage, &r->rect);
    r->dr->code[0] |= flip << 12;
    addPrim(r->order + page + r->ot, r->dr);
    r->dr++;
    g_GpuUsage.drawModes++;
}

// The cells of the wide view outside the room (or layer). The loops below
// skip them, as on the PS1. bg 0 is the foreground, 1 is bg layer 0: those
// two take tiles from the apron patch (src/pc/apron.c), and draw here the
// patch cells inside the room with a flipped art. A cell the patch does
// not cover, or a layer without a patch, shows the mirror image of the
// row about the room edge (Wide_MirrorColumn), its art flipped; a row
// above or below the layer repeats the nearest row.
// A flipped tile is a SPRT_16 under a flip draw mode. In its OT slot the
// last added is drawn first: pass f (the flip of its tiles) adds the mode
// without flip before the first tile of a slot and the flip mode after
// the last one, so no other prim sees the flip.
static void ApronPass(TilemapRenderer* r, SPRT_16* sp16, s32 bg, u16* layout,
                      s32 startTx, s32 startTy, s32 subX, s32 subY) {
    s32 i, j, tx, ty, x0, y0, in, flip, pass, page;
    s32 patched = bg <= 1 && Apron_Has(bg);
    u32 slots;
    u16 tile, clut, *row;

    if (!WIDE_COLS && !WIDE_ROWS && !patched) {
        return;
    }
    for (pass = 0; pass < 4; pass++) {
        slots = 0;
        y0 = r->y - subY - WIDE_ROWS * TILE_SIZE;
        for (i = -WIDE_ROWS; i < N_VERTI_TILES + WIDE_ROWS;
             i++, y0 += TILE_SIZE) {
            ty = startTy + i;
            x0 = r->x - subX - WIDE_COLS * TILE_SIZE;
            for (j = -WIDE_COLS; j < N_HORIZ_TILES + WIDE_COLS;
                 j++, x0 += TILE_SIZE) {
                tx = startTx + j;
                in = tx >= 0 && tx < r->roomTileWidth && ty >= 0 &&
                     ty < r->roomTileHeight;
                tile = patched ? Apron_Tile(bg, tx, ty) : 0xFFFF;
                if (in && (tile == 0xFFFF || !(tile & APRON_FLIP))) {
                    continue; // the room loop draws it
                }
                if (tile != 0xFFFF) {
                    flip = tile >> 12;
                    tile &= APRON_TILE;
                } else {
                    row = layout +
                          ClampTile(ty, r->roomTileHeight) * r->roomTileWidth;
                    flip = 0;
                    tile = row[tx >= 0 && tx < r->roomTileWidth
                                   ? tx
                                   : Wide_MirrorColumn(r->roomTileWidth, tx,
                                                       &flip)];
                }
                if (flip != pass || tile == 0 || tile >= 0x1000 ||
                    g_GpuUsage.sp16 >= MAXSPRT16) {
                    continue;
                }
                page = r->page[tile];
                if (flip && (page >= 32 || !(slots >> page & 1))) {
                    // keep 2 modes for this slot, 24 + 2 for the layer
                    // modes and the vignette
                    if (page >= 32 ||
                        g_GpuUsage.drawModes + 2 + 24 + 2 > MAX_DRAW_MODES) {
                        continue;
                    }
                    slots |= 1 << page;
                    FlipMode(r, page, 0);
                }
                clut = g_ClutIds[r->clut[tile]];
                if (r->clutAlt) {
                    clut = g_ClutIds[0x100 + r->clut[tile]];
                }
                sp16->clut = clut;
                sp16->x0 = x0;
                sp16->y0 = y0;
                // flipped: the texture is read from the far texel back
                sp16->u0 = (u8)(r->gfx[tile] << 4) + (flip & 1 ? 15 : 0);
                sp16->v0 = (r->gfx[tile] & 0xF0) + (flip & 2 ? 15 : 0);
                setSemiTrans(sp16, r->isSemiTrans);
                setShadeTex(sp16, true);
                *r->sp16 = *sp16;
                addPrim(r->order + page + r->ot, r->sp16);
                r->sp16++;
                g_GpuUsage.sp16++;
            }
        }
        for (page = 0; page < 32; page++) {
            if (slots >> page & 1) {
                FlipMode(r, page, pass);
            }
        }
    }
}

// A backdrop one screen wide (or high) behind a larger room. On the PS1 it
// fills the 256x240 view and stays still; a repeat or an edge column looks
// wrong, so stretch it over the wide view, one quad per tile.
static bool StretchBackdrop(TilemapRenderer* r, BgLayer* bg) {
    s32 sx = WIDE_X && bg->w == 1 && g_Tilemap.hSize > 1;
    s32 sy = WIDE_Y && bg->h == 1 && g_Tilemap.vSize > 1;
    s32 vw = DISP_STAGE_W + 2 * WIDE_X, vh = DISP_STAGE_H + 2 * WIDE_Y;
    s32 tx, ty, x, y, xa, xb, ya, yb;
    u16 tile, tpage;
    u8 u, v;
    POLY_GT4* p; // a GT4 slot stays GT4: menu and map code reuse it

    if ((!sx && !sy) || (r->flags & LAYER_WRAP_BG)) {
        return false;
    }
    for (ty = 0; ty < r->roomTileHeight; ty++) {
        y = ty * TILE_SIZE - bg->scrollY.i.hi;
        ya = sy ? y * vh / DISP_STAGE_H - WIDE_Y : y;
        yb = sy ? (y + TILE_SIZE) * vh / DISP_STAGE_H - WIDE_Y : y + TILE_SIZE;
        if (yb <= -WIDE_Y || ya >= DISP_STAGE_H + WIDE_Y) {
            continue;
        }
        for (tx = 0; tx < r->roomTileWidth; tx++) {
            x = tx * TILE_SIZE - bg->scrollX.i.hi;
            xa = sx ? x * vw / DISP_STAGE_W - WIDE_X : x;
            xb = sx ? (x + TILE_SIZE) * vw / DISP_STAGE_W - WIDE_X
                    : x + TILE_SIZE;
            tile = bg->layout[ty * r->roomTileWidth + tx];
            if (xb <= -WIDE_X || xa >= DISP_STAGE_W + WIDE_X || tile == 0 ||
                g_GpuUsage.gt4 >= MAX_POLY_GT4_COUNT) {
                continue;
            }
            u = r->gfx[tile] << 4;
            v = r->gfx[tile] & 0xF0;
            tpage = r->page[tile] + 8 + r->tpage;
            if (r->flags & LAYER_TPAGE_ALT) {
                tpage += 0x80;
            }
            p = &g_CurrentBuffer->polyGT4[g_GpuUsage.gt4++];
            setPolyGT4(p);
            setSemiTrans(p, r->isSemiTrans);
            setShadeTex(p, true);
            setRGB0(p, 0x80, 0x80, 0x80);
            setRGB1(p, 0x80, 0x80, 0x80);
            setRGB2(p, 0x80, 0x80, 0x80);
            setRGB3(p, 0x80, 0x80, 0x80);
            p->x0 = p->x2 = xa + r->x;
            p->x1 = p->x3 = xb + r->x;
            p->y0 = p->y1 = ya + r->y;
            p->y2 = p->y3 = yb + r->y;
            // u8: the last texel column (row) of a page ends at 0xFF
            p->u0 = p->u2 = u;
            p->u1 = p->u3 = u == 0xF0 ? 0xFF : u + TILE_SIZE;
            p->v0 = p->v1 = v;
            p->v2 = p->v3 = v == 0xF0 ? 0xFF : v + TILE_SIZE;
            p->tpage = tpage;
            p->clut = g_ClutIds[r->clut[tile] + (r->clutAlt ? 0x100 : 0)];
            addPrim(r->order + r->page[tile] + r->ot, p);
        }
    }
    return true;
}

// The wide view outside the camera range: a room, or a lock of the camera,
// narrower than the view shows the room beyond the range or the edge fill
// of ApronPass there. Then the view darkens outside the central 256 px
// (the PS1 view) toward the screen edges, in the psyz darken mode (the
// picture times 1 - red/255, so the hue stays): darkness
// d(t) = 0.95 * (1 - (1 - t)^2.5), t 0 at the 4:3 edge, 1 at the screen
// edge, as gouraud strips. A GP0 command of the port switches the mode on
// and off in the OT. It fades in and out over 30 frames, below the HUD
// (0x1EE) and the dialogue (0x1FC), above the stage layers and entities.
#define VIGNETTE_OT 0x1E0
#define VIGNETTE_FADE 30
#define VIGNETTE_STRIPS 12 // a side
#define VIGNETTE_GP0 0x10  // a NOP on the PS1 GPU
static int VignetteGp0(const u_long* words, int n, void* user) {
    Psyz_VideoSetDarken(words[0] & 1);
    return 1;
}

static void VignetteMode(TilemapRenderer* r, s32 on) {
    setlen(r->dr, 1);
    r->dr->code[0] = VIGNETTE_GP0 << 24 | on;
    addPrim(&r->ot[VIGNETTE_OT], r->dr);
    r->dr++;
    g_GpuUsage.drawModes++;
}

// darkness at x of the w px margin, of 0xFF, times fade
static s32 VignetteDark(s32 x, s32 w, s32 fade) {
    float t = (float)x / w;
    return (s32)(0xFF * 0.95f * (1.0f - powf(1.0f - t, 2.5f)) + 0.5f) * fade /
           VIGNETTE_FADE;
}

static void WideVignette(TilemapRenderer* r) {
    static s32 fade, init;
    s32 edge[2], side, k, a, b, ca, cb, xa, xb, y0, y1;
    POLY_G4* p;

    if (!init) {
        init = 1;
        Psyz_GpuRegisterCommandHandler(VIGNETTE_GP0, VignetteGp0, NULL);
    }

    edge[0] = MAX(g_Tilemap.x, 0) - g_Tilemap.scrollX.i.hi + r->x;
    edge[1] = MIN(g_Tilemap.width, g_Tilemap.hSize * 256) -
              g_Tilemap.scrollX.i.hi + r->x;
    if (g_WideX && (g_Tilemap.flags & LAYER_SHOW) &&
        edge[1] - edge[0] < DISP_STAGE_W + 2 * g_WideX) {
        fade = MIN(fade + 1, VIGNETTE_FADE);
    } else {
        fade = MAX(fade - 1, 0);
    }
    if (!fade || g_GpuUsage.drawModes + 2 > MAX_DRAW_MODES) {
        return;
    }
    // the last added is drawn first in its slot: on, the quads, off
    VignetteMode(r, 0);
    y0 = -WIDE_Y;
    y1 = DISP_STAGE_H + WIDE_Y;
    for (side = 0; side < 2; side++) {
        for (k = 0; k < VIGNETTE_STRIPS; k++) {
            // a..b px from the 4:3 edge, outward
            a = g_WideX * k / VIGNETTE_STRIPS;
            b = g_WideX * (k + 1) / VIGNETTE_STRIPS;
            if (b <= a || g_GpuUsage.g4 >= MAX_POLY_G4_COUNT) {
                continue;
            }
            ca = VignetteDark(a, g_WideX, fade);
            cb = VignetteDark(b, g_WideX, fade);
            xa = side ? DISP_STAGE_W + a : -b; // the left x of the strip
            xb = side ? DISP_STAGE_W + b : -a;
            p = &g_CurrentBuffer->polyG4[g_GpuUsage.g4++];
            setPolyG4(p);
            setSemiTrans(p, true);
            p->x0 = p->x2 = xa;
            p->x1 = p->x3 = xb;
            p->y0 = p->y1 = y0;
            p->y2 = p->y3 = y1;
            setRGB0(p, side ? ca : cb, side ? ca : cb, side ? ca : cb);
            setRGB2(p, side ? ca : cb, side ? ca : cb, side ? ca : cb);
            setRGB1(p, side ? cb : ca, side ? cb : ca, side ? cb : ca);
            setRGB3(p, side ? cb : ca, side ? cb : ca, side ? cb : ca);
            addPrim(&r->ot[VIGNETTE_OT], p);
        }
    }
    VignetteMode(r, 1);
}
#endif

void RenderTilemap(void) {
    s32 i, j;
    s32 l;
    BgLayer* bg;
    s32 subX, subY;
    s32 row;
    u16 tpage;
    u16 tile;
    u16 clut;
    s32 x0, y0;
    s32 tx, ty;
    s32 startTx, startTy;
    u16 page;
    u8 u, v;

#ifdef PLATFORM_PSYZ
    // the apron patch may also cover cells inside the room: graphics only,
    // collision keeps reading the room layout (src/pc/apron.c)
    s32 patched;
    u16 patch;
    SPRT_16 _sp16;
    TilemapRenderer _r;
    SPRT_16* sp16 = &_sp16;
    TilemapRenderer* r = &_r;

    if (g_PcStageLayerRenderer != NULL && g_PcStageLayerRenderer()) {
        return;
    }
#else
    SPRT_16* sp16 = ((SPRT_16*)SP(0x00));
    TilemapRenderer* r = ((TilemapRenderer*)SP(sizeof(SPRT_16)));
#endif

    Tilemap* t = &g_Tilemap;
    r->ot = g_CurrentBuffer->ot;
    r->sp16 = &g_CurrentBuffer->sprite16[g_GpuUsage.sp16];
    r->dr = &g_CurrentBuffer->drawModes[g_GpuUsage.drawModes];
    r->rect.x = 0;
    r->rect.y = 0;
    r->rect.w = 255;
    r->rect.h = 255;
    setSprt16(sp16);
    r->x = g_cameraOffsetX;
    r->y = g_cameraOffsetY;
    if (t->hideTimer > 0) {
        t->hideTimer--;
    } else {
        r->flags = t->flags;
        if (r->flags & LAYER_SHOW) {
            r->clutAlt = r->flags & LAYER_CLUT_ALT;
            r->page = t->tileDef->gfxPage;
            r->gfx = t->tileDef->gfxIndex;
            r->clut = t->tileDef->clut;
            r->order = t->order;
            r->roomTileWidth = t->hSize * 16;
            r->roomTileHeight = t->vSize * 16;
            subX = t->scrollX.i.hi & 0xF;
            startTx = t->scrollX.i.hi >> 4;
            subY = t->scrollY.i.hi & 0xF;
            startTy = t->scrollY.i.hi >> 4;
            r->tpage = 0;
            r->isSemiTrans = false;
            if (r->flags & LAYER_SEMI_TRANS) {
                r->isSemiTrans = true;
                r->tpage = r->flags & LAYER_TPAGE_ID;
            }
#ifdef PLATFORM_PSYZ
            patched = Apron_Has(0);
#endif
            y0 = r->y - subY - WIDE_ROWS * TILE_SIZE;
            for (i = -WIDE_ROWS; i < N_VERTI_TILES + WIDE_ROWS;
                 i++, y0 += TILE_SIZE) {
                x0 = r->x - subX - WIDE_COLS * TILE_SIZE;
                ty = startTy + i;
                if (ty < 0) {
                    continue;
                }
                if (ty >= r->roomTileHeight) {
                    break;
                }
                row = ty * r->roomTileWidth;
                r->tiles = &t->fg[startTx + row];
                for (j = -WIDE_COLS, tx = startTx - WIDE_COLS;
                     j < N_HORIZ_TILES + WIDE_COLS; j++, x0 += TILE_SIZE, tx++) {
                    if (tx < 0) {
                        continue;
                    }
                    if (tx >= r->roomTileWidth) {
                        break;
                    }
#ifdef PLATFORM_PSYZ
                    if (WIDE_COLS) {
                        // the skip above does not move the pointer
                        r->tiles = &t->fg[tx + row];
                    }
#endif

                    tile = *r->tiles++;
#ifdef PLATFORM_PSYZ
                    if (patched &&
                        (patch = Apron_Tile(0, tx, ty)) != 0xFFFF) {
                        if (patch & APRON_FLIP) {
                            continue; // ApronPass draws it
                        }
                        tile = patch;
                    }
#endif
                    if (tile == 0) {
                        continue;
                    }
                    u = r->gfx[tile] << 0x4;
                    v = r->gfx[tile] & 0xF0;
                    page = r->page[tile];
                    clut = g_ClutIds[r->clut[tile]];
                    if (r->clutAlt) {
                        clut = g_ClutIds[0x100 + r->clut[tile]];
                    }
                    if (g_GpuUsage.sp16 >= MAXSPRT16) {
                        continue;
                    }
                    sp16->clut = clut;
                    sp16->x0 = x0;
                    sp16->y0 = y0;
                    sp16->u0 = u;
                    sp16->v0 = v;
                    setSemiTrans(sp16, r->isSemiTrans);
                    setShadeTex(sp16, true);
                    *r->sp16 = *sp16;
                    addPrim(r->order + page + r->ot, r->sp16);
                    r->sp16++;
                    g_GpuUsage.sp16++;
                }
            }
#ifdef PLATFORM_PSYZ
            ApronPass(r, sp16, 0, t->fg, startTx, startTy, subX, subY);
#endif

            for (i = 0; i < 24; i++) {
                if (g_GpuUsage.drawModes >= MAX_DRAW_MODES) {
                    break;
                }
                tpage = i + 8 + r->tpage;
                if (r->flags & LAYER_TPAGE_ALT) {
                    tpage += 0x80;
                }
                SetDrawMode(r->dr, 0, 0, tpage, &r->rect);
                AddPrim(&r->ot[i + r->order], r->dr);
                r->dr++;
                g_GpuUsage.drawModes++;
            }
        }
    }

    for (l = 0, bg = g_BgLayers; l < MAX_BG_LAYER_COUNT; l++, bg++) {
        if (bg->hideTimer > 0) {
            bg->hideTimer--;
            continue;
        }
        r->flags = bg->flags;
        if ((r->flags & LAYER_SHOW) == 0) {
            continue;
        }
        r->clutAlt = r->flags & LAYER_CLUT_ALT;
        r->page = bg->tileDef->gfxPage;
        r->gfx = bg->tileDef->gfxIndex;
        r->clut = bg->tileDef->clut;
        r->order = bg->order;
        r->roomTileWidth = bg->w * 16;
        r->roomTileHeight = bg->h * 16;
        subX = bg->scrollX.i.hi & 0xF;
        startTx = bg->scrollX.i.hi >> 4;
        subY = bg->scrollY.i.hi & 0xF;
        startTy = bg->scrollY.i.hi >> 4;
        if (r->flags & LAYER_WRAP_BG) {
            startTx &= TILE_SIZE - 1;
            startTy &= TILE_SIZE - 1;
        }
        r->tpage = 0;
        r->isSemiTrans = false;
        if (r->flags & LAYER_SEMI_TRANS) {
            r->isSemiTrans = true;
            r->tpage = r->flags & LAYER_TPAGE_ID;
        }
#ifdef PLATFORM_PSYZ
        if (StretchBackdrop(r, bg)) {
            goto draw_modes;
        }
        patched = l == 0 && !(r->flags & LAYER_WRAP_BG) && Apron_Has(1);
#endif
        y0 = r->y - subY - WIDE_ROWS * TILE_SIZE;
        for (i = -WIDE_ROWS; i < N_VERTI_TILES + WIDE_ROWS;
             i++, y0 += TILE_SIZE) {
            x0 = r->x - subX - WIDE_COLS * TILE_SIZE;
            ty = startTy + i;
            if (r->flags & LAYER_WRAP_BG) {
                ty &= TILE_SIZE - 1;
            }
            if (ty < 0) {
                continue;
            }
            if (ty >= r->roomTileHeight) {
                break;
            }
            row = ty * r->roomTileWidth;
            r->tiles = &bg->layout[startTx + row];
            for (j = -WIDE_COLS, tx = startTx - WIDE_COLS;
                 j < N_HORIZ_TILES + WIDE_COLS; j++, x0 += TILE_SIZE, tx++) {
                if (r->flags & LAYER_WRAP_BG) {
                    tx &= 0xF;
                    r->tiles = &bg->layout[tx + row];
                }
                if (tx < 0) {
                    continue;
                }
                if (tx >= r->roomTileWidth) {
                    break;
                }
#ifdef PLATFORM_PSYZ
                if (WIDE_COLS) {
                    r->tiles = &bg->layout[tx + row];
                }
#endif

                tile = *r->tiles++;
#ifdef PLATFORM_PSYZ
                if (patched && (patch = Apron_Tile(1, tx, ty)) != 0xFFFF) {
                    if (patch & APRON_FLIP) {
                        continue; // ApronPass draws it
                    }
                    tile = patch;
                }
#endif
                if (tile == 0) {
                    continue;
                }
                u = r->gfx[tile] << 0x4;
                v = r->gfx[tile] & 0xF0;
                page = r->page[tile];
                clut = g_ClutIds[r->clut[tile]];
                if (r->clutAlt) {
                    clut = g_ClutIds[0x100 + r->clut[tile]];
                }
                if (g_GpuUsage.sp16 >= MAXSPRT16) {
                    continue;
                }
                sp16->clut = clut;
                sp16->x0 = x0;
                sp16->y0 = y0;
                sp16->u0 = u;
                sp16->v0 = v;
                setSemiTrans(sp16, r->isSemiTrans);
                setShadeTex(sp16, true);
                *r->sp16 = *sp16;
                addPrim(r->order + page + r->ot, r->sp16);
                r->sp16++;
                g_GpuUsage.sp16++;
            }
        }
#ifdef PLATFORM_PSYZ
        if (!(r->flags & LAYER_WRAP_BG)) {
            ApronPass(r, sp16, l + 1, bg->layout, startTx, startTy, subX, subY);
        }
    draw_modes:
#endif
        for (i = 0; i < 24; i++) {
            if (g_GpuUsage.drawModes >= MAX_DRAW_MODES) {
                break;
            }
            tpage = i + 8 + r->tpage;
            if (r->flags & LAYER_TPAGE_ALT) {
                tpage += 0x80;
            }
            SetDrawMode(r->dr, 0, 0, tpage, &r->rect);
            AddPrim(&r->ot[i + r->order], r->dr);
            r->dr++;
            g_GpuUsage.drawModes++;
        }
    }
#ifdef PLATFORM_PSYZ
    WideVignette(r);
#endif
}

static void SetRoomForegroundLayer(LayerDef* layerDef) {
    D_8003C708.flags = 0;
    g_canRevealMap = true; // Default to allowing revealing map
    g_Tilemap.tileDef = layerDef->tileDef;
    g_Tilemap.flags = 0;
    if (g_Tilemap.tileDef == 0) {
        return;
    }

    g_Tilemap.fg = layerDef->layout;
    g_Tilemap.order = layerDef->zPriority;
    if (layerDef->rect.params & LAYOUT_RECT_PARAMS_UNKNOWN_40) {
        g_Tilemap.order = 0x60;
        D_8003C708.flags = layerDef->rect.params;
        D_8003C708.unk2 = 0;
        D_8003C708.unk4 = 0;
        D_8003C708.zPriority = layerDef->zPriority;
    }
    if (layerDef->rect.params & LAYOUT_RECT_PARAMS_UNKNOWN_20) {
        g_Tilemap.order = 0x60;
        D_8003C708.flags = layerDef->rect.params;
    }
    // If this flag is set, disable revealing on map.
    // Used for hidden rooms including the "running through trees" at start,
    // and for the Nightmare stage.
    if (layerDef->rect.params & LAYOUT_RECT_PARAMS_HIDEONMAP) {
        g_Tilemap.order = 0x60;
        g_canRevealMap = 0;
    };
    g_Tilemap.flags = layerDef->flags;
    g_Tilemap.left = layerDef->rect.left;
    g_Tilemap.top = layerDef->rect.top;
    g_Tilemap.right = layerDef->rect.right;
    g_Tilemap.bottom = layerDef->rect.bottom;
    g_Tilemap.hSize = g_Tilemap.right - g_Tilemap.left + 1;
    g_Tilemap.vSize = g_Tilemap.bottom - g_Tilemap.top + 1;
    g_Tilemap.y = 0;
    g_Tilemap.x = 0;
    g_Tilemap.width = g_Tilemap.hSize << 8;
    g_Tilemap.height = g_Tilemap.vSize << 8;
    g_Tilemap.hideTimer = 1;
}

static void SetRoomBackgroundLayer(s32 index, LayerDef* layerDef) {
    g_BgLayers[index].flags = 0;
    g_BgLayers[index].tileDef = layerDef->tileDef;
    g_BgLayers[index].layout = layerDef->layout;
    if (g_BgLayers[index].tileDef != 0) {
        g_BgLayers[index].order = layerDef->zPriority;
        g_BgLayers[index].flags = layerDef->flags;
        g_BgLayers[index].w = layerDef->rect.right - layerDef->rect.left + 1;
        g_BgLayers[index].h = layerDef->rect.bottom - layerDef->rect.top + 1;
        g_BgLayers[index].scrollKind = layerDef->rect.params;
        g_BgLayers[index].hideTimer = 1;
    }
}

void LoadRoomLayer(s32 layerIndex) {
    s32 i;

    SetRoomForegroundLayer(g_api.o.tileLayers[layerIndex].fg);
    SetRoomBackgroundLayer(0, g_api.o.tileLayers[layerIndex].bg);

    for (i = 1; i < MAX_BG_LAYER_COUNT; i++) {
        g_BgLayers[i].flags = 0;
    }
#ifdef PLATFORM_PSYZ
    Apron_OnRoomLayer(layerIndex);
#endif
}
