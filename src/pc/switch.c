// SPDX-License-Identifier: AGPL-3.0-or-later
// Nintendo Switch glue: boot log, optional CD audio, rumble on damage.
#include <game.h>
#include <SDL3/SDL.h>
#include <psyz/video.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>


static bool FileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Called first in main(). Returns the cue sheet for CD audio, or NULL.
const char* Switch_Init(void) {
    mkdir(SWITCH_DIR, 0755);
    chdir(SWITCH_DIR); // memory card saves go to bu00/ and bu10/ here
    // one file per stream: the Switch FS refuses a second open for write
    freopen(SWITCH_DIR "/sotn.out.log", "w", stdout);
    // keep the log of the previous run: a fetch after a restart still has it
    remove(SWITCH_DIR "/sotn.prev.log");
    rename(SWITCH_DIR "/sotn.log", SWITCH_DIR "/sotn.prev.log");
    freopen(SWITCH_DIR "/sotn.log", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    Wide_LoadIni(SWITCH_DIR "/sotn.ini");
    {
        int SwitchNx_RomfsInit(void);
        int rc = SwitchNx_RomfsInit();
        fprintf(stderr, "[boot] romfs (HD pictures): %s (0x%x)\n",
                rc == 0 ? "ok" : "NOT MOUNTED, no HD pictures", rc);
    }
    fprintf(stderr, "[boot] data dir %s: %s\n", SOTN_DATA_DIR,
            FileExists(SOTN_DATA_DIR "/DRA.BIN") ? "ok" : "DRA.BIN MISSING");
    // XA_STR1 is a raw-sector slice of the disc; with SetCdPos(0) a game
    // sector is a sector inside it. Own cue name, rewritten every boot, so an
    // old full-disc sotn.cue can not take its place.
    const char* cue = SWITCH_DIR "/sotn-xa.cue";
    bool ok = FileExists(SOTN_DATA_DIR "/SD/XA_STR1");
    FILE* f = ok ? fopen(cue, "w") : NULL;
    if (f) {
        fputs("FILE \"data/SD/XA_STR1\" BINARY\n"
              "  TRACK 01 MODE2/2352\n"
              "    INDEX 01 00:00:00\n",
              f);
        ok = fclose(f) == 0;
    } else {
        ok = false;
    }
    fprintf(stderr, "[boot] cd audio %s -> data/SD/XA_STR1: %s\n", cue,
            ok ? "found" : "MISSING, music off");
    return ok ? cue : NULL;
}

// Called on every vsync. A short rumble when the player loses HP.
void Switch_OnFrame(void) {
    static s32 last_hp = -1;
    static bool first_frame = true;
    if (first_frame) {
        first_frame = false;
        fprintf(stderr, "[boot] first frame\n");
    }
    s32 hp = g_Status.hp;
    if (last_hp > 0 && hp < last_hp) {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        for (int i = 0; ids && i < count; i++) {
            SDL_Joystick* joy = SDL_GetJoystickFromID(ids[i]);
            if (joy) {
                SDL_RumbleJoystick(joy, 0xA000, 0xC000, 150);
            }
        }
        SDL_free(ids);
    }
    last_hp = hp;

    // touch for the port menu map (switch_nx.c reads libnx)
    int SwitchNx_Touches(float* x, float* y, int max);
    float tx[2], ty[2];
    extern s32 g_WarpMapCursor[3]; // the warp map takes touch too
    int x[2], y[2], n = (PortMenu_IsOpen() || g_WarpMapCursor[0])
                            ? SwitchNx_Touches(tx, ty, 2) : 0;
    PsyzSize d = Psyz_VideoGetDisplaySize();
    for (int i = 0; i < n; i++) {
        // touch is 1280x720 screen pixels; the display may differ
        Wide_ScreenToFrame(tx[i] * d.w / 1280, ty[i] * d.h / 720, &x[i], &y[i]);
    }
    PortMenu_Touch(n, x, y);
}
