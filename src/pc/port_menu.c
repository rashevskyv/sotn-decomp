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
// defaults), MAIN -> CHEATS -> MAP (teleport) -> CHOOSE (a cell with more
// than one room). The map draws one rect per room from g_CastleRooms. Input on
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
#include <psyz/input.h>
#ifdef __SWITCH__
#include <unistd.h>
#include <SDL3/SDL.h>
#else
#include <psyz/overlay_sdl3.h>
#endif

enum { PAGE_MAIN, PAGE_CHEATS, PAGE_MAP, PAGE_CHOOSE, PAGE_CONTROLS };
enum {
    ITEM_SCALE,
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
    // no vertical margin (240 rows) keeps the stage clip of rows 20..226
    *y0 = g_WideY ? -g_WideY : 20;
    *y1 = g_WideY ? DISP_STAGE_H + g_WideY : 226;
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

// ---- pointer ----------------------------------------------------------------

static void Activate(void);

// the line of a centred text box under frame row y, or -1
static int BoxLineAt(int y, int count) {
    int top = (FbH() - (count * LINE_H - 5 + BORDER * 2)) / 2 + BORDER;
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
                                : n_owners;
    i = BoxLineAt(y, i);
    if (page == PAGE_CONTROLS) {
        if (i > 0 && !capture) { // line 0 is the header
            sel = ctl_top + i - 1;
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
    if (sel == ITEM_SCALE) {
        scale = scale ? scale + dir : (dir > 0 ? lo : hi);
        if (scale >= lo && scale <= hi) {
            Wide_MenuSet(scale);
        }
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
            OpenMap();
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
        page = PAGE_CHEATS;
        sel = CH_TELEPORT;
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
};
#undef G

static TILE tiles[2][MAX_TILES];
static int tile_buf;
static int tz = 1; // Text zoom: the overlay over a title or menu frame
static int tile_count, tile_peak;
static OT_TYPE* menu_ot;

static void Rect(int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    TILE* t;
    if (tile_count >= MAX_TILES || w <= 0 || h <= 0) {
        return;
    }
    t = &tiles[tile_buf][tile_count++];
    setTile(t);
    setRGB0(t, r, g, b);
    t->x0 = x;
    t->y0 = y;
    t->w = w;
    t->h = h;
    addPrim(&menu_ot[0x1FF], t);
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

// the port word i under uk, else the English en
static void AddW(char* line, int i, const char* en) {
    const char* w = uk && i >= 0 ? Lang_Port(i) : NULL;
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
    w = uk ? Width(hint) : TEXT_COLS * CELL_W;
    for (i = 0; i < count; i++) { // as wide as the longest line
        w = Width(lines[i]) > w ? Width(lines[i]) : w;
    }
    w += BORDER * 2;
    h = count * LINE_H - 5 + BORDER * 2;
    x = (FbW() - w) / 2;
    y = (FbH() - h) / 2;
    for (i = 0; i < count; i++) {
        c = i == hi ? 255 : 160;
        if (i == info) {
            Line(x + BORDER, y + BORDER + i * LINE_H, lines[i], 128, 200, 255);
            continue;
        }
        Line(x + BORDER, y + BORDER + i * LINE_H, lines[i], c, c,
             i == hi ? 64 : c);
    }
    // the button hint under the box
    Line(x + BORDER, y + h + 4, hint, 255, 255, 64);
    Rect(x, y + h, w, LINE_H + 2, 8, 8, 24);
    Rect(x + 2, y + 2, w - 4, h - 4, 16, 16, 48);
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
        {P_SCALE, "SCALE"},     {P_VOICE, "VOICE"},   {P_LANGUAGE, "LANGUAGE"},
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
    // the values in one column: en 9 cells (LANGUAGE has a gap), uk one
    // cell after the longest label
    col = uk ? col + SERIF_W : 9 * CELL_W;
    for (i = 0; i < ITEM_CONTROLS; i++) {
        PadTo(lines[i], col);
    }
    if (scale) {
        sprintf(tmp, "%c %d %c", scale > lo ? '<' : ' ', scale,
                scale < hi ? '>' : ' ');
        Add(lines[ITEM_SCALE], tmp);
    } else {
        Add(lines[ITEM_SCALE], "< ");
        AddW(lines[ITEM_SCALE], P_AUTO, "AUTO");
    }
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
    menu_ot = ot;
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
