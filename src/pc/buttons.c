// SPDX-License-Identifier: AGPL-3.0-or-later
// The button marks of the menus, drawn for the port pixel by pixel (no
// console or pad maker art). Three sets: Switch, Xbox, PSX. A face button is
// a disc with its mark cut out, a shoulder button a tab with its name cut
// out, 11 rows high like the pause menu cells (row 9 meets their baseline).
// Mark b is the PS button of g_ChButtons: 0 Square, 1 Circle, 2 Cross,
// 3 Triangle, 4 R2, 5 L1, 6 R1, 7 L2; then 8 Select, 9 Start, 10 the d-pad.
// The face buttons go by position, as sdl3_common.h maps them: Square west,
// Circle east, Cross south, Triangle north.
#include <game.h>
#include <psyz/input.h>
#include <stdio.h>
#include <string.h>

int g_ButtonSet = BUTTONS_PSX;
int g_ButtonPick = -1;
const char* const g_ButtonSetNames[BUTTON_SETS] = {"switch", "xbox", "psx"};

// The set of the marks: the pick, or under auto the family of the gamepad
// in use (keyboard only: the last one, at first PlayStation; the Switch:
// Nintendo). The letters of the stock pause menu font (g_ChButtons, the en
// menus) follow it. Called at boot and on every frame.
void Buttons_Update(void) {
    extern u8 g_ChButtons[8], g_ChButtons2[4];
    static const char first[BUTTON_SETS][9] = {"YABXZLRZ", "XBAYRLRL", ""};
    static const char second[BUTTON_SETS][5] = {"R  L", "TBBT", ""};
    static u8 stock[12];
    static int auto_set = BUTTONS_PSX, shown = -1;
    int set = g_ButtonPick, i;
#ifdef __SWITCH__
    auto_set = BUTTONS_SWITCH;
#else
    switch (Psyz_PadsFamily()) {
    case PSYZ_PAD_FAMILY_PLAYSTATION:
        auto_set = BUTTONS_PSX;
        break;
    case PSYZ_PAD_FAMILY_XBOX:
        auto_set = BUTTONS_XBOX;
        break;
    case PSYZ_PAD_FAMILY_NINTENDO:
        auto_set = BUTTONS_SWITCH;
        break;
    default:
        break;
    }
#endif
    if (set < 0 || set >= BUTTON_SETS) {
        set = auto_set;
    }
    g_ButtonSet = set;
    if (set == shown) {
        return;
    }
    if (shown < 0) {
        memcpy(stock, g_ChButtons, 8);
        memcpy(stock + 8, g_ChButtons2, 4);
    }
    shown = set;
    for (i = 0; i < 8; i++) {
        g_ChButtons[i] = first[set][0] ? CH(first[set][i]) : stock[i];
    }
    for (i = 0; i < 4; i++) {
        g_ChButtons2[i] = second[set][0] ? CH(second[set][i]) : stock[8 + i];
    }
    fprintf(stderr, "[buttons] marks %s%s\n", g_ButtonSetNames[set],
            g_ButtonPick < 0 ? " (auto)" : "");
}

enum { DISC, TAB_L, TAB_R, PILL, DPAD };

// the shapes, '#' lit; TAB_R is TAB_L mirrored
static const char* const art[][11] = {
    {"...#####...", "..#######..", ".#########.", "###########",
     "###########", "###########", "###########", "###########",
     ".#########.", "..#######..", "...#####..."},
    {"...............", "....##########.", "..############.",
     ".##############", "###############", "###############",
     "###############", "###############", "###############",
     ".#############.", "..............."},
    {"...............", "...............", "..###########..",
     ".#############.", "###############", "###############",
     "###############", ".#############.", "..###########..",
     "...............", "..............."},
    {"....###....", "....###....", "....###....", "....###....",
     "###########", "####...####", "###########", "....###....",
     "....###....", "....###....", "....###...."},
};
static const u8 shape_art[] = {0, 1, 1, 2, 3};

// 5x7 marks cut out of a disc: the key, then the rows
static const char* const big[] = {
    "A" ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#",
    "B" "####." "#...#" "#...#" "####." "#...#" "#...#" "####.",
    "X" "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#",
    "Y" "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#..",
    "o" ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###.", // circle
    "x" "....." "#...#" ".#.#." "..#.." ".#.#." "#...#" ".....", // cross
    "s" "....." "#####" "#...#" "#...#" "#...#" "#####" ".....", // square
    "t" "....." "..#.." ".#.#." ".#.#." "#...#" "#####" ".....", // triangle
    "-" "....." "....." "....." "#####" "....." "....." ".....",
    "+" "....." "..#.." "..#.." "#####" "..#.." "..#.." ".....",
    "v" "....." "###.." "#.###" "###.#" "..#.#" "..###" ".....", // view
    "m" "....." "#####" "....." "#####" "....." "#####" ".....", // menu
};

// 3x5 letters cut out of a tab or a pill
static const char* const small[] = {
    "L" "#.." "#.." "#.." "#.." "###", "R" "##." "#.#" "##." "#.#" "#.#",
    "Z" "###" "..#" ".#." "#.." "###", "B" "##." "#.#" "##." "#.#" "##.",
    "T" "###" ".#." ".#." ".#." ".#.", "1" ".#." "##." ".#." ".#." "###",
    "2" "##." "..#" ".#." "#.." "###", "_" "..." "..." "###" "..." "...",
    ">" "#.." "##." "###" "##." "#..", // Select, Start
};

static const struct {
    u8 shape;
    const char* text; // a big mark on a disc, small letters on a tab or pill
} sets[BUTTON_SETS][BUTTON_MARKS] = {
    {{DISC, "Y"}, {DISC, "A"}, {DISC, "B"}, {DISC, "X"}, {TAB_R, "ZR"},
     {TAB_L, "L"}, {TAB_R, "R"}, {TAB_L, "ZL"}, {DISC, "-"}, {DISC, "+"},
     {DPAD, ""}},
    {{DISC, "X"}, {DISC, "B"}, {DISC, "A"}, {DISC, "Y"}, {TAB_R, "RT"},
     {TAB_L, "LB"}, {TAB_R, "RB"}, {TAB_L, "LT"}, {DISC, "v"}, {DISC, "m"},
     {DPAD, ""}},
    {{DISC, "s"}, {DISC, "o"}, {DISC, "x"}, {DISC, "t"}, {TAB_R, "R2"},
     {TAB_L, "L1"}, {TAB_R, "R1"}, {TAB_L, "L2"}, {PILL, "_"}, {PILL, ">"},
     {DPAD, ""}},
};

static const char* Find(const char* const* t, int n, char c) {
    int i;
    for (i = 0; i < n; i++) {
        if (t[i][0] == c) {
            return t[i] + 1;
        }
    }
    return NULL;
}

static int Width(u8 shape) { return shape == DISC || shape == DPAD ? 11 : 15; }

int Buttons_Width(int b) { return Width(sets[g_ButtonSet][b].shape); }

int Buttons_Rows(int set, int b, u8 px[11][16]) {
    u8 shape = sets[set][b].shape;
    const char* t = sets[set][b].text;
    const char* g;
    int w = Width(shape), x, y, x0;
    memset(px, 0, 11 * 16);
    for (y = 0; y < 11; y++) {
        for (x = 0; x < w; x++) {
            px[y][shape == TAB_R ? w - 1 - x : x] =
                art[shape_art[shape]][y][x] == '#';
        }
    }
    if (shape == DISC && (g = Find(big, LEN(big), *t))) {
        for (y = 0; y < 7; y++) {
            for (x = 0; x < 5; x++) {
                px[2 + y][3 + x] &= g[y * 5 + x] != '#';
            }
        }
    }
    for (x0 = (w - (int)strlen(t) * 4 + 1) / 2; shape != DISC && *t;
         t++, x0 += 4) {
        g = Find(small, LEN(small), *t);
        for (y = 0; g && y < 5; y++) {
            for (x = 0; x < 3; x++) {
                px[3 + y][x0 + x] &= g[y * 3 + x] != '#';
            }
        }
    }
    return w;
}

// Every mark of every set into the pause menu atlas, 4 bits a texel (ink:
// the white of the stock arrows): mark k = set * BUTTON_MARKS + b is the 16x11
// box at u (k & 7) * 16, v (k >> 3) * 11 of dst, stride halfwords a row.
void Buttons_Paint(u16* dst, int stride, int ink) {
    u8 px[11][16];
    int k, x, y;
    for (k = 0; k < BUTTON_SETS * BUTTON_MARKS; k++) {
        Buttons_Rows(k / BUTTON_MARKS, k % BUTTON_MARKS, px);
        for (y = 0; y < 11; y++) {
            for (x = 0; x < 16; x++) {
                if (px[y][x]) {
                    dst[((k >> 3) * 11 + y) * stride + (k & 7) * 4 + x / 4] |=
                        ink << (x & 3) * 4;
                }
            }
        }
    }
}
