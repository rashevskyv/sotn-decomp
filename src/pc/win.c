// SPDX-License-Identifier: AGPL-3.0-or-later
// Native Windows glue: runs from the folder of sotn.exe, log file, default
// cue sheet and sotn.ini, rumble on damage (as src/pc/switch.c).
#include <game.h>
#include <SDL3/SDL.h>
#include <direct.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static bool FileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void LogToStderr(void* userdata, int category, SDL_LogPriority priority,
                        const char* message) {
    (void)userdata;
    (void)category;
    (void)priority;
    fprintf(stderr, "%s\n", message);
}

// Called first in main(). Puts --disk sotn-xa.cue --ini sotn.ini before
// the user's arguments (--disk only when the cue is there), as run.sh does on
// Linux.
void Win_Init(int* argc, char*** argv) {
    _fmode = _O_BINARY; // fopen without b must not touch the bytes
    const char* base = SDL_GetBasePath(); // the folder of sotn.exe
    if (base) {
        _chdir(base); // data/, saves bu00/ and bu10/, sotn.ini live here
    }
    // a GUI program has no console: the log goes to sotn.log beside it
    remove("sotn.prev.log");
    // after a port menu RESTART the old process still holds sotn.log (and the
    // editor port) until it ends: wait for the rename, at most 6 s
    for (int t = 0; rename("sotn.log", "sotn.prev.log") != 0 &&
                    getenv("SOTN_RESTART_PID") && FileExists("sotn.log") && t < 60;
         t++) {
        SDL_Delay(100);
    }
    freopen("sotn.log", "w", stderr);
    freopen("sotn.out.log", "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    SDL_SetLogOutputFunction(LogToStderr, NULL); // psyz logs via SDL_Log
    static char* args[64];
    int n = 0;
    args[n++] = (*argv)[0];
    if (FileExists("sotn-xa.cue")) {
        args[n++] = "--disk";
        args[n++] = "sotn-xa.cue";
    }
    // always: Wide_LoadIni writes the defaults when sotn.ini is missing
    args[n++] = "--ini";
    args[n++] = "sotn.ini";
    for (int i = 1; i < *argc && n < 63; i++) {
        args[n++] = (*argv)[i];
    }
    args[n] = NULL;
    *argc = n;
    *argv = args;
}

// Called on every vsync. A short rumble when the player loses HP.
// ponytail: copy of the block in switch.c, which is not built here.
void Win_OnFrame(void) {
    static s32 last_hp = -1;
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
}
