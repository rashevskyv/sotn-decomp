// SPDX-License-Identifier: AGPL-3.0-or-later
// libnx calls for switch.c. Kept apart because <switch.h> and <game.h>
// define the same type names (see switch_crash.c).
#include <switch.h>
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
