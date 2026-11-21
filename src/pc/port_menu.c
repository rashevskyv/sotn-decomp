// SPDX-License-Identifier: AGPL-3.0-or-later
// Port menu, drawn by the game itself with TILE prims, so it works with
// every psyz renderer. L3+R3 opens and closes it. Up and down pick an item,
// left and right change it, Cross (Switch B, the bottom face button)
// activates it, Circle (Switch A, the right one) or Triangle goes back; on
// MAIN back closes the menu. SDL-switch maps the face buttons by position
// (SWITCH_JoystickGetGamepadMapping: south = B), as psyz reads them. The
// game gets no pad input while the menu is open. Changes
// apply at once (a voice set at the next spoken line) and are saved in
// sotn.ini by Wide_SaveIni.
//
// Pages: MAIN -> CONTROLS (the key and gamepad button of each PS button:
// Cross on a line, then a key or gamepad button sets it; RESET restores the
// defaults), MAIN -> CHEATS -> LIST (teleport: the castle map, the boss
// list, then every stage by name; Cross warps to its start or into the boss
// fight, left and right turn a page; a mini map beside it shows the rooms of
// the line in red, and the pointer over a room selects its line; a boss line
// shows the bestiary picture) -> MAP -> CHOOSE (a cell with more than one
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
#ifdef __SWITCH__
#include <unistd.h>
#include <SDL3/SDL.h>
#else
#include <psyz/overlay_sdl3.h>
#include <psyz/video.h>
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
enum {
    ITEM_SCALE,
#ifndef __SWITCH__
    ITEM_FULLSCREEN, // the window mode, saved as the mode at the start
#endif
    ITEM_VOICE,
    ITEM_LANGUAGE,
    ITEM_BUTTONS, // the button marks of the menus
    ITEM_OVERLAY,
    ITEM_FPS,
    ITEM_EDITOR, // the HTTP editor server; its address shows in the next line
    ITEM_CONTROLS,
    ITEM_CHEATS,
    ITEM_EXIT, // quit to hbmenu (PC: end the process); press twice
    ITEM_CLOSE,
    N_MAIN
};
enum {
    CH_REFILL,
    CH_INVINCIBLE,
    CH_WALLS,
    CH_ITEMS,
    CH_SKIP,
    CH_MAP,
    CH_CHARACTER,
    CH_APPLY,
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
static char msg[48];
static int msg_timer;
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
#define ITEM_BOSS 0x100 // + the g_CheatBosses index; else a stage id
#define BOSS_LINES 8    // the boss picture goes under the box
static int items[96];
static int n_items;
static int list_boss;           // 1: the boss list
static int list_top;            // the first item on screen
static int place_sel, boss_sel; // kept for the next open
static int ListLines(void) {
    return MIN(list_boss ? BOSS_LINES : CTL_LINES, n_items);
}

// the confirm and back buttons, and their marks in the hints: a byte 1 + b
// of a Text string is the mark of PS button b in the set of BUTTONS
// (src/pc/buttons.c), as this menu reads the pad: Cross is Switch B
#define PAD_OK PAD_CROSS
#define PAD_BACK (PAD_CIRCLE | PAD_TRIANGLE)
#define HINT_OK "\x03"                // Cross
#define HINT_BACK "\x02"              // Circle
#define HINT_CASTLE "\x01" "/" "\x09" // Square, Select

// pointer gestures
static int pt_n, pt_x[2], pt_y[2], pt_sx, pt_sy, pt_moved;

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
    msg_timer = 150;
}

static int FbW(void) { return g_CurrentBuffer->disp.disp.w; }
static int FbH(void) { return g_CurrentBuffer->disp.disp.h; }

// ---- map maths --------------------------------------------------------------

static void ViewRect(int* x0, int* y0, int* x1, int* y1) {
    *x0 = -g_WideX;
    *x1 = DISP_STAGE_W + g_WideX;
    *y0 = 20; // the stage clip rows (func_800E34DC), also in the wide view
    *y1 = 226;
}

static int CellX(int cx) { return (ox16 + cx * zoom16) >> 4; }
static int CellY(int cy) { return (oy16 + cy * zoom16) >> 4; }

static void KeepCursorVisible(void) {
    int x0, y0, x1, y1, z = zoom16 >> 4;
    ViewRect(&x0, &y0, &x1, &y1);
    y0 += 2 * LINE_H; // the text lines on top
    y1 -= LINE_H;     // the hint line at the bottom
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

static void CenterOnCursor(void) {
    ox16 = (DISP_STAGE_W / 2 << 4) - cur_x * zoom16 - zoom16 / 2;
    oy16 = (DISP_STAGE_H / 2 << 4) - cur_y * zoom16 - zoom16 / 2;
}

static void FindOwners(int cx, int cy) {
    const struct CastleRoom* r;
    int i;
    n_owners = 0;
    for (i = 0; i < g_CastleRoomCount && n_owners < MAX_OWNERS; i++) {
        r = &g_CastleRooms[i];
        if ((r->stage & 0x20) == castle && cx >= r->left && cx <= r->right &&
            cy >= r->top && cy <= r->bottom) {
            owners[n_owners++] = r;
        }
    }
}

static void Go(const struct CastleRoom* r) {
    const char* why = Cheat_Warp(r->stage, r->room, cur_x, cur_y);
    if (why) {
        Message(why);
        page = PAGE_MAP;
    } else {
        page = PAGE_MAIN;
        Close();
    }
}

static void Teleport(void) {
    FindOwners(cur_x, cur_y);
    if (n_owners == 0) {
        Message("no room here");
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
    CenterOnCursor();
    fprintf(stderr, "[cheat] map: castle %s\n", castle ? "inverted" : "normal");
}

static void OpenMap(void) {
    page = PAGE_MAP;
    if (!map_ready || castle != (g_StageId & 0x20)) {
        castle = g_StageId & 0x20;
        cur_x = D_8009791C.x & 63;
        cur_y = D_8009791C.y & 63;
        CenterOnCursor();
        map_ready = 1;
    }
    if (g_CastleRoomCount == 0) {
        Message("no castle data in this build");
    }
}

static void OpenList(int boss) {
    int castle_bit, i, j, id;
    n_items = 0;
    list_boss = boss;
    page = PAGE_LIST;
    if (boss) {
        for (i = 0; i < g_CheatBossCount && n_items < LEN(items); i++) {
            items[n_items++] = ITEM_BOSS + i;
        }
        sel = boss_sel < n_items ? boss_sel : 0;
        return;
    }
    items[n_items++] = ITEM_MAP;
    items[n_items++] = ITEM_BOSSES;
    for (castle_bit = 0; castle_bit <= 0x20; castle_bit += 0x20) {
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

// the first list line with a room at cell (cx, cy) of castle c, or -1
static int ItemAt(int cx, int cy, int c) {
    const struct CastleRoom* r;
    int i, k;
    for (k = 0; k < n_items; k++) {
        for (i = 0; i < g_CastleRoomCount; i++) {
            r = &g_CastleRooms[i];
            if ((r->stage & 0x20) == c && cx >= r->left && cx <= r->right &&
                cy >= r->top && cy <= r->bottom && ItemHas(items[k], r)) {
                return k;
            }
        }
    }
    return -1;
}

// the mini map of the list page: cell (0, 0) at (mini_ox, mini_oy) in 1/16
// px, mini_s 1/16 px a cell, inside the frame rect mini_x0..mini_x1
static int mini_ox, mini_oy, mini_s, mini_castle;
static int mini_x0, mini_y0, mini_x1, mini_y1;

// a pointer at frame (x, y) over the mini map selects the line of the room
// there; returns 1 inside the mini map
static int MiniPick(int x, int y) {
    int k;
    if (!mini_s || x < mini_x0 || x >= mini_x1 || y < mini_y0 || y >= mini_y1 ||
        (x << 4) < mini_ox || (y << 4) < mini_oy) {
        return 0;
    }
    k = ItemAt(((x << 4) - mini_ox) / mini_s, ((y << 4) - mini_oy) / mini_s,
               mini_castle);
    if (k >= 0) {
        sel = k;
    }
    return 1;
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

// The main page has one more line while the editor server is on: its
// address, under EDITOR. Line -> item, -1 for the address line.
static int MainLines(void) { return N_MAIN + (Editor_Status() != NULL); }
static int MainItemAt(int line) {
    if (!Editor_Status() || line <= ITEM_EDITOR) {
        return line;
    }
    return line == ITEM_EDITOR + 1 ? -1 : line - 1;
}

static void Tap(int x, int y) {
    int x0, y0, x1, y1, i;
    if (page == PAGE_MAP) {
        ViewRect(&x0, &y0, &x1, &y1);
        if (y < y0 + 2 * LINE_H || y >= y1 - LINE_H) {
            SwitchCastle(); // a tap on the title line or the hint line
            return;
        }
        x = ((x << 4) - ox16) / zoom16;
        y = ((y << 4) - oy16) / zoom16;
        if (x >= 0 && x < 64 && y >= 0 && y < 64) {
            cur_x = x;
            cur_y = y;
            Teleport();
        }
        return;
    }
    i = page == PAGE_MAIN       ? MainLines()
        : page == PAGE_CHEATS   ? N_CHEATS
        : page == PAGE_CONTROLS ? CTL_LINES + 1
        : page == PAGE_LIST     ? ListLines() + 1
                                : n_owners;
    if (page == PAGE_LIST && (x < box_x || x >= box_x + box_w)) {
        MiniPick(x, y); // a tap on the mini map selects, Cross then warps
        return;
    }
    i = BoxLineAt(y, i);
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
    Activate();
}

void PortMenu_Touch(int n, const int* x, const int* y) {
    int i, d, pd, mx, my;
    if (!is_open) {
        pt_n = 0;
        return;
    }
    n = n > 2 ? 2 : n;
    if (n == 1 && pt_n != 1) {
        pt_sx = x[0];
        pt_sy = y[0];
        pt_moved = pt_n == 2; // the end of a pinch is no tap
    } else if (n == 1) {
        if (abs(x[0] - pt_sx) + abs(y[0] - pt_sy) > 4) {
            pt_moved = 1;
        }
        if (pt_moved && page == PAGE_MAP) {
            ox16 += (x[0] - pt_x[0]) << 4;
            oy16 += (y[0] - pt_y[0]) << 4;
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
    }
    for (i = 0; i < n; i++) {
        pt_x[i] = x[i];
        pt_y[i] = y[i];
    }
    pt_n = n;
}

void PortMenu_Wheel(int dir, int x, int y) {
    if (is_open && page == PAGE_MAP) {
        Zoom(dir > 0 ? zoom16 * 5 / 4 + 1 : zoom16 * 4 / 5, x, y);
    }
}

#ifndef __SWITCH__
// PC: the mouse through the psyz SDL event hook. The Switch reads touch from
// libnx (switch_nx.c); the SDL-switch touch code is not used.
static PsyzOverlayEventCB_SDL3 prev_cb;
static int mouse_down, mouse_x, mouse_y;
static int mouse_seen = 1; // 0: a press not seen by a frame yet (fast click)
static void OnSdlEvent(const SDL_Event* e) {
    int x, y;
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

static void Change(int dir) {
    int scale, lo, hi, *flag;
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
               : sel == CH_SKIP       ? &g_Cheat.skipPrologue
               : sel == CH_MAP        ? &g_Cheat.map
                                      : NULL;
        if (sel == CH_CHARACTER) {
            // only the values that work; the rest are kept for later
            g_Cheat.character = Cheat_CharacterNext(g_Cheat.character, dir);
            fprintf(stderr, "[cheat] menu: character %d\n", g_Cheat.character);
            Wide_SaveIni();
        } else if (flag) {
            *flag ^= 1;
            fprintf(stderr,
                    "[cheat] menu: refill %d invincible %d walls %d items %d "
                    "map %d skip_prologue %d\n",
                    g_Cheat.refill, g_Cheat.invincible, g_Cheat.walls,
                    g_Cheat.items, g_Cheat.map, g_Cheat.skipPrologue);
            Wide_SaveIni();
        }
        return;
    }
    Wide_MenuGet(&scale, &lo, &hi);
    if (sel == ITEM_SCALE) { // lo .. hi, then MAX
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
    } else if (sel == ITEM_FULLSCREEN) {
        g_Fullscreen = !Psyz_VideoGetFullscreen();
        Psyz_VideoSetFullscreen(g_Fullscreen);
        fprintf(stderr, "[video] menu: fullscreen %d\n", g_Fullscreen);
        Wide_SaveIni();
#endif
    } else if (sel == ITEM_VOICE) {
        // the next spoken line uses the new set
        g_VoiceSet = (g_VoiceSet + dir + VOICE_COUNT) % VOICE_COUNT;
        fprintf(
            stderr, "[voice] menu: voice %s\n", g_VoiceSetNames[g_VoiceSet]);
        Wide_SaveIni();
    } else if (sel == ITEM_LANGUAGE) {
        // saved only: the language of this run is fixed at boot
        g_LangNext ^= 1;
        fprintf(stderr, "[lang] menu: language %s at the next start\n",
                g_LangNext ? "uk" : "en");
        Wide_SaveIni();
    } else if (sel == ITEM_BUTTONS) {
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
    } else if (sel == ITEM_OVERLAY || sel == ITEM_FPS) {
        flag = sel == ITEM_OVERLAY ? &g_OverlayOn : &g_FpsOn;
        *flag ^= 1;
        fprintf(stderr, "[overlay] menu: overlay %d fps %d\n", g_OverlayOn,
                g_FpsOn);
        Wide_SaveIni();
    } else if (sel == ITEM_EDITOR) {
        // Editor_OnFrame opens or closes the port at the next vsync
        g_EditorOn ^= 1;
        fprintf(stderr, "[editor] menu: editor %d\n", g_EditorOn);
        Wide_SaveIni();
    }
}

// Cross, Circle or a tap on a line
static void Activate(void) {
    if (page == PAGE_MAIN) {
        if (sel == ITEM_CHEATS) {
            page = PAGE_CHEATS;
            sel = 0;
        } else if (sel == ITEM_CONTROLS) {
            page = PAGE_CONTROLS;
            sel = ctl_top = 0;
        } else if (sel == ITEM_CLOSE) {
            Close();
        } else if (sel == ITEM_EXIT) {
            if (exit_armed) {
                Quit();
            }
            exit_armed = !exit_armed;
        } else {
            Change(1);
        }
    } else if (page == PAGE_CHEATS) {
        if (sel == CH_TELEPORT) {
            OpenList(0);
        } else if (sel == CH_APPLY) {
            const char* why = Cheat_ApplyCharacter();
            if (why) {
                Message(why); // shown in the APPLY NOW line
            } else {
                page = PAGE_MAIN;
                Close();
            }
        } else if (sel == CH_BACK) {
            page = PAGE_MAIN;
            sel = ITEM_CHEATS;
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
            sel = ITEM_CONTROLS;
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
            Message(why); // shown in the header line
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
    u16 tap = pressed & ~prev;
    int count;
#ifndef __SWITCH__
    static int hooked;
    if (!hooked) {
        hooked = 1;
        prev_cb = Psyz_OverlayEvent_SDL3(OnSdlEvent);
    }
    // a press and release inside one frame still counts as a tap
    count = mouse_down || !mouse_seen;
    mouse_seen = 1;
    PortMenu_Touch(count, &mouse_x, &mouse_y);
    {
        // the mouse over the mini map selects; only when it moves, so a
        // resting pointer does not fight the D-pad
        static int hover_x, hover_y;
        if (is_open && page == PAGE_LIST &&
            (mouse_x != hover_x || mouse_y != hover_y)) {
            MiniPick(mouse_x, mouse_y);
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
            sel = ITEM_SCALE;
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

    if (page == PAGE_MAP) {
        MapPad(pressed, tap);
        return 0;
    }
    count = page == PAGE_MAIN       ? N_MAIN
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
            sel = page == PAGE_CHEATS ? ITEM_CHEATS : ITEM_CONTROLS;
            page = PAGE_MAIN;
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

static void Rect(int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    TILE* t;
    if (tile_count >= MAX_TILES || w <= 0 || h <= 0) {
        return;
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

// A centred box of text lines. The OT draws prims in insertion order
// inside one index (LIFO), so the text goes in first and the box last.
// Line hi is the selected one; a line in info (or -1) is light blue.
static void Box(char lines[][LINE_MAX], int count, int hi, int info) {
    char hint[LINE_MAX];
    int i, x, y, w, h;
    u8 c;
    Clear(hint);
    Add(hint, HINT_OK ": ");
    AddW(hint, P_OK, "OK");
    Add(hint, "   " HINT_BACK ": ");
    AddW(hint, P_BACK, "BACK");
    w = uk || box_left ? Width(hint) : TEXT_COLS * CELL_W;
    for (i = 0; i < count; i++) { // as wide as the longest line
        w = Width(lines[i]) > w ? Width(lines[i]) : w;
    }
    w += BORDER * 2;
    h = count * LINE_H - 5 + BORDER * 2;
    x = box_left ? box_x : (FbW() - w) / 2;
    y = box_left ? box_y : (FbH() - h) / 2;
    box_x = x;
    box_y = y;
    box_w = w;
    box_h = h;
    for (i = 0; i < count; i++) {
        c = i == hi ? 255 : 160;
        if (i == info) {
            Line(x + BORDER, y + BORDER + i * LINE_H, lines[i], 128, 200, 255);
            continue;
        }
        Line(x + BORDER, y + BORDER + i * LINE_H, lines[i], c, c,
             i == hi ? 64 : c);
    }
    // the button hint under the box; beside the mini map the box lets the
    // rooms under it show through, so its frame is four lines
    Line(x + BORDER, y + h + 4, hint, 255, 255, 64);
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

static void DrawMain(void) {
    static const char* voices[VOICE_COUNT] = {"PS1 EN", "JP", "PSP EN"};
    static const char* sets[BUTTON_SETS] = {"NINTENDO", "XBOX",
                                            "PLAYSTATION"};
    static const char* short_sets[BUTTON_SETS] = {"NINTENDO", "XBOX", "PS"};
    static const struct {
        int word;
        const char* en;
    } labels[N_MAIN] = {
        {P_SCALE, "SCALE"},
#ifndef __SWITCH__
        {P_FULLSCREEN, "FULLSCREEN"},
#endif
        {P_VOICE, "VOICE"},     {P_LANGUAGE, "LANGUAGE"},
        {P_BUTTONS, "BUTTONS"}, {P_OVERLAY, "OVERLAY"}, {-1, "FPS"},
        {P_EDITOR, "EDITOR"},   {P_CONTROLS, "CONTROLS"},
        {P_CHEATS, "CHEATS"},   {P_EXIT, "EXIT GAME"},
        {P_CLOSE, "CLOSE"},
    };
    int on[3];
    char lines[N_MAIN + 1][LINE_MAX], tmp[40];
    int scale, lo, hi, i, col = 0;
    on[0] = g_OverlayOn;
    on[1] = g_FpsOn;
    on[2] = g_EditorOn;
    Wide_MenuGet(&scale, &lo, &hi);
    for (i = 0; i < N_MAIN; i++) {
        Clear(lines[i]);
        if (i == ITEM_EXIT && exit_armed) {
            if (uk) {
                AddW(lines[i], P_AGAIN, "EXIT:");
                Add(lines[i], " " HINT_OK);
            } else {
                Add(lines[i], "EXIT: " HINT_OK " AGAIN TO QUIT");
            }
            continue;
        }
        AddW(lines[i], labels[i].word, labels[i].en);
        if (i < ITEM_CONTROLS && Width(lines[i]) > col) {
            col = Width(lines[i]);
        }
    }
    // the values in one column: en 9 cells (LANGUAGE has a gap) or one
    // cell after a longer label (FULLSCREEN), uk one cell after the longest
    col = uk ? col + SERIF_W : MAX(9 * CELL_W, col + CELL_W);
    for (i = 0; i < ITEM_CONTROLS; i++) {
        PadTo(lines[i], col);
    }
    if (scale == WIDE_SCALE_MAX) {
        Add(lines[ITEM_SCALE], "< ");
        AddW(lines[ITEM_SCALE], P_MAX, "MAX");
    } else if (scale) { // MAX follows hi
        sprintf(tmp, "%c %d >", scale > lo ? '<' : ' ', scale);
        Add(lines[ITEM_SCALE], tmp);
    } else {
        Add(lines[ITEM_SCALE], "< ");
        AddW(lines[ITEM_SCALE], P_AUTO, "AUTO");
    }
#ifndef __SWITCH__
    i = Psyz_VideoGetFullscreen(); // F11 and Esc change it too
    AddValue(lines[ITEM_FULLSCREEN], i ? P_ON : P_OFF, i ? "ON" : "OFF");
#endif
    AddValue(lines[ITEM_VOICE], -1, voices[g_VoiceSet]);
    AddValue(lines[ITEM_LANGUAGE], -1, g_LangNext ? "UK" : "EN");
    if (g_LangNext != g_Lang) {
        Add(lines[ITEM_LANGUAGE], " ");
        AddW(lines[ITEM_LANGUAGE], P_RESTART, "RESTART");
    }
    if (g_ButtonPick < 0) { // "< AUTO: XBOX >"
        Add(lines[ITEM_BUTTONS], "< ");
        AddW(lines[ITEM_BUTTONS], P_AUTO, "AUTO");
        Add(lines[ITEM_BUTTONS], ": ");
        Add(lines[ITEM_BUTTONS], short_sets[g_ButtonSet]);
        Add(lines[ITEM_BUTTONS], " >");
    } else {
        AddValue(lines[ITEM_BUTTONS], -1, sets[g_ButtonPick]);
    }
    for (i = 0; i < 3; i++) { // OVERLAY FPS EDITOR
        AddValue(lines[ITEM_OVERLAY + i], on[i] ? P_ON : P_OFF,
                 on[i] ? "ON" : "OFF");
    }
    if (!Editor_Status()) {
        Box(lines, N_MAIN, sel, -1);
        return;
    }
    // the address line goes under EDITOR; the lines after it move down
    memmove(lines[ITEM_EDITOR + 2], lines[ITEM_EDITOR + 1],
            sizeof(lines[0]) * (N_MAIN - ITEM_EDITOR - 1));
    Clear(lines[ITEM_EDITOR + 1]);
    snprintf(tmp, sizeof(tmp), uk ? "%.28s" : "  %.29s", Editor_Status());
    Add(lines[ITEM_EDITOR + 1], tmp);
    Box(lines, N_MAIN + 1, sel > ITEM_EDITOR ? sel + 1 : sel, ITEM_EDITOR + 1);
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
    Box(lines, CTL_LINES + 1, sel - ctl_top + 1, 0);
}

static void DrawCheats(void) {
    static const struct {
        int word;
        const char* en;
    } names[] = {{P_REFILL, "HP MP HEARTS"}, {P_INVINCIBLE, "NO DAMAGE"},
                 {P_WALLS, "MIST WALLS"},    {P_ITEMS, "ALL ITEMS"},
                 {P_SKIP, "SKIP PROLOGUE"},  {P_MAP, "100% MAP"},
                 {P_CHARACTER, "CHARACTER"}},
      chars[CHEAT_CHAR_N] = {{P_GAME, "GAME"},       {P_ALUCARD, "ALUCARD"},
                             {P_RICHTER, "RICHTER"}, {P_RICHTER, "RICHTER"},
                             {P_MARIA, "MARIA"},     {P_MARIA, "MARIA"}};
    int flags[6];
    char lines[N_CHEATS][LINE_MAX], tmp[40];
    int i, col = 0;
    flags[0] = g_Cheat.refill;
    flags[1] = g_Cheat.invincible;
    flags[2] = g_Cheat.walls;
    flags[3] = g_Cheat.items;
    flags[4] = g_Cheat.skipPrologue;
    flags[5] = g_Cheat.map;
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
        } else {
            AddValue(lines[i], chars[g_Cheat.character].word,
                     chars[g_Cheat.character].en);
        }
    }
    Clear(lines[CH_APPLY]);
    if (msg_timer > 0) {
        msg_timer--;
        AddW(lines[CH_APPLY], P_NO, "NO:");
        snprintf(tmp, sizeof(tmp), uk ? " %.24s" : " %.27s", msg);
        Add(lines[CH_APPLY], tmp);
    } else {
        AddW(lines[CH_APPLY], P_APPLY, "APPLY NOW");
    }
    Clear(lines[CH_TELEPORT]);
    AddW(lines[CH_TELEPORT], P_TELEPORT, "TELEPORT");
    Clear(lines[CH_BACK]);
    AddW(lines[CH_BACK], P_BACK, "BACK");
    Box(lines, N_CHEATS, sel, -1);
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

// "NZ0  SLOGRA / GAIBON": the castle stage of the fight and the names; a
// third name is left out ("...")
static void AddBoss(char* line, const struct CheatBoss* b) {
    char tmp[8];
    sprintf(
        tmp, "%-4s ", Castle_StageName(b->host != 0xFF ? b->host : b->stage));
    Add(line, tmp);
    AddEnemy(line, b->enemy[0]);
    if (b->enemy[1]) {
        Add(line, " / ");
        AddEnemy(line, b->enemy[1]);
    }
    if (b->enemy[2]) {
        Add(line, " ...");
    }
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
        l = t = 63;
        r = bt = 0;
        for (i = 0; i < g_CastleRoomCount; i++) {
            rm = &g_CastleRooms[i];
            if ((rm->stage & 0x20) == mini_castle) {
                l = MIN(l, rm->left);
                t = MIN(t, rm->top);
                r = MAX(r, rm->right);
                bt = MAX(bt, rm->bottom);
            }
        }
    }
    x0 = box_x + box_w + 6;
    x0 = vx1 - 4 - x0 < 150 ? vx0 + 4 : x0;
    x1 = vx1 - 4;
    y0 = vy0 + 4;
    y1 = vy1 - 4;
    mini_s = r < l ? 0
                   : MIN(((x1 - x0) << 4) / (r - l + 1),
                         ((y1 - y0) << 4) / (bt - t + 1));
    mini_ox = (x1 << 4) - (r + 1) * mini_s; // right aligned, centred rows
    mini_oy = ((y0 + y1) << 3) - (t + bt + 1) * mini_s / 2;
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
            y = (mini_oy + rm->top * mini_s) >> 4;
            w = ((mini_ox + (rm->right + 1) * mini_s) >> 4) - x;
            h = ((mini_oy + (rm->bottom + 1) * mini_s) >> 4) - y;
            if (mini_s >= 48) { // a gap between rooms from 3 px a cell
                w--;
                h--;
            }
            // a red ring round a small boss room (a cell or two at 720p)
            if (hi && it >= ITEM_BOSS && w < 8 && h < 8) {
                Rect(x - 4, y - 4, w + 8, 1, 255, 40, 40);
                Rect(x - 4, y + h + 3, w + 8, 1, 255, 40, 40);
                Rect(x - 4, y - 3, 1, h + 6, 255, 40, 40);
                Rect(x + w + 3, y - 3, 1, h + 6, 255, 40, 40);
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
            Rect(x, y, w, h, cr, cg, cb);
        }
    }
    Rect(vx0, vy0, vx1 - vx0, vy1 - vy0, 8, 8, 24); // the background last
}

// "NO0  MARBLE GALLERY" lines, or the boss list with the boss picture
static void DrawList(void) {
    char lines[CTL_LINES + 1][LINE_MAX], tmp[40];
    const char* uk_name;
    int i, k, id, vx0, vy0, vx1, vy1, n = ListLines();
    if (sel < list_top) {
        list_top = sel;
    } else if (sel >= list_top + n) {
        list_top = sel - n + 1;
    }
    Clear(lines[0]);
    if (msg_timer > 0) {
        msg_timer--;
        AddW(lines[0], P_NO, "NO:");
        snprintf(tmp, sizeof(tmp), uk ? " %.24s" : " %.27s", msg);
        Add(lines[0], tmp);
    } else if (list_boss) {
        AddW(lines[0], P_BOSS, "BOSS");
    } else {
        AddW(lines[0], P_TELEPORT, "TELEPORT");
    }
    for (i = 1; i <= n; i++) {
        id = items[k = list_top + i - 1];
        Clear(lines[i]);
        if (id == ITEM_MAP) {
            AddW(lines[i], P_CASTLE_MAP, "CASTLE MAP");
        } else if (id == ITEM_BOSSES) {
            AddW(lines[i], P_BOSS, "BOSS");
            Add(lines[i], " >");
        } else if (id >= ITEM_BOSS) {
            AddBoss(lines[i], &g_CheatBosses[id - ITEM_BOSS]);
        } else {
            sprintf(tmp, "%-4s ", Castle_StageName(id));
            Add(lines[i], tmp);
            uk_name = uk ? Lang_Zone(id) : NULL;
            if (uk_name) {
                AddCells(lines[i], uk_name, "");
            } else {
                AddUpper(lines[i], Castle_ZoneName(id), 23);
            }
        }
    }
    ViewRect(&vx0, &vy0, &vx1, &vy1);
    box_left = 1;
    box_x = vx0 + 4;
    box_y = vy0 + 4;
    Box(lines, n + 1, sel - list_top + 1, 0);
    box_left = 0;
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

static void DrawMap(void) {
    const struct CastleRoom* rm;
    char line[LINE_MAX], legend[3][LINE_MAX], tmp[24];
    int i, x0, y0, x1, y1, x, y, w, h, gap, my0;
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
    // the hint line at the bottom, over a band that hides the rooms
    Clear(line);
    Add(line, HINT_OK " ");
    AddW(line, P_GO, "GO");
    Add(line, "  " HINT_BACK " ");
    AddW(line, P_BACK, "BACK");
    Add(line, uk ? "  " HINT_CASTLE " " : "  " HINT_CASTLE ": ");
    AddW(line, castle ? P_NORMAL : P_INVERTED, castle ? "NORMAL" : "INVERTED");
    Line(x0 + 4, y1 - LINE_H + 3, line, 255, 255, 64);
    Rect(x0, y1 - LINE_H, x1 - x0, LINE_H, 8, 8, 24);
    if (msg_timer > 0) {
        msg_timer--;
        Clear(line);
        Add(line, msg);
        Line(x0 + 4, y0 + 3 + LINE_H, line, 255, 120, 80);
    } else { // the legend: each word in its marker colour, right aligned
        for (w = 0, i = 0; i < 3; i++) {
            Clear(legend[i]);
            AddW(legend[i], P_BOSS + i, mark_names[i]);
            w += Width(legend[i]) + (i ? (uk ? SERIF_W : CELL_W) : 0);
        }
        x = x1 - 4 - w;
        for (i = 0; i < 3; i++) {
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
        ClipRect(x, y, w, 1, x0, my0, x1, y1, 255, 255, 0);
        ClipRect(x, y + w - 1, w, 1, x0, my0, x1, y1, 255, 255, 0);
        ClipRect(x, y, 1, w, x0, my0, x1, y1, 255, 255, 0);
        ClipRect(x + w - 1, y, 1, w, x0, my0, x1, y1, 255, 255, 0);
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
        ClipRect(x, y, w, w, x0, my0, x1, y1, mark_rgb[mk->kind][0],
                 mark_rgb[mk->kind][1], mark_rgb[mk->kind][2]);
    }
    // rooms; the room of the player is white
    for (i = 0; i < g_CastleRoomCount; i++) {
        rm = &g_CastleRooms[i];
        if ((rm->stage & 0x20) != castle) {
            continue;
        }
        x = CellX(rm->left);
        y = CellY(rm->top);
        w = CellX(rm->right + 1) - x - gap;
        h = CellY(rm->bottom + 1) - y - gap;
        if (x >= x1 || y >= y1 || x + w <= x0 || y + h <= my0) {
            continue;
        }
        if (rm->stage == g_StageId && D_8009791C.x >= rm->left &&
            D_8009791C.x <= rm->right && D_8009791C.y >= rm->top &&
            D_8009791C.y <= rm->bottom) {
            r = g = b = 255;
        } else {
            StageColor(rm->stage, &r, &g, &b);
        }
        ClipRect(x, y, w, h, x0, my0, x1, y1, r, g, b);
    }
    // the background last
    Rect(x0, y0, x1 - x0, y1 - y0, 8, 8, 24);
}

// ---- debug overlay ------------------------------------------------------------

int g_OverlayOn, g_FpsOn;
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
        sprintf(lines[n], "%s%s%s%s%s%s", g_Cheat.refill ? " REFILL" : "",
                g_Cheat.invincible ? " INV" : "", g_Cheat.walls ? " WALLS" : "",
                g_Cheat.items ? " ITEMS" : "",
                g_Cheat.skipPrologue ? " SKIP" : "", g_Cheat.map ? " MAP" : "");
        if (lines[n][0]) {
            memmove(lines[n], lines[n] + 1, strlen(lines[n])); // lead space
            col[n][0] = col[n][1] = 255;
            col[n][2] = 64;
            n++;
        }
    }
    if (g_FpsOn) {
        sprintf(lines[n], "FPS %d", fps);
        col[n][0] = col[n][2] = 128;
        col[n][1] = 255;
        n++;
    }
    if (!n) {
        return;
    }
    tz = Wide_Canvas(&x0, &y0, &x1, &y1);
    if (!tz) {
        ViewRect(&x0, &y0, &x1, &y1);
        tz = 1;
    }
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
static struct {
    s16 x, y;
    u8 b, cell; // cell 0: mark b
    u8 r, g, bl;
} queued[12];
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

void PortMenu_Draw(OT_TYPE* ot) {
    int i;
    drawn = 30;
    // Closed: the head of slot 0x1FF, as the game's own prims. Open: after
    // the last prim of that slot, so the menu covers the game's 0x1FF prims
    // too (dialogue text, speaker names). ClearOTag links the slot to a
    // shared terminator packet: stop at the prim before it, never write it.
    menu_tail = (P_TAG*)&ot[0x1FF];
    while (is_open && !isendprim(nextPrim(menu_tail))) {
        menu_tail = nextPrim(menu_tail);
    }
    uk = Lang_Port(P_SCALE) && Lang_MenuCell(CH('A'));
    tile_buf ^= 1;
    tile_count = 0;
    CountFps();
    for (i = 0; !is_open && i < queued_count; i++) {
        if (queued[i].cell) {
            Cell(queued[i].x, queued[i].y, Lang_MenuCellAny(queued[i].cell),
                 queued[i].r, queued[i].g, queued[i].bl);
        } else {
            Mark(queued[i].x, queued[i].y + 2, queued[i].b);
        }
    }
    queued_count = 0;
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
