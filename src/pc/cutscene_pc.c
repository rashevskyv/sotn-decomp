// SPDX-License-Identifier: AGPL-3.0-or-later
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc.h"

static const CutsceneSymbolRange* _symbols = NULL;
static int _symbolCount = 0;

// normalize 0x8018xxxx ptr to 0x0018xxxx
#define NORMALIZE_PSX_ADDR(x) ((x) & 0x0FFFFFFF)

// language = uk: the stage scripts rebuilt with the Ukrainian boxes
// (script.bin, script_psp.bin; tools/lang/FORMAT.md). A registered range
// that starts with one or more scripts of the records, one after another,
// is copied as the rebuilt scripts plus the rest of the range (the events),
// at a PSX address of its own from 0x80140000, where no stage symbol lives.
// The stock range stays.
#define LANG_PSX_BASE 0x80140000
#define LANG_MAX 8
#define LANG_SEGS 4
typedef struct {
    u32 stockOff, stockLen, newOff, newLen, nMap;
    const u8* map; // nMap x {u32 old, new}, offsets in the script
} LangSeg;
typedef struct {
    const u8* old;
    u32 size, nSeg;
    LangSeg seg[LANG_SEGS];
    u8* buf;
} LangScript;
static LangScript lang[LANG_MAX];
static int n_lang;
static CutsceneSymbolRange lang_symbols[LANG_MAX + 32];

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

// the record of script.bin that matches the stock bytes at p, or NULL
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

static void LangSwap(const CutsceneSymbolRange** symbols, int* count) {
    const u8 *f, *r;
    u32 len, n, k, at = LANG_PSX_BASE;
    int j, out = 0;
    for (j = 0; j < n_lang; j++) {
        free(lang[j].buf);
    }
    n_lang = 0;
    if (!g_Lang || *count > 32) {
        return;
    }
    f = Lang_File(
        g_VoiceSet == VOICE_PSP ? "script_psp.bin" : "script.bin", &len);
    if (!f || len < 12 || memcmp(f, "SOTNSCR1", 8)) {
        return;
    }
    n = Rd32(f + 8);
    for (j = 0; j < *count && n_lang < LANG_MAX; j++) {
        const CutsceneSymbolRange* sym = &(*symbols)[j];
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
            g->map = r + 24 + ((Rd32(r + 20) + 3) & ~3) +
                     ((newLen + 3) & ~3) + nRel * 8;
            o += g->stockLen;
            total += newLen;
        }
        if (!s->nSeg) {
            continue;
        }
        n_lang++;
        s->old = sym->pcAddr;
        s->size = sym->size;
        total += sym->size - o;
        s->buf = malloc(total);
        for (k = 0; k < s->nSeg; k++) {
            const LangSeg* g = &s->seg[k];
            const u8* rec = LangFind(f, n, sym->pcAddr + g->stockOff,
                                     sym->size - g->stockOff);
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
        memcpy(s->buf + total - (sym->size - o), sym->pcAddr + o,
               sym->size - o);
        lang_symbols[out].pcAddr = s->buf;
        lang_symbols[out].psxAddr = at;
        lang_symbols[out].size = total;
        out++;
        at += (total + 0xFF) & ~0xFF;
    }
    for (j = 0, k = 0; j < n_lang; j++) {
        k += lang[j].nSeg;
    }
    fprintf(stderr,
            "[lang] uk: %d of %d cutscene ranges use %u rebuilt scripts\n",
            out, *count, k);
    if (!out) {
        return;
    }
    // the copies first: their addresses are below every stage symbol
    memcpy(&lang_symbols[out], *symbols, *count * sizeof(**symbols));
    *symbols = lang_symbols;
    *count += out;
}

// SetCutsceneScript: a pointer into a stock script -> the rebuilt copy
u8* Lang_ScriptStart(u8* script) {
    int i;
    for (i = 0; i < n_lang; i++) {
        const LangScript* s = &lang[i];
        if (script >= s->old && script < s->old + s->size) {
            return s->buf + LangMap(s, script - s->old);
        }
    }
    return script;
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
