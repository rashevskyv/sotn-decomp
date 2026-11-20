# Fonts

Each font of the port, where it is used, and its licence.
Rule: a font file enters git only with an open licence and its licence file.
A picture rendered from a font is a picture, not a font file.
Where each picture comes from: [ASSET_SOURCES.md](ASSET_SOURCES.md).

## In the game

| Font | Where the player sees it | Cell | Files | Origin | In git | Licence |
|---|---|---|---|---|---|---|
| Stock 8x8 font | Menus, item names, HUD, stock English dialogue | 8x8 | `F_GAME.BIN` sheet at VRAM 896,256 | PS1 US disc | No | Stock |
| uk 8x8 letters | Same places as above, uk | 8x8, cells 0x66 to 0x97 | `tools/lang/uk/font8.png`, `font8_map.json` | Our drawing (Picoripi) | Yes | AGPL-3.0 (ours) |
| Serif dialogue font | uk dialogue boxes; gold speaker names | 8x11, baseline row 9 | `tools/lang/uk/dfont.png`, `glyphs.json` | Our pixel drawing (`dialogue_serif.py`, Picoripi plugin) | Yes | AGPL-3.0 (ours) |
| Serif menu font | uk pause menu, port menu (both languages), status page | 8x11 | Cyrillic: `dfont.png`; Latin, digits, marks: `tools/lang/serif_latin.py` | Our pixel drawing | Yes | AGPL-3.0 (ours) |
| 12x16 BIOS font | Descriptions, bestiary, file-select messages | 12x16 | `tools/lang/uk/bios12.png`, `bios_glyphs_uk.h` | Our serif Cyrillic; other glyphs: psyz krom table | Yes | Ours: AGPL-3.0. Krom table: UNKNOWN origin |
| Gothic 16x16 letters | File select: labels, name entry, plate words | 16x16 | Stock sheets at VRAM 800,176 (32x64) and 832,256 (32x32) in `F_SEL.BIN`; uk: `tools/lang/uk/labels/gothic.png` | Stock: PS1 disc. uk: our drawing | Stock: no. uk: yes | Stock / AGPL-3.0 (ours) |
| Gothic HD sheets | Same, with HD on | 256 px a cell | `gothic_abc__800_176_...png`, `gothic_uk__832_256_...png`, `gothic_digits__...png` | Redraws of the 1x cells (step 103) | No | Stock-derived, local |

Owner rule: every uk screen (shop, menus) uses the serif font, never the stock 8x8 menu font.

## In pictures

| Picture | Font | Font file | Licence of the font | Font in git |
|---|---|---|---|---|
| uk stage banners (13), ST0 card «Останній етап», elevator notice; 1x labels and HD | Monotype Corsiva | `C:\Windows\Fonts\MTCORSVA.TTF` (comes with Microsoft Office) | Proprietary (Monotype) | Never |
| en stage banners, HD | Monotype Corsiva | same | Proprietary | Never |
| en stage banners, 1x | Stock lettering, a chancery italic of the same class | PS1 disc | Stock | No |
| "1792" card, uk (1x and the rule for HD) | Alegreya Sans Italic | `tools/lang/fonts/AlegreyaSans-Italic.ttf` | OFL-1.1 (`OFL-AlegreyaSans.txt`) | Yes |
| Prologue title «КАСТЕЛЬВАНІЯ» (HD) | A serif TTF given on the command line of `prologue_title.py` | UNKNOWN: the run did not record the file | UNKNOWN | No |
| uk title logo, PRESS START, game-over letters, loading text | Drawn art, no font | `tools/lang/uk/local/` | Stock-derived | No |

### Stage banners: why Corsiva

Picoripi `plugins/castlevania_sotn_xbla/ps1_vram.py` made the uk labels with Monotype Corsiva.
Check (step 104, repeated in step 107): the recipe with Corsiva gives the 13 zone labels
and `bloodlines.png` pixel for pixel.
Fit score against the stock English banner art, 1.0 = identical, mean of four zones:

| Font | Score | Licence | Ukrainian letters |
|---|---|---|---|
| Monotype Corsiva | 0.553 | Proprietary | all |
| Z003 Medium Italic (URW) | 0.547 | AGPL-3.0 with font exception | all |
| Cormorant Italic | 0.514 | OFL-1.1 | all |
| Marck Script | 0.485 | OFL-1.1 | all |
| P052 Italic (URW) | 0.479 | AGPL-3.0 with font exception | all |
| Alegreya Sans Italic | 0.439 | OFL-1.1 | all |
| Kurale | 0.392 | OFL-1.1 | all |
| Philosopher Italic | 0.343 | OFL-1.1 | all |

Decision: banners use Corsiva. The stock English art is the same chancery class.
Free substitute: Z003 Medium Italic (`fonts-urw-base35`,
`/usr/share/fonts/opentype/urw-base35/Z003-MediumItalic.otf`).
A build without Corsiva can render the banners with Z003. The 1x labels in git do not change.
Do not use Alegreya Sans Italic for banners: it is a sans italic and scores 0.439.

Rendered pictures: font licences normally allow pictures made with the font. This is not legal advice.
The Corsiva file itself must never enter git or a release.

## Stand-ins to avoid

Some run scripts used system fonts as quick stand-ins. Do not ship pictures made with them.

| Font | Used in | Why to avoid |
|---|---|---|
| Georgia (`georgia.ttf`) | step 102b `text.py`, `mock.py` | Proprietary; not the game's serif; replaced by redraws in step 103 |
| Alegreya Sans Italic for anything but the 1792 card | step 102b `text.py` | Wrong class for banners and labels |
| Times New Roman Bold (`timesbd.ttf`) | step 103 `prep.py`, shape guide only | Proprietary; a guide, not final art |
| Segoe UI Bold (`segoeuib.ttf`) | step 102b `icons.py`, letters on button marks | Proprietary; UNKNOWN whether the final button PNGs keep its letters |

Candidates kept for later (all OFL-1.1): Cormorant Italic, Kurale, Marck Script, Philosopher Italic.

## Adding a font

1. Check that the licence allows a copy in git (OFL, Apache, public domain).
2. Put the file and its licence text in `tools/lang/fonts/`.
3. Add a row here and a credit line in `README.md`.
