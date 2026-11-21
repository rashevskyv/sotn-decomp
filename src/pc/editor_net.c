// SPDX-License-Identifier: AGPL-3.0-or-later
// Editor server: a small HTTP server inside the game, port 8080. Off by
// default; editor = 1 in sotn.ini, --editor (PC) or the port menu row EDITOR
// (live, no restart) starts it. It
// serves the editor page (tools/editor/index.html, built in) and its API:
//
//   GET  /             the page
//   GET  /state        JSON: stage, room, player, warp state, cheats
//   GET  /castle.json  the castle rooms in map cells (the teleport map)
//   GET  /room.bin     the current room layers and their tile tables
//   GET  /gfx.bin      the stage tile pixels and palettes
//   GET  /native.bin   the native planes of a Saturn stage room (404 if none)
//   GET  /patch        the apron patch of the stage (text)
//   POST /patch        save the apron patch (text body), shown at once
//   POST /warp?stage=&room=&cx=&cy=   teleport, 409 + reason when refused
//   POST /cheat?name=&on=             switch a cheat (as the port menu)
//   POST /equip?hand=&item=           equip a hand item (as the menu does)
//   POST /save?slot=                  write the save (as the save coffin)
//
// No password: anyone on the network can use it while it is on. All work
// runs on the game thread, one request per connection, Connection: close.
#include <game.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
void Editor_OnFrame(void) {} // ponytail: no winsock port, PC Linux + Switch
void Editor_RefreshAddress(void) {}
const char* Editor_Status(void) {
    return g_EditorOn ? "NOT IN THE WINDOWS BUILD" : NULL;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PORT 8080
#define MAX_HEAD 8192
#define MAX_BODY (1 << 20)

extern u16 g_TileVram[512 * 512]; // sim_pc.c: VRAM x 512..1023, y 0..511

static int s_listen = -1;
static int s_failed;

#define MAX_WAIT 8 // accepted connections whose request has not arrived
static int s_fd[MAX_WAIT], s_age[MAX_WAIT], s_n;

// a growing reply buffer
static char* s_out;
static int s_len, s_cap;

static void Put(const void* p, int n) {
    if (s_len + n > s_cap) {
        s_cap = (s_len + n) * 2 + 4096;
        s_out = realloc(s_out, s_cap);
    }
    memcpy(s_out + s_len, p, n);
    s_len += n;
}

static void Putf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Put(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
}

static void PutJsonString(const char* s) {
    Put("\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            Put("\\", 1);
        }
        if ((u8)*s >= 32) {
            Put(s, 1);
        }
    }
    Put("\"", 1);
}

static int SendAll(int fd, const char* p, int n) {
    int k;
    while (n > 0) {
        k = send(fd, p, n, 0);
        if (k <= 0) {
            return -1;
        }
        p += k;
        n -= k;
    }
    return 0;
}

static void Reply(int fd, int code, const char* type, const void* body,
                  int len) {
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                     "Content-Length: %d\r\nConnection: close\r\n"
                     "Cache-Control: no-store\r\n"
                     "Access-Control-Allow-Origin: *\r\n\r\n",
                     code,
                     code == 200   ? "OK"
                     : code == 404 ? "Not Found"
                     : code == 409 ? "Conflict"
                                   : "Bad Request",
                     type, len);
    if (SendAll(fd, head, n) == 0) {
        SendAll(fd, body, len);
    }
}

static void ReplyOut(int fd, int code, const char* type) {
    Reply(fd, code, type, s_out, s_len);
}

static int Arg(const char* query, const char* key, int def) {
    char k[16];
    const char* p = query;
    int n = snprintf(k, sizeof(k), "%s=", key);
    while (p && (p = strstr(p, k))) {
        if (p == query || p[-1] == '&' || p[-1] == '?') {
            return (int)strtol(p + n, NULL, 0);
        }
        p += n;
    }
    return def;
}

static void State(void) {
    const char* why = Cheat_WarpRefusal();
    Putf("{\"stage\":%d,\"name\":", g_StageId);
    PutJsonString(Castle_StageName(g_StageId));
    Putf(",\"layer\":%d,\"room\":[%d,%d,%d,%d],\"cell\":[%d,%d]", g_ApronLayer,
         g_Tilemap.left, g_Tilemap.top, g_Tilemap.right, g_Tilemap.bottom,
         D_8009791C.x, D_8009791C.y);
    Putf(",\"player\":[%d,%d],\"scroll\":[%d,%d],\"wide\":[%d,%d]",
         PLAYER.posX.i.hi + g_Tilemap.scrollX.i.hi,
         PLAYER.posY.i.hi + g_Tilemap.scrollY.i.hi, g_Tilemap.scrollX.i.hi,
         g_Tilemap.scrollY.i.hi, g_WideX, g_WideY);
    Putf(",\"hp\":%d,\"mist\":%d,\"canWarp\":%s,\"reason\":", g_Status.hp,
         (g_Player.status & PLAYER_STATUS_MIST_FORM) != 0,
         why ? "false" : "true");
    PutJsonString(why ? why : "");
    Putf(",\"cheats\":{\"refill\":%d,\"invincible\":%d,\"walls\":%d,"
         "\"items\":%d,\"map\":%d,\"skip_prologue\":%d,\"character\":%d},"
         "\"char\":%d,\"saveChar\":%d,\"sub\":[%d,%u],\"leap\":%d",
         g_Cheat.refill, g_Cheat.invincible, g_Cheat.walls, g_Cheat.items,
         g_Cheat.map, g_Cheat.skipPrologue, g_Cheat.character, g_PlayableCharacter,
         g_CheatSaveChar, g_Status.subWeapon, g_Status.D_80097C40,
         g_Status.relics[RELIC_LEAP_STONE]);
    // tests: the hands, their attack, the defence and the Saturn items owned
    Putf(",\"equip\":[%u,%u],\"atk\":[%d,%d],\"def\":%d,\"saturn\":[%d,%d,%d,%d]}",
         g_Status.equipment[0], g_Status.equipment[1],
         g_Status.attackHands[0], g_Status.attackHands[1],
         g_Status.defenseEquip, g_Status.equipHandCount[ITEM_ASTRAL_DAGGER],
         g_Status.equipHandCount[ITEM_KUSAI_MESHI],
         g_Status.equipHandCount[ITEM_WOODLAND_SHIELD],
         g_Status.equipHandCount[ITEM_DOUBLE_BLADE_AXE]);
}

static void Castle(void) {
    const struct CastleRoom* r;
    int i;
    Put("{\"stages\":[", 11);
    for (i = 0; i < g_CastleStageCount; i++) {
        Putf("%s[%d,", i ? "," : "", g_CastleStages[i].id);
        PutJsonString(g_CastleStages[i].name);
        Put("]", 1);
    }
    Put("],\"rooms\":[", 11);
    for (i = 0; i < g_CastleRoomCount; i++) {
        r = &g_CastleRooms[i];
        Putf("%s[%d,%d,%d,%d,%d,%d]", i ? "," : "", r->stage, r->room,
             r->left, r->top, r->right, r->bottom);
    }
    Put("]}", 2);
}

// room.bin: u16 header[16], the fg and bg layouts (u16), then per layer
// the tile tables gfxPage, gfxIndex, clut (4096 bytes each).
static void PutLayer(const u16* layout, int w, int h) {
    if (layout && w > 0 && h > 0) {
        Put(layout, w * h * 2);
    }
}

static void PutTileDef(const TileDefinition* t) {
    static const u8 zero[4096];
    Put(t ? t->gfxPage : zero, 4096);
    Put(t ? t->gfxIndex : zero, 4096);
    Put(t ? t->clut : zero, 4096);
}

static void Room(void) {
    const TileDefinition* ft = g_Tilemap.tileDef;
    const TileDefinition* bt = g_BgLayers[0].tileDef;
    u16 head[16] = {0};
    head[0] = 0x5253; // "SR"
    head[1] = 1;      // version
    head[2] = g_StageId;
    head[3] = g_ApronLayer;
    if (ft && g_Tilemap.fg) {
        head[4] = g_Tilemap.hSize * 16;
        head[5] = g_Tilemap.vSize * 16;
        head[8] = g_Tilemap.flags;
    }
    if (bt && g_BgLayers[0].layout) {
        head[6] = g_BgLayers[0].w * 16;
        head[7] = g_BgLayers[0].h * 16;
        head[9] = g_BgLayers[0].flags;
    }
    head[10] = 24; // apron margin left and right, src/pc/apron.c AX
    head[11] = 0;  // none above or below: the PS1 clip rows hide them
    Put(head, sizeof(head));
    PutLayer(g_Tilemap.fg, head[4], head[5]);
    PutLayer(g_BgLayers[0].layout, head[6], head[7]);
    PutTileDef(head[4] ? ft : NULL);
    PutTileDef(head[6] ? bt : NULL);
}

// native.bin: the planes a Saturn stage draws (g_PcStageNativeLayer).
// u16 "SN", version 1, layer count; per layer u16 w, h, pages, palettes, then
// the map (w * h u16), the palettes (16 u16 each), the chars (32 KB a page).
// 0 (the page gets 404) while the game draws the tile tables.
static int Native(void) {
    static const u16 head[3] = {0x4E53, 1, 3};
    const PcNativeLayer* l;
    u16 d[4];
    int i;
    if (!g_PcStageNativeLayer || !g_PcStageNativeLayer(0)) {
        return 0;
    }
    Put(head, sizeof(head));
    for (i = 0; i < head[2]; i++) {
        l = g_PcStageNativeLayer(i);
        d[0] = l ? l->w : 0;
        d[1] = l ? l->h : 0;
        d[2] = l ? l->pages : 0;
        d[3] = l ? l->paletteCount : 0;
        Put(d, sizeof(d));
        if (l) {
            Put(l->map, l->w * l->h * 2);
            Put(l->palettes, l->paletteCount * 16 * 2);
            Put(l->chars, l->pages * 256 * 256 / 2);
        }
    }
    return 1;
}

static void Handle(int fd) {
    static char req[MAX_HEAD + 1];
    char method[8], path[256], *body, *query, *p;
    int n = 0, k, len = 0, code = 200;
    const char* why;
    struct timeval rtv = {0, 20000}, stv = {0, 300000};

    req[0] = 0;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
    // Editor_OnFrame calls this only when the request has arrived, so the
    // 20 ms read timeout covers only a request that comes in parts.
    // ponytail: the 0.3 s send timeout stays: room.bin and gfx.bin (0.6 MB)
    // may wait for a slow Wi-Fi client after a click on the page; the small
    // /state replies fit the socket buffer. A thread if that ever matters.
    while (n < MAX_HEAD && !strstr(req, "\r\n\r\n")) {
        k = recv(fd, req + n, MAX_HEAD - n, 0);
        if (k <= 0) {
            return;
        }
        n += k;
        req[n] = 0;
    }
    body = strstr(req, "\r\n\r\n");
    if (!body || sscanf(req, "%7s %255s", method, path) != 2) {
        return;
    }
    body += 4;
    p = strstr(req, "Content-Length:");
    if (!p) {
        p = strstr(req, "content-length:");
    }
    if (p && p < body) {
        len = atoi(p + 15);
    }
    if (len < 0 || len > MAX_BODY) {
        Reply(fd, 400, "text/plain", "body too large", 14);
        return;
    }
    if (len > 0) {
        k = n - (int)(body - req); // body bytes already read
        p = malloc(len + 1);
        memcpy(p, body, k < len ? k : len);
        while (k < len) {
            n = recv(fd, p + k, len - k, 0);
            if (n <= 0) {
                free(p);
                return;
            }
            k += n;
        }
        body = p;
    } else {
        body = NULL;
    }
    query = strchr(path, '?');
    if (query) {
        *query++ = 0;
    }

    s_len = 0;
    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        Reply(fd, 200, "text/html; charset=utf-8", g_EditorPage[0].text,
              g_EditorPageCount ? g_EditorPage[0].len : 0);
    } else if (!strcmp(path, "/state")) {
        State();
        ReplyOut(fd, 200, "application/json");
    } else if (!strcmp(path, "/castle.json")) {
        Castle();
        ReplyOut(fd, 200, "application/json");
    } else if (!strcmp(path, "/room.bin")) {
        Room();
        ReplyOut(fd, 200, "application/octet-stream");
    } else if (!strcmp(path, "/native.bin")) {
        if (Native()) {
            ReplyOut(fd, 200, "application/octet-stream");
        } else {
            Reply(fd, 404, "text/plain", "no native planes", 16);
        }
    } else if (!strcmp(path, "/gfx.bin")) {
        // the VRAM copy of the stage tiles, then all palettes (g_Clut)
        Put(g_TileVram, sizeof(u16) * 512 * 512);
        Put(g_Clut, sizeof(u16) * 0x3000);
        ReplyOut(fd, 200, "application/octet-stream");
    } else if (!strcmp(path, "/patch") && !strcmp(method, "GET")) {
        why = Apron_Text();
        Reply(fd, 200, "text/plain; charset=utf-8", why, strlen(why));
    } else if (!strcmp(path, "/patch") && !strcmp(method, "POST")) {
        why = Apron_Save(body ? body : "", len);
        Putf("{\"ok\":%s,\"error\":", why ? "false" : "true");
        PutJsonString(why ? why : "");
        Put("}", 1);
        ReplyOut(fd, why ? 409 : 200, "application/json");
    } else if (!strcmp(path, "/warp") && !strcmp(method, "POST")) {
        why = Cheat_Warp(Arg(query, "stage", -1), Arg(query, "room", -1),
                         Arg(query, "cx", -1), Arg(query, "cy", -1));
        Putf("{\"ok\":%s,\"error\":", why ? "false" : "true");
        PutJsonString(why ? why : "");
        Put("}", 1);
        ReplyOut(fd, why ? 409 : 200, "application/json");
    } else if (!strcmp(path, "/apply_character") && !strcmp(method, "POST")) {
        why = Cheat_ApplyCharacter(); // the menu row APPLY NOW
        Putf("{\"ok\":%s,\"error\":", why ? "false" : "true");
        PutJsonString(why ? why : "");
        Put("}", 1);
        ReplyOut(fd, why ? 409 : 200, "application/json");
    } else if (!strcmp(path, "/cheat") && !strcmp(method, "POST")) {
        static const char* names[] = {"refill", "invincible",    "walls",
                                      "items",  "skip_prologue", "character",
                                      "map"};
        int* flags[] = {&g_Cheat.refill, &g_Cheat.invincible, &g_Cheat.walls,
                        &g_Cheat.items, &g_Cheat.skipPrologue,
                        &g_Cheat.character, &g_Cheat.map};
        char name[24] = "";
        code = 400;
        if (query && (p = strstr(query, "name="))) {
            sscanf(p + 5, "%23[a-z_]", name);
        }
        for (k = 0; k < LEN(names); k++) {
            if (!strcmp(name, names[k])) {
                *flags[k] = Arg(query, "on", 1);
                if (flags[k] != &g_Cheat.character) {
                    *flags[k] = *flags[k] != 0;
                } else {
                    *flags[k] = Cheat_CharacterNext(*flags[k], 0);
                }
                Wide_SaveIni();
                fprintf(stderr, "[cheat] editor: %s %d\n", name, *flags[k]);
                code = 200;
            }
        }
        State();
        ReplyOut(fd, code, "application/json");
    } else if (!strcmp(path, "/candle") && !strcmp(method, "POST")) {
        // tests: a candle of the room moves next to the player and drops
        // this item (e_breakable.h, e_collect.h)
        Entity* e = NULL;
        int item = Arg(query, "item", -1), slot = Arg(query, "slot", -1);
        // slot=N moves that entity (an enemy) instead, items unchanged
        if (slot >= STAGE_ENTITY_START && slot < TOTAL_ENTITY_COUNT &&
            g_Entities[slot].entityId) {
            e = &g_Entities[slot];
            item = 0;
        }
        for (k = STAGE_ENTITY_START; k < TOTAL_ENTITY_COUNT && !e; k++) {
            if (g_Entities[k].entityId == 1 && g_Entities[k].step) {
                e = &g_Entities[k]; // E_BREAKABLE in every stage
            }
        }
        code = 409;
        if (e && item >= 0 && item < 0x80) {
            if (item) {
                e->params = (e->params & 0xF000) | item;
            }
            e->posX.i.hi = PLAYER.posX.i.hi + Arg(query, "dx", 24);
            e->posY.i.hi = PLAYER.posY.i.hi;
            g_Status.relics[RELIC_CUBE_OF_ZOE] |=
                RELIC_FLAG_FOUND | RELIC_FLAG_ACTIVE;
            code = 200;
        }
        fprintf(stderr, "[cheat] editor: candle item %d: %s\n", item,
                e ? "moved" : "none");
        State();
        ReplyOut(fd, code, "application/json");
    } else if (!strcmp(path, "/relic") && !strcmp(method, "POST")) {
        // tests: set one relic byte (RELIC_FLAG_FOUND 1, RELIC_FLAG_ACTIVE 2)
        k = Arg(query, "id", -1);
        code = k >= 0 && k < LEN(g_Status.relics) ? 200 : 400;
        if (code == 200) {
            g_Status.relics[k] = Arg(query, "v", 0);
        }
        State();
        ReplyOut(fd, code, "application/json");
    } else if (!strcmp(path, "/ents")) {
        // tests: the live entities (slot, id, screen x y, hp, hitbox, step,
        // params, enemy id)
        Put("[", 1);
        for (k = 0, code = 0; k < TOTAL_ENTITY_COUNT; k++) {
            Entity* e = &g_Entities[k];
            if (e->entityId) {
                Putf("%s[%d,%d,%d,%d,%d,%d,%d,%d,%d]", code++ ? "," : "", k,
                     e->entityId, e->posX.i.hi, e->posY.i.hi, e->hitPoints,
                     e->hitboxState, e->step, e->params, e->enemyId);
            }
        }
        Put("]", 1);
        ReplyOut(fd, 200, "application/json");
    } else if (!strcmp(path, "/c40") && !strcmp(method, "POST")) {
        // tests: set D_80097C40 (Alucard's Muramasa counter, Maria's slot)
        g_Status.D_80097C40 = Arg(query, "v", 0);
        State();
        ReplyOut(fd, 200, "application/json");
    } else if (!strcmp(path, "/equip") && !strcmp(method, "POST")) {
        // tests: the same load as a menu close (src/dra/menu.c)
        bool LoadWeaponPrg(s32 equipIndex);
        void InitWeapon(s32 itemSlot);
        void CheckWeaponCombo(void);
        int hand = Arg(query, "hand", 0), item = Arg(query, "item", -1);
        code = 400;
        if ((hand == 0 || hand == 1) && item >= 0 &&
            item < LEN(g_Status.equipHandCount) && g_GameState == Game_Play) {
            g_Status.equipment[hand] = item;
            code = LoadWeaponPrg(hand) ? 200 : 409;
            InitWeapon(hand);
            CheckWeaponCombo(); // shield rod + shield, two Heaven Swords
            fprintf(stderr, "[cheat] editor: equip hand %d item %d\n", hand,
                    item);
        }
        State();
        ReplyOut(fd, code, "application/json");
    } else if (!strcmp(path, "/save") && !strcmp(method, "POST")) {
        // tests: write the save of the loaded slot, as the save coffin does
        // (src/dra/62DEC.c steps 4-5)
        void MakeMemcardPath(char* dstSaveName, s32 block);
        void StoreSaveData(SaveData* save, s32 block, s32 cardIcon);
        s32 MemcardWriteFile(s32 nPort, s32 nCard, const char* name,
                             void* data, s32 flags, bool create);
        s32 MemcardClose(s32 nPort);
        int MemcardDetectSave(s32 nPort, u8* expectedSaveName, s32 block);
        char name[32];
        int slot = Arg(query, "slot", D_8006C378), wr = -9, cl = 0;
        code = 409;
        if (g_GameState == Game_Play && slot >= 0 && slot < 15) {
            MakeMemcardPath(name, slot);
            StoreSaveData((SaveData*)g_Pix, slot, 0);
            wr = MemcardWriteFile(D_80097924, 0, name, g_Pix, 1,
                                  MemcardDetectSave(D_80097924, name, 0) != 1);
            for (k = 0; !wr && k < 100 && !(cl = MemcardClose(D_80097924));
                 k++) {
            }
            code = !wr && cl == 1 ? 200 : 500;
        }
        Putf("{\"slot\":%d,\"port\":%d,\"write\":%d,\"close\":%d}", slot,
             D_80097924, wr, cl);
        ReplyOut(fd, code, "application/json");
    } else {
        Reply(fd, 404, "text/plain", "not found", 9);
    }
    if (body) {
        free(body);
    }
}

// The address line of the port menu: the URL, "NO NETWORK" or the error.
static char s_status[40];

#ifdef __SWITCH__
int SwitchNx_NetInit(void);
int SwitchNx_HostIp(char* ip, int size);
#else
#include <ifaddrs.h>
#include <net/if.h>
// the first IPv4 address of an interface that is up and not loopback
static int HostIp(char* ip, int size) {
    struct ifaddrs *list, *i;
    int ok = -1;
    if (getifaddrs(&list) != 0) {
        return -1;
    }
    for (i = list; i && ok; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET &&
            (i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK)) {
            snprintf(ip, size, "%s",
                     inet_ntoa(((struct sockaddr_in*)i->ifa_addr)->sin_addr));
            ok = 0;
        }
    }
    freeifaddrs(list);
    return ok;
}
#endif

// The address to open on the PC. The port menu calls it when it opens.
void Editor_RefreshAddress(void) {
    char ip[16]; // IPv4
    int r;
    if (s_listen < 0) {
        return; // the status names the error, or the server is off
    }
#ifdef __SWITCH__
    r = SwitchNx_HostIp(ip, sizeof(ip));
#else
    r = HostIp(ip, sizeof(ip));
#endif
    if (r) {
        snprintf(s_status, sizeof(s_status), "NO NETWORK");
    } else {
        snprintf(s_status, sizeof(s_status), "HTTP://%s:%d/", ip, PORT);
    }
}

// NULL while the server is off.
const char* Editor_Status(void) {
    if (!g_EditorOn) {
        return NULL;
    }
    return s_status[0] ? s_status : "STARTING";
}

static void Start(void) {
    struct sockaddr_in a;
    int one = 1;

#ifdef __SWITCH__
    if (SwitchNx_NetInit() != 0) {
        snprintf(s_status, sizeof(s_status), "NO NETWORK (SOCKETS)");
        s_failed = 1;
        return;
    }
#else
    signal(SIGPIPE, SIG_IGN); // a closed browser tab must not end the game
#endif
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (s_listen < 0 || bind(s_listen, (struct sockaddr*)&a, sizeof(a)) < 0 ||
        listen(s_listen, 4) < 0) {
        fprintf(stderr, "[editor] port %d: %s\n", PORT, strerror(errno));
        if (s_listen >= 0) {
            close(s_listen);
        }
        s_listen = -1;
        snprintf(s_status, sizeof(s_status), "PORT %d IN USE", PORT);
        s_failed = 1;
        return;
    }
    fcntl(s_listen, F_SETFL, fcntl(s_listen, F_GETFL, 0) | O_NONBLOCK);
    Editor_RefreshAddress();
    fprintf(stderr, "[editor] listening port %d: %s\n", PORT, s_status);
}

// Called on every vsync. The port menu switches g_EditorOn live: off closes
// the port, on opens it again.
void Editor_OnFrame(void) {
    static int boot = 180; // a start from sotn.ini waits 3 s: no boot delay
    int i, fd;
    if (boot > 0) {
        boot--;
        return;
    }
    if (!g_EditorOn) {
        while (s_n > 0) {
            close(s_fd[--s_n]);
        }
        if (s_listen >= 0) {
            close(s_listen);
            s_listen = -1;
            fprintf(stderr, "[editor] stopped\n");
        }
        s_failed = 0; // the next switch on tries again
        s_status[0] = 0;
        return;
    }
    if (s_failed) {
        return;
    }
    if (s_listen < 0) {
        Start();
        return;
    }
    // never wait for a client on the game thread: a connection joins the
    // list, and its request is read on the frame it has arrived. A browser
    // opens spare connections that send nothing; they go after 2 s.
    while (s_n < MAX_WAIT && (fd = accept(s_listen, NULL, NULL)) >= 0) {
        s_fd[s_n] = fd;
        s_age[s_n++] = 0;
    }
    for (i = 0; i < s_n;) {
        struct pollfd p = {s_fd[i], POLLIN, 0};
        if (poll(&p, 1, 0) <= 0 && ++s_age[i] < 120) {
            i++;
            continue;
        }
        if (p.revents & POLLIN) {
            Handle(s_fd[i]);
        }
        close(s_fd[i]);
        s_n--;
        s_fd[i] = s_fd[s_n];
        s_age[i] = s_age[s_n];
    }
}
#endif
