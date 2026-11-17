// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bo0.h"

// Uninitialized data still owned by asm on PSX.
Pos D_us_801CD6E4[66];
u8 D_us_801CD904[0xC68];
s32 D_us_801CE56C;

// e_marionette.h reads g_EInitSlinger[0] when the Marionette dies. On PSX
// the symbol is 0x801806A6, the zero padding after g_EInitOwlKnight.
EInit g_EInitSlinger;
