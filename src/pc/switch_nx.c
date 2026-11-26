// SPDX-License-Identifier: AGPL-3.0-or-later
// libnx calls for switch.c. Kept apart because <switch.h> and <game.h>
// define the same type names (see switch_crash.c).
#include <switch.h>
#include <SDL3/SDL_gamepad.h>
#include <arpa/inet.h>
#include <stdio.h>
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

// Read the built-in pair independently of the SDL joystick slot order.
unsigned SwitchNx_HandheldPad(int sticks[2][2]) {
    static const HidNpadButton mapping[] = {
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
    _Static_assert(SDL_GAMEPAD_BUTTON_COUNT + 2 <= 32, "handheld button mask");
    unsigned bits = 0;
    int i;

    HidNpadHandheldState state;
    if (!hidGetNpadStatesHandheld(HidNpadIdType_Handheld, &state, 1) ||
        !(state.attributes & HidNpadAttribute_IsConnected)) {
        sticks[0][0] = sticks[0][1] = sticks[1][0] = sticks[1][1] = 0;
        return 0;
    }
    sticks[0][0] = state.analog_stick_l.x;
    sticks[0][1] = state.analog_stick_l.y == -32768 ? 32767 : -state.analog_stick_l.y;
    sticks[1][0] = state.analog_stick_r.x;
    sticks[1][1] = state.analog_stick_r.y == -32768 ? 32767 : -state.analog_stick_r.y;
    for (i = 0; i < (int)(sizeof(mapping) / sizeof(mapping[0])); i++) {
        if (state.buttons & mapping[i]) {
            bits |= 1u << i;
        }
    }
    return bits;
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
