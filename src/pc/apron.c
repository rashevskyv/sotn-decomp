// SPDX-License-Identifier: AGPL-3.0-or-later
// Apron: extra tiles around a room, shown in the wide view where the room
// is smaller than the screen, and tiles drawn over room cells. Visual only:
// collision reads the room layer.
// One patch per stage, apron/<STAGE>.txt next to sotn.ini; a file on disk
// wins over the copy built into the program (tools/editor/bake_apron.py).
//
//   # sotn apron v1
//   L 12 fg        layer index 12 (the LoadRoomLayer argument), fg or bg
//   -3 5 01A7      tx ty tile (hex); tx, ty in layer tiles, may be negative
//
// RenderTilemap (4CE2C.c) asks Apron_Tile for the cells outside the room and,
// graphics only, for the cells inside it (fg and the first bg layer).
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#define AX 24 // grid margin in tiles, left and right
#define AY 10 // top and bottom
#define MAX_TEXT (1 << 20)

struct Cell {
    s16 layer, tx, ty;
    u16 tile;
    u8 bg;
};

int g_ApronLayer = -1;
static int s_stage = -1; // the stage of the loaded patch
static char* s_text;     // the patch text, as loaded or saved
static struct Cell* s_cells;
static int s_count, s_cap;
static u16* s_grid[2]; // fg, bg; 0xFFFF: no tile
static int s_gw[2], s_gh[2];

static void Parse(const char* text) {
    char line[128], kind[4];
    int layer = -1, bg = 0, tx, ty, n;
    unsigned tile;
    const char* p = text;
    s_count = 0;
    while (p && *p) {
        n = strcspn(p, "\n");
        snprintf(line, sizeof(line), "%.*s", n < 127 ? n : 127, p);
        p += n + (p[n] == '\n');
        if (sscanf(line, " L %d %3s", &layer, kind) == 2) {
            bg = strcmp(kind, "bg") == 0;
            continue;
        }
        if (layer < 0 || sscanf(line, " %d %d %x", &tx, &ty, &tile) != 3 ||
            tile >= 0x1000 || tx < -AX - 256 || tx > 512 || ty < -AY - 256 ||
            ty > 512) {
            continue; // comment, bad line or a tile outside the tile tables
        }
        if (s_count == s_cap) {
            s_cap = s_cap ? s_cap * 2 : 256;
            s_cells = realloc(s_cells, s_cap * sizeof(*s_cells));
        }
        s_cells[s_count].layer = layer;
        s_cells[s_count].bg = bg;
        s_cells[s_count].tx = tx;
        s_cells[s_count].ty = ty;
        s_cells[s_count].tile = tile;
        s_count++;
    }
}

static void SetText(const char* text, int len) {
    free(s_text);
    s_text = malloc(len + 1);
    memcpy(s_text, text, len);
    s_text[len] = 0;
    Parse(s_text);
}

const char* Apron_Path(char* buf, int size) {
    const char* name = Castle_StageName(g_StageId);
    if (name[0] == '?') {
        return NULL;
    }
    snprintf(buf, size, "apron/%s.txt", name);
    return buf;
}

static void LoadStage(void) {
    char path[32], name[32];
    const char* text = "";
    FILE* f;
    long len = 0;
    int i;

    s_stage = g_StageId;
    SetText("", 0);
    if (!Apron_Path(path, sizeof(path))) {
        return;
    }
    if ((f = fopen(path, "rb"))) {
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len > 0 && len < MAX_TEXT) {
            free(s_text);
            s_text = calloc(1, len + 1);
            len = fread(s_text, 1, len, f);
            Parse(s_text);
        }
        fclose(f);
        fprintf(stderr, "[apron] %s: %d cells (file)\n", path, s_count);
        return;
    }
    snprintf(name, sizeof(name), "%s", path + 6);
    for (i = 0; i < g_ApronBakedCount; i++) {
        if (!strcmp(g_ApronBaked[i].name, name)) {
            text = g_ApronBaked[i].text;
        }
    }
    SetText(text, strlen(text));
    if (s_count) {
        fprintf(stderr, "[apron] %s: %d cells (built in)\n", name, s_count);
    }
}

static void Build(void) {
    int b, i, w, h, x, y;
    for (b = 0; b < 2; b++) {
        free(s_grid[b]);
        s_grid[b] = NULL;
        if (b == 0 && g_Tilemap.tileDef) {
            w = g_Tilemap.hSize * 16;
            h = g_Tilemap.vSize * 16;
        } else if (b == 1 && g_BgLayers[0].tileDef) {
            w = g_BgLayers[0].w * 16;
            h = g_BgLayers[0].h * 16;
        } else {
            continue;
        }
        s_gw[b] = w + 2 * AX;
        s_gh[b] = h + 2 * AY;
        for (i = 0; i < s_count; i++) {
            x = s_cells[i].tx + AX;
            y = s_cells[i].ty + AY;
            if (s_cells[i].layer != g_ApronLayer || s_cells[i].bg != b ||
                x < 0 || x >= s_gw[b] || y < 0 || y >= s_gh[b]) {
                continue;
            }
            if (!s_grid[b]) {
                s_grid[b] = malloc(s_gw[b] * s_gh[b] * sizeof(u16));
                memset(s_grid[b], 0xFF, s_gw[b] * s_gh[b] * sizeof(u16));
            }
            s_grid[b][y * s_gw[b] + x] = s_cells[i].tile;
        }
    }
}

// Called at the end of LoadRoomLayer.
void Apron_OnRoomLayer(s32 layerIndex) {
    if (g_StageId != s_stage) {
        LoadStage();
    }
    g_ApronLayer = layerIndex;
    Build();
}

int Apron_Has(int bg) { return s_grid[bg != 0] != NULL; }

// The patch tile of a cell, or 0xFFFF for none.
u16 Apron_Tile(int bg, int tx, int ty) {
    u16* g = s_grid[bg != 0];
    int x = tx + AX, y = ty + AY;
    bg = bg != 0;
    if (!g || x < 0 || x >= s_gw[bg] || y < 0 || y >= s_gh[bg]) {
        return 0xFFFF;
    }
    return g[y * s_gw[bg] + x];
}

// The patch text of the loaded stage (the editor page reads it).
const char* Apron_Text(void) {
    if (g_StageId != s_stage) {
        LoadStage();
    }
    return s_text ? s_text : "";
}

// The editor page saves the whole patch of the loaded stage: the file is
// rewritten and the room shows it at once. Returns NULL or the error.
const char* Apron_Save(const char* text, int len) {
    char path[32], tmp[40];
    FILE* f;
    if (!Apron_Path(path, sizeof(path))) {
        return "this stage has no apron";
    }
    if (len < 0 || len >= MAX_TEXT) {
        return "patch too large";
    }
#if defined(_WIN32)
    _mkdir("apron");
#else
    mkdir("apron", 0755);
#endif
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f || fwrite(text, 1, len, f) != (size_t)len) {
        if (f) {
            fclose(f);
        }
        return "can not write the file";
    }
    if (fclose(f) != 0) {
        return "can not write the file";
    }
    remove(path); // rename over a file fails on some file systems
    if (rename(tmp, path) != 0) {
        return "can not rename the file";
    }
    s_stage = g_StageId;
    SetText(text, len);
    Build();
    fprintf(stderr, "[apron] %s saved: %d cells\n", path, s_count);
    return NULL;
}
