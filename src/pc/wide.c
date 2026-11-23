// SPDX-License-Identifier: AGPL-3.0-or-later
// Wide view. The Switch uses the 16:9 screen; a PC uses the whole window
// width (an ultrawide too), up to a 960 game pixel view. The player picks
// an integer scale. In stage mode
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "room_stitch.h"
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

// aspect: the shape of the picture inside the window (auto: the window
// shape). The game sees a display of that shape, psyz centres it in
// the window: black bars on two sides.
int g_Aspect;
const char* const g_AspectNames[WIDE_ASPECTS] = {
    "auto", "4:3", "16:10", "16:9", "21:9", "2.39:1", "32:9"};
static const short aspect_wh[WIDE_ASPECTS][2] = {
    {0, 0}, {4, 3}, {16, 10}, {16, 9}, {21, 9}, {239, 100}, {32, 9}};

static PsyzSize DisplaySize(void) {
    PsyzSize d = Psyz_VideoGetDisplaySize();
    int w = aspect_wh[g_Aspect][0], h = aspect_wh[g_Aspect][1];
    // within 1% of the window shape (2.39:1 on 3440x1440): the whole window,
    // else a row less would drop the 240-row pictures to a smaller scale
    if (!w || d.w <= 0 || d.h <= 0 ||
        llabs((long long)d.w * h - (long long)d.h * w) * 100 <=
            (long long)d.h * w) {
        return d;
    }
    if ((long long)d.w * h > (long long)d.h * w) {
        d.w = (int)((long long)d.h * w / h) & ~1;
    } else {
        d.h = (int)((long long)d.w * h / w) & ~1;
    }
    return d;
}

void Wide_SetAspect(int aspect) {
    g_Aspect = (aspect + WIDE_ASPECTS) % WIDE_ASPECTS;
    last_disp.w = last_disp.h = 0; // Wide_OnFrame applies it again
    fprintf(stderr, "[video] menu: aspect %s\n", g_AspectNames[g_Aspect]);
    Wide_SaveIni();
}

float Wide_PixelAspect(void) {
    PsyzRect g = Psyz_VideoGetDrawArea(), p = Psyz_VideoGetPresentArea();
    float gw = g.ex - g.sx, gh = g.ey - g.sy, pw = p.ex - p.sx, ph = p.ey - p.sy;
    return gw > 0 && gh > 0 && pw > 0 && ph > 0 ? (pw / gw) / (ph / gh) : 1;
}

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
    "; max = always the highest scale that fits the current screen.\n"
    "; On a PC the stage fills the window width, at most 960 game pixels:\n"
    ";   3440x1440: 4 = 860, 5 = 688, 6 = 572 wide; 2560x1080: 3 = 852,\n"
    ";   4 = 640, 5 = 512 wide.\n";

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

int g_StickDpad = 1; // stick_dpad in sotn.ini
static const char ini_buttons[] =
    "\n"
    "; buttons: the button marks of the menus: auto = as the gamepad in use\n"
    "; (keyboard only: the last gamepad, at first psx; the Switch: switch),\n"
    "; psx = PlayStation, xbox = Xbox, switch = Nintendo.\n"
    "; menu_confirm: the gamepad face button that confirms in the menus\n"
    "; (title, file select, pause menu, shops, save, port menu); the other one\n"
    "; goes back. auto = the button labelled A confirms and B goes back: east\n"
    "; on a Nintendo pad and on the Switch, south on an Xbox or other pad; a\n"
    "; PlayStation pad keeps Cross and Triangle. east = the right face button,\n"
    "; south = the bottom one. The keyboard and the gameplay buttons stay as\n"
    "; they are.\n";

static const char ini_aspect[] =
    "\n"
    "; aspect: the shape of the picture: auto = the window or\n"
    "; screen shape; 4:3, 16:10, 16:9, 21:9, 2.39:1 (3440x1440), 32:9 = that\n"
    "; shape inside the window with black bars.\n";

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
int g_HdStats;        // "[hd] stats" log lines, HD counts under the FPS
int g_MusicFiles; // 1 = enhanced: music/<ID>.mp3 or .flac replaces the XA track

static const char ini_hd[] =
    "\n"
    "; hd_textures: 1 = PNG files in data/textures/<language>/ and\n"
    "; data/textures/common/ replace pictures; 0 = the original pictures.\n"
    "; hd_dump: 1 = write every picture the game draws to data/textures/dump/\n"
    "; (for artists; slow; stock art: do not share the files).\n"
    "; hd_stats: 1 = every 2 seconds the log gets the frame times and HD\n"
    "; counts; with fps = 1 the corner shows the HD draws and uploads.\n"
    "; music_source: original = the disc music (default); enhanced =\n"
    "; music/<ID>.mp3 or .flac plays in place of the disc track with that id\n"
    "; (tools/music/ost_map.py). Without those files the disc music plays.\n";

extern int g_BackgroundInput; // psyz sdl3_common.h
extern int g_WarpMapPick;     // port_menu.c
static const struct {
    const char* key;
    int* value;
} ini_flags[] = {
    {"hd_textures", &g_HdTextures},
    {"hd_dump", &g_HdDump},
    {"hd_stats", &g_HdStats},
    {"music_files", &g_MusicFiles},
    {"cheat_refill", &g_Cheat.refill},
    {"cheat_invincible", &g_Cheat.invincible},
    {"cheat_walls", &g_Cheat.walls},
    {"cheat_items", &g_Cheat.items},
    {"cheat_map", &g_Cheat.map},
    {"cheat_skip_prologue",&g_Cheat.skipPrologue},
    {"cheat_character", &g_Cheat.character},
    {"editor", &g_EditorOn},
    {"overlay", &g_OverlayOn},
    {"controller_background", &g_BackgroundInput},
    {"warp_map", &g_WarpMapPick},
    {"fps", &g_FpsOn},
    {"fullscreen", &g_Fullscreen},
    {"room_stitch", &g_RoomStitch},
    {"skip_cd_rooms", &g_SkipCdRooms},
    {"show_loading_text", &g_ShowLoadingText},
    {"room_edge_fade", &g_RoomEdgeFade},
    {"elevator_front_ornaments", &g_ElevatorFront},
};

static const char ini_stitch[] =
    "\n"
    "; room_stitch: 1 = some rooms side by side play as one wide room, with\n"
    "; no fade between them (a test: src/pc/room_stitch.c lists them).\n"
    "; room_edge_fade: 1 = in the wide view nothing shows past the left and\n"
    "; right edge of the room (black), and the edge tile fades to black over\n"
    "; its outer 8 pixels. 0 = the old fill (the next room, a mirror image)\n"
    "; under a wide dark gradient.\n"
    "; elevator_front_ornaments: 1 = the gold lattice around the elevator\n"
    "; shaft of the Clock Tower draws in front of the elevator, its chain and\n"
    "; you. 0 = the original order (the lattice behind them).\n";

static const char ini_loading[] =
    "\n"
    "; skip_cd_rooms: 1 = a door into a CD loading room (the corridor between\n"
    "; two areas) goes on to the next area at once. 0 = walk through it.\n"
    "; show_loading_text: 1 = the NOW LOADING word and logo show while a stage\n"
    "; loads. 0 = the screen stays black.\n";

static const char ini_overlay[] =
    "\n"
    "; overlay: 1 = a corner shows the stage, room, map cell and the cheats.\n"
    "; fps: 1 = a corner shows the frames per second.\n"
    "; controller_background: 1 = the controller works while the window has no\n"
    "; focus (the keyboard never does); 0 = no controller then.\n"
    "; warp_map: 1 = the warp portal shows the map and you pick the room;\n"
    "; 0 = the portal cycles through the open rooms, as on the PS1.\n";

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
    fputs(ini_aspect, f);
    fprintf(f, "aspect = %s\n", g_AspectNames[g_Aspect]);
    fputs(ini_voice, f);
    fprintf(f, "voice = %s\n", g_VoiceSetNames[g_VoiceSet]);
    fputs(ini_language, f);
    fprintf(f, "language = %s\n", g_LangNext ? "uk" : "en");
    fputs(ini_buttons, f);
    fprintf(f, "buttons = %s\n",
            g_ButtonPick < 0 ? "auto" : g_ButtonSetNames[g_ButtonPick]);
    fprintf(f, "menu_confirm = %s\n", g_MenuConfirmNames[g_MenuConfirm]);
    fprintf(f, "; stick_dpad: 1 = the left stick also works as the D-pad in the\n");
    fprintf(f, "; menus and the warp map (0 = off)\n");
    fprintf(f, "stick_dpad = %d\n", g_StickDpad);
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
    fprintf(f, "controller_background = %d\nwarp_map = %d\n", g_BackgroundInput,
            g_WarpMapPick);
    fputs(ini_editor, f);
    fprintf(f, "editor = %d\n", g_EditorOn);
    fputs(ini_overlay, f);
    fprintf(f, "overlay = %d\nfps = %d\n", g_OverlayOn, g_FpsOn);
    fputs(ini_hd, f);
    fprintf(f, "hd_textures = %d\nhd_dump = %d\nhd_stats = %d\nmusic_source = %s\n",
            g_HdTextures, g_HdDump, g_HdStats,
            g_MusicFiles ? "enhanced" : "original");
    fputs(ini_stitch, f);
    fprintf(f, "room_stitch = %d\nroom_edge_fade = %d\n", g_RoomStitch,
            g_RoomEdgeFade);
    fprintf(f, "elevator_front_ornaments = %d\n", g_ElevatorFront);
    fputs(ini_loading, f);
    fprintf(f, "skip_cd_rooms = %d\nshow_loading_text = %d\n", g_SkipCdRooms,
            g_ShowLoadingText);
    fclose(f);
}

// Reads the settings file, writes it with the defaults when it is missing.
// Keys: scale = N, voice = ps1|jp|psp, language = en|uk,
// buttons = auto|psx|xbox|switch (missing: auto), key_<button> and
// pad_<button> = <input name> (IniInput),
// jp_cards = extra|jp|off, relic_toggle = modern|stock,
// spawn = <spawn point name>, cheat_* = 0|1 (cheat_character 0..2, 4), editor, overlay, fps = 0|1,
// hd_textures = 0|1 (missing: 1), hd_dump = 0|1, music_source = original|enhanced (original; music_files = 0|1 is read too),
// room_stitch = 0|1 (0), room_edge_fade = 0|1 (1), skip_cd_rooms = 0|1 (1),
// show_loading_text = 0|1 (1).
// aspect = auto|4:3|16:10|16:9|21:9|2.39:1|32:9 (auto),
// menu_confirm = auto|east|south (auto; right, bottom: the 0.8.68 names).
// A missing key keeps its default (cheats, editor, overlay and fps off).
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
                g_Aspect = IniWord("aspect", word, g_AspectNames,
                                   WIDE_ASPECTS, 0);
            } else if (sscanf(line, " menu_confirm = %15s", word) == 1) {
                if (!strcmp(word, "right") || !strcmp(word, "bottom")) {
                    strcpy(word, word[0] == 'r' ? "east" : "south"); // 0.8.68
                }
                g_MenuConfirm = IniWord("menu_confirm", word,
                                        g_MenuConfirmNames, MENU_CONFIRMS,
                                        MENU_CONFIRM_AUTO);
            } else if (sscanf(line, " stick_dpad = %d", &v) == 1) {
                g_StickDpad = v != 0;
            } else if (sscanf(line, " voice = %15s", word) == 1) {
                for (v = 0; v < VOICE_COUNT; v++) {
                    if (!strcmp(word, g_VoiceSetNames[v])) {
                        g_VoiceSet = v;
                    }
                }
            } else if (sscanf(line, " language = %15s", word) == 1) {
                g_LangNext = !strcmp(word, "uk");
            } else if (sscanf(line, " music_source = %15s", word) == 1) {
                g_MusicFiles = !strcmp(word, "enhanced");
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
    fprintf(stderr,
            "[video] settings: scale %d fullscreen %d aspect %s menu_confirm "
            "%s\n",
            scale, g_Fullscreen, g_AspectNames[g_Aspect],
            g_MenuConfirmNames[g_MenuConfirm]);
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

// A title or menu frame: the part of the canvas the screen shows, in the
// frame's pixels, and the zoom that draws the overlay text about as large
// as on a stage. zoom 0: a stage, or no wide view.
static int canvas[4], canvas_zoom;

void Wide_Tile(TILE* tile) {
    int x = tile->x0, y = tile->y0, w = tile->w, h = tile->h;
    if (canvas_zoom) {
        // a title or menu frame (384 or 512 wide): a large tile that
        // touches a frame edge grows to the canvas edge, so a fade covers
        // the side pieces of the menu and prologue pictures too. Tiles side
        // by side (the prologue fades: 0..192, 192..384, 384..512) do not
        // overlap.
        int fw = canvas[0] + canvas[2], fh = canvas[1] + canvas[3];
        if (w < 128 || h < 120) {
            return;
        }
        if (x <= 0) {
            w += x - canvas[0];
            x = canvas[0];
        }
        if (x + w >= fw) {
            w = canvas[2] - x;
        }
        if (y <= 0) {
            h += y - canvas[1];
            y = canvas[1];
        }
        if (y + h >= fh) {
            h = canvas[3] - y;
        }
    } else if (g_WideX || g_WideY) {
        Wide_StretchRect(&x, &y, &w, &h, g_WideX, g_WideY);
    } else {
        return;
    }
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
// A frame of non-square pixels (the prologue scroll) gets the port menu in
// square pixels (port_menu.c Rect): x is in those, about the frame centre.
void Wide_ScreenToFrame(float sx, float sy, int* x, int* y) {
    PsyzRect g = Psyz_VideoGetDrawArea(), p = Psyz_VideoGetPresentArea();
    float pw = p.ex - p.sx, ph = p.ey - p.sy, a = Wide_PixelAspect();
    float fx = pw > 0 ? g.sx + (sx - p.sx) * (g.ex - g.sx) / pw : sx;
    int cx = g_CurrentBuffer->disp.disp.w / 2;
    if (a < 0.98f || a > 1.02f) {
        fx = cx + (fx - cx) * a;
    }
    *x = (int)floorf(fx); // floor: the margin left of the frame is negative
    *y = (int)floorf(ph > 0 ? g.sy + (sy - p.sy) * (g.ey - g.sy) / ph : sy);
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// The screen part of a stage frame: on the Switch the largest 16:9
// rectangle (its screens are 16:9), on a PC the whole window, so the stage
// fills the width of any monitor.
static PsyzSize StageRect(PsyzSize d) {
#ifdef __SWITCH__
    if (d.w * 9 > d.h * 16) {
        d.w = d.h * 16 / 9;
    } else {
        d.h = d.w * 9 / 16;
    }
#endif
    return d;
}

// The PS1 stage clip of func_800E34DC: rows 20..226.
#define CLIP_TOP 0x14
#define CLIP_BOTTOM (0x14 + 0xCF)

static void ScaleRange(PsyzSize req, int* lo, int* hi) {
    Wide_ScaleRange(req.w, req.h, CLIP_BOTTOM - CLIP_TOP, lo, hi);
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
    d = DisplaySize();
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

    req = StageRect(d);
    ScaleRange(req, &lo, &hi);
    k = Clamp(cfg_scale, lo, hi);
    keep = rows && fbw == DISP_STAGE_W && fbh == DISP_STAGE_H;
    if (!keep) {
        k = MIN(k, MAX(req.h / DISP_STAGE_H, 1)); // all 240 rows must fit
        if (fbw == DISP_STAGE_W && fbh == DISP_STAGE_H &&
            g_GameState != Game_GameOver) { // game over fills the 16:9 canvas
            // black bars above and below, not the VRAM rows past the frame
            req.h = MIN(req.h, DISP_STAGE_H * k);
        }
    }
    req.w = MIN(req.w, WIDE_VIEW_MAX * k); // pillars past the view budget
    g_WideX = (Wide_ViewW(req.w, k) - DISP_STAGE_W) / 2;
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
                // 512 frame in the PS1 4:3 shape; the canvas is the whole
                // display, the scroll code fills the sides (HD side pieces)
                prologue = 1;
                s = MAX(MIN(d.w / 320, d.h / 240), 1);
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
        // the prologue frame shows 4:3, so a frame pixel is fbh*4/3 / fbw wide
        int cw = prologue ? req.w * fbw * 3 / (s * fbh * 4) : req.w / s;
        int mx = (cw - fbw) / 2, my = (req.h / s - fbh) / 2;
        canvas[0] = -mx;
        canvas[1] = -my;
        canvas[2] = fbw + mx;
        canvas[3] = fbh + my;
        canvas_zoom = MAX((k + s / 2) / s, 1);
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
            "frame %dx%d internal %dx%d margin x %d y %d keep rows %d "
            "aspect %s state %d step %d\n",
            d.w, d.h, s, cfg_scale, fbw, fbh, req.w / s & ~1,
            req.h / s & ~1, (int)g_WideX, (int)g_WideY, keep,
            g_AspectNames[g_Aspect], (int)g_GameState, (int)g_GameStep);
}

// Port menu state.
void Wide_MenuGet(int* scale, int* lo, int* hi) {
    ScaleRange(StageRect(DisplaySize()), lo, hi);
    *scale = cfg_scale == WIDE_SCALE_MAX ? WIDE_SCALE_MAX
             : cfg_scale                ? Clamp(cfg_scale, *lo, *hi)
                                        : 0;
}

void Wide_MenuSet(int scale) {
    Wide_Config(scale);
    Wide_SaveIni();
}
