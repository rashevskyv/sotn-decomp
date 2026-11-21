// SPDX-License-Identifier: AGPL-3.0-or-later
// Cheats for tests. The port menu (L3+R3, CHEATS) switches them; sotn.ini
// keeps them (cheat_* = 0|1). They act only in play, outside menus and
// loads. The item cheat writes the inventory, so a later save keeps it.
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include "pc.h"

extern RelicDesc g_RelicDefs[30]; // src/dra/dra.h
void CheckCollision(s32 x, s32 y, Collider* res, s32 unk); // src/dra/dra.h

struct Cheats g_Cheat;
int g_CheatMistRequest;
int g_EditorOn;
int g_CheatSaveChar = -1;
static int s_WarpArriving;
static int s_MusicRestart;
static void RematchWatch(void);
// the boss list rematch (Cheat_WarpBoss)
static struct {
    int boss, arrived, room, record;
    u8 old[2], seen;
} s_Rematch = {-1};

static int Solid(int x, int y) {
    Collider c;
    CheckCollision(x, y, &c, 0);
    return c.effects & EFFECT_SOLID;
}

// A teleport puts the player at a cell centre, which can be inside a wall.
// Alucard arrives in mist; anyone else moves to the nearest spot, up first,
// where the body (centre and 16 px above it) is free.
static void LandOutsideWalls(void) {
    int x = PLAYER.posX.i.hi, y = PLAYER.posY.i.hi, dy, top, bottom;
    // outside the room CheckCollision finds no wall: stay inside it
    top = -g_Tilemap.scrollY.i.hi + 16;
    bottom = (g_Tilemap.vSize << 8) - g_Tilemap.scrollY.i.hi - 16;
    for (dy = 0; dy <= 256; dy += 8) {
        if (y - dy - 16 >= top && !Solid(x, y - dy) && !Solid(x, y - dy - 16)) {
            PLAYER.posY.i.hi = y - dy;
            break;
        }
        if (y + dy < bottom && !Solid(x, y + dy) && !Solid(x, y + dy - 16)) {
            PLAYER.posY.i.hi = y + dy;
            break;
        }
    }
    fprintf(stderr, "[cheat] warp arrival: moved %d px out of walls\n",
            PLAYER.posY.i.hi - y);
}

// Mode A: select.c (new game) and save_mgr.h (save load) call it.
int Cheat_Character(int game) {
    switch (g_Cheat.character) {
    case CHEAT_CHAR_ALUCARD:
        return PLAYER_ALUCARD;
    case CHEAT_CHAR_RICHTER:
        return PLAYER_RICHTER;
#ifdef USE_PLAYER_MARIA
    case CHEAT_CHAR_MARIA_PSP:
        return PLAYER_MARIA;
#endif
    }
    return game;
}

static int CharacterBuilt(int v) {
#ifdef USE_PLAYER_MARIA
    if (v == CHEAT_CHAR_MARIA_PSP) {
        return 1;
    }
#endif
    return v >= CHEAT_CHAR_GAME && v <= CHEAT_CHAR_RICHTER;
}

int Cheat_CharacterNext(int v, int dir) {
    if (!dir) {
        return CharacterBuilt(v) ? v : CHEAT_CHAR_GAME;
    }
    do {
        v = (v + dir + CHEAT_CHAR_N) % CHEAT_CHAR_N;
    } while (!CharacterBuilt(v));
    return v;
}

int Cheat_TakeMusicRestart(void) {
    int r = s_MusicRestart;
    s_MusicRestart = 0;
    return r;
}

static void FillInventory(void) {
    s32 i;
    // the game's own demo inventory (InitStatsAndGear, 5F60C.c), but a relic
    // that is already found keeps its on/off state
    for (i = 0; i < LEN(g_Status.relics); i++) {
        if (!(g_Status.relics[i] & RELIC_FLAG_FOUND)) {
            g_Status.relics[i] = RELIC_FLAG_FOUND;
            if (g_RelicDefs[i].unk0C == 0) {
                g_Status.relics[i] |= RELIC_FLAG_ACTIVE;
            }
        }
    }
    for (i = 0; i < NUM_HAND_ORDER; i++) { // the PS1's and the Saturn's
        u8 id = g_Status.equipHandOrder[i];
        if (g_Status.equipHandCount[id] < 50) {
            g_Status.equipHandCount[id] = 50;
        }
    }
    for (i = 0; i < 90; i++) {
        if (g_Status.equipBodyCount[i] < 1) {
            g_Status.equipBodyCount[i] = 1;
        }
    }
    fprintf(stderr, "[cheat] inventory filled\n");
}

// 100% MAP: the explored bits of every cell the game's map (F_MAP.BIN, at
// CASTLE_MAP_PTR) draws, in both castles, as walking there would set them
// (func_800F2014, 5087C.c). The inverted half holds the cell turned by 180
// degrees. g_RoomCount counts each new bit: 945 cells a castle, 1890 in all
// (the status page shows 200.6 %). A save keeps the map.
void func_800F2120(void); // 5087C.c: redraw the map of this castle
static void RevealMap(void) {
    const u8* bp = (const u8*)CASTLE_MAP_PTR;
    int x, y, half, cx, cy, bit, idx, n = 0;
    u8 t;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            // the centre texel (2, 2) of the cell's 4x4 texels
            t = bp[(y * 4 + 2) * WIDTH_OF_MAP_ROW_IN_BYTES + x * 2 + 1] & 15;
            for (half = 0; t && half < 2; half++) {
                cx = half ? 63 - x : x;
                cy = half ? 63 - y : y;
                bit = 1 << ((3 - (cx & 3)) * 2);
                idx = (cx >> 2) + cy * 16 + half * 0x400;
                if (!(g_CastleMap[idx] & bit)) {
                    g_CastleMap[idx] |= bit;
                    g_RoomCount++;
                    n++;
                }
            }
        }
    }
    if (n) {
        func_800F2120();
    }
    fprintf(stderr, "[cheat] map: %d cells revealed, %d rooms\n", n,
            (int)g_RoomCount);
}

// Called on every vsync.
void Cheat_OnFrame(void) {
    static int filled; // once per load of a save or a new game
    static int mapped; // the same for 100% MAP
    static int mistWait;
    static int lastItems, lastMap;

    if (g_Cheat.items != lastItems) {
        lastItems = g_Cheat.items;
        filled = 0; // switched on again in the menu: fill again
    }
    if (g_Cheat.map != lastMap) {
        lastMap = g_Cheat.map;
        mapped = 0;
    }

    if (g_GameState != Game_Play) {
        filled = mapped = 0;
        g_CheatMistRequest = 0;
        if (g_GameState != Game_NowLoading) {
            // game over or title: the next load brings its own record and
            // flags, a restore would write the old save's into it
            s_Rematch.boss = -1;
        }
        return;
    }
    if (g_GameStep != Play_Default || g_GameEngineStep != Engine_Normal) {
        return;
    }
    RematchWatch();
    if (g_Cheat.map && !mapped && g_StageId != STAGE_ST0) {
        mapped = 1;
        RevealMap();
    }
    if (g_Cheat.refill) {
        g_Status.hp = g_Status.hpMax;
        g_Status.mp = g_Status.mpMax;
        g_Status.hearts = g_Status.heartsMax;
    }
    // the player update skips a hit while this timer runs; Richter and Maria
    // have their PL_T_INVINCIBLE at the same index 14 (ric_shared.h,
    // maria_common.h)
    if (g_Cheat.invincible &&
        g_Player.timers[ALU_T_INVINCIBLE_CONSUMABLES] < 4) {
        g_Player.timers[ALU_T_INVINCIBLE_CONSUMABLES] = 4;
    }
    if (s_WarpArriving && !g_WarpTarget.active) {
        s_WarpArriving = 0;
        if (g_PlayableCharacter == PLAYER_ALUCARD) {
            g_CheatMistRequest = 1; // the player update turns into mist
        } else {
            LandOutsideWalls(); // no mist: leave the wall
        }
    }
    if (g_PlayableCharacter != PLAYER_ALUCARD) {
        if (g_Cheat.items && !filled) {
            filled = 1;
            FillInventory(); // kept for the owner of the save
            if (!g_Status.subWeapon) { // as InitStatsAndGear; Maria has 4
                g_Status.subWeapon =
                    (rand() % (g_PlayableCharacter == PLAYER_MARIA ? 4 : 9)) + 1;
            }
        }
        return; // no mist form: MIST WALLS does nothing
    }
    if (g_Cheat.walls) {
        // mist needs Form of Mist; Power of Mist makes it last 100000
        // frames; full MP: no drain
        g_Status.relics[RELIC_FORM_OF_MIST] |=
            RELIC_FLAG_FOUND | RELIC_FLAG_ACTIVE;
        g_Status.relics[RELIC_POWER_OF_MIST] |=
            RELIC_FLAG_FOUND | RELIC_FLAG_ACTIVE;
        g_Status.mp = g_Status.mpMax;
    }
    if (g_Cheat.items && !filled) {
        filled = 1;
        FillInventory();
    }
    // the player update takes the request (692E8.c); give up after 5 s
    if (!g_CheatMistRequest) {
        mistWait = 0;
    } else if (++mistWait > 300) {
        g_CheatMistRequest = 0;
        fprintf(stderr, "[cheat] mist on arrival: not taken\n");
    }
}

// A new game sets ST0 for Alucard (select.c); with the cheat the stage load
// after the intro video takes NO3, as the end of the prologue does
// (prologue_scroll.c).
void Cheat_SkipPrologue(void) {
    if (g_Cheat.skipPrologue && g_StageId == STAGE_ST0 &&
        g_DemoMode == Demo_None && g_PlayableCharacter == PLAYER_ALUCARD) {
        g_StageId = STAGE_NO3;
        fprintf(stderr, "[cheat] prologue skipped: NO3\n");
    }
}

// Teleport. The door path of the game does the load (5087C.c); this file
// gives it the stage, the room and the player position.
struct WarpTarget g_WarpTarget;

const char* Castle_StageName(int stage) {
    int i;
    for (i = 0; i < g_CastleStageCount; i++) {
        if (g_CastleStages[i].id == stage) {
            return g_CastleStages[i].name;
        }
    }
    return "?";
}

const char* Castle_ZoneName(int stage) {
    static const struct {
        u8 id;
        const char* name;
    } zones[] = {
        {STAGE_NO0, "Marble Gallery"},
        {STAGE_NO1, "Outer Wall"},
        {STAGE_LIB, "Long Library"},
        {STAGE_CAT, "Catacombs"},
        {STAGE_NO2, "Olrox's Quarters"},
        {STAGE_CHI, "Abandoned Mine"},
        {STAGE_DAI, "Royal Chapel"},
        {STAGE_NP3, "Castle Entrance"},
        {STAGE_CEN, "Center Cube"},
        {STAGE_NO4, "Underground Caverns"},
        {STAGE_ARE, "Colosseum"},
        {STAGE_TOP, "Castle Keep"},
        {STAGE_NZ0, "Alchemy Laboratory"},
        {STAGE_NZ1, "Clock Tower"},
        {STAGE_WRP, "Warp Room"},
        {STAGE_NO3, "Castle Entrance"},
        {STAGE_NO0 | STAGE_INVERTEDCASTLE_FLAG, "Black Marble Gallery"},
        {STAGE_NO1 | STAGE_INVERTEDCASTLE_FLAG, "Reverse Outer Wall"},
        {STAGE_LIB | STAGE_INVERTEDCASTLE_FLAG, "Forbidden Library"},
        {STAGE_CAT | STAGE_INVERTEDCASTLE_FLAG, "Floating Catacombs"},
        {STAGE_NO2 | STAGE_INVERTEDCASTLE_FLAG, "Death Wing's Lair"},
        {STAGE_CHI | STAGE_INVERTEDCASTLE_FLAG, "Cave"},
        {STAGE_DAI | STAGE_INVERTEDCASTLE_FLAG, "Anti-Chapel"},
        {STAGE_NP3 | STAGE_INVERTEDCASTLE_FLAG, "Reverse Entrance"},
        {STAGE_CEN | STAGE_INVERTEDCASTLE_FLAG, "Reverse Center Cube"},
        {STAGE_NO4 | STAGE_INVERTEDCASTLE_FLAG, "Reverse Caverns"},
        {STAGE_ARE | STAGE_INVERTEDCASTLE_FLAG, "Reverse Colosseum"},
        {STAGE_TOP | STAGE_INVERTEDCASTLE_FLAG, "Reverse Castle Keep"},
        {STAGE_NZ0 | STAGE_INVERTEDCASTLE_FLAG, "Necromancy Laboratory"},
        {STAGE_NZ1 | STAGE_INVERTEDCASTLE_FLAG, "Reverse Clock Tower"},
        {STAGE_WRP | STAGE_INVERTEDCASTLE_FLAG, "Reverse Warp Room"},
        {STAGE_ID_SATURN_15, "Underground Garden"},
        {STAGE_ID_SATURN_16, "Cursed Prison"},
        {STAGE_ID_REVERSE_SATURN_15, "Hell Garden"},
        {STAGE_ID_REVERSE_SATURN_16, "Soul Prison"},
    };
    int i;
    for (i = 0; i < LEN(zones); i++) {
        if (zones[i].id == stage) {
            return zones[i].name;
        }
    }
    return "";
}

const char* Cheat_WarpRefusal(void) {
    if (g_GameState != Game_Play || g_GameStep != Play_Default) {
        return "not in play";
    }
    if (g_GameEngineStep != Engine_Normal) {
        return "game menu, map or scene";
    }
    if (D_8003C730 != 0 || g_WarpTarget.active) {
        return "a load runs";
    }
    if (g_DemoMode != Demo_None) {
        return "demo";
    }
    if (g_CutsceneHasControl) {
        return "cutscene";
    }
    if (g_Player.status & PLAYER_STATUS_DEAD) {
        return "player is dead";
    }
    if (g_StageId == STAGE_ST0) {
        return "prologue";
    }
    return NULL;
}

static const struct CastleRoom* FindRoom(int stage, int room) {
    int i;
    for (i = 0; i < g_CastleRoomCount; i++) {
        if (g_CastleRooms[i].stage == stage && g_CastleRooms[i].room == room) {
            return &g_CastleRooms[i];
        }
    }
    return NULL;
}

// Rooms where the PC port crashes right after a warp (the warp test of
// step 13): DAI 0 Maria cutscene (not ported to PC), DAI 7 boss music
// sequence missing.
static int Crashes(int stage, int room) {
    static const u8 crash[][2] = {{STAGE_DAI, 0}, {STAGE_DAI, 7}};
    int i;
    for (i = 0; i < LEN(crash); i++) {
        if (stage == crash[i][0] && room == crash[i][1]) {
            return 1;
        }
    }
    return 0;
}

// mist 0: a boss door spot, the player arrives on foot (mist does not start
// some bosses and falls through floors)
static void StartWarp(int stage, int room, int cx, int cy, int mist) {
    g_WarpTarget = (struct WarpTarget){1, stage, room, cx, cy};
    // arrival in mist inside a wall must be safe: these stay on until the
    // game ends; not written to sotn.ini, so the next start has them off
    g_Cheat.items = 1;
    if (mist) {
        g_Cheat.refill = g_Cheat.walls = 1;
        s_WarpArriving = 1;
    }
    D_80097C98 = 0;
    D_8006C374 = 0; // 0x2C would force STAGE_TOP in Engine_Init
    g_GameStep = Play_PrepareNextStage;
    fprintf(stderr, "[cheat] warp %s room %d cell %d,%d: start\n",
            Castle_StageName(stage), room, cx, cy);
}

const char* Cheat_Warp(int stage, int room, int cx, int cy) {
    const struct CastleRoom* r = FindRoom(stage, room);
    const char* why = Cheat_WarpRefusal();

    if (!why && !r) {
        why = "unknown room";
    }
    if (!why && Crashes(stage, room)) {
        why = "this room crashes the port, pick another";
    }
    if (!why && (cx < r->left || cx > r->right || cy < r->top || cy > r->bottom)) {
        why = "cell is outside the room";
    }
    if (why) {
        fprintf(stderr, "[cheat] warp %s room %d cell %d,%d: refused, %s\n",
                Castle_StageName(stage), room, cx, cy, why);
        return why;
    }
    StartWarp(stage, room, cx, cy, 1);
    return NULL;
}

// The start of a stage (the port menu's list): its first room in the game's
// room list that does not crash, at the bottom left cell. The arrival in
// mist (or LandOutsideWalls) takes the player out of a wall. Stages whose
// first room is no start name their room here: DAI 1 is the spike
// corridor (DAI 11: the chapel stairs), NZ1 0 the Karasuman room and RNZ1 0
// the Darkwing Bat room (room 11: next to the Outer Wall).
const char* Cheat_WarpStart(int stage) {
    static const u8 start[][2] = {
        {STAGE_DAI, 11}, {STAGE_NZ1, 11}, {STAGE_RNZ1, 11}};
    int i, want = -1;
    for (i = 0; i < LEN(start); i++) {
        if (stage == start[i][0]) {
            want = start[i][1];
        }
    }
    for (i = 0; i < g_CastleRoomCount; i++) {
        const struct CastleRoom* r = &g_CastleRooms[i];
        if (r->stage == stage && !Crashes(stage, r->room) &&
            (want < 0 || r->room == want)) {
            return Cheat_Warp(stage, r->room, r->left, r->bottom);
        }
    }
    return "unknown room";
}

// The boss list of the port menu. stage, room, px, py: where the game's
// boss door puts the player (the D_800A245C row that g_RoomBossTeleports
// names, 5087C.c), or a spot inside the fight trigger of a boss in a castle
// stage; Scylla: BO3 room 0, past the Wyrm of room 1. Doppleganger10,
// Lesser Demon, Minotaurus and Olrox come when the player walks in.
// The player arrives on foot (no mist, no refill: a real fight).
// host, host_room: its castle room on the map (0xFF: none, DRE).
// event and flags: what the boss checks to stay away once beaten.
#define NO_EVENT 0xFF
#define BOSS(e0, e1, e2, st, rm, x, y, h, hr, ev, f0, f1, b)                   \
    { {e0, e1, e2}, st, rm, x, y, h, hr, ev, {f0, f1}, b }
const struct CheatBoss g_CheatBosses[] = {
    BOSS(0x0F3, 0x0FE, 0, STAGE_NZ0, 16, 672, 384, STAGE_NZ0, 16,
         TIMEATTACK_EVENT_SLOGRA_GAIBON_DEFEAT, 0, 0, 20),
    BOSS(0x0FD, 0, 0, STAGE_BO4, 0, 16, 132, STAGE_NO1, 11, NO_EVENT, 0, 0, 33),
    BOSS(0x017, 0, 0, STAGE_LIB, 0, 512, 640, STAGE_LIB, 0,
         TIMEATTACK_EVENT_LESSER_DEMON_DEFEAT, 0, 0, 79),
    BOSS(0x0CB, 0x0CE, 0, STAGE_BO2, 0, 16, 132, STAGE_ARE, 5, NO_EVENT, 0, 0,
         73),
    BOSS(0x118, 0, 0, STAGE_NZ1, 0, 128, 160, STAGE_NZ1, 0,
         TIMEATTACK_EVENT_KARASUMAN_DEFEAT, 0, 0, 86),
    BOSS(
        0x032, 0, 0, STAGE_BO0, 0, 496, 132, STAGE_NO2, 12, NO_EVENT, 0, 0, 91),
    BOSS(0x127, 0, 0, STAGE_BO1, 0, 496, 132, STAGE_CAT, 3, NO_EVENT, 0, 0, 97),
    BOSS(0x12C, 0, 0, STAGE_BO5, 0, 16, 132, STAGE_DAI, 4, NO_EVENT, 0, 0, 65),
    BOSS(0x11F, 0, 0, STAGE_BO3, 0, 24, 132, STAGE_NO4, 28,
         TIMEATTACK_EVENT_SCYLLA_DEFEAT, SCYLLA_DEFEATED, SCYLLA_WYRM_DEFEATED,
         58),
    BOSS(0x156, 0, 0, STAGE_DRE, 0, 16, 180, 0xFF, 0, NO_EVENT, 0, 0, 92),
    BOSS(0x16B, 0, 0, STAGE_BO7, 0, 16, 132, STAGE_CHI, 2, NO_EVENT, 0, 0, 89),
    BOSS(0x133, 0, 0, STAGE_BO6, 0, 240, 179, STAGE_TOP, 2, NO_EVENT,
         SHAFT_ORB_DEFEATED, 0, 139),
    BOSS(0x148, 0x14E, 0x151, STAGE_RBO0, 0, 496, 132, STAGE_RARE, 5, NO_EVENT,
         0, 0, 132),
    BOSS(0x144, 0, 0, STAGE_RBO1, 0, 1008, 388, STAGE_RNZ0, 16, NO_EVENT, 0, 0,
         135),
    BOSS(0x164, 0, 0, STAGE_RBO2, 0, 16, 132, STAGE_RCHI, 2, NO_EVENT, 0, 0,
         143),
    BOSS(0x16E, 0, 0, STAGE_RBO3, 0, 16, 132, STAGE_RDAI, 4, NO_EVENT, 0, 0,
         129),
    BOSS(0x172, 0, 0, STAGE_RBO4, 0, 16, 132, STAGE_RNO1, 11, NO_EVENT, 0, 0,
         130),
    BOSS(0x174, 0, 0, STAGE_RBO5, 0, 16, 132, STAGE_RNO4, 28, NO_EVENT, 0, 0,
         128),
    BOSS(0x10B, 0, 0, STAGE_RBO7, 0, 496, 132, STAGE_RNO2, 12, NO_EVENT, 0, 0,
         126),
    BOSS(0x0C6, 0, 0, STAGE_RBO8, 0, 496, 132, STAGE_RCAT, 3, NO_EVENT, 0, 0,
         141),
    BOSS(0x111, 0, 0, STAGE_RNZ1, 0, 128, 160, STAGE_RNZ1, 0,
         TIMEATTACK_EVENT_DARKWING_BAT_DEFEAT, 0, 0, 109),
    BOSS(0x15F, 0, 0, STAGE_RCEN, 1, 384, 440, STAGE_RCEN, 1, NO_EVENT, 0, 0,
         144),
    BOSS(0x17B, 0, 0, STAGE_RBO6, 2, 128, 77, STAGE_RCEN, 1, NO_EVENT, 0, 0,
         145),
};
const int g_CheatBossCount = LEN(g_CheatBosses);

// A rematch: the record and the flags of a beaten boss are 0 while the
// player stays in its stage (a boss in a castle stage: its room), so the
// boss comes back; then they get their old values, also after a new win.
// ponytail: a save made in that time keeps the 0s, the boss then comes again

static void RematchEnd(void) {
    const struct CheatBoss* b;
    int k;
    if (s_Rematch.boss < 0) {
        return;
    }
    b = &g_CheatBosses[s_Rematch.boss];
    if (b->event != NO_EVENT && s_Rematch.record) {
        g_Settings.timeAttackRecords[b->event] = s_Rematch.record;
    }
    for (k = 0; k < 2; k++) {
        if (b->flag[k] && s_Rematch.old[k]) {
            g_CastleFlags[b->flag[k]] = s_Rematch.old[k];
        }
    }
    if (b->stage == STAGE_RCEN) {
        g_CastleFlags[SHAFT_FIGHT_CS_UNK1] = s_Rematch.seen;
    }
    fprintf(stderr, "[cheat] boss %d: record %d and flags back\n",
            s_Rematch.boss, s_Rematch.record);
    s_Rematch.boss = -1;
}

// Once a frame in play: the end of a rematch, and one log line when the
// boss entity is there (the boss list test).
static void RematchWatch(void) {
    const struct CheatBoss* b;
    int i;
    if (s_Rematch.boss < 0 || g_WarpTarget.active) {
        return;
    }
    b = &g_CheatBosses[s_Rematch.boss];
    if (!s_Rematch.arrived) {
        s_Rematch.arrived = g_StageId == b->stage;
        s_Rematch.room = g_LastRoom.room;
        return;
    }
    if (g_StageId != b->stage ||
        (b->stage == b->host && g_LastRoom.room != s_Rematch.room)) {
        RematchEnd();
        return;
    }
    if (s_Rematch.arrived > 1200) {
        return;
    }
    for (i = 0; i < TOTAL_ENTITY_COUNT; i++) {
        if (g_Entities[i].entityId && g_Entities[i].enemyId == b->enemy[0] &&
            g_Entities[i].hitPoints > 0) {
            fprintf(stderr,
                    "[cheat] boss %d: enemy %03X in slot %d, hp %d, %d frames "
                    "after the arrival\n",
                    s_Rematch.boss, b->enemy[0], i, g_Entities[i].hitPoints,
                    s_Rematch.arrived);
            s_Rematch.arrived = 1201;
            return;
        }
    }
    if (++s_Rematch.arrived > 1200) {
        fprintf(stderr, "[cheat] boss %d: enemy %03X not seen in 20 s\n",
                s_Rematch.boss, b->enemy[0]);
    }
}

const char* Cheat_WarpBoss(int i) {
    const struct CheatBoss* b = &g_CheatBosses[i];
    const char* why = Cheat_WarpRefusal();
    int k;
    if (why) {
        fprintf(stderr, "[cheat] boss %d: refused, %s\n", i, why);
        return why;
    }
    RematchEnd();
    if (getenv("SOTN_TEST_BEATEN")) { // a test: as on a finished save
        if (b->event != NO_EVENT) {
            g_Settings.timeAttackRecords[b->event] = 1;
        }
        for (k = 0; k < 2; k++) {
            g_CastleFlags[b->flag[k]] |= b->flag[k] ? 1 : 0;
        }
    }
    s_Rematch.boss = i;
    s_Rematch.arrived = 0;
    s_Rematch.record = 0;
    if (b->event != NO_EVENT) {
        s_Rematch.record = g_Settings.timeAttackRecords[b->event];
        g_Settings.timeAttackRecords[b->event] = 0;
    }
    for (k = 0; k < 2; k++) {
        s_Rematch.old[k] = b->flag[k] ? g_CastleFlags[b->flag[k]] : 0;
        if (b->flag[k]) {
            g_CastleFlags[b->flag[k]] = 0;
        }
    }
    fprintf(stderr, "[cheat] boss %d: record %d, flags %d %d cleared\n", i,
            s_Rematch.record, s_Rematch.old[0], s_Rematch.old[1]);
    s_Rematch.seen = g_CastleFlags[SHAFT_FIGHT_CS_UNK1];
    if (b->stage == STAGE_RCEN) {
        // the warp lands next to Shaft in his chamber, where the talk scene
        // (spawned below it) does not start: as after the talk, Shaft then
        // starts the fight when the player is near (e_shaft.c)
        g_CastleFlags[SHAFT_FIGHT_CS_UNK1] = 1;
    }
    StartWarp(b->stage, b->room, -1, -1, 0);
    g_WarpTarget.exact = 1;
    g_WarpTarget.px = b->px;
    g_WarpTarget.py = b->py;
    return NULL;
}

// Mode B, APPLY NOW. The full player reload of the demo end (5087C.c,
// D_80097C98 0x08000000): NowLoading loads the player file, the HUD art,
// the stage and (Alucard only) the weapons and the familiar. D_8003C730 = 3
// keeps the stats, the inventory and the castle flags. The warp target
// keeps the room and the pixel; Engine_Init restarts the music (3 does
// not). A save still names its owner (g_CheatSaveChar, save_mgr.h).
const char* Cheat_ApplyCharacter(void) {
    const char* why = Cheat_WarpRefusal();
    int want, px, py;

    if (g_CheatSaveChar < 0) {
        g_CheatSaveChar = g_PlayableCharacter;
    }
    want = Cheat_Character(g_CheatSaveChar);
    if (!why && want == g_PlayableCharacter) {
        why = "already playing";
    }
    if (!why && (g_Player.status & PLAYER_STATUS_TRANSFORM)) {
        why = "change form back";
    }
    if (!why && !(g_Player.vram_flag & TOUCHING_GROUND)) {
        why = "stand on the ground";
    }
    if (!why && (!g_LastRoom.active || g_LastRoom.stage != g_StageId)) {
        why = "room unknown";
    }
    if (why) {
        fprintf(stderr, "[cheat] apply character %d: refused, %s\n", want, why);
        return why;
    }
    px = PLAYER.posX.i.hi + g_Tilemap.scrollX.i.hi;
    py = PLAYER.posY.i.hi + g_Tilemap.scrollY.i.hi;
    g_WarpTarget = (struct WarpTarget){
        1, g_StageId, g_LastRoom.room, g_Tilemap.left + (px >> 8),
        g_Tilemap.top + (py >> 8), 1, px, py};
    fprintf(stderr,
            "[cheat] apply character %d -> %d: stage %02X room %d pos %d,%d\n",
            g_PlayableCharacter, want, g_StageId, g_LastRoom.room, px, py);
    g_PlayableCharacter = want;
#ifdef USE_PLAYER_MARIA
    Maria_SyncStash();
#endif
    s_MusicRestart = 1;
    D_8003C730 = 3;
    D_80097C98 = 0x08000000;
    g_GameStep = Play_PrepareNextStage;
    return NULL;
}

static int RoomHasCell(const RoomHeader* r, int cx, int cy) {
    return r->load.tilesetId != 0xFF && cx >= r->left && cx <= r->right &&
           cy >= r->top && cy <= r->bottom;
}

RoomHeader* Cheat_WarpRoom(void) {
    RoomHeader* rooms = g_api.o.rooms;
    RoomHeader* room = NULL;
    int i, n, cx = g_WarpTarget.cx, cy = g_WarpTarget.cy;

    g_WarpTarget.active = 0;
    for (n = 0; rooms[n].left != 0x40; n++) {
    }
    // the table index is a hint: check it against the loaded stage; with no
    // cell (the boss list) the exact pixel names the cell
    if (cx < 0 && g_WarpTarget.room < n &&
        rooms[g_WarpTarget.room].load.tilesetId != 0xFF) {
        room = &rooms[g_WarpTarget.room];
        cx = room->left + (g_WarpTarget.px >> 8);
        cy = room->top + (g_WarpTarget.py >> 8);
    } else if (g_WarpTarget.room < n &&
               RoomHasCell(&rooms[g_WarpTarget.room], cx, cy)) {
        room = &rooms[g_WarpTarget.room];
    }
    for (i = 0; !room && i < n; i++) {
        if (RoomHasCell(&rooms[i], cx, cy)) {
            room = &rooms[i];
        }
    }
    if (!room) {
        room = &rooms[0];
        cx = room->left;
        cy = room->top;
    }
    if (g_WarpTarget.exact && room == &rooms[g_WarpTarget.room]) {
        PLAYER.posX.i.hi = g_WarpTarget.px; // APPLY NOW: the same spot
        PLAYER.posY.i.hi = g_WarpTarget.py;
    } else {
        PLAYER.posX.i.hi = (cx - room->left) * 256 + 128;
        PLAYER.posY.i.hi = (cy - room->top) * 256 + 128;
    }
    g_WarpTarget.exact = 0;
    D_8006C374 = 0; // a full reload sets it; 0x2C would force STAGE_TOP
    fprintf(stderr, "[cheat] warp arrival: stage %02X room %d cell %d,%d\n",
            g_StageId, (int)(room - rooms), cx, cy);
    return room;
}
