// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Section Sizes:
//
//         | Data  | RO Data | Text   | BSS  |
//         +-------+---------+--------+------+
//  PSX US | -     | -       | 0x2B0  | -    |
//
#include <stage.h>

#ifdef PLATFORM_PSYZ
// The font of the banner and the message boxes (port builds):
// 0 = the 12x16 glyph of the Equip description line (en: the BIOS font, uk:
//     the 12x16 serif Cyrillic), 1:1
// 1 = the 8x11 serif cells of the shop, the dialogue and the save box, as a
//     12x16 glyph (Lang_SerifGlyph); a code with no cell takes the glyph of 0
#ifndef MSG_FONT_CELLS
#define MSG_FONT_CELLS 0
#endif
#endif

const char* BlitChar(const char* str, u16* xOffset, u8* pix, u16 stride) {
    const u16 DOUBLE_SPACE = 0x8140;
    const u16 RIGHT_DOUBLE_QUOTATION_MARK = 0x8168;

    const int FontWidth = 12;
    const int FontHeight = 16;
    const int FontStride = FontWidth / 2;

    u16 ch;
    s32 chSize;
    s32 i, j;
    s32 letterWidth;
    u8* chPix;
    u8* ptr;

    // converts the ASCII character into Shift-JIS
    ch = *str++;
    chSize = 0;
    if (ch >= 'a' && ch <= 'z') {
        ch += 0x8220;
    } else if (ch >= 'A' && ch <= 'Z') {
        ch += 0x821F;
    } else {
        if (ch == ' ') {
            ch = DOUBLE_SPACE;
            chSize = 2;
        } else {
#ifdef PLATFORM_PSYZ
            if (!*str) { // a lead byte at the end of the text: nothing to draw
                return str;
            }
#endif
            ch = *str++ | (ch << 8);
            if (ch == DOUBLE_SPACE) {
                chSize = 2;
            }
        }
    }

    if (ch == RIGHT_DOUBLE_QUOTATION_MARK) {
        str += 2;
    }

    // use the converted Shift-JIS character to retrieve the font data
#ifdef PLATFORM_PSYZ
    // kind 0 (the glyph of the Equip description line): every row as it is.
    // Kind 1 mixes two rows into one, which blurs the text.
    chPix = MSG_FONT_CELLS ? Lang_SerifGlyph(ch) : NULL;
    if (!chPix) {
        chPix = (u8*)g_api.func_80106A28(ch, 0);
    }
#else
    chPix = g_api.func_80106A28(ch, 1);
#endif
    while (true) {
        if (ch == DOUBLE_SPACE) {
            break;
        }

        for (i = 0; i < FontHeight; i++) {
            if (chPix[i * FontStride]) {
                break;
            }
        }
        if (i != FontHeight) {
            break;
        }

        // Trim character width from the left-hand side
        for (i = 0; i < FontHeight; i++) {
            ptr = &chPix[i * FontStride];
            for (j = 0; j < 5; j++) {
                ptr[0] = ptr[1];
                ptr++;
            }
            *ptr = 0;
        }
    }

    // scroll every pixel of the letter and finds the furthest horizontal pixel
    // to calculate what the width is
    for (i = 0, letterWidth = 0; i < FontHeight; i++) {
        for (j = 0; j < FontStride; j++) {
            if (chPix[i * FontStride + j] && letterWidth < j) {
                letterWidth = j;
            }
        }
    }

    // Check the very last vertical pixel
    for (i = 0; i < FontHeight; i++) {
        if (chPix[letterWidth + i * FontStride] & 0xF0) {
            break;
        }
    }
    if (i != FontHeight) {
        letterWidth++;
    }

    // Adds at least a vertical pixel of padding at the end of the character
    if (letterWidth < FontStride) {
        letterWidth++;
    }

    // Copy content to destination
    for (i = 0; i < FontHeight; i++) {
        ptr = &pix[*xOffset + i * stride];
        *ptr++ = *chPix++;
        *ptr++ = *chPix++;
        *ptr++ = *chPix++;
        *ptr++ = *chPix++;
        *ptr++ = *chPix++;
        *ptr++ = *chPix++;
    }

    *xOffset += letterWidth + chSize;
    return str;
}
