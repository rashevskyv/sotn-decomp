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
#include <stdio.h>
#include <string.h>

static s32 ClampTile(s32 t, s32 n) { return t < 0 ? 0 : t >= n ? n - 1 : t; }

// The cells of the wide view outside the room (or layer). The loops below
// skip them, as on the PS1. bg 0 is the foreground, 1 is bg layer 0: those
// two take tiles from the apron patch (src/pc/apron.c). A cell the patch
// does not cover, or a layer without a patch, repeats the nearest edge tile
// of the layer: rock walls look right, an open doorway at the edge smears.
static void ApronPass(TilemapRenderer* r, SPRT_16* sp16, s32 bg, u16* layout,
                      s32 startTx, s32 startTy, s32 subX, s32 subY) {
    s32 i, j, tx, ty, x0, y0;
    u16 tile, clut;

    if (!WIDE_COLS && !WIDE_ROWS) {
        return;
    }
    y0 = r->y - subY - WIDE_ROWS * TILE_SIZE;
    for (i = -WIDE_ROWS; i < N_VERTI_TILES + WIDE_ROWS; i++, y0 += TILE_SIZE) {
        ty = startTy + i;
        x0 = r->x - subX - WIDE_COLS * TILE_SIZE;
        for (j = -WIDE_COLS; j < N_HORIZ_TILES + WIDE_COLS;
             j++, x0 += TILE_SIZE) {
            tx = startTx + j;
            if (tx >= 0 && tx < r->roomTileWidth && ty >= 0 &&
                ty < r->roomTileHeight) {
                continue;
            }
            tile = bg <= 1 ? Apron_Tile(bg, tx, ty) : 0xFFFF;
            if (tile == 0xFFFF) {
                tile = layout[ClampTile(ty, r->roomTileHeight) *
                                  r->roomTileWidth +
                              ClampTile(tx, r->roomTileWidth)];
            }
            if (tile == 0 || tile >= 0x1000 || g_GpuUsage.sp16 >= MAXSPRT16) {
                continue;
            }
            clut = g_ClutIds[r->clut[tile]];
            if (r->clutAlt) {
                clut = g_ClutIds[0x100 + r->clut[tile]];
            }
            sp16->clut = clut;
            sp16->x0 = x0;
            sp16->y0 = y0;
            sp16->u0 = (u8)(r->gfx[tile] << 4);
            sp16->v0 = r->gfx[tile] & 0xF0;
            setSemiTrans(sp16, r->isSemiTrans);
            setShadeTex(sp16, true);
            *r->sp16 = *sp16;
            addPrim(r->order + r->page[tile] + r->ot, r->sp16);
            r->sp16++;
            g_GpuUsage.sp16++;
        }
    }
}

extern u16 g_TileVram[512 * 512]; // sim_pc.c: VRAM x 512..1023, y 0..511

// 1 if a tile has a texel that is not transparent (colour 0). A tile on a
// page outside the copy counts as opaque. Cached per tile set and layer.
static bool TileOpaque(s32 bg, const TileDefinition* td, u16 tile, s32 alt) {
    static const TileDefinition* set[2];
    static s32 setAlt[2];
    static u8 known[2][0x1000]; // 0 not known, 1 transparent, 2 opaque
    const u16* clut = &g_Clut[0][0];
    s32 tp, gi, pal, vx, vy, u, y;

    if (!tile || tile >= 0x1000) {
        return false;
    }
    if (set[bg] != td || setAlt[bg] != alt) {
        set[bg] = td;
        setAlt[bg] = alt;
        memset(known[bg], 0, sizeof(known[bg]));
    }
    if (!known[bg][tile]) {
        tp = td->gfxPage[tile] + 8;
        gi = td->gfxIndex[tile];
        pal = (td->clut[tile] + (alt ? 0x100 : 0)) * 16;
        vx = (tp & 15) * 64 - 512;
        vy = ((tp >> 4) & 1) * 256 + (gi & 0xF0);
        known[bg][tile] = vx < 0 ? 2 : 1;
        for (y = 0; y < 16 * 16 && known[bg][tile] == 1; y++) {
            u = (gi & 0xF) * 16 + (y & 15);
            if (clut[pal + ((g_TileVram[(vy + (y >> 4)) * 512 + vx + (u >> 2)] >>
                             ((u & 3) * 4)) & 15)]) {
                known[bg][tile] = 2;
            }
        }
    }
    return known[bg][tile] == 2;
}

// 1 if the wide view draws a texel in a cell, on the fg or on bg layer 0
// when it lies on the fg (same size, same scroll). A cell takes the patch,
// else the room cell; outside the room the nearest edge cell (ApronPass).
static bool CellOpaque(s32 tx, s32 ty) {
    Tilemap* t = &g_Tilemap;
    BgLayer* bg = &g_BgLayers[0];
    s32 w = t->hSize * 16, h = t->vSize * 16;
    u16 tile = Apron_Tile(0, tx, ty);

    if (tile == 0xFFFF) {
        tile = t->fg[ClampTile(ty, h) * w + ClampTile(tx, w)];
    }
    if (TileOpaque(0, t->tileDef, tile, t->flags & LAYER_CLUT_ALT)) {
        return true;
    }
    if (!bg->tileDef || !bg->layout || !(bg->flags & LAYER_SHOW) ||
        (bg->flags & LAYER_WRAP_BG) || bg->w != t->hSize ||
        bg->h != t->vSize || bg->scrollX.i.hi != t->scrollX.i.hi ||
        bg->scrollY.i.hi != t->scrollY.i.hi) {
        return false;
    }
    tile = Apron_Tile(1, tx, ty);
    if (tile == 0xFFFF) {
        tile = bg->layout[ClampTile(ty, h) * w + ClampTile(tx, w)];
    }
    return TileOpaque(1, bg->tileDef, tile, bg->flags & LAYER_CLUT_ALT);
}

// The PS1 clips a stage to the rows 20..226 and keeps the camera 4 px below
// the room top and 12 px above its bottom, so it never shows the room rows
// above y 24 or in the last 26 px. The wide view opens the clip (wide.c
// StageClip) and shows them. A see-through cell there, with an opaque cell
// between it and the rows the PS1 shows (a ceiling, a floor), is a hole to
// the sky behind: the wide view draws it black, as the PS1 border was. Where
// the PS1 shows sky in that column, the sky goes on. A patch cell wins. vt
// and vb: the first and the last row the PS1 shows.
static bool IsHole(s32 tx, s32 ty, s32 vt, s32 vb) {
    s32 step = ty < vt ? 1 : -1, end = ty < vt ? vt : vb;
    if ((ty >= vt && ty <= vb) || Apron_Tile(0, tx, ty) != 0xFFFF ||
        Apron_Tile(1, tx, ty) != 0xFFFF || CellOpaque(tx, ty)) {
        return false;
    }
    for (ty += step; ty != end + step; ty += step) {
        if (CellOpaque(tx, ty)) {
            return true;
        }
    }
    return false;
}

#define HOLE_TOP (24 / TILE_SIZE)
#define HOLE_BOTTOM(h) (((h) * TILE_SIZE - 26) / TILE_SIZE)

// The holes in view, one black TILE per run, at the fg order.
static void HolePass(TilemapRenderer* r, s32 startTx, s32 startTy, s32 subX,
                     s32 subY) {
    s32 vt = HOLE_TOP, vb = HOLE_BOTTOM(r->roomTileHeight);
    s32 i, j, ty, run, y0;
    TILE* t;

    if (!WIDE_X && !WIDE_Y) {
        return;
    }
    y0 = r->y - subY - WIDE_ROWS * TILE_SIZE;
    for (i = -WIDE_ROWS; i < N_VERTI_TILES + WIDE_ROWS; i++, y0 += TILE_SIZE) {
        ty = startTy + i;
        if (ty >= vt && ty <= vb) {
            continue;
        }
        run = 0;
        for (j = -WIDE_COLS; j <= N_HORIZ_TILES + WIDE_COLS; j++) {
            if (j < N_HORIZ_TILES + WIDE_COLS &&
                IsHole(startTx + j, ty, vt, vb)) {
                run++;
                continue;
            }
            if (run && g_GpuUsage.tile < MAX_TILE_COUNT) {
                t = &g_CurrentBuffer->tiles[g_GpuUsage.tile++];
                setTile(t);
                setRGB0(t, 0, 0, 0);
                t->x0 = r->x - subX + (j - run) * TILE_SIZE;
                t->y0 = y0;
                t->w = run * TILE_SIZE;
                t->h = TILE_SIZE;
                addPrim(r->ot + r->order, t);
            }
            run = 0;
        }
    }
}

// The holes of the room, logged on each room load.
static void LogHoles(s32 layerIndex) {
    s32 w = g_Tilemap.hSize * 16, h = g_Tilemap.vSize * 16;
    s32 vt = HOLE_TOP, vb = HOLE_BOTTOM(h), tx, ty, top = 0, bottom = 0;

    if (!g_Tilemap.tileDef) {
        return;
    }
    for (tx = 0; tx < w; tx++) {
        for (ty = 0; ty < vt; ty++) {
            top += IsHole(tx, ty, vt, vb);
        }
        for (ty = vb + 1; ty < h; ty++) {
            bottom += IsHole(tx, ty, vt, vb);
        }
    }
    if (top || bottom) {
        fprintf(stderr, "[wide] layer %d: black cells %d top, %d bottom\n",
                layerIndex, top, bottom);
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
            HolePass(r, startTx, startTy, subX, subY);
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
    LogHoles(layerIndex);
#endif
}
