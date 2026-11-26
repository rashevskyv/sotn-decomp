// SPDX-License-Identifier: AGPL-3.0-or-later
// libnx calls for switch.c. Kept apart because <switch.h> and <game.h>
// define the same type names (see switch_crash.c).
#include <switch.h>
#include <SDL3/SDL_gamepad.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The touch screen, read directly: the SDL-switch touch events are not used.
// Fills up to max contacts in 1280x720 screen pixels, returns the count.
int SwitchNx_Touches(float* x, float* y, int max) {
    HidTouchScreenState st;
    int i, n;
    if (!hidGetTouchScreenStates(&st, 1)) {
        return 0;
    }
    n = st.count < max ? st.count : max;
    for (i = 0; i < n; i++) {
        x[i] = st.touches[i].x;
        y[i] = st.touches[i].y;
    }
    return n;
}

// Read handheld and numbered pads separately. SDL's first Switch slot
// combines handheld and No. 1 but uses No. 1's button layout for both.
unsigned SwitchNx_Pad(int sticks[2][2]) {
    static const HidNpadButton standard[] = {
        [SDL_GAMEPAD_BUTTON_EAST] = HidNpadButton_A,
        [SDL_GAMEPAD_BUTTON_SOUTH] = HidNpadButton_B,
        [SDL_GAMEPAD_BUTTON_NORTH] = HidNpadButton_X,
        [SDL_GAMEPAD_BUTTON_WEST] = HidNpadButton_Y,
        [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = HidNpadButton_L,
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = HidNpadButton_R,
        [SDL_GAMEPAD_BUTTON_LEFT_STICK] = HidNpadButton_StickL,
        [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = HidNpadButton_StickR,
        [SDL_GAMEPAD_BUTTON_START] = HidNpadButton_Plus,
        [SDL_GAMEPAD_BUTTON_BACK] = HidNpadButton_Minus,
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = HidNpadButton_Up,
        [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = HidNpadButton_Down,
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = HidNpadButton_Left,
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = HidNpadButton_Right,
        // Psyz uses the two codes after the SDL buttons for L2 and R2.
        [SDL_GAMEPAD_BUTTON_COUNT] = HidNpadButton_ZL,
        [SDL_GAMEPAD_BUTTON_COUNT + 1] = HidNpadButton_ZR,
    };
    static const HidNpadButton left[] = {
        [SDL_GAMEPAD_BUTTON_EAST] = HidNpadButton_Down,
        [SDL_GAMEPAD_BUTTON_SOUTH] = HidNpadButton_Left,
        [SDL_GAMEPAD_BUTTON_NORTH] = HidNpadButton_Right,
        [SDL_GAMEPAD_BUTTON_WEST] = HidNpadButton_Up,
        [SDL_GAMEPAD_BUTTON_START] = HidNpadButton_StickL,
        [SDL_GAMEPAD_BUTTON_BACK] = HidNpadButton_Minus,
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = HidNpadButton_StickLRight,
        [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = HidNpadButton_StickLLeft,
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = HidNpadButton_StickLUp,
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = HidNpadButton_StickLDown,
        [SDL_GAMEPAD_BUTTON_COUNT] = HidNpadButton_LeftSL,
        [SDL_GAMEPAD_BUTTON_COUNT + 1] = HidNpadButton_LeftSR,
    };
    static const HidNpadButton right[] = {
        [SDL_GAMEPAD_BUTTON_EAST] = HidNpadButton_X,
        [SDL_GAMEPAD_BUTTON_SOUTH] = HidNpadButton_A,
        [SDL_GAMEPAD_BUTTON_NORTH] = HidNpadButton_Y,
        [SDL_GAMEPAD_BUTTON_WEST] = HidNpadButton_B,
        [SDL_GAMEPAD_BUTTON_START] = HidNpadButton_StickR,
        [SDL_GAMEPAD_BUTTON_BACK] = HidNpadButton_Plus,
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = HidNpadButton_StickRLeft,
        [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = HidNpadButton_StickRRight,
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = HidNpadButton_StickRDown,
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = HidNpadButton_StickRUp,
        [SDL_GAMEPAD_BUTTON_COUNT] = HidNpadButton_RightSL,
        [SDL_GAMEPAD_BUTTON_COUNT + 1] = HidNpadButton_RightSR,
    };
    _Static_assert(SDL_GAMEPAD_BUTTON_COUNT + 2 <= 32, "gamepad button mask");
    enum { PAD_SLOTS = 1 + HidNpadIdType_No8 - HidNpadIdType_No1 + 1 };
    static struct {
        PadState pad;
        u32 style;
        unsigned previous;
        unsigned blocked;
    } slots[PAD_SLOTS];
    static int initialized, active = -1, logged_handheld;
    if (!initialized) {
        for (int i = 0; i < PAD_SLOTS; i++) {
            HidNpadIdType id = i == 0 ? HidNpadIdType_Handheld
                                     : HidNpadIdType_No1 + i - 1;
            padInitialize(&slots[i].pad, id);
        }
        initialized = 1;
    }

    int next = -1;
    unsigned buttons[PAD_SLOTS] = {0};
    int strength[2] = {0};
    memset(sticks, 0, sizeof(int[2][2]));
    for (int i = 0; i < PAD_SLOTS; i++) {
        PadState* pad = &slots[i].pad;
        padUpdate(pad);
        if (!padIsConnected(pad)) {
            slots[i].style = 0;
            if (active == i) {
                active = -1;
            }
            continue;
        }

        u32 style = padGetStyleSet(pad);
        const HidNpadButton* mapping = standard;
        const char* name = i == 0 ? "handheld Joy-Con" : "controller";
        if (style & HidNpadStyleTag_NpadJoyLeft) {
            mapping = left;
            name = "Joy-Con (L)";
        } else if (style & HidNpadStyleTag_NpadJoyRight) {
            mapping = right;
            name = "Joy-Con (R)";
        } else if (style & HidNpadStyleTag_NpadJoyDual) {
            name = "Joy-Con pair";
        } else if (style & HidNpadStyleTag_NpadFullKey) {
            name = "Pro/full-size controller";
        }

        unsigned mask = 0;
        u64 raw = padGetButtons(pad);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT + 2; b++) {
            if (raw & mapping[b]) {
                mask |= 1u << b;
            }
        }
        if (style != slots[i].style || mask != slots[i].previous) {
            fprintf(stderr, "[pad] %s slot %d mask 0x%08x\n", name, i, mask);
        }
        if (style != slots[i].style) {
            slots[i].style = style;
            slots[i].blocked = mask; // Ignore startup holds until they release.
        }
        slots[i].blocked &= mask;
        buttons[i] = mask & ~slots[i].blocked;
        if (next < 0 && (buttons[i] & ~slots[i].previous)) {
            next = i;
            if (i != active) {
                // A handover must not turn an old hold into a new tap.
                slots[i].blocked |= mask & slots[i].previous;
                buttons[i] &= ~slots[i].blocked;
            }
        }
        slots[i].previous = mask;

        // Single sideways Joy-Cons already expose their stick as D-pad bits.
        if (mapping == standard) {
            for (int j = 0; j < 2; j++) {
                HidAnalogStickState stick = padGetStickPos(pad, j);
                int total = abs(stick.x) + abs(stick.y);
                if (total > strength[j]) {
                    strength[j] = total;
                    sticks[j][0] = stick.x;
                    sticks[j][1] = stick.y == -32768 ? 32767 : -stick.y;
                }
            }
        }
    }
    if (next >= 0) {
        active = next;
    }
    if (active == 0 && !logged_handheld) {
        fprintf(stderr, "[pad] Switch handheld input active\n");
        logged_handheld = 1;
    }
    return active >= 0 ? buttons[active] : 0;
}

// Restart: asks the loader to start this NRO again with the same arguments
// once it ends (argv[0] is its path). 0 on success; nonzero when the loader
// cannot do it (the NRO was not started by hbloader).
int SwitchNx_Relaunch(int argc, char** argv) {
    char args[1024];
    size_t n = 0;
    int i;
    if (argc < 1 || !envHasNextLoad()) {
        return -1;
    }
    for (i = 0; i < argc && n < sizeof(args); i++) {
        n += snprintf(args + n, sizeof(args) - n, "%s\"%s\"", i ? " " : "",
                      argv[i]);
    }
    if (n >= sizeof(args)) {
        return -1;
    }
    return R_FAILED(envSetNextLoad(argv[0], args)) ? -1 : 0;
}

// The RomFS of the NRO (the HD picture packs): romfs:/ . 0 on success.
int SwitchNx_RomfsInit(void) {
    return (int)romfsInit();
}

// BSD sockets for the editor server: once, at the first switch on (sotn.ini
// editor = 1 or the port menu). Returns 0 on success.
int SwitchNx_NetInit(void) {
    static int done;
    Result rc;
    if (done) {
        return 0;
    }
    rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        fprintf(stderr, "[editor] socketInitializeDefault failed: 0x%x\n", rc);
        return -1;
    }
    done = 1;
    return 0;
}

// The console's IP address (nifm through gethostid). Returns -1 if there
// is no network: gethostid gives 127.0.0.1 then.
int SwitchNx_HostIp(char* ip, int size) {
    struct in_addr a;
    a.s_addr = (in_addr_t)gethostid();
    if (a.s_addr == 0 || a.s_addr == htonl(INADDR_LOOPBACK) ||
        a.s_addr == INADDR_LOOPBACK) {
        return -1;
    }
    snprintf(ip, size, "%s", inet_ntoa(a));
    return 0;
}
