// SPDX-License-Identifier: AGPL-3.0-or-later
// Wide view, always 16:9. The player picks an integer scale. In stage mode
// psyz draws a canvas of (screen / scale) game pixels; the stage code reads
// the extra pixels per side in g_WideX. The stage keeps the PS1 rows: the
// clip of func_800E34DC (rows 20..226) touches only the left and right
// frame edges, so psyz widens it and the rows above and below stay black
// bars across the whole width, as on the PS1. g_WideY stays 0. Only these
// rows must fit, so docked shows the stage at scale 5 (384x216, rows
// 16..231). A frame without that clip and other display modes show the
// PS1 frame at the largest integer scale that fits (docked 4).
#include <game.h>
#include <psyz/input.h>
#include <psyz/video.h>
#include <stdio.h>
#include <string.h>
#include "wide.h"

s32 g_WideX;
s32 g_WideY; // always 0: no extra stage rows (the black bars)
s32 g_WideBandTop, g_WideBandBottom;
static int band_clip; // Wide_BandClip narrowed the stage clip
static int cfg_scale; // 0: off, the psyz default output (PC)
static const char* ini_path; // NULL: settings are not saved (PC)
char g_IniSpawn[48];
static PsyzSize last_disp;
static int last_fbw, last_fbh, last_rows;

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
    ";   docked 1920x1080:  5 = 384x216, 4 = 480x270, 3 = 640x360,\n"
    ";                      2 = 960x540\n"
    "; The game picks the nearest value that fits the screen (5 is 3 on\n"
    "; the handheld). The stage always shows the PS1 rows with black bars\n"
    "; above and below. At 5 the stage, map and dialogues use 5; the\n"
    "; title, pause menu and other full screens use 4.\n"
    "; max = always the highest scale that fits the current screen.\n";

int g_Fullscreen;
static const char ini_fullscreen[] =
    "\n"
    "; fullscreen: 1 = the game starts in borderless fullscreen, 0 = in a\n"
    "; window. F11 (or F4) switches it during the game, Esc leaves it.\n";

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
    "; buttons: the button marks of the menus: auto = as the gamepad in use\n"
    "; (keyboard only: the last gamepad, at first psx; the Switch: switch),\n"
    "; psx = PlayStation, xbox = Xbox, switch = Nintendo.\n";

// the PS buttons of the pad word, bit 0 first (psyz/input.h)
static const char* const ps_buttons[PSYZ_PAD_BUTTONS] = {
    "l2",  "r2", "l1", "r1",    "triangle", "circle", "cross", "square",
    "select", "l3", "r3", "start", "up", "right", "down", "left"};
static const char* const input_dev[2] = {"key", "pad"};

static const char ini_controls[] =
    "\n"
    "; key_<button>, pad_<button>: the keyboard key and the gamepad button of\n"
    "; each PS button (player 1). Keys by their SDL name: X, Return, Left\n"
    "; Shift, Up, Keypad 8. Gamepad buttons by position: a = bottom, b = right,\n"
    "; x = left, y = top face button; leftshoulder, rightshoulder,\n"
    "; lefttrigger, righttrigger, leftstick, rightstick, back, start, dpup,\n"
    "; dpdown, dpleft, dpright. none = no input. The port menu page CONTROLS\n"
    "; changes them; its RESET restores the defaults.\n";

// key_<button> = <name> or pad_<button> = <name>: 1 if the line is one
static int IniInput(const char* line) {
    char dev[4], button[16], name[48];
    int d, b, code, n;
    if (sscanf(line, " %3[a-z]_%15[a-z0-9] = %47[^\r\n]", dev, button,
               name) != 3) {
        return 0;
    }
    for (d = 0; d < 2 && strcmp(dev, input_dev[d]); d++) {
    }
    for (b = 0; b < PSYZ_PAD_BUTTONS && strcmp(button, ps_buttons[b]); b++) {
    }
    if (d == 2 || b == PSYZ_PAD_BUTTONS) {
        return 0;
    }
    for (n = strlen(name); n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t');
         n--) {
        name[n - 1] = 0;
    }
    code = Psyz_PadsInputFromName(d, name);
    if (code == PSYZ_INPUT_UNKNOWN) {
        fprintf(stderr, "[ini] %s_%s = %s: unknown, kept\n", dev, button, name);
    } else {
        Psyz_PadsMap(d, b, code);
    }
    return 1;
}

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

int g_HdTextures = 1; // data/textures PNG files replace pictures
int g_HdDump;         // write the sampled pictures to data/textures/dump

static const char ini_hd[] =
    "\n"
    "; hd_textures: 1 = PNG files in data/textures/<language>/ and\n"
    "; data/textures/common/ replace pictures; 0 = the original pictures.\n"
    "; hd_dump: 1 = write every picture the game draws to data/textures/dump/\n"
    "; (for artists; slow; stock art: do not share the files).\n";

static const struct {
    const char* key;
    int* value;
} ini_flags[] = {
    {"hd_textures", &g_HdTextures},
    {"hd_dump", &g_HdDump},
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
    {"fullscreen", &g_Fullscreen},
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
    if (cfg_scale == WIDE_SCALE_MAX) {
        fputs("scale = max\n", f);
    } else {
        fprintf(f, "scale = %d\n", cfg_scale);
    }
#ifndef __SWITCH__
    fputs(ini_fullscreen, f);
    fprintf(f, "fullscreen = %d\n", g_Fullscreen);
#endif
    fputs(ini_voice, f);
    fprintf(f, "voice = %s\n", g_VoiceSetNames[g_VoiceSet]);
    fputs(ini_language, f);
    fprintf(f, "language = %s\n", g_LangNext ? "uk" : "en");
    fputs(ini_buttons, f);
    fprintf(f, "buttons = %s\n",
            g_ButtonPick < 0 ? "auto" : g_ButtonSetNames[g_ButtonPick]);
    fputs(ini_controls, f);
    for (i = 0; i < 2 * PSYZ_PAD_BUTTONS; i++) {
        fprintf(f, "%s_%s = %s\n", input_dev[i & 1], ps_buttons[i >> 1],
                Psyz_PadsInputName(i & 1, Psyz_PadsMap(i & 1, i >> 1,
                                                       PSYZ_INPUT_QUERY)));
    }
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
    fputs(ini_hd, f);
    fprintf(f, "hd_textures = %d\nhd_dump = %d\n", g_HdTextures, g_HdDump);
    fclose(f);
}

// Reads the settings file, writes it with the defaults when it is missing.
// Keys: scale = N, voice = ps1|jp|psp, language = en|uk,
// buttons = auto|psx|xbox|switch (missing: auto), key_<button> and
// pad_<button> = <input name> (IniInput),
// jp_cards = extra|jp|off, relic_toggle = modern|stock,
// spawn = <spawn point name>, cheat_* = 0|1 (cheat_character 0..2, 4), editor, overlay, fps = 0|1,
// hd_textures = 0|1 (missing: 1), hd_dump = 0|1.
// A missing key keeps its default (cheats, editor, overlay and fps off).
// The view is always 16:9: the aspect key of an old file does nothing.
void Wide_LoadIni(const char* path) {
    int scale = 5, v, i; // docked 5, handheld 3 (ScaleRange)
    char line[128], word[32];
    FILE* f = fopen(path, "r");

    ini_path = path;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, " scale = %d", &v) == 1) {
                if (v >= 1 && v <= 8) {
                    scale = v;
                }
            } else if (sscanf(line, " scale = %15s", word) == 1) {
                if (!strcmp(word, "max")) {
                    scale = WIDE_SCALE_MAX;
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
                g_ButtonPick = -1; // auto, and an unknown word
                for (v = 0; v < BUTTON_SETS; v++) {
                    if (!strcmp(word, g_ButtonSetNames[v])) {
                        g_ButtonPick = v;
                    }
                }
            } else if (IniInput(line)) {
                // a key_ or pad_ line
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
    fprintf(stderr, "[video] settings: scale %d fullscreen %d\n", scale,
            g_Fullscreen);
#ifndef __SWITCH__
    Psyz_VideoSetFullscreen(g_Fullscreen); // before the window is shown
#endif
    fprintf(
        stderr, "[voice] settings: voice %s\n", g_VoiceSetNames[g_VoiceSet]);
    fprintf(stderr, "[lang] settings: language %s\n", g_LangNext ? "uk" : "en");
    fprintf(stderr, "[buttons] settings: buttons %s\n",
            g_ButtonPick < 0 ? "auto" : g_ButtonSetNames[g_ButtonPick]);
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

// The PS1 stage clip of func_800E34DC: rows 20..226.
#define CLIP_TOP 0x14
#define CLIP_BOTTOM (0x14 + 0xCF)

// Scales that fit: the 256 columns and the stage clip rows must fit, and
// above 960x540 game pixels the tile budget runs out.
static void ScaleRange(PsyzSize req, int* lo, int* hi) {
    *hi = MAX(MIN(req.w / DISP_STAGE_W, req.h / (CLIP_BOTTOM - CLIP_TOP)), 1);
    *lo = MIN(MAX((req.w + 959) / 960, (req.h + 539) / 540), *hi);
}

// The frame clear covers only the clip: black out the rows y0..y1 of both
// pages, or they keep an old picture. A rect on a frame edge reaches the
// canvas edge.
static void ClearRows(int y0, int y1) {
    for (int i = 0; i < 2 && y1 > y0; i++) {
        DRAWENV* d = &g_GpuBuffers[i].draw;
        RECT r = {d->clip.x, y0, d->clip.w, y1 - y0};
        ClearImage(&r, 0, 0, 0);
    }
}

// The black bars of a stage frame: on a stage start (func_800E34DC), as the
// screen before may have drawn there, and on a new canvas.
void Wide_ClearBars(void) {
    if (g_WideX) {
        ClearRows(0, CLIP_TOP);
        ClearRows(CLIP_BOTTOM, DISP_STAGE_H);
    }
}

// The wide camera calls it on every frame with the art rows of the room on
// the screen. Only a stage with a band (Saturn) narrows the clip, for a room
// shorter than the view: black bars instead of the empty rows.
void Wide_BandClip(int y0, int y1) {
    DRAWENV* d0 = &g_GpuBuffers[0].draw;
    DRAWENV* d1 = &g_GpuBuffers[1].draw;
    if ((!g_WideBandTop && !g_WideBandBottom && !band_clip) ||
        d0->clip.y != d1->clip.y) {
        return; // no band, or the two-page clip of 42398.c
    }
    if (!g_WideBandTop && !g_WideBandBottom) {
        y0 = CLIP_TOP, y1 = CLIP_BOTTOM; // the stage changed: the PS1 clip
    }
    y0 = Clamp(y0, CLIP_TOP, CLIP_BOTTOM);
    y1 = Clamp(y1, y0, CLIP_BOTTOM);
    band_clip = y0 > CLIP_TOP || y1 < CLIP_BOTTOM;
    d0->clip.y = d1->clip.y = y0;
    d0->clip.h = d1->clip.h = y1 - y0;
    if (band_clip) {
        ClearRows(0, y0);
        ClearRows(y1, DISP_STAGE_H);
    }
}

// A title or menu frame: the part of the canvas the screen shows, in the
// frame's pixels, and the zoom that draws the overlay text about as large
// as on a stage. zoom 0: a stage, or no wide view.
static int canvas[4], canvas_zoom;

int Wide_Canvas(int* x0, int* y0, int* x1, int* y1) {
    *x0 = canvas[0];
    *y0 = canvas[1];
    *x1 = canvas[2];
    *y1 = canvas[3];
    return cfg_scale ? canvas_zoom : 0;
}

// Called on every vsync. Follows dock changes and display mode changes.
void Wide_OnFrame(void) {
    PsyzSize d, req;
    int fbw, fbh, k, s, lo, hi, rows, keep, prologue = 0;
    RECT* clip = &g_GpuBuffers[0].draw.clip;

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
    // the stage clip (a Saturn band is inside it): only the rows 20..226
    // must fit, so docked shows the stage at scale 5 (384x216). Game over
    // from Gameover_6 (the melt ends; it opens the clip there) and the
    // loading screen are full 240-row pictures: all rows fit.
    rows = clip->y >= CLIP_TOP && clip->y + clip->h <= CLIP_BOTTOM &&
           (g_GameState == Game_Play ||
            (g_GameState == Game_GameOver &&
             (g_GameStep < Gameover_6 || g_GameStep >= Gameover_Alt)));
    if (d.w <= 0 || d.h <= 0 ||
        (d.w == last_disp.w && d.h == last_disp.h && fbw == last_fbw &&
         fbh == last_fbh && rows == last_rows)) {
        return;
    }
    last_disp = d;
    last_fbw = fbw;
    last_fbh = fbh;
    last_rows = rows;

    req = AspectRect(d);
    ScaleRange(req, &lo, &hi);
    k = Clamp(cfg_scale, lo, hi);
    keep = rows && fbw == DISP_STAGE_W && fbh == DISP_STAGE_H;
    if (!keep) {
        k = MIN(k, MAX(req.h / DISP_STAGE_H, 1)); // all 240 rows must fit
        if (fbw == DISP_STAGE_W && fbh == DISP_STAGE_H) {
            // black bars above and below, not the VRAM rows past the frame
            req.h = MIN(req.h, DISP_STAGE_H * k);
        }
    }
    g_WideX = ((req.w / k & ~1) - DISP_STAGE_W) / 2;
    Psyz_VideoSetKeepRows(keep ? CLIP_TOP : 0, keep ? CLIP_BOTTOM : 0);

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
            if (g_GameState == Game_PrologueEnd) {
                // the prologue scroll (src/st/st0/prologue_scroll.c): the
                // 512 frame in the PS1 4:3 shape, black at the sides
                prologue = 1;
                s = MAX(MIN(d.w / 320, d.h / 240), 1);
                req.w = 320 * s;
                req.h = 240 * s;
            }
        }
        if (fbw == DISP_MENU_W && fbh == DISP_MENU_H) {
            // pause menu: the 384 frame stays centred with black margins;
            // the whole display is the canvas, so the uk relic window may
            // reach past the frame (RELIC_WIN_X in src/dra/menu.c)
            req = d;
        }
    }
    canvas_zoom = 0;
    if (fbw != DISP_STAGE_W || fbh != DISP_STAGE_H) {
        int mx = (req.w / s - fbw) / 2, my = (req.h / s - fbh) / 2;
        canvas[0] = -mx;
        canvas[1] = -my;
        canvas[2] = fbw + mx;
        canvas[3] = fbh + my;
        canvas_zoom = MAX((k + s / 2) / s, 1);
        if (prologue) { // the frame fills the 4:3 picture
            canvas[0] = canvas[1] = 0;
            canvas[2] = fbw;
            canvas[3] = fbh;
        }
    }
    Psyz_VideoSetAspectMode(prologue ? PSYZ_ASPECT_DISPLAY : PSYZ_ASPECT_SQUARE);
    // 512 columns in a 4:3 picture: nearest repeats every 8th column, so
    // the prologue text is smoothed (sharp bilinear); the rest stays nearest
    Psyz_VideoSetLinearPresent(prologue);
    Psyz_VideoSetScale(s);
    Psyz_VideoSetResolution(req);
    if (fbw == DISP_STAGE_W && fbh == DISP_STAGE_H) {
        Wide_ClearBars();
    }
    fprintf(stderr,
            "[video] output %dx%d scale %d (asked %d) "
            "frame %dx%d internal %dx%d margin x %d y %d keep rows %d\n",
            d.w, d.h, s, cfg_scale, fbw, fbh, req.w / s & ~1,
            req.h / s & ~1, (int)g_WideX, (int)g_WideY, keep);
}

// Port menu state.
void Wide_MenuGet(int* scale, int* lo, int* hi) {
    ScaleRange(AspectRect(Psyz_VideoGetDisplaySize()), lo, hi);
    *scale = cfg_scale == WIDE_SCALE_MAX ? WIDE_SCALE_MAX
             : cfg_scale                ? Clamp(cfg_scale, *lo, *hi)
                                        : 0;
}

void Wide_MenuSet(int scale) {
    Wide_Config(scale);
    Wide_SaveIni();
}
