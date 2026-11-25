// SPDX-License-Identifier: AGPL-3.0-or-later
// Port menu, drawn by the game itself with TILE prims, so it works with
// every psyz renderer. L3+R3 opens and closes it. Up and down pick an item,
// left and right change it, Cross activates it, Circle or Triangle goes
// back; on MAIN back closes the menu. SDL-switch maps the face buttons by
// position (SWITCH_JoystickGetGamepadMapping: south = B), as psyz reads them.
// menu_confirm (MenuButtons) makes the button labelled A Cross and the one
// labelled B Triangle in every menu of the game and here (Switch: A right,
// Xbox: A bottom), so A confirms and B goes back; the hints show those
// marks. A PlayStation pad keeps Cross and Circle. The
// game gets no pad input while the menu is open. Long pages scroll, with a
// scroll bar; the mouse wheel over a box scrolls it. Changes
// apply at once (a voice set at the next spoken line) and are saved in
// sotn.ini by Wide_SaveIni.
//
// Pages: MAIN -> CONTROLS (the key and gamepad button of each PS button:
// Cross on a line, then a key or gamepad button sets it; RESET restores the
// defaults), MAIN -> CHEATS -> LIST (teleport: the castle map, the boss
// list, then every stage by name; Cross warps to its start or into the boss
// fight, left and right turn a page; a mini map beside it shows the rooms of
// the line in red; the PC mouse over a line selects it, a click warps; the
// pointer over a room selects its line, and the header line names the boss
// there (place list) or the place (boss list); on the mini map the wheel
// zooms about the pointer, a drag pans, a click on a room warps into that
// room; a boss line shows the
// bestiary picture) -> MAP -> CHOOSE (a cell with more than one
// room). The map draws one rect per
// room from g_CastleRooms. Input on
// the map: D-pad moves the cell cursor, L1/L2 zoom out, R1/R2 zoom in,
// Square switches normal / inverted castle, Cross teleports, Circle or
// Triangle goes back. Pointer (PC mouse, Switch touch): tap teleports, drag
// pans, two fingers pinch zoom, the mouse wheel zooms, a tap on the title
// line switches the castle.
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <psyz/audio.h>
#include <psyz/input.h>
#include "wide.h"
#include "pc.h"
#include <psyz/video.h>
#include "room_stitch.h"
#ifndef __SWITCH__
#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif
#endif
#ifdef __SWITCH__
#include <unistd.h>
#include <SDL3/SDL.h>
#else
#include <psyz/overlay_sdl3.h>
#endif

extern EnemyDef g_EnemyDefs[]; // src/dra/dra.h
int FileReadToBuf(const char* filename, void* dst, int offset, size_t maxlen);

enum {
    PAGE_MAIN,
    PAGE_CHEATS,
    PAGE_MAP,
    PAGE_CHOOSE,
    PAGE_CONTROLS,
    PAGE_LIST
};
extern int g_WarpMapPick; // warp_map
enum {
    ITEM_CAT_DISPLAY,
    ITEM_CAT_LANG,
    ITEM_CAT_CONTROLS,
    ITEM_CAT_QOL,
    ITEM_CAT_CHEATS,
    ITEM_CAT_DEV,
    ITEM_KEYS,
    ITEM_RESTART,
    ITEM_EXIT,
    ITEM_CLOSE,
    ITEM_BACK,
    ITEM_SCALE,
    ITEM_ASPECT,
    ITEM_FULLSCREEN,
    ITEM_MENUFILL,
    ITEM_EDGEFADE,
    ITEM_LOADING,
    ITEM_ELEVATOR,
    ITEM_HD,
    ITEM_LANGUAGE,
    ITEM_VOICE,
    ITEM_MUSIC,
    ITEM_BUTTONS,
    ITEM_CONFIRM,
    ITEM_STICKDPAD,
    ITEM_STICKDEAD,
    ITEM_LOOKRANGE,
    ITEM_LOOKDEAD,
    ITEM_PADBG,
    ITEM_SKIPPROLOGUE,
    ITEM_PROLOGUEFF,
    ITEM_CDROOMS,
    ITEM_WARPMAP,
    ITEM_DIALOGSKIP,
    ITEM_MAP100,
    ITEM_HEROPULSE,
    ITEM_RELICTOG,
    ITEM_JPCARDS,
    ITEM_OVERLAY,
    ITEM_FPS,
    ITEM_EDITOR,
    ITEM_HDDUMP,
    ITEM_HDSTATS,
    ITEM_STITCH,
    ITEM_RELICHINT,
    N_MAIN
};
#define CHEAT_HELP_WORD 344 // the help words of the cheats page
// one row of the menu: its label word, English label, one line of help
static const struct {
    s16 word;
    const char* en;
    s16 help;
    const char* help_en;
} menu_items[N_MAIN] = {
    [ITEM_CAT_DISPLAY] = {237, "DISPLAY", 300, "SCALE, SHAPE, WINDOW AND TEXTURES."},
    [ITEM_CAT_LANG] = {238, "LANGUAGE AND SOUND", 301, "LANGUAGE, VOICE SET AND MUSIC."},
    [ITEM_CAT_CONTROLS] = {200, "CONTROLS", 302, "KEYS, BUTTONS, STICKS AND THE PAD."},
    [ITEM_CAT_QOL] = {239, "QUALITY OF LIFE", 303, "SMALL HELPS: SKIPS, HINTS, SWITCHES."},
    [ITEM_CAT_CHEATS] = {26, "CHEATS", 304, "CHEATS, CHARACTER AND TELEPORT."},
    [ITEM_CAT_DEV] = {240, "DEVELOPER", 305, "OVERLAY, FPS, EDITOR AND TEST TOOLS."},
    [ITEM_KEYS] = {241, "KEY BINDINGS", 306, "A KEY AND A PAD BUTTON FOR EACH PS BUTTON."},
    [ITEM_RESTART] = {34, "RESTART", 307, "START THE GAME AGAIN. PRESS TWICE."},
    [ITEM_EXIT] = {27, "EXIT GAME", 308, "QUIT THE GAME. PRESS TWICE."},
    [ITEM_CLOSE] = {28, "CLOSE", 309, "CLOSE THIS MENU."},
    [ITEM_BACK] = {32, "BACK", 310, "GO BACK ONE PAGE."},
    [ITEM_SCALE] = {20, "SCALE", 311, "PIXEL SIZE OF THE PICTURE. MAX FILLS THE SCREEN."},
    [ITEM_ASPECT] = {220, "ASPECT", 312, "SHAPE OF THE PICTURE INSIDE THE WINDOW."},
    [ITEM_FULLSCREEN] = {214, "FULLSCREEN", 313, "WINDOW OR FULL SCREEN. THE KEY F11 TOO."},
    [ITEM_MENUFILL] = {236, "MENU FILL", 314, "PAUSE MENUS FILL A WIDE SCREEN."},
    [ITEM_EDGEFADE] = {218, "ROOM EDGE FADE", 315, "BLACK PAST THE ROOM EDGE, THE EDGE FADES."},
    [ITEM_LOADING] = {217, "LOADING TEXT", 316, "SHOW NOW LOADING WHILE A STAGE LOADS."},
    [ITEM_ELEVATOR] = {242, "ELEVATOR ORNAMENTS", 317, "THE LABORATORY ELEVATOR LATTICE DRAWS IN FRONT."},
    [ITEM_HD] = {243, "HD TEXTURES", 318, "PNG TEXTURE PACKS. APPLIES AT THE NEXT START."},
    [ITEM_LANGUAGE] = {22, "LANGUAGE", 319, "GAME LANGUAGE. APPLIES AT THE NEXT START."},
    [ITEM_VOICE] = {259, "VOICE SET", 320, "VOICE AND THE MATCHING DIALOGUE SCRIPT."},
    [ITEM_MUSIC] = {228, "MUSIC", 321, "DISC MUSIC OR ENHANCED MUSIC FILES."},
    [ITEM_BUTTONS] = {260, "BUTTON MARKS", 322, "THE BUTTON PICTURES SHOWN IN THE MENUS."},
    [ITEM_CONFIRM] = {219, "MENU CONFIRM", 323, "THE PAD BUTTON THAT CONFIRMS IN MENUS."},
    [ITEM_STICKDPAD] = {244, "STICK AS D-PAD", 324, "THE LEFT STICK ALSO WORKS AS THE D-PAD."},
    [ITEM_STICKDEAD] = {245, "STICK DEAD ZONE", 325, "LEFT STICK DEAD ZONE, IN PERCENT."},
    [ITEM_LOOKRANGE] = {246, "LOOK RANGE", 326, "RIGHT STICK VIEW RANGE IN PIXELS. 0 IS OFF."},
    [ITEM_LOOKDEAD] = {247, "LOOK DEAD ZONE", 327, "RIGHT STICK DEAD ZONE, IN PERCENT."},
    [ITEM_PADBG] = {248, "PAD IN BACKGROUND", 328, "THE PAD WORKS WHEN THE WINDOW HAS NO FOCUS."},
    [ITEM_SKIPPROLOGUE] = {40, "SKIP PROLOGUE", 329, "A NEW GAME STARTS AT THE CASTLE GATE."},
    [ITEM_PROLOGUEFF] = {257, "PROLOGUE SPEED", 330, "HOLD CROSS TO SPEED IT UP. B ON THE SWITCH."},
    [ITEM_CDROOMS] = {216, "SKIP CD ROOMS", 331, "A CD LOADING CORRIDOR IS SKIPPED."},
    [ITEM_WARPMAP] = {255, "WARP MAP", 332, "A WARP PORTAL SHOWS A MAP TO PICK THE ROOM."},
    [ITEM_DIALOGSKIP] = {235, "DIALOGUE SKIP", 333, "START SKIPS DIALOGUE TEXT AND ITS WAIT."},
    [ITEM_MAP100] = {41, "100% MAP", 334, "BOTH CASTLE MAPS FULLY EXPLORED."},
    [ITEM_HEROPULSE] = {249, "HERO ROOM PULSE", 335, "A RING PULSES ROUND YOUR ROOM ON THE MAP."},
    [ITEM_RELICTOG] = {250, "RELIC SWITCH", 336, "THE LOOK OF THE RELIC SWITCH."},
    [ITEM_JPCARDS] = {251, "JP CARDS", 337, "JP FAMILIAR CARDS. APPLIES AT THE NEXT START."},
    [ITEM_OVERLAY] = {24, "OVERLAY", 338, "THE CORNER SHOWS STAGE, ROOM AND CHEATS."},
    [ITEM_FPS] = {256, "FPS", 339, "THE CORNER SHOWS FRAMES PER SECOND."},
    [ITEM_EDITOR] = {25, "EDITOR", 340, "HTTP TILE EDITOR ON PORT 8080."},
    [ITEM_HDDUMP] = {252, "HD DUMP", 341, "WRITE DRAWN PICTURES TO FILES FOR ARTISTS."},
    [ITEM_HDSTATS] = {253, "HD STATS", 342, "LOG FRAME TIMES AND HD COUNTS."},
    [ITEM_STITCH] = {254, "ROOM STITCH", 343, "SIDE ROOMS PLAY AS ONE WIDE ROOM. A TEST."},
    [ITEM_RELICHINT] = {258, "RELIC HINT", 360, "A NOTE WHEN YOU PICK UP A NEW RELIC."},
};
// the pages of the main menu: a category is a list of rows (-1 ends it).
// A future "quick item" button row of Controls goes next to KEYS.
enum { CAT_TOP, CAT_DISPLAY, CAT_LANG, CAT_CONTROLS, CAT_QOL, CAT_DEV };
static const s8 cat_rows[][16] = {
    [CAT_TOP] = {ITEM_LANGUAGE, ITEM_CAT_DISPLAY, ITEM_CAT_LANG,
                 ITEM_CAT_CONTROLS, ITEM_CAT_QOL, ITEM_CAT_CHEATS,
                 ITEM_CAT_DEV,
#ifndef __SWITCH__
                 ITEM_RESTART, // Switch: no restart (hbmenu starts it)
#endif
                 ITEM_EXIT, ITEM_CLOSE, -1},
    [CAT_DISPLAY] = {ITEM_SCALE, ITEM_ASPECT,
#ifndef __SWITCH__
                     ITEM_FULLSCREEN,
#endif
                     ITEM_MENUFILL, ITEM_EDGEFADE, ITEM_LOADING,
                     ITEM_ELEVATOR, ITEM_HD, ITEM_BACK, -1},
    [CAT_LANG] = {ITEM_LANGUAGE, ITEM_VOICE, ITEM_MUSIC, ITEM_BACK, -1},
    [CAT_CONTROLS] = {ITEM_KEYS, ITEM_BUTTONS, ITEM_CONFIRM, ITEM_STICKDPAD,
                      ITEM_STICKDEAD, ITEM_LOOKRANGE, ITEM_LOOKDEAD,
                      ITEM_PADBG, ITEM_BACK, -1},
    [CAT_QOL] = {ITEM_SKIPPROLOGUE, ITEM_PROLOGUEFF, ITEM_CDROOMS,
                 ITEM_WARPMAP, ITEM_DIALOGSKIP, ITEM_MAP100, ITEM_HEROPULSE,
                 ITEM_RELICTOG, ITEM_RELICHINT, ITEM_JPCARDS, ITEM_BACK, -1},
    [CAT_DEV] = {ITEM_OVERLAY, ITEM_FPS, ITEM_EDITOR, ITEM_HDDUMP,
                 ITEM_HDSTATS, ITEM_STITCH, ITEM_BACK, -1},
};
static int cat;      // the category on the main page
static int cat_from; // the line of the top page that opened a page
extern int g_HdTextures, g_HdDump, g_HdStats, g_BackgroundInput;
enum {
    CH_REFILL,
    CH_INVINCIBLE,
    CH_WALLS,
    CH_ITEMS,
    CH_CHARACTER,
    CH_TELEPORT,
    CH_BACK,
    N_CHEATS
};

#define COMBO (PAD_L3 | PAD_R3)
#define CELL_W 6     // 5 px glyph and 1 px space
#define LINE_H 12    // 7 px glyph and 5 px space
#define TEXT_COLS 28 // the longest line: "HTTP://255.255.255.255:8080/"
#define BORDER 8
#define MAX_TILES 8192
#define MAX_OWNERS 6
#define ZOOM_MIN 32  // cell size in 1/16 px: 2 px
#define ZOOM_MAX 256 // 16 px

static int is_open;
static int page;
static int sel;
static int wait_release; // after close: the game waits until all are up
static int drawn;        // > 0: RenderPrimitives ran in the last frames
static u16 prev;
static int hold, hold_dir;
static int frame;

// map state
static int castle;       // 0 normal, 0x20 inverted
static int cur_x, cur_y; // cursor cell
static int zoom16 = 48;  // cell size in 1/16 px
static int ox16, oy16;   // frame position of cell (0, 0), 1/16 px
static int map_ready;
static const struct CastleRoom* owners[MAX_OWNERS];
static int n_owners;
// uk words of the teleport reasons (tools/lang/uk/ua_port.json 270..279)
enum {
    P_W_NOGAME = 270, // the help line of the greyed TELEPORT
    P_W_NOGAME_MSG,
    P_W_BUSY,
    P_W_CLOSE_MAP,
    P_W_CLOSE_PAUSE,
    P_W_DEAD,
    P_W_PROLOGUE,
    P_W_OTHER_ROOM,
    P_W_SAME_HERO = 279,
};
static char msg[48];
static int msg_word = -1; // its uk word (P_*), -1: none
static int msg_timer;
static int click_act; // Activate runs for a mouse click
static int exit_armed; // EXIT was pressed once: the next press quits

// the Controls page: one line per PS button (bit of the pad word), then
// RESET and BACK; the list scrolls under a header line
static const struct {
    u8 bit;
    s16 word;
    const char* en;
} ctl_rows[] = {
    {6, 201, "CROSS"}, {5, 202, "CIRCLE"}, {7, 203, "SQUARE"},
    {4, 204, "TRIANGLE"}, {2, -1, "L1"}, {3, -1, "R1"}, {0, -1, "L2"},
    {1, -1, "R2"}, {11, -1, "START"}, {8, -1, "SELECT"}, {12, 205, "UP"},
    {14, 206, "DOWN"}, {15, 207, "LEFT"}, {13, 208, "RIGHT"}, {9, -1, "L3"},
    {10, -1, "R3"},
};
#define N_CTL_BUTTONS 16
enum { CTL_RESET = N_CTL_BUTTONS, CTL_BACK, N_CTL };
#define CTL_LINES 12     // list lines on screen
#define CAPTURE_TIME 600 // frames a capture waits
static int ctl_top;      // the first list line on screen
static int capture;      // > 0: frames left to wait for a key or button
static int capture_armed; // every input was up since the capture began
#ifdef __SWITCH__
#define CTL_KEYS 0 // no keyboard column
#else
#define CTL_KEYS 1
#endif

// the teleport list, under a header line: the castle map, the boss list,
// then the stages with rooms in g_CastleRooms and an area name, the normal
// castle first. The boss list: g_CheatBosses. It scrolls; a mini map of the
// castle beside it shows the rooms of the selected line in red.
#define ITEM_MAP (-1)
#define ITEM_BOSSES (-2)
#define ITEM_ANY (-3)   // not a line: a room of any place (RoomAt)
#define ITEM_BOSS 0x100 // + the g_CheatBosses index; else a stage id
#define BOSS_LINES 8    // the boss picture goes under the box
static int items[96];
static int n_items;
static int list_boss;           // 1: the boss list
static int list_top;            // the first item on screen
static int place_sel, boss_sel; // kept for the next open
// the cell last picked on the mini map (mark_cx -1: none): the header line
// of the list names what the other list has there, the boss on the place
// list, the place on the boss list, so both show at once
static int mark_cx = -1, mark_cy, mark_c;
// mini_user: the wheel or a drag moved the mini map, DrawMini keeps its
// origin and cell size (a new list or castle fits it again); mini_fit: the
// fit cell size
static int mini_user, mini_fit;
static int ListLines(void) {
    return MIN(list_boss ? BOSS_LINES : CTL_LINES, n_items);
}

// the confirm and back buttons, and their marks in the hints: a byte 1 + b
// of a Text string is the mark of PS button b in the set of BUTTONS
// (src/pc/buttons.c), the marks go by position: Circle is the right button.
// The hints show the gamepad buttons that give PAD_OK and PAD_BACK
// (MenuButtons): right and bottom, or bottom and right.
#define PAD_OK PAD_CROSS
#define PAD_BACK (PAD_CIRCLE | PAD_TRIANGLE)
static char hint_ok[2] = "\x03", hint_back[2] = "\x02"; // Cross, Circle
#define HINT_OK hint_ok
#define HINT_BACK hint_back
#define HINT_CASTLE "\x01" "/" "\x09" // Square, Select

// ---- menu_confirm -------------------------------------------------------------
// The US menus confirm with Cross and go back with Triangle, the gamepad
// gives Cross on the bottom button and Circle on the right one. The button
// labelled A confirms and B goes back: on a Nintendo pad (A right) in a menu
// the right button gives Cross and the bottom one Triangle; on an Xbox or an
// unknown pad (A bottom) the bottom one gives Cross and the right one
// Triangle. The confirm button also gives Start on the title (PRESS START
// takes Start only, its stage select Start or Circle). A PlayStation pad
// keeps the stock buttons. A press keeps the meaning it had when it began
// until it is up, so the press that closes a menu is no jump.
// Gameplay, the keyboard and the other buttons stay as they are.
int g_MenuConfirm;
const char* const g_MenuConfirmNames[MENU_CONFIRMS] = {"auto", "east",
                                                       "south"};
static int menu_pad; // frames a game menu still counts (PortMenu_MenuPad)

u32 PortMenu_MenuPad(u32 bits) {
    menu_pad = 8;
    return bits;
}

int PortMenu_ConfirmMode(void) {
    if (g_MenuConfirm != MENU_CONFIRM_AUTO) {
        return g_MenuConfirm;
    }
#ifdef __SWITCH__
    return MENU_CONFIRM_EAST;
#else
    switch (Psyz_PadsFamily()) {
    case PSYZ_PAD_FAMILY_NINTENDO:
        return MENU_CONFIRM_EAST;
    case PSYZ_PAD_FAMILY_PLAYSTATION:
        return MENU_CONFIRM_AUTO; // stock
    default: // Xbox, unknown, none yet
        return MENU_CONFIRM_SOUTH;
    }
#endif
}

int PortMenu_ConfirmEast(void) {
    return PortMenu_ConfirmMode() == MENU_CONFIRM_EAST;
}

int Psyz_ControllerBlocked(void); // psyz: no controller while unfocused

static int GamepadDown(SDL_GamepadButton b) {
    int n = 0, i, r = 0;
    if (Psyz_ControllerBlocked()) {
        return 0;
    }
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (i = 0; ids && i < n && !r; i++) {
        SDL_Gamepad* g = SDL_GetGamepadFromID(ids[i]);
        r = g && SDL_GetGamepadButton(g, b);
    }
    SDL_free(ids);
    return r;
}

// The sticks: psyz maps them to no PS button. The left stick works as the
// D-pad everywhere (stick_dpad), the right stick moves the view (WideCamera
// in src/dra/5087C.c). Of several pads, the one whose stick is out the most
// counts.
static void ReadStick(int right, int* x, int* y) {
    static int logged[2], seen = -1;
    int n = 0, i, ax, ay, best = 0;
    SDL_Gamepad* from = NULL;
    *x = *y = 0;
    if (Psyz_ControllerBlocked()) {
        return;
    }
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    if (n != seen) { // what the owner's sotn.log shows about the pads
        seen = n;
        fprintf(stderr, "[stick] %d gamepad(s) for the stick\n", n);
    }
    for (i = 0; ids && i < n; i++) {
        SDL_Gamepad* g = SDL_GetGamepadFromID(ids[i]);
        if (!g) {
            continue;
        }
        ax = SDL_GetGamepadAxis(
            g, right ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX);
        ay = SDL_GetGamepadAxis(
            g, right ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY);
        if (abs(ax) + abs(ay) > best) {
            best = abs(ax) + abs(ay);
            *x = ax;
            *y = ay;
            from = g;
        }
    }
    SDL_free(ids);
    if (!logged[right] && best > 12000) {
        logged[right] = 1;
        fprintf(stderr, "[stick] %s stick x %d y %d on '%s'\n",
                right ? "right" : "left", *x, *y, SDL_GetGamepadName(from));
    }
}

// The left stick as D-pad bits (Wide_StickDirs: dead zone stick_dead,
// 8 directions, hysteresis).
static u16 StickDirs(void) {
    static int held;
    int x, y, d;
    if (!g_StickDpad) {
        return 0;
    }
    ReadStick(0, &x, &y);
    held = d = Wide_StickDirs(x, y, g_StickDead * 32767 / 100, held);
    return (d & WIDE_STICK_L ? PAD_LEFT : 0) |
           (d & WIDE_STICK_R ? PAD_RIGHT : 0) |
           (d & WIDE_STICK_U ? PAD_UP : 0) | (d & WIDE_STICK_D ? PAD_DOWN : 0);
}

void PortMenu_RightStick(int* x, int* y) { ReadStick(1, x, y); }

static u16 MenuButtons(u16 pressed, int open) {
    static const SDL_GamepadButton btn[2] = {SDL_GAMEPAD_BUTTON_EAST,
                                             SDL_GAMEPAD_BUTTON_SOUTH};
    static int down[2];
    static u16 as[2]; // the PS button a held one gives, 0: as mapped
    int menu = open || g_GameState == Game_Title ||
               g_GameState == Game_MainMenu ||
               (g_GameState == Game_Play && g_GameEngineStep == Engine_Menu) ||
               menu_pad > 0;
    int mode = PortMenu_ConfirmMode(), i, b;
    u16 clear = 0, add = 0, give[2] = {0, 0}; // right, bottom
    if (mode == MENU_CONFIRM_EAST) {
        give[0] = PAD_CROSS;
        give[1] = PAD_TRIANGLE;
    } else if (mode == MENU_CONFIRM_SOUTH) {
        give[0] = PAD_TRIANGLE;
        give[1] = PAD_CROSS;
    }
    if (menu_pad > 0) {
        menu_pad--;
    }
    for (i = 0; i < 2; i++) {
        if (!GamepadDown(btn[i])) {
            down[i] = 0;
            continue;
        }
        if (!down[i]) {
            down[i] = 1;
            as[i] = menu ? give[i] : 0;
            fprintf(stderr, "[pad] %s button: %s\n", i ? "bottom" : "right",
                    as[i] == PAD_CROSS      ? "ok (cross)"
                    : as[i] == PAD_TRIANGLE ? "back (triangle)"
                                            : "as mapped");
        }
        for (b = 0; as[i] && b < PSYZ_PAD_BUTTONS; b++) {
            if (Psyz_PadsMap(PSYZ_INPUT_GAMEPAD, b, PSYZ_INPUT_QUERY) ==
                (int)btn[i]) {
                clear |= 1 << b;
                add |= as[i];
                if (as[i] == PAD_CROSS && g_GameState == Game_Title && !open) {
                    add |= PAD_START;
                }
            }
        }
    }
    return (pressed & ~clear) | add;
}

// pointer gestures
static int pt_n, pt_x[2], pt_y[2], pt_sx, pt_sy, pt_moved, pt_mini;

static void Close(void) {
    is_open = 0;
    wait_release = 1;
    exit_armed = 0;
}

// sotn.ini first, then the same quit as the window close: psyz ends the
// process at the next event poll (Switch: back to hbmenu)
static void Quit(void) {
    SDL_Event e;
    fprintf(stderr, "[menu] exit\n");
    Wide_SaveIni();
    fflush(stderr);
    SDL_zero(e);
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}

// Restart: the same program with the same arguments, then the quit above.
// Switch: no row (hbmenu starts the program).
extern int g_ArgC;
extern char** g_ArgV;
static void Message(const char* s);
// g_ArgV: the arguments as the user gave them (main.c saves them before
// Win_Init adds --disk and --ini). The new process waits for this one to end
// (SOTN_RESTART_PID: win.c waits for the log file, main.c for the pid); Quit
// then saves sotn.ini. If it cannot start, the menu stays open and says so.
// ponytail: an argument with a quote inside is not escaped on Windows
static void Restart(void) {
#ifndef __SWITCH__
    char** av = calloc(g_ArgC + 1, sizeof(char*));
    char pid[16];
    int i, rc;
    if (!av || g_ArgC < 1) {
        Message("could not restart");
        return;
    }
    for (i = 0; i < g_ArgC; i++) {
        av[i] = g_ArgV[i];
#ifdef _WIN32
        if (strchr(av[i], ' ')) { // the C runtime does not quote
            av[i] = malloc(strlen(g_ArgV[i]) + 3);
            sprintf(av[i], "\"%s\"", g_ArgV[i]);
        }
#endif
    }
    fprintf(stderr, "[menu] restart\n");
#ifdef _WIN32
    snprintf(pid, sizeof(pid), "%d", _getpid());
    _putenv_s("SOTN_RESTART_PID", pid);
    rc = _spawnv(_P_NOWAIT, _pgmptr, (const char* const*)av) == -1;
#else
    {
        pid_t p;
        snprintf(pid, sizeof(pid), "%d", (int)getpid());
        setenv("SOTN_RESTART_PID", pid, 1);
        rc = posix_spawn(&p, "/proc/self/exe", NULL, NULL, av, environ);
    }
#endif
    if (rc) {
        Message("could not restart");
        return;
    }
#endif
    Quit();
}

int PortMenu_IsOpen(void) { return is_open; }

// Once a frame from the main loop (42398.c), before UpdateGame. While the
// menu is open in normal play the game stands still: the loop skips
// UpdateGame, the sound commands, the palette steps and the play clock, and
// the audio device pauses (SFX, XA music and voices stop at one sample). The
// XA length timer D_8013AEF4 counts vsyncs: it moves by the frozen vsyncs, so
// a voice line or a track does not end early. The title, the pause menu, the
// map and the loads draw from UpdateGame and wait for input already: no
// freeze there. Every way the menu closes ends the freeze on the next frame.
// psyz pauses the device in the background and resumes it on return only if
// it played before (sdl3_common.h): open, it stays paused until the close.
// ponytail: a close while in the background would resume the audio there; the
// menu closes only by input, which the background does not get
extern s32 D_8013AEF4;
int Psyz_AudioIsPaused(void);
int PortMenu_Freeze(void) {
    static int frozen, vsync0, was_paused;
    int on = is_open && g_GameState == Game_Play &&
             g_GameEngineStep == Engine_Normal;
    if (on != frozen) {
        if (on) {
            vsync0 = VSync(-1);
            was_paused = Psyz_AudioIsPaused();
            Psyz_AudioPause();
        } else {
            D_8013AEF4 += VSync(-1) - vsync0;
            if (!was_paused) {
                Psyz_AudioUnpause();
            }
        }
        fprintf(stderr,
                "[menu] freeze %s at vsync %d (%d frozen), clock "
                "%d:%02d:%02d.%02d, player %d,%d\n",
                on ? "on" : "off", VSync(-1), on ? 0 : VSync(-1) - vsync0,
                g_Status.timerHours, g_Status.timerMinutes,
                g_Status.timerSeconds, g_Status.timerFrames,
                PLAYER.posX.i.hi, PLAYER.posY.i.hi);
        frozen = on;
    }
    return on;
}

static void Message(const char* s) {
    int i;
    for (i = 0; s[i] && i < (int)sizeof(msg) - 1; i++) {
        msg[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i];
    }
    msg[i] = 0;
    msg_word = -1;
    msg_timer = 150;
}

// uk words of the reasons Cheat_WarpRefusal and the warp code give (cheats.c)
static const struct {
    const char* en;
    int word;
} why_words[] = {{"start or load a game first", P_W_NOGAME_MSG},
                 {"the game is busy", P_W_BUSY},
                 {"close the map first", P_W_CLOSE_MAP},
                 {"close the pause menu first", P_W_CLOSE_PAUSE},
                 {"you are dead", P_W_DEAD},
                 {"not in the prologue", P_W_PROLOGUE},
                 {"pick another room", P_W_OTHER_ROOM},
                 {"already playing this hero", P_W_SAME_HERO}};

static void MessageWhy(const char* why) {
    int i;
    Message(why);
    for (i = 0; i < LEN(why_words); i++) {
        if (!strcmp(why, why_words[i].en)) {
            msg_word = why_words[i].word;
        }
    }
}

static int FbW(void) { return g_CurrentBuffer->disp.disp.w; }
static int FbH(void) { return g_CurrentBuffer->disp.disp.h; }

// ---- map maths --------------------------------------------------------------

// The visible frame columns: the stage with its wide margins, or the whole
// canvas of a full-screen menu (pause menu), as Wide_ScreenToFrame maps them.
static void ViewRect(int* x0, int* y0, int* x1, int* y1) {
    PsyzRect g = Psyz_VideoGetDrawArea();
    *x0 = g.ex > g.sx ? g.sx : -g_WideX;
    *x1 = g.ex > g.sx ? g.ex : DISP_STAGE_W + g_WideX;
    *y0 = 20; // the stage clip rows (func_800E34DC), also in the wide view
    *y1 = 226;
}

static int CellX(int cx) { return (ox16 + cx * zoom16) >> 4; }
static int CellY(int cy) { return (oy16 + cy * zoom16) >> 4; }

// The cell under frame pixel p, of cells s/16 px from origin o16 (1/16 px):
// the exact inverse of the drawing, cell c covers pixels (o16 + c s) >> 4 up
// to the next cell's first one, also left of or above the origin.
static int CellAt(int p, int o16, int s) {
    int a = ((p + 1) << 4) - 1 - o16;
    return a >= 0 ? a / s : -((s - 1 - a) / s);
}

// the top of the hint lines under the map (DrawMap sets it each frame)
static int map_bottom = 226 - LINE_H;

static void KeepCursorVisible(void) {
    int x0, y0, x1, y1, z = zoom16 >> 4;
    ViewRect(&x0, &y0, &x1, &y1);
    y0 += 2 * LINE_H; // the text lines on top
    y1 = map_bottom;  // the hint lines at the bottom
    if (CellX(cur_x) < x0) {
        ox16 += (x0 - CellX(cur_x)) << 4;
    }
    if (CellX(cur_x) + z > x1) {
        ox16 -= (CellX(cur_x) + z - x1) << 4;
    }
    if (CellY(cur_y) < y0) {
        oy16 += (y0 - CellY(cur_y)) << 4;
    }
    if (CellY(cur_y) + z > y1) {
        oy16 -= (CellY(cur_y) + z - y1) << 4;
    }
}

// zoom about the frame point (fx, fy)
static void Zoom(int nz, int fx, int fy) {
    nz = nz < ZOOM_MIN ? ZOOM_MIN : nz > ZOOM_MAX ? ZOOM_MAX : nz;
    ox16 = (fx << 4) - (int)((long long)((fx << 4) - ox16) * nz / zoom16);
    oy16 = (fy << 4) - (int)((long long)((fy << 4) - oy16) * nz / zoom16);
    zoom16 = nz;
}

// The teleport map draws the Saturn gardens off their real cells: ST15 lies
// on rows 44 of NO3, RS15 (its 180 degree mirror) on row 19 of RNO3, so they
// are drawn two rows away, in free cells (ST15 down, RS15 up). Only the
// drawing and the cursor use these; the game's cells, rooms and doors stay,
// and a warp gets the real cell back (Cheat_Warp, MapDy).
static int MapDy(int stage) {
    return stage == STAGE_ID_SATURN_15           ? 2
           : stage == STAGE_ID_REVERSE_SATURN_15 ? -2
                                                 : 0;
}
#define RTOP(r) ((r)->top + MapDy((r)->stage))
#define RBOT(r) ((r)->bottom + MapDy((r)->stage))

// map_fit: the next DrawMap fits the castle's rooms into the map area (it
// knows the hint lines) and keeps the cursor in it
static int map_fit;
static void FitCastle(int x0, int y0, int x1, int y1) {
    int i, l = 63, t = 63, r = 0, b = 0;
    for (i = 0; i < g_CastleRoomCount; i++) {
        const struct CastleRoom* rm = &g_CastleRooms[i];
        if ((rm->stage & 0x20) == castle) {
            l = MIN(l, rm->left);
            t = MIN(t, RTOP(rm));
            r = MAX(r, rm->right);
            b = MAX(b, RBOT(rm));
        }
    }
    if (r < l) {
        l = t = 0;
        r = b = 63;
    }
    zoom16 = MIN(((x1 - x0 - 8) << 4) / (r - l + 1),
                 ((y1 - y0 - 4) << 4) / (b - t + 1));
    zoom16 = MAX(ZOOM_MIN, MIN(ZOOM_MAX, zoom16));
    ox16 = ((x0 + x1) << 3) - (l + r + 1) * zoom16 / 2;
    oy16 = ((y0 + y1) << 3) - (t + b + 1) * zoom16 / 2;
    KeepCursorVisible();
}

static void FindOwners(int cx, int cy) {
    const struct CastleRoom* r;
    int i;
    n_owners = 0;
    for (i = 0; i < g_CastleRoomCount && n_owners < MAX_OWNERS; i++) {
        r = &g_CastleRooms[i];
        if ((r->stage & 0x20) == castle && cx >= r->left && cx <= r->right &&
            cy >= RTOP(r) && cy <= RBOT(r)) {
            owners[n_owners++] = r;
        }
    }
}

static void Go(const struct CastleRoom* r) {
    const char* why = Cheat_Warp(r->stage, r->room, cur_x, cur_y - MapDy(r->stage));
    if (why) {
        MessageWhy(why);
        page = PAGE_MAP;
    } else {
        page = PAGE_MAIN;
        Close();
    }
}

static void Teleport(void) {
    FindOwners(cur_x, cur_y);
    if (n_owners == 0) {
        MessageWhy("pick another room");
    } else if (n_owners == 1) {
        Go(owners[0]);
    } else {
        page = PAGE_CHOOSE;
        sel = 0;
    }
}

static void SwitchCastle(void) {
    castle ^= 0x20;
    cur_x = 63 - cur_x;
    cur_y = 63 - cur_y;
    map_fit = 1;
    fprintf(stderr, "[cheat] map: castle %s\n", castle ? "inverted" : "normal");
}

static void OpenMap(void) {
    page = PAGE_MAP;
    if (!map_ready || castle != (g_StageId & 0x20)) {
        castle = g_StageId & 0x20;
        cur_x = D_8009791C.x & 63;
        cur_y = (D_8009791C.y & 63) + MapDy(g_StageId);
        map_fit = 1;
        map_ready = 1;
    }
    if (g_CastleRoomCount == 0) {
        Message("no castle map in this build");
    }
}

static void OpenList(int boss) {
    int castle_bit, i, j, id;
    n_items = 0;
    list_boss = boss;
    page = PAGE_LIST;
    mark_cx = -1;
    mini_user = 0; // the mini map fits the view again
    if (boss) {
        for (i = 0; i < g_CheatBossCount && n_items < LEN(items); i++) {
            items[n_items++] = ITEM_BOSS + i;
        }
        sel = boss_sel < n_items ? boss_sel : 0;
        return;
    }
    items[n_items++] = ITEM_MAP;
    items[n_items++] = ITEM_BOSSES;
    if (!map_ready) { // the castle of the stage we are in
        castle = g_StageId & 0x20;
    }
    // only the stages of the castle in use (normal or inverted, never both)
    castle_bit = castle;
    for (i = 0; i < g_CastleStageCount && n_items < LEN(items); i++) {
        id = g_CastleStages[i].id;
        for (j = 0; j < g_CastleRoomCount && g_CastleRooms[j].stage != id;
             j++) {
        }
        // BO4 has rooms but no area: it is in the boss list
        if ((id & 0x20) == castle_bit && j < g_CastleRoomCount &&
            *Castle_ZoneName(id)) {
            items[n_items++] = id;
        }
    }
    sel = place_sel < n_items ? place_sel : 0;
}

// the list line of a castle room: its stage, or a boss fought there
static int ItemHas(int it, const struct CastleRoom* rm) {
    if (it >= ITEM_BOSS) {
        return rm->stage == g_CheatBosses[it - ITEM_BOSS].host &&
               rm->room == g_CheatBosses[it - ITEM_BOSS].host_room;
    }
    return it >= 0 && rm->stage == it;
}

// the first room at cell (cx, cy) of castle c of list item it (ITEM_ANY:
// of any stage with an area name), or NULL
static const struct CastleRoom* RoomAt(int cx, int cy, int c, int it) {
    const struct CastleRoom* r;
    int i;
    for (i = 0; i < g_CastleRoomCount; i++) {
        r = &g_CastleRooms[i];
        if ((r->stage & 0x20) == c && cx >= r->left && cx <= r->right &&
            cy >= RTOP(r) && cy <= RBOT(r) &&
            (it == ITEM_ANY ? *Castle_ZoneName(r->stage) != 0
                            : ItemHas(it, r))) {
            return r;
        }
    }
    return NULL;
}

// the first list line with a room at cell (cx, cy) of castle c, or -1
static int ItemAt(int cx, int cy, int c) {
    int k;
    for (k = 0; k < n_items; k++) {
        if (RoomAt(cx, cy, c, items[k])) {
            return k;
        }
    }
    return -1;
}

// the g_CheatBosses index fought at cell (cx, cy) of castle c, or -1
static int BossAt(int cx, int cy, int c) {
    int b;
    for (b = 0; b < g_CheatBossCount; b++) {
        if (RoomAt(cx, cy, c, ITEM_BOSS + b)) {
            return b;
        }
    }
    return -1;
}

// the mini map of the list page: cell (0, 0) at (mini_ox, mini_oy) in 1/16
// px, mini_s 1/16 px a cell, inside the frame rect mini_x0..mini_x1
static int mini_ox, mini_oy, mini_s, mini_castle;
static int mini_x0, mini_y0, mini_x1, mini_y1;

static int InMini(int x, int y) {
    return mini_s && x >= mini_x0 && x < mini_x1 && y >= mini_y0 &&
           y < mini_y1;
}

// the wheel over the mini map: zoom about frame point (x, y), from half the
// fit size to 16 px a cell
static void MiniZoom(int dir, int x, int y) {
    int ns = dir > 0 ? mini_s * 5 / 4 + 1 : mini_s * 4 / 5;
    ns = MAX(ns, mini_fit / 2);
    ns = MIN(ns, ZOOM_MAX);
    if (!mini_s || ns == mini_s) {
        return;
    }
    mini_ox = (x << 4) - (int)((long long)((x << 4) - mini_ox) * ns / mini_s);
    mini_oy = (y << 4) - (int)((long long)((y << 4) - mini_oy) * ns / mini_s);
    mini_s = ns;
    mini_user = 1;
    fprintf(stderr, "[menu] mini map zoom %d at %d,%d (fit %d)\n", mini_s, x,
            y, mini_fit);
}

// a pointer at frame (x, y) over the mini map selects the line of the room
// there and marks the cell; returns 1 inside the mini map
static int MiniPick(int x, int y) {
    int k;
    mark_cx = mark_cy = -1;
    if (!InMini(x, y) || CellAt(x, mini_ox, mini_s) < 0 ||
        CellAt(y, mini_oy, mini_s) < 0) {
        return 0;
    }
    mark_cx = CellAt(x, mini_ox, mini_s);
    mark_cy = CellAt(y, mini_oy, mini_s);
    mark_c = mini_castle;
    k = ItemAt(mark_cx, mark_cy, mark_c);
    if (k >= 0) {
        sel = k;
    }
    return 1;
}

// a click on a room of the mini map: a warp into that room, at that cell
// (of the rooms there the one of the selected line first)
static void MiniWarp(int x, int y) {
    const struct CastleRoom* r;
    const char* why;
    int i;
    if (!MiniPick(x, y)) {
        return;
    }
    r = sel < n_items ? RoomAt(mark_cx, mark_cy, mark_c, items[sel]) : NULL;
    for (i = 0; !r && i < g_CastleRoomCount; i++) {
        const struct CastleRoom* c = &g_CastleRooms[i];
        if ((c->stage & 0x20) == mark_c && mark_cx >= c->left &&
            mark_cx <= c->right && mark_cy >= RTOP(c) && mark_cy <= RBOT(c)) {
            r = c;
        }
    }
    if (!r) {
        return;
    }
    why = Cheat_Warp(r->stage, r->room, mark_cx, mark_cy - MapDy(r->stage));
    fprintf(stderr, "[menu] mini map click %d,%d: cell %d,%d %s room %d: %s\n",
            x, y, mark_cx, mark_cy, Castle_StageName(r->stage), r->room,
            why ? why : "warp");
    if (why) {
        MessageWhy(why); // shown in the header line
    } else {
        page = PAGE_MAIN;
        Close();
    }
}

// ---- pointer ----------------------------------------------------------------

static void Activate(void);

// the last text box drawn (Box); box_left = 1: Box puts it at box_x, box_y
static int box_x, box_y, box_w, box_h, box_left;

// the line of the text box under frame row y, or -1
static int BoxLineAt(int y, int count) {
    int top = box_y + BORDER;
    int i = (y - top + 3) / LINE_H;
    return y >= top - 3 && i >= 0 && i < count ? i : -1;
}

static int InBox(int x, int y) {
    return x >= box_x && x < box_x + box_w && y >= box_y && y < box_y + box_h;
}

#ifndef __SWITCH__
// PC: the mouse moved on the list page. Over a line it selects that line,
// as the D-pad does (its rooms go red on the mini map); over the mini map
// it selects the line of the room there (MiniPick). The box first: at 4:3
// the mini map lies behind it.
static void Hover(int x, int y) {
    int i, old = sel, old_cx = mark_cx, old_cy = mark_cy, b;
    const struct CastleRoom* r;
    if (InBox(x, y)) {
        mark_cx = mark_cy = -1;
        i = BoxLineAt(y, ListLines() + 1);
        if (i > 0) { // line 0 is the header
            sel = list_top + i - 1;
        }
    } else {
        MiniPick(x, y);
    }
    if (sel == old && mark_cx == old_cx && mark_cy == old_cy) {
        return;
    }
    b = mark_cx < 0 ? -1 : BossAt(mark_cx, mark_cy, mark_c);
    r = mark_cx < 0 ? NULL : RoomAt(mark_cx, mark_cy, mark_c, ITEM_ANY);
    fprintf(stderr,
            "[menu] pointer %d,%d: line %d item %X, map cell %d,%d place %s "
            "boss %d\n",
            x, y, sel, sel < n_items ? items[sel] : -1, mark_cx, mark_cy,
            r ? Castle_StageName(r->stage) : "-", b);
}
#endif

// The main page: the rows of the category on show. In the developer page
// one more line shows while the editor server is on: its address, under
// EDITOR. Line -> row, -1 for the address line.
static int MainRows(void) {
    int n = 0;
    while (cat_rows[cat][n] >= 0) {
        n++;
    }
    return n;
}
static int CatItem(void) { return cat_rows[cat][MIN(sel, MainRows() - 1)]; }
static int EditorRow(void) {
    int i;
    for (i = 0; Editor_Status() && cat_rows[cat][i] >= 0; i++) {
        if (cat_rows[cat][i] == ITEM_EDITOR) {
            return i;
        }
    }
    return -1;
}
static int MainLines(void) { return MainRows() + (EditorRow() >= 0); }
static int MainItemAt(int line) {
    int e = EditorRow();
    if (e < 0 || line <= e) {
        return line;
    }
    return line == e + 1 ? -1 : line - 1;
}
static int MainLineOf(int item) {
    int e = EditorRow();
    return e >= 0 && item > e ? item + 1 : item;
}
// the main page scrolls: MAIN_LINES on screen (with the hint and two help
// lines, which Box reserves on this page, they fit the stage rows 20..226 up
// to 11 lines), the first is main_top
#define MAIN_LINES 11
static int main_top;
static int MainVis(void) { return MIN(MAIN_LINES, MainLines()); }

// The mouse wheel over a box with more lines than it shows: the lines move
// by one, the selection stays on screen.
static void Scroll(int dir) {
    int *top, n, vis, lo, hi, line;
    if (page == PAGE_LIST) {
        top = &list_top, n = n_items, vis = ListLines();
    } else if (page == PAGE_CONTROLS) {
        top = &ctl_top, n = N_CTL, vis = CTL_LINES;
    } else if (page == PAGE_MAIN) {
        top = &main_top, n = MainLines(), vis = MainVis();
    } else {
        return;
    }
    *top = MAX(0, MIN(*top - dir, n - vis));
    lo = *top;
    hi = *top + vis - 1;
    if (page == PAGE_MAIN) {
        line = MAX(lo, MIN(MainLineOf(sel), hi));
        sel = MainItemAt(line) >= 0 ? MainItemAt(line) : MainItemAt(line - 1);
    } else {
        sel = MAX(lo, MIN(sel, hi));
    }
    fprintf(stderr, "[menu] wheel %s: page %d top %d of %d, line %d\n",
            dir > 0 ? "up" : "down", page, *top, n, sel);
}

// The map cell under frame point (x, y): 1 on the rooms area (between the
// text lines on top and the hints at the bottom), 0 elsewhere
static int MapCellAt(int x, int y, int* cx, int* cy) {
    int x0, y0, x1, y1;
    ViewRect(&x0, &y0, &x1, &y1);
    *cx = CellAt(x, ox16, zoom16);
    *cy = CellAt(y, oy16, zoom16);
    return x >= x0 && x < x1 && y >= y0 + 2 * LINE_H && y < map_bottom &&
           *cx >= 0 && *cx < 64 && *cy >= 0 && *cy < 64;
}

static void Tap(int x, int y) {
    int x0, y0, x1, y1, i, cx, cy;
    if (page == PAGE_MAP) {
        ViewRect(&x0, &y0, &x1, &y1);
        if (x >= x0 && x < x1 && y >= y0 && y < y0 + LINE_H) {
            SwitchCastle(); // a tap on the title line: "< NORMAL >"
        } else if (MapCellAt(x, y, &cx, &cy)) {
            cur_x = cx;
            cur_y = cy;
            FindOwners(cx, cy);
            fprintf(stderr, "[menu] map click %d,%d: cell %d,%d, %d rooms\n",
                    x, y, cx, cy, n_owners);
            Teleport();
        }
        return;
    }
    i = page == PAGE_MAIN       ? MainVis()
        : page == PAGE_CHEATS   ? N_CHEATS
        : page == PAGE_CONTROLS ? CTL_LINES + 1
        : page == PAGE_LIST     ? ListLines() + 1
                                : n_owners;
    if (page == PAGE_LIST && !InBox(x, y)) {
        MiniWarp(x, y); // a tap on a room of the mini map warps there
        return;
    }
    i = BoxLineAt(y, i);
    if (i >= 0 && page == PAGE_MAIN) {
        i += main_top;
    }
    if (page == PAGE_CONTROLS || page == PAGE_LIST) {
        if (i > 0 && !capture) { // line 0 is the header
            sel = (page == PAGE_LIST ? list_top : ctl_top) + i - 1;
            Activate();
        }
        return;
    }
    if (i >= 0 && page == PAGE_MAIN) {
        i = MainItemAt(i);
        if (i < 0) {
            return; // the address line
        }
    }
    if (i < 0) {
        if (page == PAGE_CHOOSE) {
            page = PAGE_MAP; // a tap outside the list
        }
        return;
    }
    sel = i;
    click_act = 1; // a click on CHARACTER picks the next one, it does not apply
    Activate();
    click_act = 0;
}

static int WarpPointer(int n, const int* x, const int* y);
static int WarpNear(int px, int py);
static void WarpSelect(int r);
extern s32 g_WarpMapCursor[3];
static int warp_click = -1; // a room tapped: PortMenu_WarpPick warps there
static int warp_cancel;             // a right click: back out of the warp map
static long long focus_tick = -1000; // SDL ticks (ms) of the last focus gain

void PortMenu_Touch(int n, const int* x, const int* y) {
    int i, d, pd, mx, my;
    if (WarpPointer(n, x, y)) { // the warp map: pick a room by pointer or touch
        pt_n = 0;
        return;
    }
    if (!is_open) {
        pt_n = 0;
        return;
    }
    n = n > 2 ? 2 : n;
    if (n == 1 && pt_n != 1) {
        pt_sx = x[0];
        pt_sy = y[0];
        pt_moved = pt_n == 2; // the end of a pinch is no tap
        // a drag that starts on the mini map pans it
        pt_mini = page == PAGE_LIST && InMini(x[0], y[0]) && !InBox(x[0], y[0]);
    } else if (n == 1) {
        if (abs(x[0] - pt_sx) + abs(y[0] - pt_sy) > 4) {
            pt_moved = 1;
        }
        if (pt_moved && page == PAGE_MAP) {
            ox16 += (x[0] - pt_x[0]) << 4;
            oy16 += (y[0] - pt_y[0]) << 4;
        } else if (pt_moved && pt_mini && page == PAGE_LIST &&
                   (x[0] != pt_x[0] || y[0] != pt_y[0])) {
            mini_ox += (x[0] - pt_x[0]) << 4;
            mini_oy += (y[0] - pt_y[0]) << 4;
            if (!mini_user) {
                fprintf(stderr, "[menu] mini map drag from %d,%d\n", pt_sx,
                        pt_sy);
            }
            mini_user = 1;
        }
    } else if (n == 2 && pt_n == 2 && page == PAGE_MAP) {
        d = abs(x[0] - x[1]) + abs(y[0] - y[1]);
        pd = abs(pt_x[0] - pt_x[1]) + abs(pt_y[0] - pt_y[1]);
        mx = (x[0] + x[1]) / 2;
        my = (y[0] + y[1]) / 2;
        ox16 += (mx - (pt_x[0] + pt_x[1]) / 2) << 4;
        oy16 += (my - (pt_y[0] + pt_y[1]) / 2) << 4;
        if (pd > 4 && d > 4) {
            Zoom(zoom16 * d / pd, mx, my);
        }
    } else if (n == 0 && pt_n == 1 && !pt_moved) {
        Tap(pt_x[0], pt_y[0]);
    } else if (n == 0 && pt_n == 1 && pt_mini) {
        fprintf(stderr, "[menu] mini map drag to %d,%d: origin %d,%d\n",
                pt_x[0], pt_y[0], mini_ox >> 4, mini_oy >> 4);
    } else if (n == 0 && pt_n == 1 && page == PAGE_MAP) {
        fprintf(stderr, "[menu] map drag to %d,%d: origin %d,%d\n", pt_x[0],
                pt_y[0], ox16 >> 4, oy16 >> 4);
    }
    for (i = 0; i < n; i++) {
        pt_x[i] = x[i];
        pt_y[i] = y[i];
    }
    pt_n = n;
}

// The wheel: zoom the map about the pointer, the mini map too (outside the
// list box); over a box scroll it.
void PortMenu_Wheel(int dir, int x, int y) {
    if (!is_open) {
        return;
    }
    if (page == PAGE_MAP) {
        Zoom(dir > 0 ? zoom16 * 5 / 4 + 1 : zoom16 * 4 / 5, x, y);
        fprintf(stderr, "[menu] map zoom %d at %d,%d\n", zoom16, x, y);
    } else if (page == PAGE_LIST && !InBox(x, y) && InMini(x, y)) {
        MiniZoom(dir, x, y);
    } else if (InBox(x, y)) {
        Scroll(dir);
    }
}

#ifndef __SWITCH__
// PC: the mouse through the psyz SDL event hook. The Switch reads touch from
// libnx (switch_nx.c); the SDL-switch touch code is not used.
static PsyzOverlayEventCB_SDL3 prev_cb;
static int mouse_down, mouse_x, mouse_y;
static int mouse_seen = 1; // 0: a press not seen by a frame yet (fast click)

// SOTN_TEST_PAD=switch|xbox|ps|other (tests; any other value: switch): an
// SDL virtual pad of that type (vendor and product ids). Keypad 6, 2, 8, 4
// hold its right, bottom, top, left face button, keypad 5 its Start; keypad
// 1, 3, 7, 9 push its left stick left, right, up, down; keypad /, *, -, +
// its right stick.
static SDL_Joystick* test_pad;
static void TestPadInit(void) {
    static const struct {
        const char* name;
        u16 vendor, product;
    } types[] = {{"switch", 0x057E, 0x2009}, // Switch Pro Controller
                 {"xbox", 0x045E, 0x028E},   // Xbox 360 Controller
                 {"ps", 0x054C, 0x09CC},     // DualShock 4
                 {"other", 0x1209, 0x0001}}; // a vendor SDL does not know
    SDL_VirtualJoystickDesc d;
    const char* want = getenv("SOTN_TEST_PAD");
    int t = 0;
    if (!want) {
        return;
    }
    while (t < LEN(types) - 1 && strcmp(want, types[t].name)) {
        t++;
    }
    t = strcmp(want, types[t].name) ? 0 : t;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.vendor_id = types[t].vendor;
    d.product_id = types[t].product;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    d.name = "sotn test pad";
    test_pad = SDL_OpenJoystick(SDL_AttachVirtualJoystick(&d));
    fprintf(stderr, "[pad] test pad %s: %s\n", types[t].name,
            test_pad ? "on" : SDL_GetError());
}
static void TestPadKey(const SDL_Event* e) {
    static const struct {
        SDL_Scancode key;
        SDL_GamepadButton button;
    } keys[] = {{SDL_SCANCODE_KP_6, SDL_GAMEPAD_BUTTON_EAST},
                {SDL_SCANCODE_KP_2, SDL_GAMEPAD_BUTTON_SOUTH},
                {SDL_SCANCODE_KP_8, SDL_GAMEPAD_BUTTON_NORTH},
                {SDL_SCANCODE_KP_4, SDL_GAMEPAD_BUTTON_WEST},
                {SDL_SCANCODE_KP_5, SDL_GAMEPAD_BUTTON_START}};
    static const struct {
        SDL_Scancode key;
        u8 axis;
        s16 value;
    } sticks[] = {{SDL_SCANCODE_KP_1, SDL_GAMEPAD_AXIS_LEFTX, -32767},
                  {SDL_SCANCODE_KP_3, SDL_GAMEPAD_AXIS_LEFTX, 32767},
                  {SDL_SCANCODE_KP_7, SDL_GAMEPAD_AXIS_LEFTY, -32767},
                  {SDL_SCANCODE_KP_9, SDL_GAMEPAD_AXIS_LEFTY, 32767},
                  {SDL_SCANCODE_KP_DIVIDE, SDL_GAMEPAD_AXIS_RIGHTX, -32767},
                  {SDL_SCANCODE_KP_MULTIPLY, SDL_GAMEPAD_AXIS_RIGHTX, 32767},
                  {SDL_SCANCODE_KP_MINUS, SDL_GAMEPAD_AXIS_RIGHTY, -32767},
                  {SDL_SCANCODE_KP_PLUS, SDL_GAMEPAD_AXIS_RIGHTY, 32767}};
    int i, down = e->type == SDL_EVENT_KEY_DOWN;
    if (!test_pad ||
        (e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP)) {
        return;
    }
    for (i = 0; i < LEN(keys); i++) {
        if (e->key.scancode == keys[i].key) {
            SDL_SetJoystickVirtualButton(test_pad, keys[i].button, down);
            if (down && !e->key.repeat) {
                fprintf(stderr, "[pad] test pad button %d: down\n",
                        keys[i].button);
            }
        }
    }
    for (i = 0; i < LEN(sticks); i++) {
        if (e->key.scancode == sticks[i].key) {
            SDL_SetJoystickVirtualAxis(
                test_pad, sticks[i].axis, down ? sticks[i].value : 0);
        }
    }
}

static void OnSdlEvent(const SDL_Event* e) {
    int x, y;
    TestPadKey(e);
    // SDL gives window points; the present rect is in pixels (HiDPI)
    SDL_Window* w = SDL_GetWindowFromID(e->motion.windowID);
    float k = w ? SDL_GetWindowPixelDensity(w) : 1.0f;
    if (e->type == SDL_EVENT_MOUSE_MOTION) {
        Wide_ScreenToFrame(
            e->motion.x * k, e->motion.y * k, &mouse_x, &mouse_y);
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               e->button.button == SDL_BUTTON_LEFT) {
        Wide_ScreenToFrame(
            e->button.x * k, e->button.y * k, &mouse_x, &mouse_y);
        mouse_down = 1;
        mouse_seen = 0;
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               e->button.button == SDL_BUTTON_RIGHT) {
        warp_cancel = g_WarpMapCursor[0] && !is_open &&
                      SDL_GetTicks() - focus_tick > 300;
    } else if (e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        focus_tick = SDL_GetTicks(); // the click that focuses is no click
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP &&
               e->button.button == SDL_BUTTON_LEFT) {
        mouse_down = 0;
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL && e->wheel.y != 0) {
        Wide_ScreenToFrame(e->wheel.mouse_x * k, e->wheel.mouse_y * k, &x, &y);
        PortMenu_Wheel(e->wheel.y > 0 ? 1 : -1, x, y);
    }
    if (prev_cb) {
        prev_cb(e);
    }
}
#endif

// ---- pad ----------------------------------------------------------------------

// the rows that are one on/off value in a variable
static int* ToggleVar(int it) {
    switch (it) {
    case ITEM_CDROOMS: return &g_SkipCdRooms;
    case ITEM_LOADING: return &g_ShowLoadingText;
    case ITEM_EDGEFADE: return &g_RoomEdgeFade;
    case ITEM_DIALOGSKIP: return &g_DialogSkip;
    case ITEM_MENUFILL: return &g_MenuFill;
    case ITEM_OVERLAY: return &g_OverlayOn;
    case ITEM_FPS: return &g_FpsOn;
    case ITEM_EDITOR: return &g_EditorOn;
    case ITEM_WARPMAP: return &g_WarpMapPick;
    case ITEM_ELEVATOR: return &g_ElevatorFront;
    case ITEM_HD: return &g_HdTextures;
    case ITEM_STICKDPAD: return &g_StickDpad;
    case ITEM_PADBG: return &g_BackgroundInput;
    case ITEM_SKIPPROLOGUE: return &g_Cheat.skipPrologue;
    case ITEM_PROLOGUEFF: return &g_PrologueFF;
    case ITEM_MAP100: return &g_Cheat.map;
    case ITEM_HEROPULSE: return &g_MapHeroPulse;
    case ITEM_HDDUMP: return &g_HdDump;
    case ITEM_HDSTATS: return &g_HdStats;
    case ITEM_STITCH: return &g_RoomStitch;
    case ITEM_RELICHINT: return &g_RelicHint;
    }
    return NULL;
}

static void Change(int dir) {
    int scale, lo, hi, *flag, it = CatItem();
    if (page == PAGE_LIST) { // a page up or down, no wrap
        sel += dir * ListLines();
        sel = sel < 0 ? 0 : sel >= n_items ? n_items - 1 : sel;
        return;
    }
    if (page == PAGE_CONTROLS) {
        return;
    }
    if (page == PAGE_CHEATS) {
        flag = sel == CH_REFILL       ? &g_Cheat.refill
               : sel == CH_INVINCIBLE ? &g_Cheat.invincible
               : sel == CH_WALLS      ? &g_Cheat.walls
               : sel == CH_ITEMS      ? &g_Cheat.items
                                      : NULL;
        if (sel == CH_CHARACTER) {
            // only the values that work; the rest are kept for later
            g_Cheat.character = Cheat_CharacterNext(g_Cheat.character, dir);
            fprintf(stderr, "[cheat] menu: character %d\n", g_Cheat.character);
            Wide_SaveIni();
        } else if (flag) {
            *flag ^= 1;
            fprintf(stderr,
                    "[cheat] menu: refill %d invincible %d walls %d items %d\n",
                    g_Cheat.refill, g_Cheat.invincible, g_Cheat.walls,
                    g_Cheat.items);
            Wide_SaveIni();
        }
        return;
    }
    Wide_MenuGet(&scale, &lo, &hi);
    if (it == ITEM_SCALE) { // lo .. hi, then MAX
        scale = scale == WIDE_SCALE_MAX ? (dir < 0 ? hi : 0)
                : scale                 ? scale + dir
                                        : (dir > 0 ? lo : hi);
        if (scale == hi + 1) {
            scale = WIDE_SCALE_MAX;
        }
        if (scale == WIDE_SCALE_MAX || (scale >= lo && scale <= hi)) {
            Wide_MenuSet(scale);
        }
#ifndef __SWITCH__
    } else if (it == ITEM_FULLSCREEN) {
        g_Fullscreen = !Psyz_VideoGetFullscreen();
        Psyz_VideoSetFullscreen(g_Fullscreen);
        fprintf(stderr, "[video] menu: fullscreen %d\n", g_Fullscreen);
        Wide_SaveIni();
#endif
    } else if (it == ITEM_VOICE) {
        // the next spoken line uses the new set
        // only the sets whose clips are there (PS1 always)
        do {
            g_VoiceSet = (g_VoiceSet + dir + VOICE_COUNT) % VOICE_COUNT;
        } while (!Voice_SetAvailable(g_VoiceSet));
        fprintf(
            stderr, "[voice] menu: voice %s\n", g_VoiceSetNames[g_VoiceSet]);
        Wide_SaveIni();
    } else if (it == ITEM_MUSIC) {
        if (Music_FilesAvailable()) { // the track restarts from the new source
            g_MusicFiles ^= 1;
            Music_Switched();
            fprintf(stderr, "[music] menu: music_source %s\n",
                    g_MusicFiles ? "enhanced" : "original");
            Wide_SaveIni();
        }
    } else if (it == ITEM_LANGUAGE) {
        // saved only: the language of this run is fixed at boot
        g_LangNext ^= 1;
        fprintf(stderr, "[lang] menu: language %s at the next start\n",
                g_LangNext ? "uk" : "en");
        Wide_SaveIni();
    } else if (it == ITEM_BUTTONS) {
        // AUTO, PLAYSTATION, XBOX, NINTENDO
        static const int order[] = {-1, BUTTONS_PSX, BUTTONS_XBOX,
                                    BUTTONS_SWITCH};
        int i = 0;
        while (i < 3 && order[i] != g_ButtonPick) {
            i++;
        }
        g_ButtonPick = order[(i + dir + 4) % 4];
        Buttons_Update();
        fprintf(stderr, "[buttons] menu: buttons %s\n",
                g_ButtonPick < 0 ? "auto" : g_ButtonSetNames[g_ButtonPick]);
        Wide_SaveIni();
    } else if (it == ITEM_ASPECT) {
        Wide_SetAspect(g_Aspect + dir); // at the next vsync
    } else if (it == ITEM_CONFIRM) {
        g_MenuConfirm = (g_MenuConfirm + dir + MENU_CONFIRMS) % MENU_CONFIRMS;
        fprintf(stderr, "[pad] menu: menu_confirm %s (now %s)\n",
                g_MenuConfirmNames[g_MenuConfirm],
                PortMenu_ConfirmMode() == MENU_CONFIRM_AUTO
                    ? "stock"
                    : g_MenuConfirmNames[PortMenu_ConfirmMode()]);
        Wide_SaveIni();
    } else if (it == ITEM_WARPMAP) {
        g_WarpMapPick ^= 1;
        fprintf(stderr, "[warp] menu: warp_map %d\n", g_WarpMapPick);
        Wide_SaveIni();
    } else if (it == ITEM_CDROOMS || it == ITEM_LOADING) {
        flag = it == ITEM_CDROOMS ? &g_SkipCdRooms : &g_ShowLoadingText;
        *flag ^= 1;
        fprintf(stderr,
                "[cdroom] menu: skip_cd_rooms %d show_loading_text %d\n",
                g_SkipCdRooms, g_ShowLoadingText);
        Wide_SaveIni();
    } else if (it == ITEM_DIALOGSKIP) {
        g_DialogSkip ^= 1;
        fprintf(stderr, "[dlgskip] menu: dialogue_skip %d\n", g_DialogSkip);
        Wide_SaveIni();
    } else if (it == ITEM_MENUFILL) {
        g_MenuFill ^= 1;
        fprintf(stderr, "[video] menu: menu_fill_screen %d\n", g_MenuFill);
        Wide_SaveIni();
    } else if (it == ITEM_EDGEFADE) {
        g_RoomEdgeFade ^= 1;
        fprintf(stderr, "[video] menu: room_edge_fade %d\n", g_RoomEdgeFade);
        Wide_SaveIni();
    } else if (it == ITEM_OVERLAY || it == ITEM_FPS) {
        flag = it == ITEM_OVERLAY ? &g_OverlayOn : &g_FpsOn;
        *flag ^= 1;
        fprintf(stderr, "[overlay] menu: overlay %d fps %d\n", g_OverlayOn,
                g_FpsOn);
        Wide_SaveIni();
    } else if (it == ITEM_EDITOR) {
        // Editor_OnFrame opens or closes the port at the next vsync
        g_EditorOn ^= 1;
        fprintf(stderr, "[editor] menu: editor %d\n", g_EditorOn);
        Wide_SaveIni();
    } else if (it == ITEM_STICKDEAD || it == ITEM_LOOKDEAD) {
        flag = it == ITEM_STICKDEAD ? &g_StickDead : &g_LookDead;
        *flag = MAX(5, MIN(90, *flag + dir * 5));
        Wide_SaveIni();
    } else if (it == ITEM_LOOKRANGE) {
        g_LookRange = MAX(0, MIN(120, g_LookRange + dir * 8));
        Wide_SaveIni();
    } else if (it == ITEM_RELICTOG) {
        g_RelicToggle ^= 1;
        Wide_SaveIni();
    } else if (it == ITEM_JPCARDS) {
        g_JpCards = (g_JpCards + dir + JP_CARDS_MODES) % JP_CARDS_MODES;
        Wide_SaveIni(); // read once at the start
    } else if ((flag = ToggleVar(it))) {
        *flag ^= 1;
        fprintf(stderr, "[menu] row %d = %d\n", it, *flag);
        Wide_SaveIni();
    }
}

// Cross, Circle or a tap on a line
static void Restart(void);
static void Activate(void) {
    int it = page == PAGE_MAIN ? CatItem() : -1;
    if (page == PAGE_MAIN) {
        static const s8 cat_of[] = {CAT_DISPLAY, CAT_LANG, CAT_CONTROLS,
                                    CAT_QOL, -1, CAT_DEV};
        if (it == ITEM_CAT_CHEATS) {
            cat_from = sel;
            page = PAGE_CHEATS;
            sel = 0;
        } else if (it >= ITEM_CAT_DISPLAY && it <= ITEM_CAT_DEV) {
            cat_from = sel;
            cat = cat_of[it];
            sel = 0;
        } else if (it == ITEM_KEYS) {
            page = PAGE_CONTROLS;
            sel = ctl_top = 0;
        } else if (it == ITEM_BACK) {
            cat = CAT_TOP;
            sel = cat_from;
        } else if (it == ITEM_CLOSE) {
            Close();
        } else if (it == ITEM_EXIT || it == ITEM_RESTART) {
            if (exit_armed == it) {
                if (it == ITEM_EXIT) {
                    Quit();
                } else {
                    Restart();
                }
            }
            exit_armed = exit_armed == it ? 0 : it;
        } else {
            Change(1);
        }
    } else if (page == PAGE_CHEATS) {
        if (sel == CH_TELEPORT && g_GameState != Game_Play) {
            // greyed out: the help line says why
        } else if (sel == CH_TELEPORT) {
            map_ready = 0; // the map, on the player's room
            OpenMap();
        } else if (sel == CH_CHARACTER && click_act) {
            Change(1);
        } else if (sel == CH_CHARACTER) {
            const char* why = Cheat_ApplyCharacter();
            if (why) {
                MessageWhy(why); // shown in the CHARACTER line
            } else {
                page = PAGE_MAIN;
                Close();
            }
        } else if (sel == CH_BACK) {
            page = PAGE_MAIN;
            sel = cat_from;
        } else {
            Change(1);
        }
    } else if (page == PAGE_CONTROLS) {
        if (sel < N_CTL_BUTTONS) {
            capture = CAPTURE_TIME;
            capture_armed = 0;
        } else if (sel == CTL_RESET) {
            Psyz_PadsMapReset();
            fprintf(stderr, "[controls] menu: defaults\n");
            Wide_SaveIni();
        } else {
            page = PAGE_MAIN;
            sel = 0; // the KEYS row of its category
        }
    } else if (page == PAGE_CHOOSE && sel < n_owners) {
        Go(owners[sel]);
    } else if (page == PAGE_LIST && sel < n_items) {
        int it = items[sel];
        const char* why = NULL;
        *(list_boss ? &boss_sel : &place_sel) = sel;
        if (it == ITEM_MAP) {
            OpenMap();
        } else if (it == ITEM_BOSSES) {
            OpenList(1);
        } else if ((why = it >= ITEM_BOSS ? Cheat_WarpBoss(it - ITEM_BOSS)
                                          : Cheat_WarpStart(it))) {
            MessageWhy(why); // shown in the header line
        } else {
            page = PAGE_MAIN;
            Close();
        }
    }
}

// Set the input of the selected PS button; the PS button that had it gets
// this one's old input (a swap), so no input drives two PS buttons.
static void Bind(PsyzInputDevice dev, int code) {
    int bit = ctl_rows[sel].bit, old = Psyz_PadsMap(dev, bit, code), i;
    for (i = 0; i < PSYZ_PAD_BUTTONS; i++) {
        if (i != bit && Psyz_PadsMap(dev, i, PSYZ_INPUT_QUERY) == code) {
            Psyz_PadsMap(dev, i, old);
        }
    }
    fprintf(stderr, "[controls] menu: %s %s = %s\n", ctl_rows[sel].en,
            dev == PSYZ_INPUT_KEY ? "key" : "pad",
            Psyz_PadsInputName(dev, code));
    Wide_SaveIni();
}

// A capture: wait until every input is up, then take the first key (PC)
// or gamepad button pressed. The time out keeps the old input.
static void Capture(void) {
    int key = CTL_KEYS ? Psyz_PadsHeld(PSYZ_INPUT_KEY) : PSYZ_INPUT_NONE;
    int pad = Psyz_PadsHeld(PSYZ_INPUT_GAMEPAD);
    if (--capture <= 0) {
        capture = 0;
        prev = 0xFFFF;
    } else if (!capture_armed) {
        capture_armed = key < 0 && pad < 0;
    } else if (key >= 0 || pad >= 0) {
        capture = 0;
        prev = 0xFFFF; // its PS button may change: no tap until it is up
        if (key >= 0) {
            Bind(PSYZ_INPUT_KEY, key);
        } else {
            Bind(PSYZ_INPUT_GAMEPAD, pad);
        }
    }
}

static void MapPad(u16 pressed, u16 tap) {
    int dx = 0, dy = 0;
    u16 dirs = pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);

    // a held direction repeats after 12 frames, every 2 frames
    if (dirs && dirs == hold_dir) {
        hold++;
    } else {
        hold = 0;
        hold_dir = dirs;
    }
    if ((tap & dirs) || (hold > 12 && !(hold & 1))) {
        dx = (dirs & PAD_RIGHT ? 1 : 0) - (dirs & PAD_LEFT ? 1 : 0);
        dy = (dirs & PAD_DOWN ? 1 : 0) - (dirs & PAD_UP ? 1 : 0);
    }
    if (dx || dy) {
        cur_x = (cur_x + dx) & 63;
        cur_y = (cur_y + dy) & 63;
        KeepCursorVisible();
        fprintf(stderr, "[menu] map cursor %d,%d\n", cur_x, cur_y);
    }
    if (tap & (PAD_L1 | PAD_L2)) {
        Zoom(zoom16 * 2 / 3, CellX(cur_x), CellY(cur_y));
    } else if (tap & (PAD_R1 | PAD_R2)) {
        Zoom(zoom16 * 3 / 2, CellX(cur_x), CellY(cur_y));
        KeepCursorVisible();
    } else if (tap & (PAD_SQUARE | PAD_SELECT)) {
        SwitchCastle();
    } else if (tap & PAD_OK) {
        Teleport();
    } else if (tap & PAD_BACK) {
        // back on the line of the place under the cursor
        OpenList(0);
        dx = ItemAt(cur_x, cur_y, castle);
        sel = dx >= 0 ? dx : 0;
    }
}

// Called from ReadPads for pad 1. Returns the buttons the game sees.
u16 PortMenu_Pad(u16 pressed) {
    u16 tap;
    int count;
    pressed = MenuButtons(pressed, is_open);
    // the stick in the game and in every menu; the D-pad and the keys win,
    // so the game never sees left and right (or up and down) at once
    if (!(pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT))) {
        pressed |= StickDirs();
    }
    tap = pressed & ~prev;
#ifndef __SWITCH__
    static int hooked;
    if (!hooked) {
        hooked = 1;
        prev_cb = Psyz_OverlayEvent_SDL3(OnSdlEvent);
        TestPadInit();
    }
    // a press and release inside one frame still counts as a tap
    count = mouse_down || !mouse_seen;
    mouse_seen = 1;
    PortMenu_Touch(count, &mouse_x, &mouse_y);
    {
        // the mouse over a line or the mini map selects; only when it
        // moves, so a resting pointer does not fight the D-pad
        static int hover_x, hover_y;
        int cx, cy;
        if (is_open && page == PAGE_LIST &&
            (mouse_x != hover_x || mouse_y != hover_y)) {
            Hover(mouse_x, mouse_y);
        }
        // on the map the cursor follows the pointer (not in a drag)
        if (is_open && page == PAGE_MAP && !mouse_down &&
            (mouse_x != hover_x || mouse_y != hover_y) &&
            MapCellAt(mouse_x, mouse_y, &cx, &cy) &&
            (cx != cur_x || cy != cur_y)) {
            cur_x = cx;
            cur_y = cy;
            FindOwners(cx, cy);
            fprintf(stderr, "[menu] map pointer %d,%d: cell %d,%d %s room %d\n",
                    mouse_x, mouse_y, cx, cy,
                    n_owners ? Castle_StageName(owners[0]->stage) : "-",
                    n_owners ? owners[0]->room : -1);
        }
        if (g_WarpMapCursor[0] && !is_open && !mouse_down &&
            (mouse_x != hover_x || mouse_y != hover_y)) {
            WarpSelect(WarpNear(mouse_x, mouse_y)); // the pointer picks
        }
        hover_x = mouse_x;
        hover_y = mouse_y;
    }
#endif
    prev = pressed;
    if (drawn > 0) {
        drawn--;
    }
    Buttons_Update();
    if (capture) { // the held input is no tap when the capture ends
        if (is_open && drawn) {
            Capture();
            return 0;
        }
        capture = 0;
    }

    if ((pressed & COMBO) == COMBO && (tap & COMBO)) {
        if (is_open) {
            Close();
        } else if (drawn > 0) {
            is_open = 1;
            page = PAGE_MAIN;
            cat = CAT_TOP;
            sel = cat_from = 0;
            Editor_RefreshAddress(); // the network may have changed
        }
        return 0;
    }
    if (is_open && !drawn) {
        Close(); // nothing draws the menu (load, movie): give the pad back
    }
    if (!is_open) {
        if (wait_release) {
            if (pressed) {
                return 0;
            }
            wait_release = 0;
        }
        return pressed;
    }

    if (tap) {
        mark_cx = -1; // the D-pad line has no map cell
    }
    if (page == PAGE_MAP) {
        MapPad(pressed, tap);
        return 0;
    }
    count = page == PAGE_MAIN       ? MainRows()
            : page == PAGE_CHEATS   ? N_CHEATS
            : page == PAGE_CONTROLS ? N_CTL
            : page == PAGE_LIST     ? n_items
                                    : n_owners;
    if (tap & ~PAD_OK) {
        exit_armed = 0; // only a second OK on EXIT quits
    }
    if (count <= 0) {
        page = PAGE_MAP;
    } else if (tap & PAD_UP) {
        sel = (sel + count - 1) % count;
    } else if (tap & PAD_DOWN) {
        sel = (sel + 1) % count;
    } else if ((tap & PAD_LEFT) && page != PAGE_CHOOSE) {
        Change(-1);
    } else if ((tap & PAD_RIGHT) && page != PAGE_CHOOSE) {
        Change(1);
    } else if (tap & PAD_OK) {
        Activate();
    } else if (tap & PAD_BACK) {
        if (page == PAGE_CHEATS || page == PAGE_CONTROLS) {
            sel = page == PAGE_CHEATS ? cat_from : 0;
            page = PAGE_MAIN;
        } else if (page == PAGE_MAIN && cat != CAT_TOP) {
            cat = CAT_TOP;
            sel = cat_from;
        } else if (page == PAGE_CHOOSE) {
            page = PAGE_MAP;
        } else if (page == PAGE_LIST && list_boss) {
            boss_sel = sel;
            OpenList(0);
            sel = 1; // the boss list line
        } else if (page == PAGE_LIST) {
            place_sel = sel;
            page = PAGE_CHEATS;
            sel = CH_TELEPORT;
        } else {
            Close();
        }
    }
    return 0;
}

// ---- drawing ------------------------------------------------------------------

// 5x7 glyphs, '#' is a lit pixel, one string per row from the top.
#define G(a, b, c, d, e, f, g) a b c d e f g
static const struct {
    char c;
    const char* rows;
} font[] = {
    {'A', G(".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#")},
    {'B', G("####.", "#...#", "#...#", "####.", "#...#", "#...#", "####.")},
    {'C', G(".###.", "#...#", "#....", "#....", "#....", "#...#", ".###.")},
    {'D', G("####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####.")},
    {'E', G("#####", "#....", "#....", "####.", "#....", "#....", "#####")},
    {'F', G("#####", "#....", "#....", "####.", "#....", "#....", "#....")},
    {'G', G(".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####")},
    {'H', G("#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#")},
    {'I', G(".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###.")},
    {'J', G("..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##..")},
    {'K', G("#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#")},
    {'L', G("#....", "#....", "#....", "#....", "#....", "#....", "#####")},
    {'M', G("#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#")},
    {'N', G("#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#")},
    {'O', G(".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.")},
    {'P', G("####.", "#...#", "#...#", "####.", "#....", "#....", "#....")},
    {'Q', G(".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#")},
    {'R', G("####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#")},
    {'S', G(".####", "#....", "#....", ".###.", "....#", "....#", "####.")},
    {'T', G("#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..")},
    {'U', G("#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.")},
    {'V', G("#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#..")},
    {'W', G("#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#.")},
    {'X', G("#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#")},
    {'Y', G("#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#..")},
    {'Z', G("#####", "....#", "...#.", "..#..", ".#...", "#....", "#####")},
    {'0', G(".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###.")},
    {'1', G("..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###.")},
    {'2', G(".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####")},
    {'3', G("#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###.")},
    {'4', G("...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#.")},
    {'5', G("#####", "#....", "####.", "....#", "....#", "#...#", ".###.")},
    {'6', G("..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###.")},
    {'7', G("#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#...")},
    {'8', G(".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###.")},
    {'9', G(".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##..")},
    {':', G(".....", ".##..", ".##..", ".....", ".##..", ".##..", ".....")},
    {'<', G("...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#.")},
    {'>', G(".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#...")},
    {'-', G(".....", ".....", ".....", "#####", ".....", ".....", ".....")},
    {'.', G(".....", ".....", ".....", ".....", ".....", ".##..", ".##..")},
    {',', G(".....", ".....", ".....", ".....", ".##..", "..#..", ".#...")},
    {'/', G("....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#....")},
    {'(', G("..#..", ".#...", "#....", "#....", "#....", ".#...", "..#..")},
    {')', G("..#..", "...#.", "....#", "....#", "....#", "...#.", "..#..")},
    {'[', G(".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###.")},
    {']', G(".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###.")},
    {'%', G("##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##")},
    {'\'', G("..#..", "..#..", ".#...", ".....", ".....", ".....", ".....")},
};
#undef G

static TILE tiles[2][MAX_TILES];
static int tile_buf;
static int tz = 1; // Text zoom: the overlay over a title or menu frame
static int tile_count, tile_peak;
static P_TAG* menu_tail; // the tiles go after this prim (PortMenu_Draw)
static int semi;         // 1: the next tiles are half transparent
// a frame of non-square pixels (the prologue scroll: 512 columns in 4:3):
// px_a is the width of a frame pixel on the screen over its height. The
// menu draws in square pixels about the frame centre px_cx (x * 1 / px_a),
// as Wide_ScreenToFrame maps the pointer.
static float px_a = 1;
static int px_cx;

static int clip_x0, clip_x1; // Rect keeps to [x0, x1) while x1 > x0 (Marquee)

static void Rect(int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    TILE* t;
    if (clip_x1 > clip_x0) {
        if (x < clip_x0) {
            w -= clip_x0 - x;
            x = clip_x0;
        }
        w = MIN(w, clip_x1 - x);
    }
    if (tile_count >= MAX_TILES || w <= 0 || h <= 0) {
        return;
    }
    if (px_a != 1) {
        int x1 = px_cx + (int)((x + w - px_cx) / px_a + 0.5f + 4096) - 4096;
        x = px_cx + (int)((x - px_cx) / px_a + 0.5f + 4096) - 4096;
        w = MAX(x1 - x, 1);
    }
    t = &tiles[tile_buf][tile_count++];
    setTile(t);
    setSemiTrans(t, semi);
    setRGB0(t, r, g, b);
    t->x0 = x;
    t->y0 = y;
    t->w = w;
    t->h = h;
    addPrim(menu_tail, t);
}

// A button mark: one tile per run of one colour, 11 rows from y - 2 (its
// row 9 on the baseline of the 5x7 glyphs), in its own colours
// (g_ButtonRgb): the text tint never colours a mark. Returns its width.
static int Mark(int x, int y, int b) {
    int row, col, run, w;
    u8 px[11][16];
    const u8* c;
    w = Buttons_Rows(g_ButtonSet, b, px);
    for (row = 0; row < 11; row++) {
        for (col = 0; col < w; col += run) {
            for (run = 1; col + run < w && px[row][col + run] == px[row][col];
                 run++) {
            }
            c = g_ButtonRgb[px[row][col]];
            if (px[row][col]) {
                Rect(x + col, y - 2 + row, run, 1, c[0], c[1], c[2]);
            }
        }
    }
    return w;
}

// One tile per run of lit pixels in a glyph row.
// A byte 1..BUTTON_MARKS is a button mark (HINT_OK).
static void Text(int x, int y, const char* s, u8 r, u8 g, u8 b) {
    int i, row, col, run;
    const char* rows;
    for (; *s; s++, x += CELL_W * tz) {
        if ((u8)*s <= BUTTON_MARKS) {
            x += Mark(x, y, *s - 1) + 1 - CELL_W;
            continue;
        }
        rows = NULL;
        for (i = 0; i < LEN(font); i++) {
            if (font[i].c == *s) {
                rows = font[i].rows;
            }
        }
        if (!rows) {
            continue; // space
        }
        for (row = 0; row < 7; row++) {
            for (col = 0; col < 5; col += run ? run : 1) {
                for (run = 0; col + run < 5 && rows[row * 5 + col + run] == '#';
                     run++) {
                }
                if (run) {
                    Rect(x + col * tz, y + row * tz, run * tz, tz, r, g, b);
                }
            }
        }
    }
}

// ---- uk: one serif font -------------------------------------------------------
// Under uk every line of the menu is in the 8x11 serif cells of the pause
// menu font (Lang_MenuCell): the dialogue font's Cyrillic, and Latin
// letters, digits and marks drawn to match it (tools/lang/serif_latin.py).
// The words are the port table's (tools/lang/uk/ua_port.json, P_*). A uk
// line is cell codes with an FF end; MARK_ESC then 1 + b is the mark of PS
// button b. en lines stay ASCII in the 5x7 font, as does the debug overlay.
#undef P_OVERLAY // MinGW process.h defines it
#define P_CASTLE_MAP 74 // a status page word
enum {
    P_SCALE = 20,
    P_VOICE,
    P_LANGUAGE,
    P_BUTTONS,
    P_OVERLAY,
    P_EDITOR,
    P_CHEATS,
    P_EXIT,
    P_CLOSE,
    P_OFF,
    P_ON,
    P_OK,
    P_BACK,
    P_AUTO,
    P_RESTART,
    P_AGAIN, // EXIT armed: the OK mark follows
    P_REFILL,
    P_INVINCIBLE,
    P_WALLS,
    P_ITEMS,
    P_SKIP,
    P_MAP,
    P_CHARACTER,
    P_GAME,
    P_ALUCARD,
    P_RICHTER,
    P_MARIA,
    P_APPLY,
    P_TELEPORT,
    P_NO,
    P_ROOM,
    P_ROOMS,
    P_NORMAL,
    P_INVERTED,
    P_GO,
    P_BOSS, // P_SAVE, P_WARP follow (mark_names)
};
// the Controls page; 201..208 the PS button words of ctl_rows
enum {
    P_CONTROLS = 200,
    P_BUTTON = 209,
    P_KEYBOARD,
    P_GAMEPAD,
    P_PRESS,
    P_DEFAULTS,
    P_FULLSCREEN, // the main page: after the Controls words
    P_MAX,
    P_CDROOMS, // the main page: SKIP CD ROOMS, LOADING TEXT
    P_LOADING,
    P_EDGEFADE, // ROOM EDGE FADE
    P_CONFIRM,  // MENU OK (menu_confirm)
    P_ASPECT,   // ASPECT
    P_LIST,     // the map hints: LIST, the mouse ones, MOVE (the D-pad)
    P_CLICK,
    P_WHEEL,
    P_DRAG,
    P_PAN,
    P_MOVE,
    P_SATURN, // the SS tag of the teleport lists (the legend)
    P_MUSIC,  // 228..231: the MUSIC line
    P_ORIGINAL,
    P_ENHANCED,
    P_NOFILES,
    P_WARP_GO = 232, // the warp map: Телепортуватись, Обрати, no other warp
    P_WARP_PICK,
    P_WARP_NONE,
    P_DIALOGSKIP, // 235: DIALOGUE SKIP
    P_MENUFILL, // 236: MENU FILL
};
#define LINE_MAX 64
#define MARK_ESC 0xFE
#define SERIF_W 8
static int uk; // this frame: the port words and the serif cells are there

static int Len(const char* s) {
    int n = 0;
    if (!uk) {
        return (int)strlen(s);
    }
    while ((u8)s[n] != 0xFF) {
        n++;
    }
    return n;
}

static void Clear(char* line) { line[0] = uk ? (char)0xFF : 0; }

// ASCII onto the end of a line (bytes 1..BUTTON_MARKS are marks)
static void Add(char* line, const char* a) {
    char* d = line + Len(line);
    if (!uk) {
        snprintf(d, LINE_MAX - (d - line), "%s", a);
        return;
    }
    for (; *a && d < line + LINE_MAX - 3; a++) {
        if ((u8)*a <= BUTTON_MARKS) {
            *d++ = (char)MARK_ESC;
        }
        *d++ = (u8)*a <= BUTTON_MARKS ? *a
               : *a == '<'              ? MENU_CELL_LT
               : *a == '>'              ? MENU_CELL_GT
                                        : CH(*a);
    }
    *d = (char)0xFF;
}

// uk cells w (FF end) onto the end of a line, else the English en
static void AddCells(char* line, const char* w, const char* en) {
    char* d;
    if (!w) {
        Add(line, en);
        return;
    }
    for (d = line + Len(line); (u8)*w != 0xFF && d < line + LINE_MAX - 1;) {
        *d++ = *w++;
    }
    *d = (char)0xFF;
}

// the port word i under uk, else the English en
static void AddW(char* line, int i, const char* en) {
    AddCells(line, uk && i >= 0 ? Lang_Port(i) : NULL, en);
}

// the width of a line in pixels
static int Width(const char* s) {
    int i, w = 0, n = Len(s);
    for (i = 0; i < n; i++) {
        if (uk && (u8)s[i] == MARK_ESC) {
            w += Buttons_Width(s[++i] - 1) + 1;
        } else if (!uk && (u8)s[i] <= BUTTON_MARKS) {
            w += Buttons_Width(s[i] - 1) + 1;
        } else {
            w += uk ? SERIF_W : CELL_W;
        }
    }
    return w;
}

// spaces up to x pixels
static void PadTo(char* line, int x) {
    while (Width(line) < x && Len(line) < LINE_MAX - 3) {
        Add(line, " ");
    }
}

// a serif cell's 11 rows from y: one tile per run of lit pixels
static void Cell(int x, int y, const u8* rows, u8 r, u8 g, u8 b) {
    int row, col, run;
    for (row = 0; rows && row < 11; row++) {
        for (col = 0; col < SERIF_W; col += run ? run : 1) {
#define INK(i) ((rows[row * 4 + (i) / 2] >> ((i) & 1) * 4) & 15)
            for (run = 0; col + run < SERIF_W && INK(col + run); run++) {
            }
#undef INK
            if (run) {
                Rect(x + col, y + row, run, 1, r, g, b);
            }
        }
    }
}

// a uk line: the cells' 11 rows from y - 2 (row 9 on the 5x7 baseline + 1)
static void SerifText(int x, int y, const char* s, u8 r, u8 g, u8 b) {
    static u8 warned[256];
    const u8* rows;
    int c;
    for (; (u8)*s != 0xFF; s++) {
        if ((u8)*s == MARK_ESC) {
            s++;
            x += Mark(x, y, *s - 1) + 1;
            continue;
        }
        c = (u8)*s;
        rows = Lang_MenuCell(c);
        if (!rows && c != CH(' ') && !warned[c]) { // a cell with no design
            warned[c] = 1;
            fprintf(stderr, "[menu] no serif cell %02X\n", c);
        }
        Cell(x, y - 2, rows, r, g, b);
        x += SERIF_W;
    }
}

// a line of the menu in its language's font
static void Line(int x, int y, const char* s, u8 r, u8 g, u8 b) {
    if (uk) {
        SerifText(x, y, s, r, g, b);
    } else {
        Text(x, y, s, r, g, b);
    }
}

// A line in a field w px wide. A line that fits is drawn as it is. A longer one
// is cut to the field; with run it also scrolls: 1 s still, one pixel per 3
// frames to its end, 1 s still, again (the time starts when the text appears).
static int mq_now; // frames drawn
static struct {
    unsigned hash;
    int t0, seen;
} mq[6];
static void Marquee(int x, int y, int w, const char* s, u8 r, u8 g, u8 b,
                    int run) {
    int over = Width(s) - w, i, t = 0, off = 0, n = Len(s);
    unsigned h = 5381;
    if (over <= 0) {
        Line(x, y, s, r, g, b);
        return;
    }
    if (run) {
        for (i = 0; i < n; i++) {
            h = h * 33 + (u8)s[i];
        }
        for (i = 0; i < 6 && mq[i].hash != h; i++) {
        }
        if (i == 6 || mq[i].seen < mq_now - 1) { // new: the oldest slot
            if (i == 6) { // the slot not seen for the longest time
                for (i = 0, t = 1; t < 6; t++) {
                    i = mq[t].seen < mq[i].seen ? t : i;
                }
            }
            mq[i].hash = h;
            mq[i].t0 = mq_now;
        }
        mq[i].seen = mq_now;
        t = (mq_now - mq[i].t0) % (120 + over * 3);
        off = t < 60 ? 0 : MIN(over, (t - 60) / 3);
    }
    clip_x0 = x;
    clip_x1 = x + w;
    Line(x - off, y, s, r, g, b);
    clip_x0 = clip_x1 = 0;
}

// A centred box of text lines. The OT draws prims in insertion order
// inside one index (LIFO), so the text goes in first and the box last.
// Line hi is the selected one; a line in info (or -1) is light blue. The
// teleport list (box_left) also lights line box_mark, puts the lit lines on
// a red band and is at least box_min_w wide.
// A list longer than the box (sb_total lines, sb_top the first on screen,
// box lines sb_first.. show them) gets a scroll bar inside the right edge:
// a thumb as long as the part on screen, at its place.
static int box_mark = -1, box_min_w;
static int box_off = -1; // a greyed line (one frame): it does nothing
static int help_word = -1; // the help line under the box (one frame)
static const char* help_en;
static int sb_first, sb_total, sb_top;
static void Box(char lines[][LINE_MAX], int count, int hi, int info) {
    char hint[LINE_MAX];
    int i, x, y, w, h, lit, vis = count - sb_first, ty, th, tl;
    int bar = sb_total > vis && vis > 0;
    u8 c;
    // the help: wider than the frame (uk: 32 cells = 256 px) it is two lines,
    // split at the last space that fits; a still wider line scrolls (Marquee)
    char hl[2][LINE_MAX];
    int fw = FbW() - 8, help_h = 0;
    Clear(hint);
    Add(hint, HINT_OK);
    Add(hint, ": ");
    AddW(hint, P_OK, "OK");
    Add(hint, "   ");
    Add(hint, HINT_BACK);
    Add(hint, ": ");
    AddW(hint, P_BACK, "BACK");
    w = uk || box_left ? Width(hint) : TEXT_COLS * CELL_W;
    w = MAX(w, box_min_w);
    for (i = 0; i < count; i++) { // as wide as the longest line
        w = Width(lines[i]) > w ? Width(lines[i]) : w;
    }
    w += BORDER * 2 + (bar ? 6 : 0);
    if (!box_left) {
        w = MIN(w, FbW() - 8); // a wider line scrolls (Marquee)
    }
    h = count * LINE_H - 5 + BORDER * 2;
    if (help_en) {
        int n, k = -1;
        char sp = uk ? CH(' ') : ' ', end = uk ? (char)0xFF : 0;
        Clear(hl[0]);
        AddW(hl[0], help_word, help_en);
        Clear(hl[1]);
        n = Len(hl[0]);
        for (i = 1; i < n; i++) {
            if (hl[0][i] == sp) {
                hl[0][i] = end;
                k = Width(hl[0]) <= fw ? i : k;
                hl[0][i] = sp;
            }
        }
        if (Width(hl[0]) > fw && k > 0) {
            memcpy(hl[1], hl[0] + k + 1, n - k - 1);
            hl[1][n - k - 1] = end;
            hl[0][k] = end;
        }
    }
    // reserved per page, not per row: the box must not move with the row
    help_h = page == PAGE_MAIN || page == PAGE_CHEATS ? LINE_H : 0;
    x = box_left ? box_x : (FbW() - w) / 2;
    y = box_left ? box_y : (FbH() - h - help_h) / 2;
    box_x = x;
    box_y = y;
    box_w = w;
    box_h = h;
    for (i = 0; i < count; i++) {
        lit = i == hi || (box_left && i == box_mark);
        c = lit ? 255 : 160;
        if (i == box_off) {
            Marquee(x + BORDER, y + BORDER + i * LINE_H,
                    w - BORDER * 2 - (bar ? 6 : 0), lines[i], 90, 90, 90,
                    i == hi);
            continue;
        }
        if (i == info && !lit) {
            Marquee(x + BORDER, y + BORDER + i * LINE_H,
                    w - BORDER * 2 - (bar ? 6 : 0), lines[i], 128, 200, 255, 0);
            continue;
        }
        Marquee(x + BORDER, y + BORDER + i * LINE_H,
                w - BORDER * 2 - (bar ? 6 : 0), lines[i], c, c, lit ? 64 : c,
                i == hi);
    }
    if (bar) { // the thumb, then the track under it, over the red band
        ty = y + BORDER + sb_first * LINE_H - 2;
        tl = y + h - BORDER + 1 - ty;
        th = MAX(6, tl * vis / sb_total);
        ty += (tl - th) * sb_top / (sb_total - vis);
        Rect(x + w - 8, ty, 4, th, 220, 220, 220);
        Rect(x + w - 8, y + BORDER + sb_first * LINE_H - 2, 4, tl, 48, 48, 96);
    }
    sb_total = sb_first = 0;
    box_off = -1;
    // the button hint under the box; beside the mini map the box lets the
    // rooms under it show through, so its frame is four lines
    Line(x + BORDER, y + h + 4, hint, 255, 255, 64);
    if (help_en) { // the help, centred in the frame (split above)
        int hw, hx, l;
        for (l = 0; l < 2 && Len(hl[l]); l++) {
            hw = MIN(Width(hl[l]), fw);
            hx = (FbW() - hw) / 2;
            Marquee(hx, y + h + (l + 1) * LINE_H + 6, hw, hl[l], 128, 200, 255,
                    1);
            Rect(hx - 4, y + h + (l + 1) * LINE_H + 2, hw + 8, LINE_H + 2, 8, 8,
                 24);
        }
        help_en = NULL;
    }
    for (i = 0; box_left && i < count; i++) { // under the text, on the box
        if (i == hi || i == box_mark) {
            Rect(
                x + 2, y + BORDER + i * LINE_H - 2, w - 4, LINE_H, 150, 24, 24);
        }
    }
    semi = box_left;
    Rect(x, y + h, w, LINE_H + 2, 8, 8, 24);
    Rect(x + 2, y + 2, w - 4, h - 4, 16, 16, 48);
    semi = 0;
    if (box_left) {
        Rect(x, y, w, 2, 200, 200, 200);
        Rect(x, y + h - 2, w, 2, 200, 200, 200);
        Rect(x, y + 2, 2, h - 4, 200, 200, 200);
        Rect(x + w - 2, y + 2, 2, h - 4, 200, 200, 200);
        return;
    }
    Rect(x, y, w, h, 200, 200, 200);
}

// "< word >" onto a line
static void AddValue(char* line, int word, const char* en) {
    Add(line, "< ");
    AddW(line, word, en);
    Add(line, " >");
}

// The words of a value row and the value itself (the part after the label).
static int boot_val[N_MAIN], boot_set; // the values at the first draw

static void ItemValue(char* line, int it) {
    static const char* voices[VOICE_COUNT] = {"PS1 EN", "JP", "PSP EN"};
    static const char* sets[BUTTON_SETS] = {"NINTENDO", "XBOX",
                                            "PLAYSTATION"};
    static const char* short_sets[BUTTON_SETS] = {"NINTENDO", "XBOX", "PS"};
    char tmp[40];
    int scale, lo, hi, i, *flag = ToggleVar(it);
    if (flag) {
        AddValue(line, *flag ? P_ON : P_OFF, *flag ? "ON" : "OFF");
    }
    switch (it) {
    case ITEM_SCALE:
        Wide_MenuGet(&scale, &lo, &hi);
        if (scale == WIDE_SCALE_MAX) {
            Add(line, "< ");
            AddW(line, P_MAX, "MAX");
        } else if (scale) { // MAX follows hi
            sprintf(tmp, "%c %d >", scale > lo ? '<' : ' ', scale);
            Add(line, tmp);
        } else {
            Add(line, "< ");
            AddW(line, P_AUTO, "AUTO");
        }
        break;
#ifndef __SWITCH__
    case ITEM_FULLSCREEN:
        i = Psyz_VideoGetFullscreen(); // F11 and Esc change it too
        AddValue(line, i ? P_ON : P_OFF, i ? "ON" : "OFF");
        break;
#endif
    case ITEM_VOICE:
        AddValue(line, -1, voices[g_VoiceSet]);
        break;
    case ITEM_MUSIC:
        if (Music_FilesAvailable()) {
            AddValue(line, g_MusicFiles ? P_ENHANCED : P_ORIGINAL,
                     g_MusicFiles ? "ENHANCED" : "ORIGINAL");
        } else { // no files: the switch is off, the disc music plays
            Add(line, "  ");
            AddW(line, P_ORIGINAL, "ORIGINAL");
            Add(line, " ");
            AddW(line, P_NOFILES, "NO FILES");
        }
        break;
    case ITEM_LANGUAGE:
        AddValue(line, -1, g_LangNext ? "UK" : "EN");
        if (g_LangNext != g_Lang) {
            Add(line, " ");
            AddW(line, P_RESTART, "RESTART");
        }
        break;
    case ITEM_BUTTONS:
        if (g_ButtonPick < 0) { // "< AUTO: XBOX >"
            Add(line, "< ");
            AddW(line, P_AUTO, "AUTO");
            Add(line, ": ");
            Add(line, short_sets[g_ButtonSet]);
            Add(line, " >");
        } else {
            AddValue(line, -1, sets[g_ButtonPick]);
        }
        break;
    case ITEM_ASPECT:
        if (g_Aspect) {
            AddValue(line, -1, g_AspectNames[g_Aspect]);
        } else {
            AddValue(line, P_AUTO, "AUTO");
        }
        break;
    case ITEM_CONFIRM:
        Add(line, "< "); // "< AUTO: (A) >": the mark that confirms
        if (g_MenuConfirm == MENU_CONFIRM_AUTO) {
            AddW(line, P_AUTO, "AUTO");
            Add(line, ": ");
        }
        Add(line, HINT_OK);
        Add(line, " >");
        break;
    case ITEM_STICKDEAD:
    case ITEM_LOOKDEAD:
        sprintf(tmp, "< %d%% >", it == ITEM_STICKDEAD ? g_StickDead : g_LookDead);
        Add(line, tmp);
        break;
    case ITEM_LOOKRANGE:
        if (g_LookRange) {
            sprintf(tmp, "< %d >", g_LookRange);
            Add(line, tmp);
        } else {
            AddValue(line, P_OFF, "OFF");
        }
        break;
    case ITEM_RELICTOG:
        AddValue(line, g_RelicToggle ? P_ORIGINAL : 262,
                 g_RelicToggle ? "STOCK" : "MODERN");
        break;
    case ITEM_JPCARDS:
        AddValue(line, g_JpCards == JP_CARDS_EXTRA ? 261
                       : g_JpCards == JP_CARDS_OFF ? P_OFF
                                                   : -1,
                 g_JpCards == JP_CARDS_EXTRA ? "EXTRA"
                 : g_JpCards == JP_CARDS_OFF ? "OFF"
                                             : "JP");
        break;
    }
    // room stitch is read when a stage loads
    if (it == ITEM_STITCH && g_RoomStitchUsed >= 0 &&
        g_RoomStitchUsed != g_RoomStitch) {
        Add(line, " ");
        AddW(line, 265, "NEXT STAGE");
    }
    // read once at the start: a changed value waits for a restart
    if (it == ITEM_JPCARDS || it == ITEM_HD || it == ITEM_HDDUMP ||
        it == ITEM_HDSTATS) {
        int now = it == ITEM_JPCARDS ? g_JpCards : *flag;
        if (now != boot_val[it]) {
            Add(line, " ");
            AddW(line, P_RESTART, "RESTART");
        }
    }
}

static void DrawMain(void) {
    char lines[18][LINE_MAX];
    int n = MainRows(), i, it, col = 0, line, vis, ed = EditorRow();
    if (!boot_set) {
        boot_set = 1;
        boot_val[ITEM_JPCARDS] = g_JpCards;
        boot_val[ITEM_HD] = g_HdTextures;
        boot_val[ITEM_HDDUMP] = g_HdDump;
        boot_val[ITEM_HDSTATS] = g_HdStats;
    }
    for (i = 0; i < n; i++) {
        it = cat_rows[cat][i];
        Clear(lines[i]);
        if (it == ITEM_RESTART && msg_timer > 0) { // the restart did not start
            msg_timer--;
            AddW(lines[i], P_NO, "NO:");
            Add(lines[i], " ");
            Add(lines[i], msg);
            continue;
        }
        if (exit_armed == it && (it == ITEM_EXIT || it == ITEM_RESTART)) {
            if (uk) {
                AddW(lines[i], it == ITEM_EXIT ? P_AGAIN : 264, "EXIT:");
                Add(lines[i], " ");
                Add(lines[i], HINT_OK);
            } else {
                Add(lines[i], it == ITEM_EXIT ? "EXIT: " : "RESTART: ");
                Add(lines[i], HINT_OK);
                Add(lines[i], it == ITEM_EXIT ? " AGAIN TO QUIT"
                                              : " AGAIN TO RESTART");
            }
            continue;
        }
        AddW(lines[i], menu_items[it].word, menu_items[it].en);
        if (it >= ITEM_SCALE && Width(lines[i]) > col) {
            col = Width(lines[i]);
        }
    }
    col += uk ? SERIF_W : CELL_W; // the values in one column
    for (i = 0; i < n; i++) {
        it = cat_rows[cat][i];
        if (it >= ITEM_SCALE) {
            // a value too wide for the box (uk "< Додатково >") loses the
            // spaces inside its arrows, then moves left, never into the name
            char v[LINE_MAX];
            int st = col, avail = FbW() - 8 - 2 * BORDER, k, n;
            char sp = uk ? CH(' ') : ' ';
            char lt = uk ? MENU_CELL_LT : '<', gt = uk ? MENU_CELL_GT : '>';
            Clear(v);
            ItemValue(v, it);
            if (st + Width(v) > avail) {
                n = Len(v);
                for (k = n - 2; k > 0; k--) { // " >" before "<" " "
                    if (v[k] == sp && v[k + 1] == gt) {
                        memmove(v + k, v + k + 1, n - k);
                        n--;
                        break;
                    }
                }
                if (n > 1 && v[0] == lt && v[1] == sp) {
                    memmove(v + 1, v + 2, n - 1);
                }
                st = MAX(MIN(st, avail - Width(v)), Width(lines[i]));
            }
            PadTo(lines[i], st);
            AddCells(lines[i], uk ? v : NULL, v);
        }
    }
    i = -1;
    if (ed >= 0) {
        // the address line goes under EDITOR; the lines after it move down
        char tmp[40];
        memmove(lines[ed + 2], lines[ed + 1],
                sizeof(lines[0]) * (n - ed - 1));
        Clear(lines[ed + 1]);
        snprintf(tmp, sizeof(tmp), uk ? "%.28s" : "  %.29s", Editor_Status());
        Add(lines[ed + 1], tmp);
        i = ed + 1;
    }
    // the help of the selected row
    it = CatItem();
    if (!(exit_armed == it && (it == ITEM_EXIT || it == ITEM_RESTART))) {
        help_word = menu_items[it].help;
        help_en = menu_items[it].help_en;
    }
    // the lines on screen: main_top .. + MainVis(), the selected one in them
    col = MainLineOf(sel);
    if (col < main_top) {
        main_top = col;
    } else if (col >= main_top + MainVis()) {
        main_top = col - MainVis() + 1;
    }
    main_top = MAX(0, MIN(main_top, MainLines() - MainVis()));
    sb_total = MainLines();
    sb_top = main_top;
    vis = MainVis();
    line = col - main_top;
    Box(lines + main_top, vis, line, i < 0 ? -1 : i - main_top);
}

// ASCII in capitals onto a line, at most n characters
static void AddUpper(char* line, const char* s, int n) {
    char tmp[24];
    int i;
    for (i = 0; s[i] && i < n && i < (int)sizeof(tmp) - 1; i++) {
        tmp[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i];
    }
    tmp[i] = 0;
    Add(line, tmp);
}

// a gamepad input: the mark of its position in the button set, or a name
static void AddPadInput(char* line, int code) {
    static const struct {
        const char* sdl;
        s8 mark;
        s16 word;
        const char* en;
    } pads[] = {
        {"x", 0, -1, ""},         {"b", 1, -1, ""},
        {"a", 2, -1, ""},         {"y", 3, -1, ""},
        {"righttrigger", 4, -1, ""}, {"leftshoulder", 5, -1, ""},
        {"rightshoulder", 6, -1, ""}, {"lefttrigger", 7, -1, ""},
        {"back", 8, -1, ""},      {"start", 9, -1, ""},
        {"dpup", 10, 205, "UP"},  {"dpdown", 10, 206, "DOWN"},
        {"dpleft", 10, 207, "LEFT"}, {"dpright", 10, 208, "RIGHT"},
    };
    const char* name = Psyz_PadsInputName(PSYZ_INPUT_GAMEPAD, code);
    char m[2] = {0, 0};
    int i;
    if (code < 0) {
        Add(line, "-");
        return;
    }
    for (i = 0; i < LEN(pads); i++) {
        if (!strcmp(name, pads[i].sdl)) {
            m[0] = 1 + pads[i].mark;
            Add(line, m);
            if (*pads[i].en) {
                Add(line, " ");
                AddW(line, pads[i].word, pads[i].en);
            }
            return;
        }
    }
    if (!strcmp(name, "leftstick") || !strcmp(name, "rightstick")) {
        Add(line, g_ButtonSet == BUTTONS_PSX ? (name[0] == 'l' ? "L3" : "R3")
                                             : (name[0] == 'l' ? "LS" : "RS"));
        return;
    }
    AddUpper(line, name, 8);
}

static void AddKeyInput(char* line, int code) {
    AddUpper(line, code < 0 ? "-" : Psyz_PadsInputName(PSYZ_INPUT_KEY, code),
             10);
}

static void DrawControls(void) {
    char lines[CTL_LINES + 1][LINE_MAX], cell[LINE_MAX], tmp[8];
    int i, k, w, col1 = 0, col2 = 0, gap = 2 * (uk ? SERIF_W : CELL_W);
    // the columns: as wide as their longest text on any line
    for (i = 0; i <= N_CTL_BUTTONS; i++) {
        Clear(cell);
        if (i < N_CTL_BUTTONS) {
            AddW(cell, ctl_rows[i].word, ctl_rows[i].en);
        } else {
            AddW(cell, P_BUTTON, "BUTTON");
        }
        col1 = Width(cell) > col1 ? Width(cell) : col1;
        Clear(cell);
        if (i < N_CTL_BUTTONS) {
            AddKeyInput(cell, Psyz_PadsMap(PSYZ_INPUT_KEY, ctl_rows[i].bit,
                                           PSYZ_INPUT_QUERY));
        } else {
            AddW(cell, P_KEYBOARD, "KEYBOARD");
        }
        col2 = Width(cell) > col2 ? Width(cell) : col2;
    }
    col1 += gap;
    col2 = CTL_KEYS ? col1 + col2 + gap : col1;
    if (sel < ctl_top) {
        ctl_top = sel;
    } else if (sel >= ctl_top + CTL_LINES) {
        ctl_top = sel - CTL_LINES + 1;
    }
    // the header: the column names, or the capture prompt and its seconds
    Clear(lines[0]);
    if (capture) {
        AddW(lines[0], P_PRESS, "PRESS A KEY OR BUTTON");
        sprintf(tmp, " %d", (capture + 59) / 60);
        Add(lines[0], tmp);
    } else {
        AddW(lines[0], P_BUTTON, "BUTTON");
        PadTo(lines[0], col1);
        if (CTL_KEYS) {
            AddW(lines[0], P_KEYBOARD, "KEYBOARD");
            PadTo(lines[0], col2);
        }
        AddW(lines[0], P_GAMEPAD, "GAMEPAD");
    }
    for (i = 1; i <= CTL_LINES; i++) {
        k = ctl_top + i - 1;
        Clear(lines[i]);
        if (k == CTL_RESET) {
            AddW(lines[i], P_DEFAULTS, "RESET TO DEFAULTS");
        } else if (k == CTL_BACK) {
            AddW(lines[i], P_BACK, "BACK");
        } else {
            AddW(lines[i], ctl_rows[k].word, ctl_rows[k].en);
            PadTo(lines[i], col1);
            if (CTL_KEYS) {
                AddKeyInput(lines[i], Psyz_PadsMap(PSYZ_INPUT_KEY,
                                                   ctl_rows[k].bit,
                                                   PSYZ_INPUT_QUERY));
                PadTo(lines[i], col2);
            }
            w = Psyz_PadsMap(PSYZ_INPUT_GAMEPAD, ctl_rows[k].bit,
                             PSYZ_INPUT_QUERY);
            AddPadInput(lines[i], w);
        }
    }
    sb_first = 1;
    sb_total = N_CTL;
    sb_top = ctl_top;
    Box(lines, CTL_LINES + 1, sel - ctl_top + 1, 0);
}

static void DrawCheats(void) {
    static const struct {
        int word;
        const char* en;
    } names[] = {{P_REFILL, "HP MP HEARTS"}, {P_INVINCIBLE, "NO DAMAGE"},
                 {P_WALLS, "MIST WALLS"},    {P_ITEMS, "ALL ITEMS"},
                 {P_CHARACTER, "CHARACTER"}},
      chars[CHEAT_CHAR_N] = {{P_GAME, "GAME"},       {P_ALUCARD, "ALUCARD"},
                             {P_RICHTER, "RICHTER"}, {P_RICHTER, "RICHTER"},
                             {P_MARIA, "MARIA"},     {P_MARIA, "MARIA"}};
    static const char* const hints[N_CHEATS] = {
        "HP MP HEARTS STAY FULL.",
        "HITS DO NOTHING.",
        "MIST FORM PASSES THROUGH WALLS.",
        "YOU HAVE ALL WEAPONS, ITEMS AND RELICS.",
        "LEFT AND RIGHT PICK. OK APPLIES AT ONCE.",
        "OPEN THE CASTLE MAP AND WARP.",
        "GO BACK ONE PAGE."};
    int flags[4];
    char lines[N_CHEATS][LINE_MAX], tmp[40];
    int i, col = 0;
    flags[0] = g_Cheat.refill;
    flags[1] = g_Cheat.invincible;
    flags[2] = g_Cheat.walls;
    flags[3] = g_Cheat.items;
    for (i = 0; i <= CH_CHARACTER; i++) {
        Clear(lines[i]);
        AddW(lines[i], names[i].word, names[i].en);
        col = Width(lines[i]) > col ? Width(lines[i]) : col;
    }
    col = uk ? col + SERIF_W : 15 * CELL_W; // en: "%-14s < %s >"
    for (i = 0; i <= CH_CHARACTER; i++) {
        PadTo(lines[i], col);
        if (i < CH_CHARACTER) {
            AddValue(lines[i], flags[i] ? P_ON : P_OFF, flags[i] ? "ON" : "OFF");
        } else if (msg_timer > 0) { // why the character did not change
            msg_timer--;
            Clear(lines[i]);
            AddW(lines[i], msg_word, msg);
        } else {
            AddValue(lines[i], chars[g_Cheat.character].word,
                     chars[g_Cheat.character].en);
        }
    }
    Clear(lines[CH_TELEPORT]);
    AddW(lines[CH_TELEPORT], P_TELEPORT, "TELEPORT");
    Clear(lines[CH_BACK]);
    AddW(lines[CH_BACK], P_BACK, "BACK");
    help_word = CHEAT_HELP_WORD + sel;
    help_en = hints[sel];
    if (g_GameState != Game_Play) { // no game: the teleport has nowhere to go
        box_off = CH_TELEPORT;
        if (sel == CH_TELEPORT) {
            help_word = P_W_NOGAME;
            help_en = "START OR LOAD A GAME TO USE THE TELEPORT.";
        }
    }
    Box(lines, N_CHEATS, sel, -1);
}

// Which game has a place (CastleRoom.src): the Saturn only, the PlayStation
// only, or both. A stage is one if every room of it is; a boss fight takes
// the src of the room it is in.
static int StageSrc(int stage) {
    int i, src = -1;
    for (i = 0; i < g_CastleRoomCount; i++) {
        if (g_CastleRooms[i].stage == stage) {
            src = src < 0 || src == g_CastleRooms[i].src ? g_CastleRooms[i].src
                                                          : ROOM_BOTH;
        }
    }
    return src < 0 ? ROOM_BOTH : src;
}

static int ItemSrc(int id) {
    int i;
    if (id < 0) {
        return ROOM_BOTH;
    }
    if (id < ITEM_BOSS) {
        return StageSrc(id);
    }
    for (i = 0; i < g_CastleRoomCount; i++) {
        if (ItemHas(id, &g_CastleRooms[i])) {
            return g_CastleRooms[i].src;
        }
    }
    return StageSrc(g_CheatBosses[id - ITEM_BOSS].stage);
}

// "[SS] " (Saturn only), "[PS] " (PlayStation only); both: blanks (pad) or
// nothing
static void AddTag(char* line, int src, int pad) {
    Add(line, src == ROOM_SATURN ? "[SS] " : src == ROOM_PSX ? "[PS] "
                                         : pad               ? "     "
                                                             : "");
}

// "[SS] SATURN  [PS] PSX": what the tags mean (no tag: in both games)
static void AddLegend(char* line) {
    Add(line, "[SS] ");
    AddW(line, P_SATURN, "SATURN");
    Add(line, "  [PS] PSX");
}

// "NO0   ROOM 3" onto a line
static void AddRoom(char* line, const struct CastleRoom* rm) {
    char tmp[16];
    sprintf(tmp, "%-5s ", Castle_StageName(rm->stage));
    Add(line, tmp);
    AddW(line, P_ROOM, "ROOM");
    sprintf(tmp, " %d", rm->room);
    Add(line, tmp);
}

static void DrawChoose(void) {
    char lines[MAX_OWNERS][LINE_MAX];
    int i;
    for (i = 0; i < n_owners; i++) {
        Clear(lines[i]);
        AddRoom(lines[i], owners[i]);
    }
    Box(lines, n_owners, sel, -1);
}

// an enemy name of g_EnemyDefs (cells, FF end; uk after Lang_Init)
static void AddEnemy(char* line, int id) {
    const u8* s = (const u8*)g_EnemyDefs[id].name;
    char tmp[32];
    int n = 0;
    if (uk) {
        AddCells(line, (const char*)s, "");
        return;
    }
    for (; *s != 0xFF && n < (int)sizeof(tmp) - 1; s++) {
        tmp[n++] = *s + 0x20 >= 'a' && *s + 0x20 <= 'z' ? *s : *s + 0x20;
    }
    tmp[n] = 0;
    Add(line, tmp);
}

// "SLOGRA / GAIBON": the names of a boss fight; a third name is left out
// ("...")
static void AddNames(char* line, const struct CheatBoss* b) {
    AddEnemy(line, b->enemy[0]);
    if (b->enemy[1]) {
        Add(line, " / ");
        AddEnemy(line, b->enemy[1]);
    }
    if (b->enemy[2]) {
        Add(line, " ...");
    }
}

// "NZ0  SLOGRA / GAIBON": the castle stage of the fight and the names
static void AddBoss(char* line, const struct CheatBoss* b) {
    char tmp[8];
    sprintf(
        tmp, "%-4s ", Castle_StageName(b->host != 0xFF ? b->host : b->stage));
    Add(line, tmp);
    AddTag(line, ItemSrc(ITEM_BOSS + (int)(b - g_CheatBosses)), 1);
    AddNames(line, b);
}

// The bestiary picture of a boss (the librarian's, BIN/MONSTER.BIN: 96x112
// 15-bit pixels a record) at half size: one tile per run of one colour in a
// row. Read when the boss changes. Only a raw 0 pixel is clear (0x8000 is
// opaque black, the librarian draws it so); a half size pixel is the mean of
// the opaque ones of its 2x2, clear when 3 or 4 of them are clear. It lies on
// a dark box, so black art stays black (on the old grey box Doppleganger,
// mostly black, came out as grey holes).
#define PIC_W 48
#define PIC_H 56
static struct {
    u8 x, y, w, r, g, b;
} pic[3000];
static int pic_n, pic_of = -1;

static void LoadPicture(int bestiary) {
    static u16 px[0x5800 / 2];
    static int half[PIC_W]; // a row at half size, -1 clear, else 0x7FFF rgb
    int x, y, i, n, r, g, b, run;
    u16 c;
    pic_of = bestiary;
    pic_n = 0;
    if (FileReadToBuf(SOTN_DATA_DIR "/BIN/MONSTER.BIN", px, bestiary * 0x5800,
                      sizeof(px)) < 96 * 112 * 2) {
        fprintf(stderr, "[menu] no bestiary picture %d\n", bestiary);
        return;
    }
    for (y = 0; y < PIC_H; y++) {
        for (x = 0; x < PIC_W; x++) {
            n = r = g = b = 0;
            for (i = 0; i < 4; i++) {
                c = px[(y * 2 + (i >> 1)) * 96 + x * 2 + (i & 1)];
                if (c) {
                    n++;
                    r += c & 31;
                    g += c >> 5 & 31;
                    b += c >> 10 & 31;
                }
            }
            half[x] = n < 2 ? -1 : r / n | g / n << 5 | b / n << 10;
        }
        for (x = 0; x < PIC_W; x += run) {
            for (run = 1; x + run < PIC_W && half[x + run] == half[x]; run++) {
            }
            if (half[x] >= 0 && pic_n < LEN(pic)) {
                pic[pic_n].x = x;
                pic[pic_n].y = y;
                pic[pic_n].w = run;
                pic[pic_n].r = (half[x] & 31) << 3;
                pic[pic_n].g = (half[x] >> 5 & 31) << 3;
                pic[pic_n++].b = (half[x] >> 10 & 31) << 3;
            }
        }
    }
}

static void DrawPicture(int x, int y, int bestiary) {
    int i;
    if (bestiary != pic_of) {
        LoadPicture(bestiary);
    }
    for (i = 0; i < pic_n; i++) {
        Rect(x + pic[i].x, y + pic[i].y, pic[i].w, 1, pic[i].r, pic[i].g,
             pic[i].b);
    }
    if (pic_n) { // a dark box in a grey frame, under the picture
        Rect(x, y, PIC_W, PIC_H, 8, 8, 16);
        Rect(x - 2, y - 2, PIC_W + 4, PIC_H + 4, 200, 200, 200);
    }
}

static void StageColor(int stage, u8* r, u8* g, u8* b);
static void ClipRect(int x, int y, int w, int h, int vx0, int vy0, int vx1,
                     int vy1, u8 r, u8 g, u8 b);

// The mini map of the list page: every room of one castle, dimmed, the
// rooms of the selected line red on top, the player's room white. It fits
// the view right of the list box; a narrow view (4:3) gets the whole view
// behind the half transparent box.
static void DrawMini(int it) {
    static int bbox_of = -1, l, t, r, bt;
    const struct CastleRoom* rm;
    int i, pass, x0, y0, x1, y1, vx0, vy0, vx1, vy1, x, y, w, h, hi;
    u8 cr, cg, cb;

    ViewRect(&vx0, &vy0, &vx1, &vy1);
    if (it >= ITEM_BOSS && g_CheatBosses[it - ITEM_BOSS].host != 0xFF) {
        mini_castle = g_CheatBosses[it - ITEM_BOSS].host & 0x20;
    } else if (it >= 0 && it < ITEM_BOSS) {
        mini_castle = it & 0x20;
    } else if (it < 0) {
        mini_castle = g_StageId & 0x20;
    }
    if (bbox_of != mini_castle) { // the cells the castle's rooms cover
        bbox_of = mini_castle;
        mini_user = 0;
        l = t = 63;
        r = bt = 0;
        for (i = 0; i < g_CastleRoomCount; i++) {
            rm = &g_CastleRooms[i];
            if ((rm->stage & 0x20) == mini_castle) {
                l = MIN(l, rm->left);
                t = MIN(t, RTOP(rm));
                r = MAX(r, rm->right);
                bt = MAX(bt, RBOT(rm));
            }
        }
    }
    x0 = box_x + box_w + 6;
    x0 = vx1 - 4 - x0 < 150 ? vx0 + 4 : x0;
    x1 = vx1 - 4;
    y0 = vy0 + 4;
    y1 = vy1 - 4;
    mini_fit = r < l ? 0
                     : MIN(((x1 - x0) << 4) / (r - l + 1),
                           ((y1 - y0) << 4) / (bt - t + 1));
    if (!mini_user || !mini_s) {
        mini_s = mini_fit;
        mini_ox = (x1 << 4) - (r + 1) * mini_s; // right aligned, centred rows
        mini_oy = ((y0 + y1) << 3) - (t + bt + 1) * mini_s / 2;
    }
    if (!mini_user && mini_s) {
        // the rooms of the selected line must show beside the list box, not
        // behind it: centre their box in the free part of the view
        int fx0 = x0, fy0 = y0, hl = 99, ht = 99, hr = -1, hb = -1;
        if (x0 <= box_x + box_w) { // the map runs behind the box
            if (x1 - (box_x + box_w + 6) >= 40) {
                fx0 = box_x + box_w + 6; // right of the box
            } else {
                fy0 = box_y + box_h + 6; // under the box
            }
        }
        for (i = 0; i < g_CastleRoomCount; i++) {
            rm = &g_CastleRooms[i];
            if ((rm->stage & 0x20) == mini_castle && ItemHas(it, rm)) {
                hl = MIN(hl, rm->left);
                ht = MIN(ht, RTOP(rm));
                hr = MAX(hr, rm->right);
                hb = MAX(hb, RBOT(rm));
            }
        }
        if (hr >= 0) {
            int sx0 = (mini_ox + hl * mini_s) >> 4;
            int sx1 = (mini_ox + (hr + 1) * mini_s) >> 4;
            int sy0 = (mini_oy + ht * mini_s) >> 4;
            int sy1 = (mini_oy + (hb + 1) * mini_s) >> 4;
            if (sx0 < fx0 || sx1 > x1 || sy0 < fy0 || sy1 > y1) {
                mini_ox += (((fx0 + x1) << 3) - ((sx0 + sx1) << 3));
                mini_oy += (((fy0 + y1) << 3) - ((sy0 + sy1) << 3));
            }
        }
    }
    mini_x0 = x0;
    mini_y0 = y0;
    mini_x1 = x1;
    mini_y1 = y1;
    for (pass = 0; pass < 2; pass++) { // the red rooms first: on top (LIFO)
        for (i = 0; mini_s && i < g_CastleRoomCount; i++) {
            rm = &g_CastleRooms[i];
            hi = ItemHas(it, rm);
            if ((rm->stage & 0x20) != mini_castle || hi != !pass) {
                continue;
            }
            x = (mini_ox + rm->left * mini_s) >> 4;
            y = (mini_oy + RTOP(rm) * mini_s) >> 4;
            w = ((mini_ox + (rm->right + 1) * mini_s) >> 4) - x;
            h = ((mini_oy + (RBOT(rm) + 1) * mini_s) >> 4) - y;
            if (mini_s >= 48) { // a gap between rooms from 3 px a cell
                w--;
                h--;
            }
            // a red ring round a small boss room (a cell or two at 720p)
            if (hi && it >= ITEM_BOSS && w < 8 && h < 8) {
                ClipRect(x - 4, y - 4, w + 8, 1, x0, y0, x1, y1, 255, 40, 40);
                ClipRect(
                    x - 4, y + h + 3, w + 8, 1, x0, y0, x1, y1, 255, 40, 40);
                ClipRect(x - 4, y - 3, 1, h + 6, x0, y0, x1, y1, 255, 40, 40);
                ClipRect(
                    x + w + 3, y - 3, 1, h + 6, x0, y0, x1, y1, 255, 40, 40);
            }
            if (hi) {
                cr = 255;
                cg = cb = 40;
            } else if (rm->stage == g_StageId && D_8009791C.x >= rm->left &&
                       D_8009791C.x <= rm->right && D_8009791C.y >= rm->top &&
                       D_8009791C.y <= rm->bottom) {
                cr = cg = cb = 255;
            } else {
                StageColor(rm->stage, &cr, &cg, &cb);
                cr = cr * 2 / 3;
                cg = cg * 2 / 3;
                cb = cb * 2 / 3;
            }
            ClipRect(x, y, w, h, x0, y0, x1, y1, cr, cg, cb);
        }
    }
    Rect(vx0, vy0, vx1 - vx0, vy1 - vy0, 8, 8, 24); // the background last
}

// "MARBLE GALLERY": the area name of a stage
static void AddZone(char* line, int id) {
    const char* uk_name = uk ? Lang_Zone(id) : NULL;
    if (uk_name) {
        AddCells(line, uk_name, "");
    } else {
        AddUpper(line, Castle_ZoneName(id), 23);
    }
}

// the line of list item id: "NO0  MARBLE GALLERY", "NZ0  SLOGRA / GAIBON"
static void ItemLine(char* line, int id) {
    char tmp[8];
    if (id == ITEM_MAP) {
        AddW(line, P_CASTLE_MAP, "CASTLE MAP");
    } else if (id == ITEM_BOSSES) {
        AddW(line, P_BOSS, "BOSS");
        Add(line, " >");
    } else if (id >= ITEM_BOSS) {
        AddBoss(line, &g_CheatBosses[id - ITEM_BOSS]);
    } else {
        sprintf(tmp, "%-4s ", Castle_StageName(id));
        Add(line, tmp);
        AddTag(line, StageSrc(id), 1);
        AddZone(line, id);
    }
}

// The widest line of the list page, on screen or not: the box keeps that
// width while the list scrolls, so the mini map beside it stays put under
// the pointer.
static int ListWidth(void) {
    static int w[2], w_uk[2] = {-1, -1};
    char line[LINE_MAX];
    int i, *pw = &w[list_boss];
    if (w_uk[list_boss] == uk) {
        return *pw;
    }
    w_uk[list_boss] = uk;
    *pw = 0;
    for (i = 0; i < n_items; i++) {
        Clear(line);
        ItemLine(line, items[i]);
        *pw = MAX(*pw, Width(line));
    }
    return *pw;
}

// a line cut to w pixels (lines of plain cells, no button marks)
static void ClipTo(char* line, int w) {
    int n = Len(line);
    while (n > 0 && Width(line) > w) {
        line[--n] = uk ? (char)0xFF : 0;
    }
}

// "NO0  MARBLE GALLERY" lines, or the boss list with the boss picture
static void DrawList(void) {
    char lines[CTL_LINES + 1][LINE_MAX], tmp[40];
    const struct CastleRoom* rm = NULL;
    int i, vx0, vy0, vx1, vy1, id, n = ListLines(), b = -1;
    if (sel < list_top) {
        list_top = sel;
    } else if (sel >= list_top + n) {
        list_top = sel - n + 1;
    }
    if (mark_cx >= 0 && list_boss) {
        rm = RoomAt(mark_cx, mark_cy, mark_c, ITEM_ANY);
    } else if (mark_cx >= 0) {
        b = BossAt(mark_cx, mark_cy, mark_c);
    }
    // the box keeps the width of its widest line, but not so wide that the
    // mini map (150 px from the box, DrawMini) must go behind it
    ViewRect(&vx0, &vy0, &vx1, &vy1);
    box_min_w = MIN(ListWidth(), vx1 - vx0 - 164 - BORDER * 2);
    Clear(lines[0]);
    if (msg_timer > 0) {
        msg_timer--;
        AddW(lines[0], msg_word, msg);
    } else if (rm) { // the place of the room under the pointer
        AddTag(lines[0], rm->src, 0);
        AddZone(lines[0], rm->stage);
        ClipTo(lines[0], box_min_w); // the header never widens the box
        box_mark = 0;
    } else if (b >= 0) { // the boss of the room under the pointer
        AddTag(lines[0], ItemSrc(ITEM_BOSS + b), 0);
        AddNames(lines[0], &g_CheatBosses[b]);
        ClipTo(lines[0], box_min_w);
        box_mark = 0;
    } else {
        AddW(lines[0], list_boss ? P_BOSS : P_TELEPORT,
             list_boss ? "BOSS" : "TELEPORT");
        Add(lines[0], "  ");
        AddLegend(lines[0]);
    }
    for (i = 1; i <= n; i++) {
        Clear(lines[i]);
        ItemLine(lines[i], items[list_top + i - 1]);
    }
    box_left = 1;
    box_x = vx0 + 4;
    box_y = vy0 + 4;
    sb_first = 1;
    sb_total = n_items;
    sb_top = list_top;
    Box(lines, n + 1, sel - list_top + 1, 0);
    box_left = box_min_w = 0;
    box_mark = -1;
    id = sel < n_items ? items[sel] : ITEM_MAP;
    if (id >= ITEM_BOSS) {
        DrawPicture(box_x + 4, box_y + box_h + LINE_H + 8,
                    g_CheatBosses[id - ITEM_BOSS].bestiary);
    }
    DrawMini(id);
}

// one colour per stage, the same in both castles
static void StageColor(int stage, u8* r, u8* g, u8* b) {
    static const u8 pal[16][3] = {
        {64, 96, 200},   {64, 160, 96},  {160, 128, 64},  {120, 72, 160},
        {48, 140, 160},  {150, 60, 60},  {110, 110, 110}, {80, 120, 60},
        {170, 100, 140}, {60, 90, 140},  {140, 140, 60},  {90, 60, 120},
        {60, 150, 130},  {130, 80, 50},  {100, 100, 170}, {150, 110, 90},
    };
    int i = ((stage & 0x1F) ^ (stage >> 6)) & 15;
    *r = pal[i][0];
    *g = pal[i][1];
    *b = pal[i][2];
}

// a rect clipped to the view
static void ClipRect(int x, int y, int w, int h, int vx0, int vy0, int vx1,
                     int vy1, u8 r, u8 g, u8 b) {
    if (x < vx0) {
        w -= vx0 - x;
        x = vx0;
    }
    if (y < vy0) {
        h -= vy0 - y;
        y = vy0;
    }
    if (x + w > vx1) {
        w = vx1 - x;
    }
    if (y + h > vy1) {
        h = vy1 - y;
    }
    Rect(x, y, w, h, r, g, b);
}

// MARK_BOSS, MARK_SAVE, MARK_WARP
static const char* mark_names[] = {"BOSS", "SAVE", "WARP"};
static const u8 mark_rgb[][3] = {{255, 64, 255}, {230, 40, 40}, {255, 220, 0}};

// The hints under the map, over a band that hides the rooms: the pad
// buttons, then (PC) the mouse, packed into lines as wide as the view.
// Sets map_bottom, the top of the band.
static void DrawMapHints(int x0, int x1, int y1) {
    char seg[8][LINE_MAX], lines[6][LINE_MAX];
    int n = 0, n_pad, i, k = 0, wmax = x1 - x0 - 8, mouse = LEN(lines);
    const char* gap = uk ? " " : ": ";
    for (i = 0; i < LEN(seg); i++) {
        Clear(seg[i]);
    }
    Add(seg[n], HINT_OK), Add(seg[n], " "), AddW(seg[n++], P_GO, "GO");
    Add(seg[n], HINT_BACK), Add(seg[n], " "), AddW(seg[n++], P_LIST, "LIST");
    Add(seg[n], "\x0B "), AddW(seg[n++], P_MOVE, "MOVE"); // the D-pad
    Add(seg[n], "\x06/\x07 "), AddW(seg[n++], P_SCALE, "ZOOM"); // L1/R1
    Add(seg[n], HINT_CASTLE), Add(seg[n], gap);
    AddW(seg[n++], castle ? P_NORMAL : P_INVERTED,
         castle ? "NORMAL" : "INVERTED");
    n_pad = n;
#ifndef __SWITCH__
    AddW(seg[n], P_CLICK, "CLICK"), Add(seg[n], ": "), AddW(seg[n++], P_GO, "GO");
    AddW(seg[n], P_WHEEL, "WHEEL"), Add(seg[n], ": ");
    AddW(seg[n++], P_SCALE, "ZOOM");
    AddW(seg[n], P_DRAG, "DRAG"), Add(seg[n], ": "), AddW(seg[n++], P_PAN, "PAN");
#endif
    // a line takes segments while they fit; the mouse starts a new line,
    // light blue
    Clear(lines[0]);
    for (i = 0; i < n; i++) {
        if (Len(lines[k]) && k + 1 < LEN(lines) &&
            (i == n_pad ||
             Width(lines[k]) + 2 * SERIF_W + Width(seg[i]) > wmax)) {
            Clear(lines[++k]);
        }
        if (i == n_pad) {
            mouse = k;
        }
        if (Len(lines[k])) {
            Add(lines[k], "  ");
        }
        AddCells(lines[k], uk ? seg[i] : NULL, seg[i]);
    }
    map_bottom = y1 - (k + 1) * LINE_H;
    for (i = 0; i <= k; i++) {
        Line(x0 + 4, map_bottom + i * LINE_H + 3, lines[i],
             i >= mouse ? 128 : 255, i >= mouse ? 200 : 255,
             i >= mouse ? 255 : 64);
    }
    Rect(x0, map_bottom, x1 - x0, y1 - map_bottom, 8, 8, 24);
}

static void DrawMap(void) {
    const struct CastleRoom* rm;
    char line[LINE_MAX], legend[3][LINE_MAX], tmp[24];
    int i, j, x0, y0, x1, y1, x, y, w, h, gap, my0;
    u8 r, g, b;

    ViewRect(&x0, &y0, &x1, &y1);
    my0 = y0 + 2 * LINE_H;
    gap = zoom16 >= 48 ? 1 : 0;

    // text first: drawn on top (LIFO)
    FindOwners(cur_x, cur_y);
    // the castle name looks like a menu toggle: a tap on it switches
    Clear(line);
    AddValue(line, castle ? P_INVERTED : P_NORMAL, castle ? "INVERTED" : "NORMAL");
    Add(line, "  ");
    if (n_owners == 1) {
        sprintf(tmp, "%s ", Castle_StageName(owners[0]->stage));
        Add(line, tmp);
        AddW(line, P_ROOM, "ROOM");
        sprintf(tmp, " %d", owners[0]->room);
        Add(line, tmp);
    } else if (n_owners > 1 && uk) {
        AddW(line, P_ROOMS, "ROOMS");
        sprintf(tmp, " %d", n_owners);
        Add(line, tmp);
    } else if (n_owners > 1) {
        sprintf(tmp, "%d ROOMS", n_owners);
        Add(line, tmp);
    } else {
        Add(line, "-");
    }
    Line(x0 + 4, y0 + 3, line, 255, 255, 255);
    Clear(legend[0]);
    AddLegend(legend[0]);
    x = x1 - 4 - Width(legend[0]);
    if (x >= x0 + 4 + Width(line) + 2 * SERIF_W) { // where it fits
        Line(x, y0 + 3, legend[0], 128, 200, 255);
    }
    DrawMapHints(x0, x1, y1);
    if (map_fit) {
        map_fit = 0;
        FitCastle(x0, my0, x1, map_bottom);
    }
    // line 2: a message, else the place or the boss of the room under the
    // cursor, and the legend right aligned where it fits
    Clear(line);
    if (msg_timer > 0) {
        msg_timer--;
        AddW(line, msg_word, msg);
        Line(x0 + 4, y0 + 3 + LINE_H, line, 255, 120, 80);
    } else if (n_owners) {
        i = BossAt(cur_x, cur_y, castle);
        if (i >= 0) {
            AddTag(line, ItemSrc(ITEM_BOSS + i), 0);
            AddNames(line, &g_CheatBosses[i]);
        } else if (*Castle_ZoneName(owners[0]->stage)) {
            AddTag(line, owners[0]->src, 0);
            AddZone(line, owners[0]->stage);
        } else {
            Add(line, Castle_StageName(owners[0]->stage));
        }
        Line(x0 + 4, y0 + 3 + LINE_H, line, 255, 240, 150);
    }
    if (msg_timer <= 0) { // the legend: each word in its marker colour
        for (w = 0, i = 0; i < 3; i++) {
            Clear(legend[i]);
            AddW(legend[i], P_BOSS + i, mark_names[i]);
            w += Width(legend[i]) + (i ? (uk ? SERIF_W : CELL_W) : 0);
        }
        x = x1 - 4 - w;
        for (i = 0; i < 3 && x >= x0 + 4 + Width(line) + 2 * SERIF_W; i++) {
            Line(x, y0 + 3 + LINE_H, legend[i], mark_rgb[i][0], mark_rgb[i][1],
                 mark_rgb[i][2]);
            x += Width(legend[i]) + (uk ? SERIF_W : CELL_W);
        }
    }
    // cursor: a blinking frame
    x = CellX(cur_x) - 1;
    y = CellY(cur_y) - 1;
    w = CellX(cur_x + 1) - CellX(cur_x) + 2;
    if (frame & 16) {
        ClipRect(x, y, w, 1, x0, my0, x1, map_bottom, 255, 255, 0);
        ClipRect(x, y + w - 1, w, 1, x0, my0, x1, map_bottom, 255, 255, 0);
        ClipRect(x, y, 1, w, x0, my0, x1, map_bottom, 255, 255, 0);
        ClipRect(x + w - 1, y, 1, w, x0, my0, x1, map_bottom, 255, 255, 0);
    }
    // the hero room: a thin ring grows off its edge and fades, on its own
    // period (47 frames, sotn.ini), prime to the cursor blink (32)
    if (g_MapHeroPulse) {
        int t = frame % g_MapHeroPeriod, ex, hx, hy, hw, hh;
        u8 c;
        for (i = 0; i < g_CastleRoomCount; i++) {
            rm = &g_CastleRooms[i];
            if (rm->stage != g_StageId || D_8009791C.x < rm->left ||
                D_8009791C.x > rm->right || D_8009791C.y < rm->top ||
                D_8009791C.y > rm->bottom) {
                continue;
            }
            hx = CellX(rm->left);
            hy = CellY(RTOP(rm));
            hw = CellX(rm->right + 1) - hx - gap;
            hh = CellY(RBOT(rm) + 1) - hy - gap;
            // grows by about a third of a cell (min 2 px), fades 255 -> 80
            ex = 1 + t * MAX(2, (CellX(1) - CellX(0)) / 3) / g_MapHeroPeriod;
            c = 255 - 175 * t / g_MapHeroPeriod;
            ClipRect(hx - ex, hy - ex, hw + 2 * ex, 1, x0, my0, x1, map_bottom, c, c, c);
            ClipRect(hx - ex, hy + hh + ex - 1, hw + 2 * ex, 1, x0, my0, x1, map_bottom, c, c, c);
            ClipRect(hx - ex, hy - ex, 1, hh + 2 * ex, x0, my0, x1, map_bottom, c, c, c);
            ClipRect(hx + hw + ex - 1, hy - ex, 1, hh + 2 * ex, x0, my0, x1, map_bottom, c, c, c);
            break;
        }
    }
    // markers over the rooms: a square in the middle of each marked area
    for (i = 0; i < g_CastleMarkCount; i++) {
        const struct CastleMark* mk = &g_CastleMarks[i];
        if ((mk->castle & 0x20) != castle) {
            continue;
        }
        w = (CellX(mk->left + 1) - CellX(mk->left)) * 2 / 3;
        w = w < 3 ? 3 : w;
        x = (CellX(mk->left) + CellX(mk->right + 1) - w) / 2;
        y = (CellY(mk->top) + CellY(mk->bottom + 1) - w) / 2;
        ClipRect(x, y, w, w, x0, my0, x1, map_bottom, mark_rgb[mk->kind][0],
                 mark_rgb[mk->kind][1], mark_rgb[mk->kind][2]);
    }
    // rooms; the room of the player is white
    for (i = 0; i < g_CastleRoomCount; i++) {
        rm = &g_CastleRooms[i];
        if ((rm->stage & 0x20) != castle) {
            continue;
        }
        x = CellX(rm->left);
        y = CellY(RTOP(rm));
        w = CellX(rm->right + 1) - x - gap;
        h = CellY(RBOT(rm) + 1) - y - gap;
        if (x >= x1 || y >= y1 || x + w <= x0 || y + h <= my0) {
            continue;
        }
        for (j = 0; j < n_owners && owners[j] != rm; j++) {
        }
        if (rm->stage == g_StageId && D_8009791C.x >= rm->left &&
            D_8009791C.x <= rm->right && D_8009791C.y >= rm->top &&
            D_8009791C.y <= rm->bottom) {
            r = g = b = 255;
        } else if (j < n_owners) { // a room under the cursor: light yellow
            r = 255;
            g = 240;
            b = 150;
        } else {
            StageColor(rm->stage, &r, &g, &b);
        }
        ClipRect(x, y, w, h, x0, my0, x1, map_bottom, r, g, b);
    }
    // the background last
    Rect(x0, y0, x1 - x0, y1 - y0, 8, 8, 24);
}

// ---- debug overlay ------------------------------------------------------------

int g_OverlayOn, g_FpsOn;
int g_SkipCdRooms = 1, g_ShowLoadingText = 1; // sotn.ini, game.h
int g_RoomEdgeFade = 1;                       // sotn.ini, game.h
int g_ElevatorFront = 1;                      // sotn.ini, game.h
u32 g_ElevatorOrnTimer;                       // the elevator entity, game.h
static int room_num = -1; // the index in g_api.o.rooms, as the teleport uses
static int fps;

static const char* StageLabel(int stage) {
    static char hex[8];
    const char* name = Castle_StageName(stage);
    if (name[0] != '?') {
        return name;
    }
    sprintf(hex, "ST%02X", stage & 0xFF);
    return hex;
}

struct WarpTarget g_LastRoom;

void PortMenu_OnRoom(const RoomLoadDef* def, int cx, int cy) {
    const RoomHeader* r = (const RoomHeader*)(def - 1);
    room_num = r - g_api.o.rooms;
    g_LastRoom = (struct WarpTarget){1, g_StageId, room_num, cx, cy};
    // player position in the new room's pixels: the cell gives the high
    // byte, the player position on screen the low byte (the room change
    // wraps it to the cell the same way, 5087C.c)
    const char* zone = Castle_ZoneName(g_StageId); // "" for a boss stage
    fprintf(stderr, "[room] %s%s%s%s id %02X room %d cell %d,%d pos %d,%d\n",
            StageLabel(g_StageId), *zone ? " (" : "", zone, *zone ? ")" : "",
            g_StageId & 0xFF, room_num, cx, cy,
            (cx - r->left) * 256 + (PLAYER.posX.i.hi & 0xFF),
            (cy - r->top) * 256 + (PLAYER.posY.i.hi & 0xFF));
    fflush(stderr);
#ifdef __SWITCH__
    fsync(fileno(stderr)); // the last line names the room if the game hangs
#endif
}

// Frames drawn in the last second.
static void CountFps(void) {
    static struct timespec t0;
    static int frames;
    struct timespec t;
    long ms;
    clock_gettime(CLOCK_MONOTONIC, &t);
    frames++;
    ms = (t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000;
    if (ms >= 1000) {
        fps = (frames * 1000 + ms / 2) / ms;
        frames = 0;
        t0 = t;
    }
}

// Top right corner of the view; the HUD is top left. Over a title or menu
// frame: the corner of the screen, the text zoomed to its stage size.
int Wide_Canvas(int* x0, int* y0, int* x1, int* y1);
static void DrawOverlay(void) {
    char lines[4][40];
    u8 col[4][3];
    int n = 0, i, len, maxlen = 0, x0, y0, x1, y1;

    if (g_OverlayOn && g_GameState == Game_Play &&
        g_GameEngineStep == Engine_Normal) {
        sprintf(lines[n], "%s ROOM %d", StageLabel(g_StageId), room_num);
        col[n][0] = col[n][1] = col[n][2] = 255;
        n++;
        sprintf(lines[n], "CELL %d,%d %s",
                g_Tilemap.left +
                    ((PLAYER.posX.i.hi + g_Tilemap.scrollX.i.hi) >> 8),
                g_Tilemap.top +
                    ((PLAYER.posY.i.hi + g_Tilemap.scrollY.i.hi) >> 8),
                g_StageId & STAGE_INVERTEDCASTLE_FLAG ? "INVERTED" : "NORMAL");
        col[n][0] = col[n][1] = col[n][2] = 255;
        n++;
        sprintf(lines[n], "%s%s%s%s%s", g_Cheat.refill ? " REFILL" : "",
                g_Cheat.invincible ? " INV" : "", g_Cheat.walls ? " WALLS" : "",
                g_Cheat.items ? " ITEMS" : "", g_Cheat.map ? " MAP" : "");
        if (lines[n][0]) {
            memmove(lines[n], lines[n] + 1, strlen(lines[n])); // lead space
            col[n][0] = col[n][1] = 255;
            col[n][2] = 64;
            n++;
        }
    }
    if (g_FpsOn) {
        int hd_draws, hd_uploads;
        sprintf(lines[n], "FPS %d", fps);
        col[n][0] = col[n][2] = 128;
        col[n][1] = 255;
        n++;
        if (Psyz_HdTexGetStats(&hd_draws, &hd_uploads) && n < 4) {
            sprintf(lines[n], "HD %d UP %d", hd_draws, hd_uploads);
            col[n][0] = col[n][2] = 128;
            col[n][1] = 255;
            n++;
        }
    }
    if (!n) {
        return;
    }
    tz = Wide_Canvas(&x0, &y0, &x1, &y1);
    if (!tz) {
        ViewRect(&x0, &y0, &x1, &y1);
        tz = 1;
    }
    x1 = px_cx + (int)((x1 - px_cx) * px_a); // in the square pixels of Rect
    // text first: drawn on top (LIFO); 9 px lines, right aligned
    for (i = 0; i < n; i++) {
        len = strlen(lines[i]);
        maxlen = len > maxlen ? len : maxlen;
        Text(x1 - (1 + len * CELL_W) * tz, y0 + (3 + i * 9) * tz, lines[i],
             col[i][0], col[i][1], col[i][2]);
    }
    Rect(x1 - (4 + maxlen * CELL_W) * tz, y0 + tz, (maxlen * CELL_W + 4) * tz,
         (n * 9 + 2) * tz, 8, 8, 24);
    tz = 1;
}

// Called at the end of RenderPrimitives with the frame's ordering table.
// The OT is drawn in index order, so 0x1FF puts the menu on top.
// Marks and serif cells a stage asked for this frame (PortMenu_Mark,
// PortMenu_Cell), drawn once and forgotten.
int g_WarpMapPick = 1; // warp_map: the portal picks the room on the map
s32 g_WarpMapCursor[3];
static int warp_here; // the room of the portal
static int warp_none; // no other room is open
static int warp_hint;

// The warp portal map (warp_map): the game's own map screen (Engine_Map) with
// the cursor on the open warp rooms. Cells of src/st/wrp/warp.c (normal
// castle) and src/st/rwrp/warp.c (inverted castle).
static const s16 warp_cell_n[6][2] = {{0x0F, 0x26}, {0x23, 0x2C}, {0x3B, 0x11},
                                      {0x28, 0x0C}, {0x25, 0x15}, {0x02, 0x2D}};
static const s16 warp_cell_r[5][2] = {
    {0x30, 0x19}, {0x1C, 0x13}, {0x04, 0x2E}, {0x17, 0x33}, {0x1A, 0x2A}};
static int warp_cur;
static int warp_arm; // 0 until every arrow is released after the map opened
static int warp_inv; // the map of this portal: the inverted castle

#define WARP_COUNT() (warp_inv ? LEN(warp_cell_r) : LEN(warp_cell_n))
#define WARP_CELL(r) (warp_inv ? warp_cell_r[r] : warp_cell_n[r])

// A warp room can be picked when its own map cell is explored (the map cheat
// explores them all) or the castle warp flag of the original is set (the
// portal was used): the union, nothing the original allowed is lost.
static int WarpOpen(int room) {
    int cx, cy, idx;
    if (room < 0 || room >= WARP_COUNT()) {
        return 0;
    }
#ifndef ENABLE_SATURN_STAGES
    if (!warp_inv && room == 5) { // the Saturn warp room needs its stages
        return 0;
    }
#endif
    if (g_CastleFlags[warp_inv ? RWRP_UNLOCKS : WRP_UNLOCKS] & (1 << room)) {
        return 1;
    }
    cx = WARP_CELL(room)[0];
    cy = WARP_CELL(room)[1];
    idx = (cx >> 2) + cy * 16 +
          (warp_inv ? 2 * 4 * WIDTH_OF_MAP_ROW_IN_BYTES : 0);
    return (g_CastleMap[idx] >> ((3 - (cx & 3)) * 2) & 1) != 0;
}

// the area next to the map cell of a warp room (the warp room itself is a
// stage of its own, WRP or RWRP: the nearest other area names the place)
static int WarpZone(int room) {
    int i, best = g_StageId, bd = 9999, cx = WARP_CELL(room)[0],
           cy = WARP_CELL(room)[1];
    for (i = 0; i < g_CastleRoomCount; i++) {
        const struct CastleRoom* r = &g_CastleRooms[i];
        int dx = cx < r->left ? r->left - cx : cx > r->right ? cx - r->right : 0;
        int dy = cy < r->top ? r->top - cy : cy > r->bottom ? cy - r->bottom : 0;
        if (dx + dy < bd && (r->stage & ~STAGE_INVERTEDCASTLE_FLAG) != STAGE_WRP &&
            *Castle_ZoneName(r->stage) && !!(r->stage & STAGE_INVERTEDCASTLE_FLAG) == !!warp_inv) {
            bd = dx + dy;
            best = r->stage;
        }
    }
    return best;
}

// Opens the map with the cursor on this portal room; warp_none is set when no
// other room is open (the map then shows the place and a message). Always 1.
int PortMenu_WarpOpen(int here) {
    int r;
    warp_inv = (g_StageId & STAGE_INVERTEDCASTLE_FLAG) != 0;
    warp_arm = 0;
    warp_click = -1;
    warp_cancel = 0;
    if (here < 0 || here >= WARP_COUNT()) {
        here = 0;
    }
    warp_here = here;
    warp_cur = here;
    warp_none = 1;
    for (r = 1; r < WARP_COUNT(); r++) {
        if (WarpOpen((here + r) % WARP_COUNT())) {
            warp_none = 0;
            break;
        }
    }
    fprintf(stderr, "[warp] portal %d cell %d,%d, open mask %#x, %s\n", here,
            WARP_CELL(here)[0], WARP_CELL(here)[1],
            g_CastleFlags[warp_inv ? RWRP_UNLOCKS : WRP_UNLOCKS],
            warp_none ? "no other room open" : "other rooms open");
    g_api.SetFadeMode(FADE_SHOW_MAP);
    D_800974A4 = 1;
    g_GameEngineStep = Engine_Map;
    return 1;
}

// The pad and the left stick as one set of arrows: a new press, then a repeat
// after 24 frames every 8 (the stick has a dead zone in StickDirs).
static u32 WarpArrows(void) {
    static u16 prev;
    static int held;
    u16 now = (u16)(g_pads[0].pressed | StickDirs()) &
              (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN);
    u16 hit;
    if (!warp_arm) { // the UP that opened the map is still held
        prev = now;
        held = 0;
        warp_arm = now == 0;
        return 0;
    }
    hit = now & ~prev;
    held = now && now == prev ? held + 1 : 0;
    if (held >= 24 && (held - 24) % 8 == 0) {
        hit = now;
    }
    prev = now;
    return hit;
}

// The arrows move the cursor over the map: to the nearest room (this portal
// room or an open one) in that direction, first among those whose main axis is
// the arrow's, else any room on that side; none: stay.
static long long jump_tick = -1000; // ms of the last arrow jump
static void WarpMove(u32 dir) {
    int r, best = -1, bestd = 0, pass;
    int cx = WARP_CELL(warp_cur)[0], cy = WARP_CELL(warp_cur)[1];
    for (pass = 0; pass < 2 && best < 0; pass++) {
        for (r = 0; r < WARP_COUNT(); r++) {
            int dx = WARP_CELL(r)[0] - cx, dy = WARP_CELL(r)[1] - cy;
            int d, p, dist;
            if (r == warp_cur || (r != warp_here && !WarpOpen(r))) {
                continue;
            }
            if (dir & PAD_LEFT) {
                d = -dx, p = dy;
            } else if (dir & PAD_RIGHT) {
                d = dx, p = dy;
            } else if (dir & PAD_UP) {
                d = -dy, p = dx;
            } else {
                d = dy, p = dx;
            }
            if (d <= 0 || (pass == 0 && abs(p) > d)) {
                continue;
            }
            dist = dx * dx + dy * dy;
            if (best < 0 || dist < bestd) {
                best = r, bestd = dist;
            }
        }
    }
    if (best >= 0) {
        warp_cur = best;
        jump_tick = SDL_GetTicks(); // a resting mouse must not undo the jump
        fprintf(stderr, "[warp] jump %s to room %d cell %d,%d\n",
                dir & PAD_LEFT ? "left" : dir & PAD_RIGHT ? "right"
                : dir & PAD_UP ? "up" : "down",
                best, WARP_CELL(best)[0], WARP_CELL(best)[1]);
    }
}

// ---- the warp map by pointer (PC mouse, SDL touch as mouse, Switch touch) ----

// the room whose map cell is nearest (pointer in frame pixels, within 12) among
// this portal room and the open ones, or -1
static int WarpNear(int px, int py) {
    int r, best = -1, bd = 12 * 12 + 1;
    for (r = 0; r < WARP_COUNT(); r++) {
        int dx, dy;
        if (r != warp_here && !WarpOpen(r)) {
            continue;
        }
        dx = px - (WARP_CELL(r)[0] * 4 + (warp_inv ? 1 : 2));
        dy = py - (WARP_CELL(r)[1] * 4 + (warp_inv ? -14 : 3));
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = r;
        }
    }
    return best;
}

static void WarpSelect(int r) {
    if (r >= 0 && r != warp_cur && SDL_GetTicks() - jump_tick > 400) {
        warp_cur = r;
        fprintf(stderr, "[warp] cursor room %d cell %d,%d (pointer)\n", r,
                WARP_CELL(r)[0], WARP_CELL(r)[1]);
    }
}

// While the warp map is open the pointer is ours: touching or pressing selects
// the nearest room, lifting warps there; the hint bar cancels. 1 = handled.
static int WarpPointer(int n, const int* x, const int* y) {
    static int down, blocked, lx, ly;
    int x0, y0, x1, y1, r;
    if (!g_WarpMapCursor[0] || g_GameEngineStep != Engine_Map || is_open) {
        down = 0;
        return 0;
    }
    if (n >= 1) {
        if (!down) {
            down = 1;
            blocked = SDL_GetTicks() - focus_tick < 300;
        }
        lx = x[0];
        ly = y[0];
        if (!blocked) {
            WarpSelect(WarpNear(lx, ly));
        }
    } else if (down) {
        down = 0;
        ViewRect(&x0, &y0, &x1, &y1);
        if (!blocked) {
            if (ly >= y1 - LINE_H) {
                warp_cancel = 1;
            } else if ((r = WarpNear(lx, ly)) >= 0 && r != warp_here) {
                warp_click = r;
                fprintf(stderr, "[warp] pointer pick room %d\n", r);
            }
        }
    }
    return 1;
}

// Each frame while the map is open: -1 still open, -2 backed out, else the
// room picked (the map closes). The cursor starts on this portal room.
int PortMenu_WarpPick(int here) {
    u32 tap = g_pads[0].tapped, dir = WarpArrows();
    int res = -1;
    int east = PortMenu_ConfirmEast();
    if (g_GameEngineStep != Engine_Map) { // START or SELECT closed it
        g_WarpMapCursor[0] = 0;
        g_PauseAllowed = true; // the map screen cleared it: START pauses again
        g_Player.demo_timer = 0;
        return -2;
    }
    if (dir) {
        WarpMove(dir);
    }
    if (warp_click >= 0) {
        res = warp_click;
        warp_click = -1;
    } else if (warp_cancel) {
        res = -2;
    } else if (tap & (east ? PAD_CIRCLE : PAD_CROSS)) {
        if (!warp_none && warp_cur != warp_here) {
            res = warp_cur;
        }
    } else if (tap & (east ? PAD_CROSS : PAD_CIRCLE)) {
        res = -2;
    }
    if (res != -1) {
        g_WarpMapCursor[0] = 0;
        if (res == -2) {
            g_PauseAllowed = true;
            g_Player.demo_timer = 0;
        }
        g_api.SetFadeMode(FADE_HIDE_MAP);
        D_800974A4 = 0;
        g_GameEngineStep = Engine_Normal;
    } else {
        g_WarpMapCursor[0] = 1;
        g_WarpMapCursor[1] = WARP_CELL(warp_cur)[0];
        g_WarpMapCursor[2] = WARP_CELL(warp_cur)[1];
        warp_hint = 1;
    }
    return res;
}

static struct {
    s16 x, y;
    u8 b, cell; // cell 0: mark b
    u8 r, g, bl;
} queued[48]; // the file-select hint labels: up to 18 cells
static int queued_count;

void PortMenu_Mark(int x, int y, int b) {
    if (queued_count < LEN(queued)) {
        queued[queued_count].x = x;
        queued[queued_count].y = y;
        queued[queued_count].cell = 0;
        queued[queued_count++].b = b;
    }
}

void PortMenu_Cell(int x, int y, int cell, u32 rgb) {
    if (queued_count < LEN(queued) && cell) {
        queued[queued_count].x = x;
        queued[queued_count].y = y;
        queued[queued_count].cell = cell;
        queued[queued_count].r = rgb >> 16;
        queued[queued_count].g = rgb >> 8;
        queued[queued_count++].bl = rgb;
    }
}

// The window icon: a loose icon.png next to the program (never in git). Tried
// once the window exists; no file, no change.
static void WindowIcon(void) {
    static int done;
    int n = 0;
    SDL_Window** w;
    char path[1024];
    const char* base;
    SDL_Surface* ic;
    if (done || !(w = SDL_GetWindows(&n))) {
        return;
    }
    if (n < 1) {
        SDL_free(w);
        return;
    }
    done = 1;
    if ((base = SDL_GetBasePath())) { // NULL on some targets: no icon
        snprintf(path, sizeof(path), "%sicon.png", base);
        if ((ic = SDL_LoadPNG(path))) {
            SDL_SetWindowIcon(w[0], ic);
            SDL_DestroySurface(ic);
        }
    }
    SDL_free(w);
}

void PortMenu_Draw(OT_TYPE* ot) {
    int i;
    WindowIcon();
    drawn = 30;
    mq_now++;
    // Closed: the head of slot 0x1FF, as the game's own prims. Open: after
    // the last prim of that slot, so the menu covers the game's 0x1FF prims
    // too (dialogue text, speaker names). ClearOTag links the slot to a
    // shared terminator packet: stop at the prim before it, never write it.
    menu_tail = (P_TAG*)&ot[0x1FF];
    while (is_open && !isendprim(nextPrim(menu_tail))) {
        menu_tail = nextPrim(menu_tail);
    }
    uk = Lang_Port(P_SCALE) && Lang_MenuCell(CH('A'));
    i = PortMenu_ConfirmEast();
    hint_ok[0] = i ? 2 : 3; // the mark of Circle (right) or Cross (bottom)
    hint_back[0] = i ? 3 : 2;
    px_a = Wide_PixelAspect();
    px_a = px_a > 0.98f && px_a < 1.02f ? 1 : px_a;
    px_cx = FbW() / 2;
    tile_buf ^= 1;
    tile_count = 0;
    CountFps();
    for (i = 0; !is_open && i < queued_count; i++) {
        if (queued[i].cell) {
            const u8* rows = Lang_MenuCellAny(queued[i].cell);
            if (!rows && !uk && queued[i].cell + 0x20 < 0x7F) {
                // en: no serif cell, so the 5x7 letter (quick item toast)
                char ch[2] = {queued[i].cell + 0x20, 0};
                Text(queued[i].x, queued[i].y + 2, ch, queued[i].r,
                     queued[i].g, queued[i].bl);
            } else {
                Cell(queued[i].x, queued[i].y, rows, queued[i].r,
                     queued[i].g, queued[i].bl);
            }
        } else {
            Mark(queued[i].x, queued[i].y + 2, queued[i].b);
        }
    }
    queued_count = 0;
    if (warp_hint && !is_open) { // the warp portal map: name and buttons
        char s[LINE_MAX];
        int x0, y0, x1, y1;
        ViewRect(&x0, &y0, &x1, &y1);
        Clear(s);
        if (warp_none) {
            AddW(s, P_WARP_NONE, "NO OTHER WARP UNLOCKED YET");
            Line(x0 + 4, y1 - 2 * LINE_H + 3, s, 255, 160, 64);
        } else {
            AddZone(s, WarpZone(warp_cur));
            Line(x0 + 4, y1 - 2 * LINE_H + 3, s, 255, 255, 255);
        }
        Rect(x0, y1 - 2 * LINE_H, x1 - x0, LINE_H, 8, 8, 24);
        Clear(s);
        if (!warp_none) {
            Add(s, HINT_OK), Add(s, " "), AddW(s, P_WARP_GO, "WARP");
            Add(s, "  ");
        }
        Add(s, HINT_BACK), Add(s, " "), AddW(s, P_BACK, "BACK");
        if (!warp_none) {
            Add(s, "  \x0B "), AddW(s, P_WARP_PICK, "SELECT");
        }
        Line(x0 + 4, y1 - LINE_H + 3, s, 255, 255, 64);
        Rect(x0, y1 - LINE_H, x1 - x0, LINE_H, 8, 8, 24);
    }
    warp_hint = 0;
    if (!is_open) {
        DrawOverlay();
        return;
    }
    frame++;
    if (page == PAGE_MAIN) {
        DrawMain();
    } else if (page == PAGE_CHEATS) {
        DrawCheats();
    } else if (page == PAGE_CONTROLS) {
        DrawControls();
    } else if (page == PAGE_LIST) {
        DrawList();
    } else {
        if (page == PAGE_CHOOSE) {
            DrawChoose();
        }
        DrawMap();
    }
    if (tile_count > tile_peak) { // MAX_TILES is the ceiling
        tile_peak = tile_count;
        fprintf(stderr, "[menu] tiles peak %d of %d\n", tile_peak, MAX_TILES);
    }
}
