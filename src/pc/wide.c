// SPDX-License-Identifier: AGPL-3.0-or-later
// Wide view, always 16:9. The player picks an integer scale. In stage mode
// psyz draws a canvas of (screen / scale) game pixels; the stage code reads
// the extra pixels per side in g_WideX and g_WideY. Other display modes
// show the PS1 frame at the largest integer scale that fits.
#include <game.h>
#include <psyz/video.h>
#include <stdio.h>
#include <string.h>
#include "wide.h"

s32 g_WideX;
s32 g_WideY;
s32 g_WideBandTop, g_WideBandBottom;
static int band_clip; // Wide_BandClip narrowed the stage clip
static int cfg_scale; // 0: off, the psyz default output (PC)
static const char* ini_path; // NULL: settings are not saved (PC)
char g_IniSpawn[48];
static PsyzSize last_disp;
static int last_fbw, last_fbh;

int Wide_GetScale(void) { return cfg_scale; }

void Wide_Config(int scale) {
    cfg_scale = scale;
    last_disp.w = last_disp.h = 0; // Wide_OnFrame applies it again
    Psyz_VideoSetAspectMode(PSYZ_ASPECT_SQUARE); // 1 game px = k x k px
}

static const char ini_head[] =
    "; Castlevania: SOTN native port settings.\n"
    "; The port menu (L3+R3 in game) changes them and writes this file.\n"
    "; Lines are key = value. A line that starts with ; is a comment.\n"
    "\n"
    "; scale: size of one game pixel on the screen, a whole number.\n"
    "; The view is the screen size divided by the scale:\n"
    ";   handheld 1280x720: 3 = 426x240, 2 = 640x360\n"
    ";   docked 1920x1080:  4 = 480x270, 3 = 640x360, 2 = 960x540\n"
    "; The game picks the nearest value that fits the screen.\n";

static const char ini_voice[] =
    "\n"
    "; voice: ps1 = PS1 English, jp = Japanese, psp = PSP English.\n"
    "; jp and psp need the clips in voice/jp and voice/psp\n"
    "; (tools/voicepack/extract_voice.py). A missing clip plays PS1 English.\n";

static const char ini_language[] =
    "\n"
    "; language: en = English, uk = Ukrainian. A change needs a restart.\n";

static const char ini_cheats[] =
    "\n"
    "; cheats, 1 = on, 0 = off. The port menu page CHEATS switches them.\n"
    "; Use a spare save: a save keeps the items, relics and map.\n"
    "; cheat_refill: HP, MP and hearts stay full.\n"
    "; cheat_invincible: hits do nothing.\n"
    "; cheat_walls: mist form passes every wall, mist does not end, no MP drain.\n"
    "; cheat_items: all weapons, items and relics.\n"
    "; cheat_map: the whole map of both castles explored (the room count too).\n"
    "; cheat_skip_prologue: a new game starts as Alucard at the castle gate.\n"
    "; cheat_character: who plays a loaded save, and the first card of the\n"
    "; Player select of a new game (the pick wins). 0 = the game's own rule,\n"
    "; 1 = Alucard; 2 = Richter; 4 = Maria (PSP). 3 (Richter, Saturn)\n"
    "; and 5 (Maria, Saturn) are kept for later. A save keeps its own owner.\n";

static const char ini_buttons[] =
    "\n"
    "; buttons: the button marks of the menus: switch, xbox or psx.\n";

int g_JpCards; // JP_CARDS_EXTRA
const char* const g_JpCardsNames[JP_CARDS_MODES] = {"extra", "jp", "off"};
static const char ini_jp_cards[] =
    "\n"
    "; jp_cards: the two JP familiar cards. extra = keep all US items and\n"
    "; add the two cards next to them, jp = the JP/PSP placement (they\n"
    "; replace two US items), off = US (28 relics). Read at boot.\n";

int g_RelicToggle; // 0 modern
static const char* const relic_toggle_names[] = {"modern", "stock"};
static const char ini_relic_toggle[] =
    "\n"
    "; relic_toggle: the relic switch picture: modern or stock (ON/OFF).\n";

// key = word: the index of word in names, or the default with a log line
static int IniWord(const char* key, const char* word, const char* const* names,
                   int n, int def) {
    int i;
    for (i = 0; i < n; i++) {
        if (!strcmp(word, names[i])) {
            return i;
        }
    }
    fprintf(stderr, "[ini] %s = %s: unknown, using %s\n", key, word, names[def]);
    return def;
}

static const char ini_editor[] =
    "\n"
    "; editor: 1 = the game serves the tile editor page on port 8080\n"
    "; (http://<console address>:8080/ in a PC browser on the same network).\n"
    "; No password: switch it on only on a trusted network.\n";

static const struct {
    const char* key;
    int* value;
} ini_flags[] = {
    {"cheat_refill", &g_Cheat.refill},
    {"cheat_invincible", &g_Cheat.invincible},
    {"cheat_walls", &g_Cheat.walls},
    {"cheat_items", &g_Cheat.items},
    {"cheat_map", &g_Cheat.map},
    {"cheat_skip_prologue",&g_Cheat.skipPrologue},
    {"cheat_character", &g_Cheat.character},
    {"editor", &g_EditorOn},
    {"overlay", &g_OverlayOn},
    {"fps", &g_FpsOn},
};

static const char ini_overlay[] =
    "\n"
    "; overlay: 1 = a corner shows the stage, room, map cell and the cheats.\n"
    "; fps: 1 = a corner shows the frames per second.\n";

static const char ini_spawn[] =
    "\n"
    "; spawn: start at a named point at boot, for tests. Empty = normal start.\n"
    "; Names: np3-stage15-door, marble-soul, caverns-soul and the reverse\n"
    "; castle ones: reverse-trapdoor-underground, reverse-marble-soul,\n"
    "; reverse-caverns-soul.\n";

void Wide_SaveIni(void) {
    FILE* f;
    int i;
    if (!ini_path || !(f = fopen(ini_path, "w"))) {
        return;
    }
    fputs(ini_head, f);
    fprintf(f, "scale = %d\n", cfg_scale);
    fputs(ini_voice, f);
    fprintf(f, "voice = %s\n", g_VoiceSetNames[g_VoiceSet]);
    fputs(ini_language, f);
    fprintf(f, "language = %s\n", g_LangNext ? "uk" : "en");
    fputs(ini_buttons, f);
    fprintf(f, "buttons = %s\n", g_ButtonSetNames[g_ButtonSet]);
    fputs(ini_jp_cards, f);
    fprintf(f, "jp_cards = %s\n", g_JpCardsNames[g_JpCards]);
    fputs(ini_relic_toggle, f);
    fprintf(f, "relic_toggle = %s\n", relic_toggle_names[g_RelicToggle]);
    fputs(ini_spawn, f);
    fprintf(f, "spawn = %s\n", g_IniSpawn);
    fputs(ini_cheats, f);
    for (i = 0; i < LEN(ini_flags); i++) {
        if (!strncmp(ini_flags[i].key, "cheat_", 6)) {
            fprintf(f, "%s = %d\n", ini_flags[i].key, *ini_flags[i].value);
        }
    }
    fputs(ini_editor, f);
    fprintf(f, "editor = %d\n", g_EditorOn);
    fputs(ini_overlay, f);
    fprintf(f, "overlay = %d\nfps = %d\n", g_OverlayOn, g_FpsOn);
    fclose(f);
}

// Reads the settings file, writes it with the defaults when it is missing.
// Keys: scale = N, voice = ps1|jp|psp, language = en|uk,
// buttons = switch|xbox|psx (missing: switch on the Switch, xbox on PC),
// jp_cards = extra|jp|off, relic_toggle = modern|stock,
// spawn = <spawn point name>, cheat_* = 0|1 (cheat_character 0..2, 4), editor, overlay, fps = 0|1.
// A missing key keeps its default (cheats, editor, overlay and fps off).
// The view is always 16:9: the aspect key of an old file does nothing.
void Wide_LoadIni(const char* path) {
    int scale = 3, v, i;
    char line[128], word[32];
    FILE* f = fopen(path, "r");

    ini_path = path;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, " scale = %d", &v) == 1) {
                if (v >= 1 && v <= 8) {
                    scale = v;
                }
            } else if (sscanf(line, " aspect = %15s", word) == 1) {
                // an old key: ignored
            } else if (sscanf(line, " voice = %15s", word) == 1) {
                for (v = 0; v < VOICE_COUNT; v++) {
                    if (!strcmp(word, g_VoiceSetNames[v])) {
                        g_VoiceSet = v;
                    }
                }
            } else if (sscanf(line, " language = %15s", word) == 1) {
                g_LangNext = !strcmp(word, "uk");
            } else if (sscanf(line, " jp_cards = %15s", word) == 1) {
                g_JpCards = IniWord("jp_cards", word, g_JpCardsNames,
                                    JP_CARDS_MODES, JP_CARDS_EXTRA);
            } else if (sscanf(line, " relic_toggle = %15s", word) == 1) {
                g_RelicToggle = IniWord("relic_toggle", word,
                                        relic_toggle_names, 2, 0);
            } else if (sscanf(line, " buttons = %15s", word) == 1) {
                for (v = 0; v < BUTTON_SETS; v++) {
                    if (!strcmp(word, g_ButtonSetNames[v])) {
                        g_ButtonSet = v;
                    }
                }
            } else if (sscanf(line, " %31[a-z_] = %d", word, &v) == 2) {
                for (i = 0; i < LEN(ini_flags); i++) {
                    if (!strcmp(word, ini_flags[i].key)) {
                        *ini_flags[i].value = v != 0;
                        if (ini_flags[i].value == &g_Cheat.character) {
                            // a value, not a flag; unknown values act as 0
                            g_Cheat.character = Cheat_CharacterNext(v, 0);
                        }
                    }
                }
            } else {
                sscanf(line, " spawn = %47s", g_IniSpawn);
            }
        }
        fclose(f);
    }
    fprintf(stderr, "[video] settings: scale %d\n", scale);
    fprintf(
        stderr, "[voice] settings: voice %s\n", g_VoiceSetNames[g_VoiceSet]);
    fprintf(stderr, "[lang] settings: language %s\n", g_LangNext ? "uk" : "en");
    fprintf(stderr, "[buttons] settings: buttons %s\n",
            g_ButtonSetNames[g_ButtonSet]);
    fprintf(stderr, "[relics] settings: jp_cards %s relic_toggle %s\n",
            g_JpCardsNames[g_JpCards], relic_toggle_names[g_RelicToggle]);
    fprintf(stderr,
            "[cheat] settings: refill %d invincible %d walls %d items %d "
            "map %d skip_prologue %d character %d editor %d overlay %d fps %d\n",
            g_Cheat.refill, g_Cheat.invincible, g_Cheat.walls, g_Cheat.items,
            g_Cheat.map, g_Cheat.skipPrologue, g_Cheat.character, g_EditorOn, g_OverlayOn, g_FpsOn);
    Wide_Config(scale);
    if (!f) {
        Wide_SaveIni();
        fprintf(stderr, "[video] %s missing: wrote the defaults\n", path);
    }
}

void Wide_Tile(TILE* tile) {
    int x = tile->x0, y = tile->y0, w = tile->w, h = tile->h;
    if (!g_WideX && !g_WideY) {
        return;
    }
    Wide_StretchRect(&x, &y, &w, &h, g_WideX, g_WideY);
    tile->x0 = x;
    tile->y0 = y;
    tile->w = w;
    tile->h = h;
}

// The bottom-corner name plate (enemy names on the right, picked-up items and
// gold on the left) moves from the PS1 corner to the same corner of the wide
// view. The plate keeps the side its caller picked.
void Wide_MoveCornerText(s32 prims, s32 left) {
    Primitive* p;
    s32 dx = left ? -g_WideX : g_WideX;

    if ((!g_WideX && !g_WideY) || prims < 0 || prims >= MAX_PRIM_COUNT) {
        return;
    }
    for (p = &g_PrimBuf[prims]; p; p = p->next) {
        p->x0 += dx;
        p->x1 += dx;
        p->x2 += dx;
        p->x3 += dx;
        p->y0 += g_WideY;
        p->y1 += g_WideY;
        p->y2 += g_WideY;
        p->y3 += g_WideY;
    }
}

// Display pixels (window or touch) to frame pixels, the coordinates of the
// port menu prims: 0,0 is the top-left of the 256x240 frame.
void Wide_ScreenToFrame(float sx, float sy, int* x, int* y) {
    PsyzRect g = Psyz_VideoGetDrawArea(), p = Psyz_VideoGetPresentArea();
    float pw = p.ex - p.sx, ph = p.ey - p.sy;
    *x = pw > 0 ? g.sx + (int)((sx - p.sx) * (g.ex - g.sx) / pw) : (int)sx;
    *y = ph > 0 ? g.sy + (int)((sy - p.sy) * (g.ey - g.sy) / ph) : (int)sy;
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// The largest 16:9 rectangle on the screen d.
static PsyzSize AspectRect(PsyzSize d) {
    if (d.w * 9 > d.h * 16) {
        d.w = d.h * 16 / 9;
    } else {
        d.h = d.w * 9 / 16;
    }
    return d;
}

// Scales that fit: the 256x240 frame must fit, and above 960x540 game
// pixels the tile budget runs out.
static void ScaleRange(PsyzSize req, int* lo, int* hi) {
    *hi = MAX(MIN(req.w / DISP_STAGE_W, req.h / DISP_STAGE_H), 1);
    *lo = MIN(MAX((req.w + 959) / 960, (req.h + 539) / 540), *hi);
}

// The stage clip (func_800E34DC) is set when a stage starts: rows 20..226
// with no margin, the whole frame in the wide view. The port menu scale can
// turn the wide view on in a stage, so open the PS1 clip then. The other way
// keeps the whole frame until the next stage start; the extra rows hold room
// tiles.
static void StageClip(void) {
    DRAWENV* d0 = &g_GpuBuffers[0].draw;
    DRAWENV* d1 = &g_GpuBuffers[1].draw;
    if ((g_WideX || g_WideY) && d0->clip.y == 0x14 && d0->clip.h == 0xCF &&
        d1->clip.y == 0x14 && d1->clip.h == 0xCF) {
        d0->clip.y = d1->clip.y = 0;
        d0->clip.h = d1->clip.h = DISP_STAGE_H;
    }
}

// The wide camera calls it on every frame with the art rows of the room on
// the screen. Only a stage with a band (Saturn) narrows the clip, for a room
// shorter than the view: black bars instead of the empty rows. The whole
// frame (0..240) is the wide clip, psyz extends it to the canvas.
void Wide_BandClip(int y0, int y1) {
    DRAWENV* d0 = &g_GpuBuffers[0].draw;
    DRAWENV* d1 = &g_GpuBuffers[1].draw;
    if ((!g_WideBandTop && !g_WideBandBottom && !band_clip) ||
        d0->clip.y != d1->clip.y) {
        return; // no band, or the two-page clip of 42398.c
    }
    if (!g_WideBandTop && !g_WideBandBottom) {
        y0 = 0, y1 = DISP_STAGE_H; // the stage changed: the wide clip again
    }
    y0 = Clamp(y0, 0, DISP_STAGE_H);
    y1 = Clamp(y1, y0, DISP_STAGE_H);
    band_clip = y0 > 0 || y1 < DISP_STAGE_H;
    d0->clip.y = d1->clip.y = y0;
    d0->clip.h = d1->clip.h = y1 - y0;
    if (band_clip) {
        // the frame clear covers only the clip: black out the rows outside
        // it, or they keep the last room's picture
        DRAWENV* d[] = {d0, d1};
        for (int i = 0; i < 2; i++) {
            RECT top = {d[i]->clip.x, 0, d[i]->clip.w, y0};
            RECT bottom = {d[i]->clip.x, y1, d[i]->clip.w, DISP_STAGE_H - y1};
            ClearImage(&top, 0, 0, 0);
            ClearImage(&bottom, 0, 0, 0);
        }
    }
}

// Called on every vsync. Follows dock changes and display mode changes.
void Wide_OnFrame(void) {
    PsyzSize d, req;
    int fbw, fbh, k, s, lo, hi;

    if (!cfg_scale) {
        return;
    }
    d = Psyz_VideoGetDisplaySize();
    fbw = g_GpuBuffers[0].disp.disp.w;
    fbh = g_GpuBuffers[0].disp.disp.h;
    if (fbw <= 0 || fbh <= 0) {
        fbw = DISP_STAGE_W;
        fbh = DISP_STAGE_H;
    }
    if (d.w <= 0 || d.h <= 0 ||
        (d.w == last_disp.w && d.h == last_disp.h && fbw == last_fbw &&
         fbh == last_fbh)) {
        return;
    }
    last_disp = d;
    last_fbw = fbw;
    last_fbh = fbh;

    req = AspectRect(d);
    ScaleRange(req, &lo, &hi);
    k = Clamp(cfg_scale, lo, hi);
    g_WideX = ((req.w / k & ~1) - DISP_STAGE_W) / 2;
    g_WideY = ((req.h / k & ~1) - DISP_STAGE_H) / 2;

    if (fbw == DISP_STAGE_W && fbh == DISP_STAGE_H) {
        s = k;
    } else {
        // title, menus, movies: the PS1 frame at the largest integer scale
        // that fits, nothing around it
        s = MAX(MIN(d.w / fbw, d.h / fbh), 1);
        req.w = fbw * s;
        req.h = fbh * s;
        if (fbw == DISP_TITLE_W && fbh == DISP_TITLE_H) {
            // title: the canvas is the whole display at that scale; the
            // title code (src/st/sel/title.c) stretches its background
            req = d;
        }
        if (fbw == DISP_MENU_W && fbh == DISP_MENU_H) {
            // pause menu: the 384 frame stays centred with black margins;
            // the whole display is the canvas, so the uk relic window may
            // reach past the frame (RELIC_WIN_X in src/dra/menu.c)
            req = d;
        }
    }
    StageClip();
    Psyz_VideoSetScale(s);
    Psyz_VideoSetResolution(req);
    fprintf(stderr,
            "[video] output %dx%d scale %d (asked %d) "
            "frame %dx%d internal %dx%d margin x %d y %d\n",
            d.w, d.h, s, cfg_scale, fbw, fbh, req.w / s & ~1,
            req.h / s & ~1, (int)g_WideX, (int)g_WideY);
}

// Port menu state.
void Wide_MenuGet(int* scale, int* lo, int* hi) {
    ScaleRange(AspectRect(Psyz_VideoGetDisplaySize()), lo, hi);
    *scale = cfg_scale ? Clamp(cfg_scale, *lo, *hi) : 0;
}

void Wide_MenuSet(int scale) {
    Wide_Config(scale);
    Wide_SaveIni();
}
