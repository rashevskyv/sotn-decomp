# SPDX-License-Identifier: AGPL-3.0-or-later
"""The three small file-select plates and the card before the opening video, for the native port.
The plates (2026-11-20) are cleared only: the game draws «Ім'я» (stock "Name") and «НОСІЙ» with
the 1 or 2 (stock "CARD" over an engraved 1 or 2) at run time in the gothic letters of the file
select (src/st/sel/select.c SelPlateCaptions), so the 1x plates and the HD window frames (empty
plaques) show the same words. Read as data by sotn_lang.py (ast), never imported.

The pack takes each stock rect from F_SEL.BIN on the disc and clears the stock word (each of its
texels takes the nearest other texel of the same row), so no stock pixel is in git.
rect: VRAM halfwords x, y, w, h, inside one 32x128 F_SEL block (one LoadImage holds it all).
cols, rows: the texels to clear in the rect (inclusive). stock_ink: the nibbles that go there.
gold: clear a row only between its first and its last texel of these nibbles (the plaque), so
the dark rim of the plaque stays. like: take the other keys from that plate.
Optional word (the plates up to 2026-11-19): at: texel column, row of its top-left; word: '#'
ink; ink: the nibble of each word row.
"""
PLATES = {
    "name_plate": {
        "rect": [785, 89, 6, 5], "cols": [3, 20], "rows": [0, 4], "stock_ink": [4, 5],
    },
    # the word (5) and the engraved digit (1, 9..12) on the gold (6..8) of the plaque, rows 4..15
    "card1_plate": {
        "rect": [819, 4, 6, 12], "cols": [0, 23], "rows": [0, 11], "stock_ink": [1, 5, 9, 10, 11, 12],
        "gold": [6, 7, 8],
    },
    "card2_plate": {"like": "card1_plate", "rect": [875, 4, 6, 12]},
    # The card before the opening video (2026-11-17, the owner's option A): the stock English card
    # of two lines, F_TITLE1.BIN at VRAM 768,256 (232x32 texels, tpage 0x1C, HandleVideoPlayback).
    # src: the art source. font: in tools/lang/fonts (OFL fonts, each with its licence file there).
    # ramp: the grey of each nibble 0..15 under clut 0x1A.
    # lines: [text, size px, baseline row, centre column, tracking px, word gap px, font].
    # Line 1 (Alegreya Sans Italic, the closest of four candidates to the stock lettering): the values
    # that make the font's English line fit the stock line best.
    # Line 2 (2026-11-17, the owner's choice of two previews): also Alegreya Sans Italic, sized so the
    # line is as wide as the stock line 2 (223 px) at 1 px tracking and 1 px more per word gap.
    "title_card": {
        "src": "F_TITLE1.BIN @y256", "rect": [768, 256, 58, 32], "font": "AlegreyaSans-Italic.ttf",
        "ramp": [0, 16, 33, 49, 66, 82, 99, 115, 140, 156, 173, 189, 206, 222, 239, 255],
        "lines": [["ПОВЕРНІМОСЯ В 1792 РІК,", 16.125, 12.5, 115.5, 0.5],
                  ["у трансильванську глушину.", 18.5, 26.0, 115.5, 1.0, 1]],
    },
}
