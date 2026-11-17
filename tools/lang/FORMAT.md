# Ukrainian language pack

The pack lives on the SD card in `sdmc:/switch/sotn-native/lang/uk/`.
`tools/lang/sotn_lang.py pack` writes it to `build/lang/uk/`.
All numbers are little-endian unless the text says otherwise.
FNV-1a 32 is the hash everywhere (offset 2166136261, prime 16777619).

## Inputs

Git holds only the Ukrainian side, in `tools/lang/uk/`:

| File | Content |
|------|---------|
| `ua.json` | `{xbox id: ua}` for every translated line. Dialogue uses the markup below. |
| `ua_psp.json` | Dialogue boxes that differ for the PSP voice set. |
| `ua_code.json` | File-select lines (`code:<addr>`) and gothic words (`gothic:<addr>`). Not packed yet. |
| `ua_menu.json` | `{g_MenuStr index: ua}`: MENU rows that replace the Xbox line (ДОСВІД, the two Saturn familiars). |
| `ua_port.json` | `{index: ua}`: words of the port's own screens (PORT table): 0..5 the stat names of the status page, ATT DEF STR CON INT LCK in full, in sentence case; 6 the map word of its records; 7..11 the line of each item of the pause main list, in its order, 33 cells at most; 20..57 the words of the port menu (src/pc/port_menu.c P_*), drawn in the serif menu cells. The pack adds 100 + row: the status page words of g_MenuStr (`STATUS_ROWS` in sotn_lang.py) in sentence case. |
| `ua_zones.json` | `{stage id, 2 hex digits: ua}`: area names (ZONE table). `proposed` lists the names not from the owner yet. |
| `plates.py` | The words of the file-select name and card plates, drawn over the stock plate at pack time. |
| `font8.png` | 128x128 palette image of the 8x8 sheet. Index = 4bpp nibble. Only cells 0x66..0x97 hold ink. |
| `font8_map.json` | Character to cell code, and the Latin twins. |
| `bios_glyphs_uk.h` | Six 12x16 letters for `g_SjisPages` / `g_CustomGlyphs`. |
| `art_map.json` | Where each VRAM art record goes, and if that is proven. |
| `labels/*.png` | The owner's label art (128 px wide palette images). Labels that keep stock pixels (`STOCK_PIXELS` in sotn_lang.py) are not in git; pack takes them from `ps1vram_uk.bin` when `SOTN_LANG_MEDIA` names its folder, else leaves them out. |
| `places.json` | Where each translated line sits in the US data: table, place, FNV-1a of the stock text (no text). Dialogue lines: the yaml box key. `code`: the file-select messages by the FNV-1a of their Shift-JIS literal. |
| `glyphs.json` | The character order of `dfont.png` and `bios12.png`. |
| `dfont.png` | The owner's dialogue font, 8x11 cells, 16 a row. |
| `bios12.png` | The owner's 12x16 letters (16 x 15 cells): the serif Cyrillic table, then the private letters. |
| `art/*.png` | The changed texels of the font sheets (index 16: keep the stock texel of the US disc). |

`tools/lang/serif_latin.py` holds the Latin letters, digits, marks and stat arrows of the serif menu
font (mfont.bin). Both languages use it, so it is not in `uk/`.

The English text and every stock byte come from the extracted US disc at pack time:
`SOTN_DATA_ROOT` (tree with `assets/`, `disks/us/`, `tools/psyz`; default: this repository).
One pack input is optional: `SOTN_LANG_MEDIA` (mod folder with `ps1vram_uk.bin`).
The owner's tools (`extract`, `glyphs`, `join`, `width`, `art`, `vendor`, `guard`) also read
`SOTN_LANG_XBLA` (`lines_game.json`, `lines_psp.json`) and
`SOTN_LANG_PICORIPI` (plugin folder with `dialogue_serif.py`, `bios_glyphs_uk.bin`, `ps1_code.py`, `labels/`).
After a change there, `sotn_lang.py vendor` writes `places.json`, `glyphs.json`, `dfont.png`, `bios12.png` and `art/`.

### Dialogue markup

Text runs stay as text. `\n` is LINE_BREAK. `{sN}` is SET_SPEED N. `{wN}` is SET_WAIT N.
`{}` stands for the next native op of the box that is not LINE_BREAK, SET_SPEED or SET_WAIT.
The native op takes its arguments from the native yaml, so no address or sound id is in git.

## Encodings

8x8 cell font (`_S` strings, `MenuDrawStr`): byte = cell code, then `FF` (the C string adds `00`).
Latin twins (А В Е К М Н О Р С Т Х І а е о р с у х і, and З = digit 3) use the stock cell.
The other 50 letters and marks use cells 0x66..0x97 (stock kana cells).
MENU, EQUIP_NAME and ACC_NAME write З as cell 0x98 (Z_CELL), because the serif menu font has its own
digit 3. The port copies the stock 3 to cell 0x98 of the sheet, so the 8x8 drawers still show З.
Dialogue scripts: byte = cell code + 0x20, so ASCII stays ASCII.

12x16 BIOS font (`func_800F99B8`): ASCII letters and space are one byte.
І і are written as I i. Ґ Є Ї ґ є ї are the private codes 0x8540..0x8545 (big-endian).
Everything else is Shift-JIS (cp932). ASCII digits and marks become full-width forms.
`"`, `”` and `¨` (the Xbox text writes the stock `””` pair as `¨`) become `81 68 81 68` because the drawer skips two bytes after `81 68`.
« » become ≪ ≫. The text ends with `00`. `@` stays one byte (the bestiary line break).

## text.bin

```
char magic[8] = "SOTNLANG"; u32 version = 2; u32 count;
struct { u16 table; u8 font; u8 flags; u32 stock_hash; u32 offset; u16 length; u16 index; } entry[count];
u8 data[];   // offset is from the start of data[]; each entry is 16 bytes
```

`stock_hash` is FNV-1a of the stock bytes: cells with the `FF` (no `00`), Shift-JIS without the `00`,
actor names as their u16 list. The tool checks that each stock key occurs in a US disc BIN.
`index` is the row in a positional table: MENU = g_MenuStr index, SEL_AREA = 2 * stage + column.
It is 0xFFFF for all other tables. The hook looks up (table, stock_hash, index).
MENU and SEL_AREA repeat English words with different translations (the Ukrainian gender forms),
so a hash alone is not enough there.
`flags` bit 0: two Xbox lines with a different translation still share one key (now 0 entries).
Entries are sorted by table, then hash, then index.
`font`: 0 cell, 1 Shift-JIS, 2 actor u16 list (ends `FFFF`), 3 UTF-8 (status labels; needs a new glyph path).

Tables: 0 MENU (g_MenuStr), 1 EQUIP_NAME, 2 EQUIP_DESC, 3 ACC_NAME, 4 ACC_DESC, 5 ENEMY,
6 SPELL_NAME, 7 SPELL_DESC, 8 RELIC_NAME, 9 RELIC_DESC, 10 HELP (D_800A2D48), 11 STATUS (D_800A2D68),
12 SEL_AREA (stage_names), 13 ACTOR, 14 BESTIARY (LIB D_us_80180FB4), 15 OTHER_CELL (any other `_S`),
16 OTHER_SJIS, 17 ZONE (area names as menu cells, hash 0, index = stage id; the port's teleport
map and list draw them with mfont.bin), 18 PORT (menu cells, hash 0, index from `ua_port.json`, and 100 + g_MenuStr row for the status page words). A relic name keeps the leading space of its stock name.

## dialogue.bin, dialogue_psp.bin

```
char magic[8] = "SOTNDLG1"; u32 count; u32 names_offset; u32 data_offset;
struct { u32 key_hash; u32 name_offset; u32 data_offset; u32 length; } entry[count];
char names[];  // "st/st0/cutscene_script_psx.yaml#3": yaml path under assets/, then the box index
u8 data[];
```

A box starts after NEXT_DIALOG or END_CUTSCENE of the native yaml (box 0 is the start).
Data bytes: text (cell + 0x20), `01` LINE_BREAK, `02 n` SET_SPEED, `03 n` SET_WAIT,
`1F` = copy the next native non-flow op with its arguments. The box ends with its last `1F`.
The rebuild must fix SCRIPT_SWITCH, SCRIPT_UNKNOWN_15 and SET_EVENTS targets, because box sizes change.
`dialogue_psp.bin` holds the same boxes with the PSP-voice lines where they differ.

## font8.bin

```
char magic[8] = "SOTNFNT8"; u32 count;
struct { u8 code; u8 rows[8][4]; } cell[count];   // 4bpp, low nibble = left texel
```

Paint each cell at VRAM x = 896 + (code & 15) * 2, y = 256 + (code >> 4) * 8 (halfwords),
after `F_GAME.BIN` or `F_GAME2.BIN` loads. The file select keeps a copy of the sheet at y = 0.

## dfont.bin

The owner's dialogue font (`uk/dfont.png`, from `dialogue_serif.py` in the Picoripi plugin): 8x11 cells,
baseline row 9, descender row 10. Letters `ORDER` are bytes 0x80 + i, the punctuation `PUNCT` 0xC7 + j.

```
char magic[8] = "SOTNDFN1"; u32 count;
struct { u8 code, pad[3]; u8 rows[11][4]; } cell[count];   // 4bpp, low nibble = left texel, ink 6
```

The port loads a cell into the dialogue line buffer in place of the 8x8 sheet cell of the same byte.
The 8x8 cells it keeps go 3 rows down, so all baselines meet at row 9 of the 12-row line slot.

## mfont.bin

The serif menu font: the same 8x11 cells, keyed by the 8x8 cell code they replace.
Cyrillic cells take the dialogue design, Z_CELL takes З, Latin letters, digits, `/ : ' - . ,` and the
stat arrows (cells 0xFA, 0xFB) take `serif_latin.py`. Other cells have no record.
`shared` = 1 marks a `serif_latin.py` cell: under en the port loads only these.

```
char magic[8] = "SOTNMFN1"; u32 count;
struct { u8 code, shared, pad[2]; u8 rows[11][4]; } cell[count];   // 4bpp, low nibble = left texel, ink 6
```

While the uk pause menu is open, the port saves VRAM 832,256 (32x176 halfwords), writes there an atlas of
16x16 cells of 8x11 (a cell with no record gets its 8x8 sheet cell in rows 3..10) and draws menu text
from it with texture page 0x1D, 3 px higher. The menu restores the rect as it closes.

## bios.bin

```
char magic[8] = "SOTNBIOS"; u32 count;
struct { u16 sjis; u16 flags; u8 rows[15][2]; } glyph[count];  // MSB = left pixel, 16 columns
```

`flags` 0: private letter (0x8540..0x8545), also in `bios_glyphs_uk.h`.
`flags` 1: the owner's serif design for a stock Cyrillic code 0x8440..0x8491: 58 glyphs.
Ё Ъ Ы Э ё ъ ы э are left out: they are not Ukrainian, and the owner's design has none of them.
The port may use these in place of the psyz krom glyphs.

## art.bin

```
char magic[8] = "SOTNART1"; u32 count;
struct { u16 x, y, w, h; u32 first_hash, whole_hash; u8 proven; u8 name_len; char name[name_len];
         u16 texels[w * h]; } record[count];   // halfword coordinates, native little-endian texels
```

The hashes are FNV-1a of the native stock rows (little-endian bytes): of the first row and of all rows.
They are 0 when no native source is proven; the hook then writes the record on demand.
`art_map.json` names the native source of each proven record (a disc file block or a stage graphic).
Its `proven` field has three levels. `"placement"`: the hash matches the file at the y the game
really loads it at (F_SEL 0, F_GAME 256, F_GAME2 256, F_TITLE0 0, F_TITLE1 256), so the VRAM rect is proven.
`"content"`: the pixels match a native source, but the load position is not proven
(stage graphics are PNGs with no VRAM position; sheet0 matches F_GAME at y 0, which the game never uses).
`false`: no native source matched. The `proven` byte in art.bin is 1 for both proven levels.

The last records are the plates of `plates.py` (`name_plate`, `card1_plate`, `card2_plate`). The pack cuts
each rect from `F_SEL.BIN` at y 0, clears the stock word and draws the Ukrainian word, so git holds
no stock pixel. Without the disc they are left out and the plates stay stock.

## script.bin, script_psp.bin

The native cutscene scripts rebuilt with the Ukrainian boxes, one record per yaml that has one.
The port picks `script_psp.bin` for the PSP voice set.

```
char magic[8] = "SOTNSCR1"; u32 count;
struct { u32 stock_hash, stock_len, new_len, nrel, nmap, name_len;
         char name[name_len]; pad to 4;           // the yaml path under assets/
         u8 bytes[new_len]; pad to 4;             // the rebuilt script
         struct { u32 pos, target; } rel[nrel];   // address words in bytes: pos, PSX target
         struct { u32 old, new; } map[nmap];      // stock item offset -> rebuilt offset
} record[count];
```

`stock_hash` is FNV-1a of the stock script bytes as `include/cutscene.h` makes them from the yaml.
The port hashes the first `stock_len` bytes of each `CutscenePcAlloc` range and swaps on a match.
The range may go on after the script (the events): those bytes are kept and moved by `new_len - stock_len`.
A text or flow item maps to the end of the native op before it. The last map entry is (stock_len, new_len).

## lang_pack_uk.c

`sotn_lang.py pack --c FILE` writes all the files above (not the dialogue files) as one C array
`g_LangPackUk` of `g_LangPackUkSize` bytes, built into the program:

```
char magic[8] = "SOTNPACK"; u32 count;
struct { char name[16]; u32 offset, length; } file[count];   // offsets from the array start, 4-aligned
```

Without `ps1vram_uk.bin` the array has every file, but `art.bin` has no `STOCK_PIXELS` labels.
