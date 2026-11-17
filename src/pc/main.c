// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include "pc.h"
#include "spawn_point.h"
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* allowed_players[] = {"alu", "ric", "mar"};
static const char* allowed_tests[] = {"sndlib"};
#define PARSE_PARAM(param, allowed) parseStrParam(param, allowed, LEN(allowed))
static int parseIntParam(const char* param) {
    char* end;
    long i = strtol(param, &end, 10);
    if (end != param && *end == '\0' && i != LONG_MIN && i != LONG_MAX &&
        i >= 0) {
        return (int)i;
    }
    return -1;
}
static int parseStrParam(
    const char* param, const char* allowedValues[], int n) {
    long i;
    if (isdigit(param[0])) {
        i = strtol(param, NULL, 10);
        if (i != LONG_MIN && i != LONG_MAX && i >= 0) {
            return (int)i;
        }
    } else {
        for (i = 0; i < n; i++) {
            if (allowedValues[i] == NULL || allowedValues[i][0] == '\0') {
                continue;
            }
            if (!strcmp(param, allowedValues[i])) {
                return i;
            }
        }
    }
    return -1;
}
static void printHelp(void) {
    printf("Usage: ./sotn [OPTIONS]\n");
    printf("Options:\n");
    printf("  --disk <path>      file name of the second track\n");
    printf("  --stage <stage>    stage name or ID (e.g., nz0)\n");
    printf("  --player <name>    player name or ID (e.g. ric)\n");
    printf("  --demo <number>    play a specific Tactics demo\n");
    printf("  --scale <number>   game internal resolution integer scale "
           "(default 1)\n");
    printf("  --test <mode>      run automated tests\n");
    printf("         sndlib      test sound library\n");
    printf("  --record <path>    record controller input to a file\n");
    printf("  --replay <path>    replay controller input from a file\n");
    printf("  --spawn-point <name>  start at a named spawn point\n");
    printf("  --replay-and-exit  quit automatically once the replay or demo "
           "ends\n");
    printf("  --replay-fast      disable frame limit during replay or demo\n");
    printf("  --editor           serve the tile editor page on port 8080\n");
    printf("  --wide <scale>     16:9 wide view at this integer scale "
           "(default 3, 0 = off)\n");
    printf("  --ini <file>       settings file as sotn.ini on the Switch\n");
    printf("  --help             show this help message\n");
}
static void printAllowedParams(const char* allowedValues[], int n) {
    int i;
    bool first = true;

    printf("allowed params are: ");
    for (i = 0; i < n; i++) {
        if (allowedValues[i] == NULL || allowedValues[i][0] == '\0') {
            continue;
        }
        printf("%s%s", first ? "" : ", ", allowedValues[i]);
        first = false;
    }
    printf("\n");
}
static bool wide_set; // --wide or --ini picked the view
static bool parseArgs(
    struct InitGameParams* outParams, int argc, char* argv[]) {
    assert(!!outParams);
    outParams->diskPath = NULL;
    outParams->testMode = NO_TEST;
    outParams->stage = -1;
    outParams->stageName = NULL;
    outParams->stageOwnsAssets = false;
    outParams->player = -1;
    outParams->demo = -1;
    outParams->scale = 1;
    outParams->recordPath = NULL;
    outParams->replayPath = NULL;
    outParams->spawnPoint = NULL;
    outParams->exitAfterReplay = false;
    outParams->replayBoundlessFramerate = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printHelp();
            exit(0);
        }
        if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
            outParams->diskPath = argv[++i];
        } else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) {
            const char* stage = argv[++i];
            outParams->stage = parseIntParam(stage);
            if (outParams->stage < 0) {
                outParams->stageName = stage;
                static const int saturnStages[] = {
                    STAGE_ID_SATURN_15,
                    STAGE_ID_SATURN_16,
                    STAGE_ID_REVERSE_SATURN_15,
                    STAGE_ID_REVERSE_SATURN_16,
                };
                for (int j = 0; j < LEN(saturnStages); j++) {
                    if (!strcmp(stage, SaturnStageOverlay(saturnStages[j]))) {
                        outParams->stage = saturnStages[j];
                        outParams->stageOwnsAssets = true;
                    }
                }
            }
        } else if (strcmp(argv[i], "--player") == 0 && i + 1 < argc) {
            outParams->player = PARSE_PARAM(argv[++i], allowed_players);
            if (outParams->player < 0) {
                printf("player '%s' is invalid or not recognized\n", argv[i]);
                printAllowedParams(allowed_players, LEN(allowed_players));
                return false;
            }
        } else if (strcmp(argv[i], "--demo") == 0 && i + 1 < argc) {
            outParams->demo = parseIntParam(argv[++i]);
            if (outParams->demo < 0) {
                printf("invalid demo index %s\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            outParams->scale = parseIntParam(argv[++i]);
            if (outParams->scale < 1 || outParams->scale > 8) {
                printf("invalid resolution scale %s\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            outParams->testMode = PARSE_PARAM(argv[++i], allowed_tests) + 1;
            if (outParams->testMode < 0) {
                printf("test '%s' is invalid or not recognized\n", argv[i]);
                printAllowedParams(allowed_tests, LEN(allowed_tests));
                return false;
            }
        } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            outParams->recordPath = argv[++i];
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            outParams->replayPath = argv[++i];
        } else if (strcmp(argv[i], "--spawn-point") == 0 && i + 1 < argc) {
            outParams->spawnPoint = argv[++i];
            if (SpawnPoint_StageForName(outParams->spawnPoint) < 0) {
                printf(
                    "spawn point '%s' is invalid or not recognized\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "--replay-and-exit") == 0) {
            outParams->exitAfterReplay = true;
        } else if (strcmp(argv[i], "--replay-fast") == 0) {
            outParams->replayBoundlessFramerate = true;
        } else if (strcmp(argv[i], "--editor") == 0) {
            g_EditorOn = 1;
        } else if (strcmp(argv[i], "--wide") == 0 && i + 1 < argc) {
            Wide_Config(atoi(argv[++i]));
            wide_set = true;
        } else if (strcmp(argv[i], "--ini") == 0 && i + 1 < argc) {
            Wide_LoadIni(argv[++i]); // the Switch sotn.ini, for tests
            wide_set = true;
        } else {
            printf("argument %s not recognized", argv[i]);
            return false;
        }
    }
    return true;
}

// The button names of the pause menus (Button config, the ATT and DEF
// marks of Equip) as the Switch shows them: the face buttons by position
// (sdl3_common.h: Square = west = Y, Circle = east = A, Cross = south = B,
// Triangle = north = X), R2 = ZR, L1 = L, R1 = R, L2 = ZL. On the Switch
// always; on PC with SOTN_SWITCH_MARKS=1, for screenshots.
static void SwitchMarks(void) {
    extern u8 g_ChButtons[8], g_ChButtons2[4];
    static const char first[] = "YABXZLRZ", second[] = "R  L";
    int i;
#ifndef __SWITCH__
    const char* env = getenv("SOTN_SWITCH_MARKS");
    if (!env || strcmp(env, "1")) {
        return;
    }
#endif
    for (i = 0; i < 8; i++) {
        g_ChButtons[i] = CH(first[i]);
    }
    for (i = 0; i < 4; i++) {
        g_ChButtons2[i] = CH(second[i]);
    }
}

int Main(int argc, char* argv[]) {
    struct InitGameParams params;
#ifdef __SWITCH__
    const char* Switch_Init(void);
    const char* cue = Switch_Init();
#elif defined(_WIN32)
    void Win_Init(int* argc, char*** argv);
    Win_Init(&argc, &argv);
#endif
    fprintf(stderr, "[boot] sotn-native %s\n", SOTN_VERSION);
    if (!parseArgs(&params, argc, argv)) {
        printHelp();
        return -1;
    }
#ifdef __SWITCH__
    if (!params.diskPath) {
        params.diskPath = cue;
    }
    if (!params.spawnPoint && g_IniSpawn[0]) {
        bool ok = SpawnPoint_StageForName(g_IniSpawn) >= 0;
        fprintf(stderr, "[boot] spawn %s: %s\n", g_IniSpawn,
                ok ? "ok" : "unknown name, normal start");
        if (ok) {
            params.spawnPoint = g_IniSpawn;
        }
    }
#endif
    // 16:9 at scale 3 when no option and no sotn.ini set the view; the demo,
    // replay and test runs keep the stock view their inputs were made with
    int Wide_GetScale(void);
    if (!wide_set && !Wide_GetScale() && params.demo < 0 &&
        !params.replayPath && params.testMode == NO_TEST) {
        Wide_Config(3);
    }
    if (params.stage < 0 && params.spawnPoint != NULL) {
        params.stage = SpawnPoint_StageForName(params.spawnPoint);
    }
    Psyz_VideoSetInternalResolution(params.scale);
    if (!InitGame(&params)) {
        return -1;
    }
    Lang_Init();
    SwitchMarks();
    MainGame();
    ResetGame();
    if (params.replayPath && Replay_DidDrift()) {
        return -1;
    }
    if (Replay_DemoAborted()) {
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) { return Main(argc, argv); }
