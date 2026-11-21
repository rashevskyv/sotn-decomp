// SPDX-License-Identifier: AGPL-3.0-or-later
// Saturn 0x1E-0x20: the PS1's Cave Troll (RNO4) with the converted Saturn
// sprites (the Saturn keeps its PS1 frame numbers). rno4.h is skipped (its
// guard): rstage15.h gives the ids and the inits.
#include "rstage15.h"

#define RNO4_H
// two calls pass a third argument the function does not take (PS1 decomp)
#define AnimateEntity(frames, entity, ...) AnimateEntity(frames, entity)
#include "../rno4/e_cave_troll.c"
