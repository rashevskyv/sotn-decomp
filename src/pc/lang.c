// SPDX-License-Identifier: AGPL-3.0-or-later
// The Ukrainian language (tools/lang, formats in tools/lang/FORMAT.md).
// sotn.ini `language = uk` turns it on; the language is fixed at boot.
// The pack g_LangPackUk is built into the program by CMake. With an empty
// pack, uk stays English. Under en only the serif Latin cells of the pause
// menu (tools/lang/serif_latin.py: letters, digits, marks and the stat
// arrows MENU_CELL_UP, MENU_CELL_DOWN) come from the pack.
#include <game.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dra/dra.h"

extern const unsigned int g_LangPackUkSize;
extern const unsigned char g_LangPackUk[];
extern const char* g_MenuStr[];
extern const char* D_800A2D48[];
void Lang_SetStatusLabels(const char* glyphs, const char* att, const char* def);

int g_Lang;     // 0 English, 1 Ukrainian: the language of this run
int g_LangNext; // the sotn.ini value, used at the next boot

enum {
    T_MENU,
    T_EQUIP_NAME,
    T_EQUIP_DESC,
    T_ACC_NAME,
    T_ACC_DESC,
    T_ENEMY,
    T_SPELL_NAME,
    T_SPELL_DESC,
    T_RELIC_NAME,
    T_RELIC_DESC,
    T_HELP,
    T_STATUS,
    T_SEL_AREA,
    T_ACTOR,
    T_BESTIARY,
    T_OTHER_CELL,
    T_OTHER_SJIS,
    T_ZONE, // the area names: the teleport list of the port menu (Lang_Zone)
    T_PORT
};
#define NO_INDEX 0xFFFF

typedef struct {
    u16 table, index;
    u32 hash;
    const char* text; // the pack bytes + two zero bytes
} LangEntry;

static LangEntry* entries;
static int n_entries;
static const u8* font8;  // n x {u8 code; u8 rows[8][4]}
static int n_font8;
static const u8* glyphs; // n x {u16 code, flags; u8 rows[30]}
static int n_glyphs;
static const u8* dfont[256]; // the dialogue font: 8x11 cells, 4 bytes a row
static int n_dfont;
static const u8* mfont[256]; // the pause menu's serif cells by 8x8 cell code
static u16 msjis[256]; // the 12x16 font code of a uk mfont cell (bytes 2, 3)
static int n_mfont;
typedef struct {
    RECT rect;
    u32 first, whole, hits;
    u16* texels;
    const char* name; // in the pack, after its length byte
} Art;
static Art art[64]; // art.bin records with a stock hash
static int n_art;
static Art gothic; // art.bin "gothic": the drawn gothic letters (no stock hash)

static u32 Rd32(const u8* p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 Rd16(const u8* p) { return p[0] | p[1] << 8; }

const u8* Lang_File(const char* name, u32* len) {
    const u8* p = g_LangPackUk;
    u32 i, n;
    if (g_LangPackUkSize < 12 || memcmp(p, "SOTNPACK", 8)) {
        return NULL;
    }
    n = Rd32(p + 8);
    for (i = 0; i < n; i++) {
        const u8* e = p + 12 + i * 24;
        if (!strncmp((const char*)e, name, 16)) {
            *len = Rd32(e + 20);
            return p + Rd32(e + 16);
        }
    }
    return NULL;
}

// FNV-1a. Cell strings are hashed up to and with the 0xFF end mark,
// Shift-JIS strings up to the NUL.
static u32 Hash(const char* s, int cell) {
    u32 h = 0x811C9DC5;
    const u8* p = (const u8*)s;
    for (; cell ? 1 : *p != 0; p++) {
        h = (h ^ *p) * 0x01000193;
        if (cell && *p == 0xFF) {
            break;
        }
    }
    return h;
}

// entries are sorted by table, hash, index
static const char* Find(int table, u32 hash, int index) {
    int lo = 0, hi = n_entries;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        LangEntry* e = &entries[mid];
        if (e->table < table || (e->table == table && e->hash < hash)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < n_entries && entries[lo].table == table && entries[lo].hash == hash; lo++) {
        if (entries[lo].index == index) {
            return entries[lo].text;
        }
    }
    return NULL;
}

static int n_swap, n_keep, keep_by[T_OTHER_SJIS + 1];

static void Swap(const char** p, int table, int cell) {
    const char* ua;
    if (!*p || !**p && !cell) {
        return;
    }
    ua = Find(table, Hash(*p, cell), NO_INDEX);
    if (ua) {
        *p = ua;
        n_swap++;
    } else {
        n_keep++;
        keep_by[table]++;
    }
}

// mfont.bin: the pause menu's serif cells; under en only the shared ones
// (byte 1 of a record = 1): the Latin letters, digits, marks, stat arrows
static void LoadMenuCells(bool sharedOnly) {
    u32 len, i;
    const u8* p = Lang_File("mfont.bin", &len);
    if (p && len >= 12 && !memcmp(p, "SOTNMFN1", 8)) {
        u32 n = Rd32(p + 8);
        for (i = 0; i < n; i++) {
            u8 c = p[12 + i * 48];
            if (!sharedOnly || (p[12 + i * 48 + 1] & 1)) {
                mfont[c] = p + 12 + i * 48 + 4;
                msjis[c] = sharedOnly ? 0
                                      : p[12 + i * 48 + 2] << 8 |
                                            p[12 + i * 48 + 3];
                n_mfont++;
            }
        }
    }
}

static bool LoadText(void) {
    u32 len, i;
    const u8* p = Lang_File("text.bin", &len);
    const u8* blob;
    if (!p || len < 16 || memcmp(p, "SOTNLANG", 8) || Rd32(p + 8) != 2) {
        return false;
    }
    n_entries = Rd32(p + 12);
    blob = p + 16 + n_entries * 16;
    entries = calloc(n_entries, sizeof(LangEntry));
    for (i = 0; i < (u32)n_entries; i++) {
        const u8* e = p + 16 + i * 16;
        u16 n = Rd16(e + 12);
        char* s = calloc(n + 2, 1);
        memcpy(s, blob + Rd32(e + 8), n);
        entries[i].table = Rd16(e);
        entries[i].hash = Rd32(e + 4);
        entries[i].index = Rd16(e + 14);
        entries[i].text = s;
    }
    p = Lang_File("font8.bin", &len);
    if (p && len >= 12 && !memcmp(p, "SOTNFNT8", 8)) {
        font8 = p + 12;
        n_font8 = Rd32(p + 8);
    }
    p = Lang_File("dfont.bin", &len);
    if (p && len >= 12 && !memcmp(p, "SOTNDFN1", 8)) {
        u32 n = Rd32(p + 8);
        for (i = 0; i < n; i++) {
            dfont[p[12 + i * 48]] = p + 12 + i * 48 + 4;
            n_dfont++;
        }
    }
    LoadMenuCells(false);
    p = Lang_File("bios.bin", &len);
    if (p && len >= 12 && !memcmp(p, "SOTNBIOS", 8)) {
        glyphs = p + 12;
        n_glyphs = Rd32(p + 8);
    }
    p = Lang_File("art.bin", &len);
    if (p && len >= 12 && !memcmp(p, "SOTNART1", 8)) {
        u32 n = Rd32(p + 8);
        p += 12;
        for (i = 0; i < n && n_art < LEN(art); i++) {
            u16 w = Rd16(p + 4), h = Rd16(p + 6);
            u8 nl = p[17];
            // a stock hash proves the place on this run; the sheet records
            // are the 8x8 cells, which font8.bin already paints
            if (nl == 6 && !memcmp(p + 18, "gothic", 6)) {
                gothic.rect.x = Rd16(p);
                gothic.rect.y = Rd16(p + 2);
                gothic.rect.w = w;
                gothic.rect.h = h;
                gothic.texels = malloc(w * h * 2);
                memcpy(gothic.texels, p + 18 + nl, w * h * 2);
            }
            if (p[16] && Rd32(p + 12) && strncmp((const char*)p + 18, "sheet", 5)) {
                Art* a = &art[n_art++];
                a->rect.x = Rd16(p);
                a->rect.y = Rd16(p + 2);
                a->rect.w = w;
                a->rect.h = h;
                a->first = Rd32(p + 8);
                a->whole = Rd32(p + 12);
                a->name = (const char*)p + 18;
                a->texels = malloc(w * h * 2);
                memcpy(a->texels, p + 18 + nl, w * h * 2);
            }
            p += 18 + nl + w * h * 2;
        }
    }
    return true;
}

// FNV-1a of h rows of w halfwords, stride halfwords apart
static u32 HashRows(const u8* p, int w, int h, int stride, u32 hash) {
    int x, y;
    for (y = 0; y < h; y++, p += stride * 2) {
        for (x = 0; x < w * 2; x++) {
            hash = (hash ^ p[x]) * 0x01000193;
        }
    }
    return hash;
}

// The Ukrainian pictures: after an upload that holds the stock pixels of an
// art.bin record at its VRAM place, upload the record over them.
int __real_LoadImage(RECT* rect, u_long* p);
int __wrap_LoadImage(RECT* rect, u_long* p) {
    int i, ret = __real_LoadImage(rect, p);
    for (i = 0; g_Lang && i < n_art; i++) {
        Art* a = &art[i];
        const u8* src;
        if (a->rect.x < rect->x || a->rect.y < rect->y ||
            a->rect.x + a->rect.w > rect->x + rect->w ||
            a->rect.y + a->rect.h > rect->y + rect->h) {
            continue;
        }
        src = (const u8*)p +
              ((a->rect.y - rect->y) * rect->w + a->rect.x - rect->x) * 2;
        if (HashRows(src, a->rect.w, 1, rect->w, 0x811C9DC5) == a->first &&
            HashRows(src, a->rect.w, a->rect.h, rect->w, 0x811C9DC5) ==
                a->whole) {
            __real_LoadImage(&a->rect, (u_long*)a->texels);
            if (!a->hits++) {
                fprintf(stderr, "[lang] uk: picture %.*s placed\n",
                        a->name[-1], a->name);
            }
        }
    }
    return ret;
}

// "АТК" and "ЗАХ" from the five glyphs func_800F98AC loads for the status
// page: А Т К З Х (Shift-JIS 8440 8453 844B 8448 8456).
static const char status_glyphs[] = "\x84\x40\x84\x53\x84\x4B\x84\x48\x84\x56";

void Lang_Init(void) {
    int i;
    g_Lang = 0;
    if (!g_LangNext) {
        LoadMenuCells(true);
        return;
    }
    if (!LoadText()) {
        fprintf(stderr, "[lang] uk: the pack is empty, English stays\n");
        return;
    }
    g_Lang = 1;
    for (i = 0; i < n_entries; i++) {
        LangEntry* e = &entries[i];
        if (e->table == T_MENU && e->index != NO_INDEX &&
            Hash(g_MenuStr[e->index], 1) == e->hash) {
            g_MenuStr[e->index] = e->text;
            n_swap++;
        }
    }
    for (i = 0; i < LEN(g_EquipDefs); i++) {
        Swap(&g_EquipDefs[i].name, T_EQUIP_NAME, 1);
        Swap(&g_EquipDefs[i].description, T_EQUIP_DESC, 0);
    }
    for (i = 0; i < LEN(g_AccessoryDefs); i++) {
        Swap(&g_AccessoryDefs[i].name, T_ACC_NAME, 1);
        Swap(&g_AccessoryDefs[i].description, T_ACC_DESC, 0);
    }
    for (i = 0; i < LEN(g_EnemyDefs); i++) {
        Swap(&g_EnemyDefs[i].name, T_ENEMY, 1);
    }
    for (i = 0; i < LEN(g_SpellDefs); i++) {
        Swap(&g_SpellDefs[i].name, T_SPELL_NAME, 0);
        Swap((const char**)&g_SpellDefs[i].description, T_SPELL_DESC, 0);
    }
    for (i = 0; i < LEN(g_RelicDefs); i++) {
        Swap(&g_RelicDefs[i].name, T_RELIC_NAME, 0);
        Swap((const char**)&g_RelicDefs[i].desc, T_RELIC_DESC, 0);
    }
    for (i = 1; i < 7; i++) {
        Swap(&D_800A2D48[i], T_HELP, 0);
    }
    Lang_SetStatusLabels(status_glyphs, "\x00\x01\x02\xFF", "\x03\x00\x04\xFF");
    fprintf(
        stderr,
        "[lang] uk: %d texts, %d strings swapped, %d kept, %d 8x8 cells, "
        "%d glyphs, %d dialogue cells, %d menu cells, %d pictures\n",
        n_entries, n_swap, n_keep, n_font8, n_glyphs, n_dfont, n_mfont, n_art);
    fprintf(stderr, "[lang] uk: kept by table:");
    for (i = 0; i < LEN(keep_by); i++) {
        fprintf(stderr, " %d", keep_by[i]);
    }
    fprintf(stderr, "\n");
}

// Draw-time lookups for strings that live outside the DRA tables.
const char* Lang_Cell(const char* s) {
    const char* ua;
    if (!g_Lang || !s) {
        return s;
    }
    ua = Find(T_OTHER_CELL, Hash(s, 1), NO_INDEX);
    return ua ? ua : s;
}

const char* Lang_Sjis(const char* s) {
    const char* ua;
    u32 h;
    if (!g_Lang || !s) {
        return s;
    }
    h = Hash(s, 0);
    ua = Find(T_OTHER_SJIS, h, NO_INDEX);
    if (!ua) {
        ua = Find(T_BESTIARY, h, NO_INDEX);
    }
    return ua ? ua : s;
}

// A row of a positional table (SEL_AREA: index 2 * stage + line).
const char* Lang_Row(int table, int index, const char* s) {
    const char* ua;
    if (!g_Lang || !s) {
        return s;
    }
    ua = Find(table, Hash(s, 1), index);
    return ua ? ua : s;
}

// The gothic words of the file select (src/st/sel/utils.c DrawString16x16),
// the owner's choice in the Xbox 360 port (ps1_code.GOTHIC_WORDS). A letter
// with a Latin look-alike is the stock lowercase cell; 'A' + k is the k-th
// cell of the drawn page (art.bin "gothic", GOTHIC_LETTERS).
static const char* const gothic_words[][2] = {
    {"select", "oAepF"},      // ОБЕРИ
    {"your", "cboK"},         // СВОЮ
    {"destiny", "CoHK"},      // ДОЛЮ
    {"new game", "hoba Bpa"}, // НОВА ГРА
    {"used", "EaGhLto"},      // ЗАЙНЯТО
    {"no data", "IopoDhJo"},  // ПОРОЖНЬО
    {"no", "AeE"},            // БЕЗ
    {"card", "hociL"},        // НОСІЯ
    // port words ('#': no stock string or typed name starts so): the plate
    // captions of the file select and the names of the player select
    {"#card", "hociG"},     // НОСІЙ
    {"#1", "M"},            // 1
    {"#2", "N"},            // 2
    {"#name", "im'L"},      // ІМ'Я
    {"#alucard", "aHykapC"}, // АЛУКАРД
    {"#richter", "pixtep"},  // РІХТЕР
    {"#maria", "mapiL"},     // МАРІЯ
};
// the cells of the drawn page, 8 a row: Б Г Д Ж З И Й Л П Ь Ю Я 1 2

// en: only the port words, in the stock cells; the digits 1 and 2 (cells 12
// and 13 of the drawn page, from tools/lang/uk/labels/gothic.png) on an
// otherwise empty page, so its HD file has one fixed content
static const char* const gothic_words_en[][2] = {
    {"#card", "card"},       {"#1", "M"},
    {"#2", "N"},             {"#name", "name"},
    {"#alucard", "alucard"}, {"#richter", "richter"},
    {"#maria", "maria"},     {"#player", "player select"},
};
static const u16 gothic_digits[16][8] = {
    {0x0000, 0x0000, 0x0032, 0x0000, 0x0000, 0x2000, 0x0000, 0x0000},
    {0x0000, 0x4300, 0x0245, 0x0000, 0x0000, 0x5430, 0x0045, 0x0000},
    {0x0000, 0x5543, 0x0035, 0x0000, 0x0000, 0x5553, 0x2455, 0x0000},
    {0x0000, 0x5423, 0x0034, 0x0000, 0x0000, 0x4344, 0x3555, 0x0000},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x3000, 0x2024, 0x3554, 0x0000},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x0000, 0x0003, 0x3453, 0x0000},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x0000, 0x0000, 0x2453, 0x0000},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x0000, 0x0000, 0x0344, 0x0000},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x0000, 0x3000, 0x0034, 0x0000},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x0000, 0x4300, 0x0002, 0x0003},
    {0x0000, 0x5400, 0x0034, 0x0000, 0x0000, 0x2430, 0x3222, 0x0024},
    {0x0000, 0x5520, 0x0245, 0x0000, 0x0000, 0x5553, 0x5555, 0x0003},
    {0x0000, 0x5542, 0x2455, 0x0000, 0x3000, 0x5555, 0x4555, 0x0003},
    {0x0000, 0x3442, 0x2343, 0x0000, 0x4200, 0x3334, 0x4333, 0x0002},
    {0x0000, 0x2220, 0x0222, 0x0000, 0x0000, 0x0022, 0x2000, 0x0000},
    {0},
};

static const char* Lang_GothicEn(const char* word) {
    static u16 page[32 * 32];
    RECT r = {832, 256, 32, 32};
    int i, y;
    if (word[0] != '#') {
        return NULL;
    }
    for (i = 0; i < LEN(gothic_words_en); i++) {
        if (!strcmp(word, gothic_words_en[i][0])) {
            if (!page[16 * 32 + 18]) {
                for (y = 0; y < 16; y++) {
                    memcpy(page + (16 + y) * 32 + 16, gothic_digits[y], 16);
                }
            }
            LoadImage(&r, (u_long*)page);
            return gothic_words_en[i][1];
        }
    }
    return NULL;
}

const char* Lang_Gothic(const char* word) {
    int i;
    if (!g_Lang) {
        return Lang_GothicEn(word);
    }
    if (!gothic.texels) {
        return NULL;
    }
    for (i = 0; i < LEN(gothic_words); i++) {
        if (!strcmp(word, gothic_words[i][0])) {
            // the page is free on the file select: write it before a draw,
            // as nothing restores it after a stage used that VRAM
            LoadImage(&gothic.rect, (u_long*)gothic.texels);
            return gothic_words[i][1];
        }
    }
    return NULL;
}

// The name entry shows the file-select cancel label, but there the button
// deletes a letter: row 1 of that cell has the word (sotn_lang.py pack).
const char* Lang_NameDelete(const char* s) {
    return Lang_Row(T_OTHER_CELL, 1, s);
}

// One dialogue character into the line buffer: the stage code moves the
// 8x8 cell `rect` of the sheet to x, y. Under uk a byte 0x80.. is a cell of
// the dialogue font (8x11, baseline row 9), and the 8x8 cells (baseline
// row 6) go 3 rows down to the same baseline. The line slots are 12 rows.
// Under en an ASCII byte takes the serif cell of the menu (mfont.bin shared
// cells, sheet cell b - 0x20), as the shop and the status page do.
void Lang_DialogueGlyph(RECT* rect, s32 x, s32 y) {
    u8 b;
    RECT r;
    u16 buf[22];
    const u8* cell;
    b = ((rect->y - 240) / 8) << 4 | ((rect->x - 896) / 2 & 15);
    if (g_Lang) {
        cell = dfont[b];
    } else {
        cell = b > 0x20 && b < 0x80 && b - 0x20 != MENU_CELL_LT &&
                       b - 0x20 != MENU_CELL_GT
                   ? mfont[b - 0x20]
                   : NULL;
    }
    if (!cell) {
        MoveImage(rect, x, y + 3);
        return;
    }
    memcpy(buf, cell, 44);
    r.x = x;
    r.y = y;
    r.w = 2;
    r.h = 11;
    LoadImage(&r, (u_long*)buf);
}

// The right edge (exclusive x) of the blue cutscene box, stock 248. A uk
// line has up to 23 cells: its ink ends at x 254 (text sprite at 64, first
// cell at 72, 8 px a cell). The uk box ends at 257, so 2 px of blue follow
// the text, as 2 px lie between the box and the portrait (x 6). Its border
// tile (x 3, width = edge - 2, 255 at most) keeps 3 px off the view edge,
// as on the left: a 256 px view gets 252, and the text there still passes.
// The en scripts of the JP and PSP EN voice sets (PSP EN and JP-en text)
// have lines of up to 24 cells (tools/lang/sotn_lang.py DLG_LINE_CELLS): the
// box takes the uk width (the border tile width is a u8, so 257 at most).
s32 Lang_DialogueBoxRight(void) {
    return g_Lang || g_VoiceSet != VOICE_PS1 ? MIN(257, 252 + g_WideX) : 248;
}

// px the text line sprite moves left, so 24 cells end at the box edge: the
// line at x 60 + 4 - shift, its first cell 8 px on, 24 cells of 8 px end
// at 264 - shift. 8 in a box to 257, 12 in a 256 px view (box to 252). The
// portrait (x 6..54) keeps its place; the ink starts 10 or 6 px after it.
s32 Lang_DialogueTextShift(void) {
    return g_Lang || g_VoiceSet == VOICE_PS1
               ? 0
               : MIN(12, 265 - Lang_DialogueBoxRight());
}

// The owner's serif 12x16 design for a stock Cyrillic code, or NULL.
const u8* Lang_Glyph(u16 ch) {
    int i;
    if (!g_Lang) {
        return NULL;
    }
    for (i = 0; i < n_glyphs; i++) {
        const u8* g = glyphs + i * 34;
        if (Rd16(g) == ch && Rd16(g + 2) == 1) {
            return g + 4;
        }
    }
    return NULL;
}

// The Ukrainian 8x8 letters over the cells 0x66..0x97 of the font sheet at
// VRAM 896,256 (cell c at x 896 + (c & 15) * 2, y 256 + (c >> 4) * 8).
void Lang_PaintFont8(void) {
    int i;
    u16 buf[16];
    RECT rect;
    if (!g_Lang) {
        return;
    }
    for (i = 0; i < n_font8; i++) {
        const u8* c = font8 + i * 33;
        memcpy(buf, c + 1, 32);
        rect.x = 896 + (c[0] & 15) * 2;
        rect.y = 256 + (c[0] >> 4) * 8;
        rect.w = 2;
        rect.h = 8;
        LoadImage(&rect, (u_long*)buf);
    }
    // cell 0x98: З of the pause menu strings (the menu digit 3 is serif
    // there), the stock 3 for the other 8x8 drawers (the pick-up line)
    rect.x = 896 + (CH('3') & 15) * 2;
    rect.y = 256 + (CH('3') >> 4) * 8;
    rect.w = 2;
    rect.h = 8;
    MoveImage(&rect, 896 + (0x98 & 15) * 2, 256 + (0x98 >> 4) * 8);
    while (DrawSync(1)) {
    }
}

// The pause menu (uk; en with the Latin cells) draws its cells 8x11 from an
// atlas: the serif cells of mfont.bin, and the sheet's own 8x8 cell 3 rows
// down for the rest (the baselines meet). Cell c is at u (c & 15) * 8,
// v (c >> 4) * 11 of
// texture page 0x1D; the button marks of src/pc/buttons.c follow from
// v BUTTON_MARKS_V, a dotted row at v MENU_DOTS_V. The menu opens over that VRAM, which a stage may use:
// it is saved on open and put back on close.
#define MENU_FONT_X 832
#define MENU_FONT_Y 256
#define MENU_FONT_H (MENU_DOTS_V + 1)
static int menu_font_on;
static u16 atlas[32 * MENU_FONT_H];
// the mark CLUT (16 halfwords from u BUTTON_CLUT_U) lies where no mark is
_Static_assert(BUTTON_SETS * BUTTON_MARKS % 8 &&
                   BUTTON_SETS * BUTTON_MARKS % 8 * 16 <= BUTTON_CLUT_U &&
                   (MENU_FONT_X * 4 + BUTTON_CLUT_U) % 64 == 0,
               "mark CLUT spot");

int Lang_MenuFont(int on) {
    static u16 saved[32 * MENU_FONT_H];
    static u16 sheet[32 * 128];
    RECT r = {MENU_FONT_X, MENU_FONT_Y, 32, MENU_FONT_H};
    RECT s = {896, 256, 32, 128};
    int c, y;
    if (!n_mfont || on == menu_font_on) {
        return menu_font_on;
    }
    if (on) {
        StoreImage(&r, (u_long*)saved);
        StoreImage(&s, (u_long*)sheet);
        DrawSync(0);
        memset(atlas, 0, sizeof(atlas));
        for (c = 0; c < 256; c++) {
            u16* dst = atlas + (c >> 4) * 11 * 32 + (c & 15) * 2;
            for (y = 0; y < 11; y++) {
                if (mfont[c]) {
                    memcpy(dst + y * 32, mfont[c] + y * 4, 4);
                } else if (y >= 3) {
                    memcpy(dst + y * 32,
                           sheet + ((c >> 4) * 8 + y - 3) * 32 + (c & 15) * 2,
                           4);
                }
            }
        }
        // the dots take the white of the stock arrows (cell 0xE0): its most
        // frequent ink; the serif cells' ink 6 is a light grey. The marks
        // have their own CLUT: 1 that white, then g_ButtonRgb.
        {
            int n[16] = {0}, ink = 6;
            u16 font_clut[16], *mark_clut;
            RECT f = {(g_ClutIds[0x196] & 0x3F) << 4, g_ClutIds[0x196] >> 6,
                      16, 1};
            for (y = 0; y < 16; y++) { // 8 rows of 2 halfwords
                c = sheet[((0xE0 >> 4) * 8 + (y >> 1)) * 32 + (y & 1)];
                for (; c; c >>= 4) {
                    n[c & 15]++;
                }
            }
            for (c = 1; c < 16; c++) {
                ink = n[c] > n[ink] ? c : ink;
            }
            Buttons_Paint(atlas + BUTTON_MARKS_V * 32, 32);
            for (c = 0; c < 32; c++) { // a dot every other texel
                atlas[MENU_DOTS_V * 32 + c] = ink | ink << 8;
            }
            StoreImage(&f, (u_long*)font_clut);
            DrawSync(0);
            mark_clut = atlas + BUTTON_CLUT_V * 32 + BUTTON_CLUT_U / 4;
            mark_clut[1] = font_clut[ink];
            for (c = 2; c < BUTTON_INKS; c++) { // the bit 15 of the white
                mark_clut[c] = (font_clut[ink] & 0x8000) |
                               g_ButtonRgb[c][0] >> 3 |
                               g_ButtonRgb[c][1] >> 3 << 5 |
                               g_ButtonRgb[c][2] >> 3 << 10;
            }
            g_ClutIds[BUTTON_CLUT] =
                GetClut(MENU_FONT_X + BUTTON_CLUT_U / 4,
                        MENU_FONT_Y + BUTTON_CLUT_V);
        }
        LoadImage(&r, (u_long*)atlas);
    } else {
        LoadImage(&r, (u_long*)saved);
    }
    DrawSync(0);
    menu_font_on = on;
    return on;
}

int Lang_MenuFontOn(void) { return menu_font_on; }

// uk: the 11 rows (4 bytes, 4 bits a pixel) of a serif menu cell, or NULL
const u8* Lang_MenuCell(int code) { return g_Lang ? mfont[code & 0xFF] : NULL; }

// the same under en too: there only the shared cells (the stat arrows)
const u8* Lang_MenuCellAny(int code) { return mfont[code & 0xFF]; }

void Lang_MenuCellRows(int code, u8 rows[44]) {
    int y;
    code &= 0xFF;
    for (y = 0; y < 11; y++) {
        memcpy(rows + y * 4,
               atlas + ((code >> 4) * 11 + y) * 32 + (code & 15) * 2, 4);
    }
}

// The speaker name of a cutscene box (src/st/cutscene_actor_name.h), the
// owner's mockup B of step 77: the serif 8x11 menu cells (mfont.bin; a cell
// with no record takes its 8x8 sheet cell 3 rows down), 8 px apart, a space
// 4 px; gold from light at the top to dark at the bottom, a dark shadow 1 px
// right and down. The name goes once into a 4bpp strip of 48x12 halfwords at
// NAME_X, NAME_Y with its CLUT at NAME_X + 48 (slot NAME_CLUT), one sprite
// shows it. Rows 496..507 at x 0..63 are free in every stage: below the
// line buffers (0,384..), outside all draw areas (240 rows high) and the CLUT
// rows 240..255; only the whole-screen g_Vram clears reach it, never while a
// box is open. A VRAM survey of all 56 stage ids found the rect empty.
#define NAME_X 0
#define NAME_Y 496
#define NAME_CLUT 0x3FE
_Static_assert(NAME_X % 64 == 0 && (NAME_Y & 255) + 12 <= 256,
               "the strip and its CLUT in one texture page");
#define NAME_TEXELS 192 // 48 halfwords
static const u8 name_gold[11][3] = {
    {248, 224, 160}, {240, 216, 152}, {232, 208, 144}, {232, 200, 136},
    {224, 192, 120}, {224, 184, 112}, {216, 176, 104}, {208, 168, 88},
    {208, 160, 80},  {200, 152, 72},  {200, 144, 64}};
#define NAME_SHADOW 15 // ink 1 + row: gold; 15: the shadow (16, 8, 32)

void Lang_ActorName(const char* s, Primitive* prim) {
    static u8 tx[12][NAME_TEXELS + 1];
    static u16 strip[48 * 12];
    u16 clut[16] = {0};
    RECT r = {NAME_X, NAME_Y, 48, 12};
    RECT c = {NAME_X + 48, NAME_Y, 16, 1};
    u8 cell[44];
    int x = 0, y, i;
    u8 ch;
    memset(tx, 0, sizeof(tx));
    for (;;) {
        ch = *s++;
        if (ch == DIAG_EOL && (ch = *s++) == DIAG_EOS) {
            break;
        }
        if (ch == CH(' ')) {
            x += FONT_SPACE;
            continue;
        }
        if (x + 9 > NAME_TEXELS) {
            break;
        }
        if (mfont[ch]) {
            memcpy(cell, mfont[ch], 44);
        } else { // the sheet cell (VRAM 896,256) 3 rows down
            RECT k = {896 + (ch & 15) * 2, 256 + (ch >> 4) * 8, 2, 8};
            memset(cell, 0, 12);
            StoreImage(&k, (u_long*)(cell + 12));
            DrawSync(0);
        }
        for (y = 0; y < 11; y++) {
            for (i = 0; i < 8; i++) {
                if (cell[y * 4 + i / 2] >> (i & 1) * 4 & 15) {
                    tx[y][x + i] = 1 + y;
                }
            }
        }
        x += FONT_GAP;
    }
    for (y = 11; y > 0; y--) { // bottom up: only gold casts a shadow
        for (i = NAME_TEXELS; i > 0; i--) {
            if (!tx[y][i] && tx[y - 1][i - 1]) {
                tx[y][i] = NAME_SHADOW;
            }
        }
    }
    for (y = 0; y < 12; y++) {
        for (i = 0; i < 48; i++) {
            u8* t = &tx[y][i * 4];
            strip[y * 48 + i] = t[0] | t[1] << 4 | t[2] << 8 | t[3] << 12;
        }
    }
    for (i = 0; i < 11; i++) {
        clut[1 + i] = name_gold[i][0] >> 3 | name_gold[i][1] >> 3 << 5 |
                      name_gold[i][2] >> 3 << 10;
    }
    clut[NAME_SHADOW] = 16 >> 3 | 8 >> 3 << 5 | 32 >> 3 << 10;
    LoadImage(&r, (u_long*)strip);
    LoadImage(&c, (u_long*)clut);
    DrawSync(0);
    g_ClutIds[NAME_CLUT] = GetClut(c.x, c.y);
    prim->type = PRIM_SPRT;
    prim->tpage = (NAME_Y & 0x100) >> 4 | NAME_X >> 6; // 4bpp
    prim->clut = NAME_CLUT;
    prim->u0 = (NAME_X & 63) * 4;
    prim->v0 = NAME_Y & 255;
    prim->u1 = x + 1;
    prim->v1 = 12;
}

// The banners of EntityLevelUpAnimation (src/dra/7E4BC.c): 112x24 pictures of
// 4 bits at VRAM x 800, y 256 + 0x30, 0x18, 0, 0x48 (Level up, Heart max up,
// Life max up, Got an item), read through the CLUT 0x19F (black to white).
// Under uk the picture is the serif line of ua_port.json 228 + which, 8 px a
// cell, centred, ink 15 at the top to 12 at the foot, a shadow 1 px right and
// down in ink 5.
#define BANNER_W 112
#define BANNER_H 24
int Lang_Banner(int which) {
    static const u8 v[4] = {0x30, 0x18, 0x00, 0x48};
    u8 tx[BANNER_H][BANNER_W + 1];
    u16 pic[BANNER_H * BANNER_W / 4];
    RECT r = {800, 256, BANNER_W / 4, BANNER_H};
    const char* s = Lang_Port(228 + which);
    int n, x, y, i;
    if (!s || which < 0 || which > 3) {
        return 0;
    }
    for (n = 0; (u8)s[n] != 0xFF; n++) {
    }
    r.y += v[which];
    memset(tx, 0, sizeof(tx));
    x = (BANNER_W - n * 8) / 2;
    for (i = 0; i < n; i++, x += 8) {
        const u8* cell = Lang_MenuCell((u8)s[i]);
        for (y = 0; cell && y < 11; y++) {
            int c;
            for (c = 0; c < 8; c++) {
                if (x + c >= 0 && x + c < BANNER_W &&
                    cell[y * 4 + c / 2] >> (c & 1) * 4 & 15) {
                    tx[6 + y][x + c] = 15 - y / 4;
                }
            }
        }
    }
    for (y = BANNER_H - 1; y > 0; y--) { // only ink casts a shadow
        for (x = BANNER_W - 1; x > 0; x--) {
            if (!tx[y][x] && tx[y - 1][x - 1] > 8) {
                tx[y][x] = 5;
            }
        }
    }
    for (y = 0; y < BANNER_H; y++) {
        for (x = 0; x < BANNER_W / 4; x++) {
            const u8* t = &tx[y][x * 4];
            pic[y * (BANNER_W / 4) + x] = t[0] | t[1] << 4 | t[2] << 8 | t[3] << 12;
        }
    }
    LoadImage(&r, (u_long*)pic);
    DrawSync(0);
    return 1;
}

// the Shift-JIS marks with an ASCII cell (― ‐ ‘ ’ “ ” × as the nearest)
static const u16 jis_marks[][2] = {
    {0x8140, ' '}, {0x8143, ','}, {0x8144, '.'},  {0x8146, ':'},
    {0x8147, ';'}, {0x8148, '?'}, {0x8149, '!'},  {0x814D, '`'},
    {0x814F, '^'}, {0x8151, '_'}, {0x815C, '-'},  {0x815D, '-'},
    {0x815E, '/'}, {0x815F, '\\'}, {0x8160, '~'}, {0x8162, '|'},
    {0x8165, '\''}, {0x8166, '\''}, {0x8167, '"'}, {0x8168, '"'},
    {0x8169, '('}, {0x816A, ')'}, {0x816D, '['},  {0x816E, ']'},
    {0x816F, '{'}, {0x8170, '}'}, {0x817B, '+'},  {0x817C, '-'},
    {0x817E, 'x'}, {0x8181, '='}, {0x8183, '<'},  {0x8184, '>'},
    {0x8190, '$'}, {0x8193, '%'}, {0x8194, '#'},  {0x8195, '&'},
    {0x8196, '*'}, {0x8197, '@'}};

int Lang_SjisCell(int sj) {
    int c;
    if (sj < 0x80) { // the one-byte ASCII letters and space
        return sj == ' ' ? 0 : sj - 0x20;
    }
    for (c = 0; c < 256; c++) { // uk letters, « » — ’ …
        if (msjis[c] == sj && mfont[c]) {
            return c;
        }
    }
    c = 0;
    if (sj < 0x8200) {
        int i;
        for (i = 0; i < LEN(jis_marks); i++) {
            if (jis_marks[i][0] == sj) {
                c = jis_marks[i][1];
            }
        }
    } else if (sj >= 0x824F && sj <= 0x8258) {
        c = '0' + sj - 0x824F;
    } else if (sj >= 0x8260 && sj <= 0x8279) {
        c = 'A' + sj - 0x8260;
    } else if (sj >= 0x8281 && sj <= 0x829A) {
        c = 'a' + sj - 0x8281;
    }
    return c ? c - 0x20 : -1;
}

// uk: a word of the port's own screens (tools/lang/uk/ua_port.json) as menu
// cells (FF end), or NULL
const char* Lang_Port(int index) {
    return g_Lang ? Find(T_PORT, 0, index) : NULL;
}

// uk: the area name of a stage id (tools/lang/uk/ua_zones.json) as menu
// cells (FF end), or NULL
const char* Lang_Zone(int stage) {
    return g_Lang ? Find(T_ZONE, 0, stage) : NULL;
}

