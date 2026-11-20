// SPDX-License-Identifier: AGPL-3.0-or-later
// Room stitch (sotn.ini room_stitch = 1, off by default): two rooms side by
// side on the map play as one wide room, so the walk across their border
// has no fade and no room load. Only the pairs of s_Pairs, each checked in
// a hidden run (walk across, camera, entities, no crash); the checks of
// Merge skip a pair whose data does not fit. No game data here: the merge
// copies the loaded stage tables at run time.
//
// The merge, after the stage overlay loads:
// - a copy of the room table, the rows of both rooms with the merged
//   bounds and the tile layer and entity layout of the left room A;
// - the layers of A with the merged rect and a layout of A's columns then
//   B's (RoomStitch_TileLayer, LoadRoomLayer and the margin preview);
// - entity lists of A's and B's entries, B's moved right by A's width and
//   its slots and spawn flags moved clear of A's (RoomStitch_Layouts).
#include "room_stitch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_RoomStitch;

// stage overlay name, the left room, the right room (room table indexes)
static const struct {
    const char* stage;
    u8 a, b;
} s_Pairs[] = {
    {"no3", 5, 6},
    // rno2 10+9: B's spikes sit at slot 144, no room for the block shift
};

typedef struct {
    s32 a, b, layer; // the rooms, A's tile layer index
    s32 dx;          // px from A's left edge to B's
    u8 objA, objB;   // the entity layouts
    RoomDef def;     // the merged layers
    LayerDef fg, bg;
    u16* tiles[2]; // the merged layouts of fg and bg
    u16 *h, *v;    // the merged entity lists, built on the first use
    s32 failed;    // the entity lists did not merge
} Stitch;

static Stitch s_St[8];
static s32 s_Count;
static RoomHeader* s_Rooms; // the room table copy
static char s_Stage[16];

extern RoomTeleport D_800A245C[];
#define TELEPORT_COUNT 131 // the retail teleport table of DRA

// a layer on its room's map cells, and two such layers alike
static bool OnCells(const LayerDef* l, const RoomHeader* r) {
    return l->rect.left == r->left && l->rect.top == r->top &&
           l->rect.right == r->right && l->rect.bottom == r->bottom;
}

static bool Alike(const LayerDef* la, const LayerDef* lb, const RoomHeader* a,
                  const RoomHeader* b) {
    return la && lb && la->tileDef && la->tileDef == lb->tileDef &&
           OnCells(la, a) && OnCells(lb, b) &&
           la->rect.params == lb->rect.params &&
           la->zPriority == lb->zPriority && la->flags == lb->flags;
}

// the longest run of rows open on both sides of the border
static s32 Seam(const LayerDef* fa, const LayerDef* fb, s32 wa, s32 wb, s32 h) {
    const u8* col = fa->tileDef->collision;
    s32 y, run = 0, best = 0;
    for (y = 0; y < h; y++) {
        run = !col[fa->layout[y * wa + wa - 1]] && !col[fb->layout[y * wb]]
                  ? run + 1
                  : 0;
        best = run > best ? run : best;
    }
    return best;
}

static u16* JoinTiles(const u16* la, const u16* lb, s32 wa, s32 wb, s32 h) {
    u16* out = malloc((wa + wb) * h * sizeof(u16));
    s32 y;
    for (y = 0; y < h; y++) {
        memcpy(out + y * (wa + wb), la + y * wa, wa * sizeof(u16));
        memcpy(out + y * (wa + wb) + wa, lb + y * wb, wb * sizeof(u16));
    }
    return out;
}

static const char* Check(Overlay* o, s32 n, s32 a, s32 b) {
    const RoomHeader *A = &o->rooms[a], *B = &o->rooms[b], *R;
    const RoomDef *da, *db;
    s32 i, w;

    if (a >= n || b >= n) {
        return "no such room";
    }
    if (A->load.tilesetId == 0xFF || B->load.tilesetId == 0xFF) {
        return "a teleport row";
    }
    if (B->left != A->right + 1 || B->top != A->top || B->bottom != A->bottom) {
        return "not side by side";
    }
    if (A->load.tilesetId != B->load.tilesetId) {
        return "the palettes differ";
    }
    if (A->load.objGfxId != B->load.objGfxId) {
        return "the object graphics differ";
    }
    da = &o->tileLayers[A->load.tileLayoutId];
    db = &o->tileLayers[B->load.tileLayoutId];
    if (!Alike(da->fg, db->fg, A, B)) {
        return "the fg layers differ";
    }
    // 0x80 sends to another room, 0x20 and 0x40 are save and loading rooms
    if (da->fg->rect.params & 0xE0) {
        return "a save, loading or redirect room";
    }
    if (da->bg->tileDef || db->bg->tileDef) {
        if (!Alike(da->bg, db->bg, A, B)) {
            return "the bg layers differ";
        }
    }
    for (i = 0, R = o->rooms; i < n; i++, R++) {
        if (i != a && i != b && R->load.tilesetId != 0xFF &&
            (R->load.tileLayoutId == A->load.tileLayoutId ||
             R->load.tileLayoutId == B->load.tileLayoutId ||
             R->load.objLayoutId == A->load.objLayoutId ||
             R->load.objLayoutId == B->load.objLayoutId)) {
            return "a layer or entity layout of another room";
        }
    }
    for (i = 0; i < TELEPORT_COUNT; i++) {
        w = D_800A245C[i].roomId / sizeof(RoomHeader);
        if (D_800A245C[i].stageId == g_StageId && (w == a || w == b)) {
            return "a teleport target";
        }
    }
    for (i = 0; i < s_Count; i++) {
        if (s_St[i].a == a || s_St[i].b == a || s_St[i].a == b ||
            s_St[i].b == b) {
            return "in another pair";
        }
    }
    w = (A->right - A->left + 1) * 16;
    if (Seam(da->fg, db->fg, w, (B->right - B->left + 1) * 16,
             (A->bottom - A->top + 1) * 16) < 3) {
        return "the border is closed";
    }
    return NULL;
}

static void Merge(Stitch* s, Overlay* o, s32 a, s32 b) {
    const RoomHeader *A = &o->rooms[a], *B = &o->rooms[b];
    const RoomDef* da = &o->tileLayers[A->load.tileLayoutId];
    const RoomDef* db = &o->tileLayers[B->load.tileLayoutId];
    s32 wa = (A->right - A->left + 1) * 16, wb = (B->right - B->left + 1) * 16;
    s32 h = (A->bottom - A->top + 1) * 16;

    memset(s, 0, sizeof(*s));
    s->a = a;
    s->b = b;
    s->layer = A->load.tileLayoutId;
    s->dx = wa * 16;
    s->objA = A->load.objLayoutId;
    s->objB = B->load.objLayoutId;
    s->fg = *da->fg;
    s->fg.rect.right = B->right;
    s->fg.layout = s->tiles[0] =
        JoinTiles(da->fg->layout, db->fg->layout, wa, wb, h);
    s->def.fg = &s->fg;
    s->def.bg = da->bg;
    if (da->bg->tileDef) {
        s->bg = *da->bg;
        s->bg.rect.right = B->right;
        s->bg.layout = s->tiles[1] =
            JoinTiles(da->bg->layout, db->bg->layout, wa, wb, h);
        s->def.bg = &s->bg;
    }
}

static void FreeAll(void) {
    s32 i;
    for (i = 0; i < s_Count; i++) {
        free(s_St[i].tiles[0]);
        free(s_St[i].tiles[1]);
        free(s_St[i].h);
        free(s_St[i].v);
    }
    s_Count = 0;
    free(s_Rooms);
    s_Rooms = NULL;
}

// ponytail: a stage load frees the buffers of the last stage while
// g_Tilemap may still point at them; the new stage loads a room before it
// draws again. Keep them one load longer if a draw in between shows up.
void RoomStitch_HookOverlay(const char* name, Overlay* o) {
    s32 i, n, k;
    const char* why;
    Stitch* s;

    FreeAll();
    snprintf(s_Stage, sizeof(s_Stage), "%s", name);
    if (!g_RoomStitch || !o->rooms || !o->tileLayers) {
        return;
    }
    for (n = 0; o->rooms[n].left != 0x40; n++) {
    }
    for (i = 0; i < LEN(s_Pairs); i++) {
        if (strcmp(s_Pairs[i].stage, name) || s_Count == LEN(s_St)) {
            continue;
        }
        why = Check(o, n, s_Pairs[i].a, s_Pairs[i].b);
        if (why) {
            fprintf(stderr, "[stitch] %s rooms %d+%d: skipped, %s\n", name,
                    s_Pairs[i].a, s_Pairs[i].b, why);
            continue;
        }
        Merge(&s_St[s_Count++], o, s_Pairs[i].a, s_Pairs[i].b);
    }
    if (!s_Count) {
        return;
    }
    s_Rooms = malloc((n + 1) * sizeof(RoomHeader));
    memcpy(s_Rooms, o->rooms, (n + 1) * sizeof(RoomHeader));
    for (i = 0; i < s_Count; i++) {
        s = &s_St[i];
        for (k = 0; k < 2; k++) {
            RoomHeader* r = &s_Rooms[k ? s->b : s->a];
            r->left = o->rooms[s->a].left;
            r->right = o->rooms[s->b].right;
            r->load.tileLayoutId = s->layer;
            r->load.objLayoutId = s->objA;
        }
        fprintf(stderr,
                "[stitch] %s rooms %d+%d: merged, %dx%d px, stage %02X\n", name,
                s->a, s->b, (s->fg.rect.right - s->fg.rect.left + 1) * 256,
                (s->fg.rect.bottom - s->fg.rect.top + 1) * 256, g_StageId);
    }
    o->rooms = s_Rooms;
}

const RoomDef* RoomStitch_TileLayer(s32 index) {
    s32 i;
    for (i = 0; i < s_Count; i++) {
        if (s_St[i].layer == index) {
            return &s_St[i].def;
        }
    }
    return &g_api.o.tileLayers[index];
}

// Entity lists: LayoutEntity records of 5 u16, a head record (key 0xFFFE)
// and a tail one (0xFFFF), sorted by x (key 0, horizontal) or y (key 1).
// The count between them, or -1 (a list with a random choice header).
#define REC 5
static s32 Count(const u16* p, s32 key) {
    s32 n = 0;
    if (p[key] != 0xFFFE) {
        return -1;
    }
    for (p += REC; p[key] != 0xFFFF; p += REC) {
        n++;
    }
    return n;
}

// the low and high slot (low byte) or flag (high byte, 0 = none)
static void Range(const u16* p, s32 n, s32 shift, s32* lo, s32* hi) {
    s32 i, x;
    for (i = 1; i <= n; i++) {
        x = p[i * REC + 3] >> shift & 0xFF;
        if (shift && !x) {
            continue;
        }
        *lo = x < *lo ? x : *lo;
        *hi = x > *hi ? x : *hi;
    }
}

static u16* JoinList(const u16* pa, s32 na, const u16* pb, s32 nb, s32 key,
                     s32 dx, s32 ds, s32 df) {
    u16* out = malloc((na + nb + 2) * REC * sizeof(u16));
    u16 *e, t[REC];
    s32 i, j, slot, flag;

    memcpy(out, pa, (na + 1) * REC * sizeof(u16)); // the head and A
    e = out + (na + 1) * REC;
    for (i = 1; i <= nb; i++, e += REC) {
        memcpy(e, pb + i * REC, REC * sizeof(u16));
        slot = (e[3] & 0xFF) + ds;
        flag = e[3] >> 8;
        e[0] += dx;
        e[3] = (flag ? flag + df : 0) << 8 | slot;
    }
    memcpy(e, pa + (na + 1) * REC, REC * sizeof(u16)); // the tail
    // ponytail: insertion sort, the lists have a few dozen records
    for (i = 2; i <= na + nb; i++) {
        memcpy(t, out + i * REC, sizeof(t));
        for (j = i - 1; j >= 1 && out[j * REC + key] > t[key]; j--) {
            memcpy(out + (j + 1) * REC, out + j * REC, sizeof(t));
        }
        memcpy(out + (j + 1) * REC, t, sizeof(t));
    }
    return out;
}

static void Lists(Stitch* s, u16** H, u16** V) {
    const u16 *ha = H[s->objA], *hb = H[s->objB];
    const u16 *va = V[s->objA], *vb = V[s->objB];
    s32 na = Count(ha, 0), nb = Count(hb, 0), ds = 0, df = 0;
    // the low and high slot and flag of A [0] and B [1]
    s32 sl[2] = {255, 255}, sh[2] = {-1, -1}, fl[2] = {255, 255},
        fh[2] = {-1, -1};

    if (na < 0 || nb < 0 || Count(va, 1) != na || Count(vb, 1) != nb) {
        s->failed = 1;
        fprintf(stderr, "[stitch] rooms %d+%d: entity lists not merged\n", s->a,
                s->b);
        return;
    }
    Range(ha, na, 0, &sl[0], &sh[0]);
    Range(hb, nb, 0, &sl[1], &sh[1]);
    Range(ha, na, 8, &fl[0], &fh[0]);
    Range(hb, nb, 8, &fl[1], &fh[1]);
    // B's slots move as one block above A's (a part entity takes the
    // slots after its own), clear of the free pool at entity 160
    if (sh[0] >= 0 && sh[1] >= 0 && sl[1] <= sh[0] + 4 && sh[1] >= sl[0]) {
        ds = sh[0] + 4 - sl[1];
        if (STAGE_ENTITY_START + sh[1] + ds >= 160) {
            s->failed = 1;
            fprintf(stderr, "[stitch] rooms %d+%d: no free entity slots\n",
                    s->a, s->b);
            return;
        }
    }
    if (fh[0] >= 0 && fh[1] >= 0 && fl[1] <= fh[0] && fh[1] >= fl[0]) {
        df = fh[0] + 1 - fl[1];
        if (fh[1] + df > 255) {
            s->failed = 1;
            fprintf(stderr, "[stitch] rooms %d+%d: no free spawn flags\n", s->a,
                    s->b);
            return;
        }
    }
    s->h = JoinList(ha, na, hb, nb, 0, s->dx, ds, df);
    s->v = JoinList(va, na, vb, nb, 1, s->dx, ds, df);
    fprintf(stderr,
            "[stitch] rooms %d+%d: entities %d+%d, B slots +%d, B flags +%d\n",
            s->a, s->b, na, nb, ds, df);
}

void RoomStitch_Layouts(u16** H, u16** V, u16** h, u16** v) {
    s32 i;
    Stitch* s;
    for (i = 0; i < s_Count; i++) {
        s = &s_St[i];
        if (g_Tilemap.fg != s->fg.layout) {
            continue;
        }
        if (!s->h && !s->failed) {
            Lists(s, H, V);
        }
        if (s->h) {
            *h = s->h;
            *v = s->v;
        }
        fprintf(stderr, "[stitch] %s rooms %d+%d: room entities, scroll %d\n",
                s_Stage, s->a, s->b, g_Tilemap.scrollX.i.hi);
        return;
    }
}
