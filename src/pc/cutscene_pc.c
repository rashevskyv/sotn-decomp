// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include <cutscene.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc.h"

static const CutsceneSymbolRange* _symbols = NULL;
static int _symbolCount = 0;

// normalize 0x8018xxxx ptr to 0x0018xxxx
#define NORMALIZE_PSX_ADDR(x) ((x) & 0x0FFFFFFF)

// The dialogue text follows the voice set (tools/lang/FORMAT.md, script
// tables). A table is a SOTNSCR1 file of the pack: the stage scripts rebuilt
// with other boxes.
//   en: ps1 = the stock scripts, jp = script_jp_en.bin, psp = script_psp_en.bin
//   uk: ps1 = script.bin (from ua_ps1.json, the US lines), jp = script_jp.bin,
//       psp = script_psp.bin
// A missing en table gives the stock scripts, a missing uk table script.bin.
// A voice set without clips plays the US voice, so it takes the ps1 table.
// CutscenePcAlloc copies, for each table, every registered range that starts
// with one or more scripts of the table, one after another: the rebuilt
// scripts plus the rest of the range (the events), at a PSX address of its
// own from 0x80140000, where no stage symbol lives. The stock range stays.
// All copies stay until the next stage, so a jump or an event pointer of
// any table stays valid. SetCutsceneScript (Lang_ScriptStart) takes the
// table of the voice set, and each CSOP_NEXT_DIALOG (Lang_NextBox) takes it
// again: a voice change in the port menu changes the text from the next box.
#define LANG_PSX_BASE 0x80140000
#define LANG_PSX_END 0x80180000
#define LANG_MAX 48
#define LANG_SEGS 4
typedef struct {
    u32 stockOff, stockLen, newOff, newLen, nMap;
    const u8* map; // nMap x {u32 old, new}, offsets in the script
} LangSeg;
typedef struct {
    const u8* old;   // the stock range
    u32 size, psx;   // its size and PSX address
    u32 nSeg, total; // total: the size of the copy
    int table;       // the voice set whose file this copy is from
    LangSeg seg[LANG_SEGS];
    u8* buf;
} LangScript;
static LangScript lang[LANG_MAX];
static int n_lang;
static CutsceneSymbolRange lang_symbols[LANG_MAX + 32];
static const char* const lang_files[2][VOICE_COUNT] = {
    {NULL, "script_jp_en.bin", "script_psp_en.bin"},
    {"script.bin", "script_jp.bin", "script_psp.bin"},
};
static int lang_table[VOICE_COUNT];        // voice set -> its table
static const char* lang_name[VOICE_COUNT]; // table -> its file, NULL: stock

const u8* Lang_File(const char* name, u32* len);

static u32 Rd32(const u8* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static u32 Fnv(const u8* p, u32 n) {
    u32 h = 0x811C9DC5;
    while (n--) {
        h = (h ^ *p++) * 0x01000193;
    }
    return h;
}

// a stock offset in the range -> the offset in the rebuilt copy
static u32 LangMap(const LangScript* s, u32 off) {
    u32 i, k, best = 0;
    const LangSeg* g;
    for (k = 0; k < s->nSeg; k++) {
        g = &s->seg[k];
        if (off >= g->stockOff && off < g->stockOff + g->stockLen) {
            off -= g->stockOff;
            for (i = 0; i < g->nMap; i++) {
                if (Rd32(g->map + i * 8) <= off) {
                    best = i;
                }
            }
            return g->newOff + Rd32(g->map + best * 8 + 4) + off -
                   Rd32(g->map + best * 8);
        }
    }
    g = &s->seg[s->nSeg - 1];
    return g->newOff + g->newLen + off - (g->stockOff + g->stockLen);
}

// the record of the table that matches the stock bytes at p, or NULL
static const u8* LangFind(const u8* f, u32 n, const u8* p, u32 avail) {
    const u8* r = f + 12;
    u32 i;
    for (i = 0; i < n; i++) {
        u32 stockLen = Rd32(r + 4), newLen = Rd32(r + 8);
        u32 nRel = Rd32(r + 12), nMap = Rd32(r + 16), nameLen = Rd32(r + 20);
        if (stockLen <= avail && Fnv(p, stockLen) == Rd32(r)) {
            return r;
        }
        r += 24 + ((nameLen + 3) & ~3) + ((newLen + 3) & ~3) + nRel * 8 +
             nMap * 8;
    }
    return NULL;
}

static const u8* LangTableFile(const char* name, u32* n) {
    u32 len;
    const u8* f = name ? Lang_File(name, &len) : NULL;
    if (!f || len < 12 || memcmp(f, "SOTNSCR1", 8)) {
        return NULL;
    }
    *n = Rd32(f + 8);
    return f;
}

// the copies of one table; returns the next free PSX address
static u32 LangCopies(
    int t, const CutsceneSymbolRange* symbols, int count, u32 at, int* out) {
    const u8 *f, *r;
    u32 n, k;
    int j;
    if (!(f = LangTableFile(lang_name[t], &n))) {
        return at;
    }
    for (j = 0; j < count && n_lang < LANG_MAX; j++) {
        const CutsceneSymbolRange* sym = &symbols[j];
        LangScript* s = &lang[n_lang];
        u32 o = 0, total = 0, base = NORMALIZE_PSX_ADDR(sym->psxAddr);
        s->nSeg = 0;
        while (s->nSeg < LANG_SEGS &&
               (r = LangFind(f, n, sym->pcAddr + o, sym->size - o))) {
            LangSeg* g = &s->seg[s->nSeg++];
            u32 newLen = Rd32(r + 8), nRel = Rd32(r + 12);
            g->stockOff = o;
            g->stockLen = Rd32(r + 4);
            g->newOff = total;
            g->newLen = newLen;
            g->nMap = Rd32(r + 16);
            g->map = r + 24 + ((Rd32(r + 20) + 3) & ~3) + ((newLen + 3) & ~3) +
                     nRel * 8;
            o += g->stockLen;
            total += newLen;
        }
        if (!s->nSeg) {
            continue;
        }
        total += sym->size - o;
        if (at + total > LANG_PSX_END) {
            fprintf(stderr, "[lang] %s: no PSX room for more copies\n",
                    lang_name[t]);
            break;
        }
        n_lang++;
        s->old = sym->pcAddr;
        s->size = sym->size;
        s->psx = sym->psxAddr;
        s->total = total;
        s->table = t;
        s->buf = malloc(total);
        for (k = 0; k < s->nSeg; k++) {
            const LangSeg* g = &s->seg[k];
            const u8* rec = LangFind(
                f, n, sym->pcAddr + g->stockOff, sym->size - g->stockOff);
            const u8* bytes = rec + 24 + ((Rd32(rec + 20) + 3) & ~3);
            const u8* rel = bytes + ((g->newLen + 3) & ~3);
            u32 i, nRel = Rd32(rec + 12);
            memcpy(s->buf + g->newOff, bytes, g->newLen);
            for (i = 0; i < nRel; i++) {
                u32 pos = g->newOff + Rd32(rel + i * 8);
                u32 tgt = NORMALIZE_PSX_ADDR(Rd32(rel + i * 8 + 4) | 0x100000);
                u32 x;
                if (tgt - base >= sym->size) {
                    continue; // outside the range: the stock word stays
                }
                x = NORMALIZE_PSX_ADDR(at) + LangMap(s, tgt - base);
                s->buf[pos] = (x & 0xFF000) >> 12;
                s->buf[pos + 1] = (x & 0xFF00) >> 8;
                s->buf[pos + 2] = (x & 0xFF0) >> 4;
                s->buf[pos + 3] = x & 0xFF;
            }
        }
        memcpy(
            s->buf + total - (sym->size - o), sym->pcAddr + o, sym->size - o);
        lang_symbols[*out].pcAddr = s->buf;
        lang_symbols[*out].psxAddr = at;
        lang_symbols[*out].size = total;
        (*out)++;
        at += (total + 0xFF) & ~0xFF;
    }
    return at;
}

static void LangSwap(const CutsceneSymbolRange** symbols, int* count) {
    u32 n, at = LANG_PSX_BASE;
    int j, v, out = 0;
    for (j = 0; j < n_lang; j++) {
        free(lang[j].buf);
    }
    n_lang = 0;
    // the table of each voice set: the same file is one table
    for (v = 0; v < VOICE_COUNT; v++) {
        const char* name = lang_files[g_Lang ? 1 : 0][v];
        if (!LangTableFile(name, &n)) {
            name = g_Lang ? lang_files[1][VOICE_PS1] : NULL;
        }
        lang_name[v] = name;
        lang_table[v] = v;
        for (j = 0; j < v; j++) {
            if (lang_name[j] == name ||
                (name && lang_name[j] && !strcmp(lang_name[j], name))) {
                lang_table[v] = j;
                break;
            }
        }
    }
    if (*count > 32) {
        return;
    }
    for (v = 0; v < VOICE_COUNT; v++) {
        if (lang_table[v] == v) {
            at = LangCopies(v, *symbols, *count, at, &out);
        }
    }
    fprintf(stderr,
            "[lang] %s: %d copies of %d cutscene ranges (tables ps1=%s jp=%s "
            "psp=%s)\n",
            g_Lang ? "uk" : "en", out, *count,
            lang_name[lang_table[0]] ? lang_name[lang_table[0]] : "stock",
            lang_name[lang_table[1]] ? lang_name[lang_table[1]] : "stock",
            lang_name[lang_table[2]] ? lang_name[lang_table[2]] : "stock");
    if (!out) {
        return;
    }
    // the copies first: their addresses are below every stage symbol
    memcpy(&lang_symbols[out], *symbols, *count * sizeof(**symbols));
    *symbols = lang_symbols;
    *count += out;
}

// 1 if voice/<set>/ holds clips (358.pcm, the first voice id; voice.c);
// without them the set plays the US voice
static int VoiceHasClips(int v) {
    static signed char have[VOICE_COUNT] = {1, -1, -1};
    if (have[v] < 0) {
        char path[32];
        FILE* f;
        snprintf(path, sizeof(path), "voice/%s/358.pcm", g_VoiceSetNames[v]);
        f = fopen(path, "rb");
        have[v] = f != NULL;
        if (f) {
            fclose(f);
        }
    }
    return have[v];
}

typedef struct {
    const u8* old; // the stock range
    u32 off, psx;  // the stock offset in it; its PSX address
} LangPos;

// p in a copy or a stock range -> the stock position. In a copy, p must be
// a native op (a map entry with the same byte).
static int LangLocate(const u8* p, LangPos* at) {
    int i;
    u32 k, m;
    for (i = 0; i < n_lang; i++) {
        const LangScript* s = &lang[i];
        const LangSeg* g;
        u32 n;
        if (p < s->buf || p >= s->buf + s->total) {
            continue;
        }
        n = p - s->buf;
        at->old = s->old;
        at->psx = s->psx;
        for (k = 0; k < s->nSeg; k++) {
            g = &s->seg[k];
            if (n < g->newOff || n >= g->newOff + g->newLen) {
                continue;
            }
            for (m = 0; m < g->nMap; m++) {
                u32 o = g->stockOff + Rd32(g->map + m * 8);
                if (Rd32(g->map + m * 8 + 4) == n - g->newOff && o < s->size &&
                    s->old[o] == *p) {
                    at->off = o;
                    return 1;
                }
            }
            return 0;
        }
        g = &s->seg[s->nSeg - 1];
        at->off = n - (g->newOff + g->newLen) + g->stockOff + g->stockLen;
        return 1;
    }
    for (i = 0; i < _symbolCount; i++) {
        const CutsceneSymbolRange* r = &_symbols[i];
        const u8* base = (const u8*)r->pcAddr;
        if (p >= base && p < base + r->size) {
            at->old = base;
            at->off = p - at->old;
            at->psx = r->psxAddr;
            return 1;
        }
    }
    return 0;
}

// FNV-1a of the text bytes (and line breaks) of a box, up to its end
static u32 BoxHash(const u8* p) {
    static const u8 args[25] = {0, 0, 1, 1, 0, 2, 0, 2, 0, 2, 0, 0, 4,
                                0, 0, 4, 1, 1, 0, 5, 2, 0, 1, 0, 1};
    u32 h = 0x811C9DC5;
    int n;
    for (n = 0; n < 512; n++) {
        u8 c = *p++;
        if (c >= 0x20 || c == CSOP_LINE_BREAK) {
            h = (h ^ c) * 0x01000193;
        } else if (c == CSOP_END_CUTSCENE || c == CSOP_NEXT_DIALOG ||
                   c == CSOP_SCRIPT_SWITCH || c >= LEN(args)) {
            break;
        } else {
            p += args[c];
        }
    }
    return h;
}

// the stock position -> the same place in the table of the voice set now
static u8* LangPick(const LangPos* at, const char* what) {
    int i, v = g_VoiceSet, t;
    const char* table = "stock";
    u8* p = (u8*)at->old + at->off;
    if (v != VOICE_PS1 && !VoiceHasClips(v)) {
        v = VOICE_PS1;
    }
    t = lang_table[v];
    for (i = 0; lang_name[t] && i < n_lang; i++) {
        if (lang[i].old == at->old && lang[i].table == t) {
            p = lang[i].buf + LangMap(&lang[i], at->off);
            table = lang_name[t];
            break;
        }
    }
    fprintf(
        stderr, "[script] %s table=%s voice=%s lang=%s at=0x%08X txt=%08X%s\n",
        what, table, g_VoiceSetNames[v], g_Lang ? "uk" : "en",
        at->psx + at->off, BoxHash(p),
        v != g_VoiceSet ? " (no clips of the set: ps1)" : "");
    return p;
}

// SetCutsceneScript: the first box
u8* Lang_ScriptStart(u8* script) {
    LangPos at;
    return LangLocate(script, &at) ? LangPick(&at, "start") : script;
}

// CSOP_NEXT_DIALOG: p is after the op; the next box
u8* Lang_NextBox(u8* p) {
    LangPos at;
    if (!LangLocate(p - 1, &at) || at.old[at.off] != CSOP_NEXT_DIALOG) {
        return p;
    }
    at.off++;
    return LangPick(&at, "box");
}

void CutscenePcAlloc(const CutsceneSymbolRange* symbols, int count) {
    int i;

    if (!symbols || count <= 0) {
        _symbols = NULL;
        _symbolCount = 0;
        return;
    }

    LangSwap(&symbols, &count);
    _symbols = symbols;
    _symbolCount = count;
    for (i = 1; i < count; i++) {
        // catch overlapping symbols
        u32 prevEnd =
            NORMALIZE_PSX_ADDR(symbols[i - 1].psxAddr) + symbols[i - 1].size;
        if (NORMALIZE_PSX_ADDR(symbols[i].psxAddr) < prevEnd) {
            ERRORF("cutscene symbol at 0x%08x overlaps the previous entry "
                   "ending at 0x%08x; a registered size is wrong",
                   symbols[i].psxAddr, prevEnd);
        }
    }
}

u8* CutsceneAddrToPc(u32 psxAddr) {
    u32 addr = NORMALIZE_PSX_ADDR(psxAddr);
    for (int i = 0; i < _symbolCount; i++) {
        u32 base = NORMALIZE_PSX_ADDR(_symbols[i].psxAddr);
        if (addr >= base && addr < base + _symbols[i].size) {
            return (u8*)_symbols[i].pcAddr + (addr - base);
        }
    }
    ERRORF("address 0x%08x not found", psxAddr);
    return NULL;
}
