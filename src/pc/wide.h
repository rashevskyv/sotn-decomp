// SPDX-License-Identifier: AGPL-3.0-or-later
// Wide view maths. No game headers, so src/pc/wide_test.c can check it.
#ifndef WIDE_H
#define WIDE_H

// Above this view width in game pixels the tile budget runs out.
#define WIDE_VIEW_MAX 960

// Stage scales (size of one game pixel) that fit a screen part of w x h
// pixels: the 256 columns and the stage clip rows must fit (hi), and the
// view w / scale must stay within WIDE_VIEW_MAX (lo). lo <= hi always.
static inline void Wide_ScaleRange(int w, int h, int rows, int* lo, int* hi) {
    *hi = w / 256 < h / rows ? w / 256 : h / rows;
    *hi = *hi < 1 ? 1 : *hi;
    *lo = (w + WIDE_VIEW_MAX - 1) / WIDE_VIEW_MAX;
    *lo = *lo > *hi ? *hi : *lo;
}

// The view width in game pixels at scale k: the screen width, at most
// WIDE_VIEW_MAX (black pillars past it), even.
static inline int Wide_ViewW(int w, int k) {
    return (w / k < WIDE_VIEW_MAX ? w / k : WIDE_VIEW_MAX) & ~1;
}

// Scroll range [*lo, *hi] that keeps the view inside the room [start, end).
// The view is the fb (256 or 240 px) plus m px on each side. pad0 and pad1
// are the original clamp insets (x: 0 and 256, y: 4 and 252); m = 0 gives
// the original range. A room smaller than the view gets its centre.
static inline void Wide_ScrollRange(
    int start, int end, int fb, int m, int pad0, int pad1, int* lo, int* hi) {
    *lo = start + (m > pad0 ? m : pad0);
    *hi = end - (fb + m > pad1 ? fb + m : pad1);
    if (*lo > *hi) {
        *lo = *hi = (start + end - fb) / 2;
    }
}

// The range for a room whose top bt and bottom bb rows hold no art (Saturn
// stages): the view never shows them. bt = bb = 0 is Wide_ScrollRange.
static inline void Wide_BandRange(int start, int end, int fb, int m, int pad0,
                                  int pad1, int bt, int bb, int* lo, int* hi) {
    Wide_ScrollRange(start + bt, end - bb, fb, m, bt ? 0 : pad0,
                     bb ? fb : pad1, lo, hi);
}

// The PS1 bars at the room top and bottom. The PS1 shows only the frame
// rows c0..c1, at a scroll in [lo, hi]: no room row above lo + c0 or below
// hi + c1 was ever shown. Narrow the clip rows *y0..*y1 (frame rows at this
// scroll) to the rows the PS1 shows. A one-screen room (lo == hi) keeps the
// bars c0..c1; a tall room shows them only at its top and bottom, and they
// scroll away with the room.
static inline void Wide_PsxRows(
    int lo, int hi, int scroll, int c0, int c1, int* y0, int* y1) {
    if (*y0 < lo + c0 - scroll) {
        *y0 = lo + c0 - scroll;
    }
    if (*y1 > hi + c1 - scroll) {
        *y1 = hi + c1 - scroll;
    }
}

// A large screen tile (fade, flash) that touches an edge of the 256x240
// frame grows by the margin on that side, so it covers the wide view.
static inline void Wide_StretchRect(
    int* x, int* y, int* w, int* h, int mx, int my) {
    if (*w < 128 || *h < 120) {
        return;
    }
    if (*x <= 0) {
        *w += *x + mx;
        *x = -mx;
    }
    if (*x + *w >= 256) {
        *w += mx;
    }
    if (*y <= 0) {
        *h += *y + my;
        *y = -my;
    }
    if (*y + *h >= 240) {
        *h += my;
    }
}

// Vertical follow of the original camera, anchor 136 px. A move up is
// smoothed by 4 px a frame, a move down snaps. snap != 0 is a room load or
// a cutscene. The result stays in [lo, hi], so the player stays in view,
// except for the original slide of 4 px into a room whose top is below.
static inline int Wide_FollowY(
    int scroll, int py, int lo, int hi, int top, int snap) {
    if (snap) {
        scroll = py - 136;
    } else if (py < lo + 136) {
        if (scroll - (py - 136) >= 4 && lo + 4 < scroll) {
            scroll -= 4;
        } else if (scroll < top && top != 0) {
            return scroll + 4;
        } else {
            scroll = lo;
        }
    } else if (hi + 136 < py) {
        scroll = hi;
    } else if (scroll - (py - 136) >= 4) {
        scroll -= 4;
    } else {
        scroll = py - 136;
    }
    return scroll < lo ? lo : scroll > hi ? hi : scroll;
}

// The layout column that fills a wide-view cell outside a layer row of w
// tiles (tx < 0 or tx >= w): a true mirror image about the room edge, so
// the art always joins there. Cell -1 shows column 0, -2 column 1, and so
// on; *flip is 1 where the tile art must be drawn reversed. Past a whole
// room width the mirror image is mirrored again (period 2w).
static inline int Wide_MirrorColumn(int w, int tx, int* flip) {
    int t = tx % (2 * w);

    t = t < 0 ? t + 2 * w : t;
    *flip = t >= w;
    return t < w ? t : 2 * w - 1 - t;
}

// The PS1 creates a layout entity when it is in this screen window. The wide
// view creates entities earlier; an entity that must act at its original
// moment checks this window itself.
static inline int Wide_InPsxSpawnWindow(int sx, int sy) {
    return sx >= -0x40 && sx <= 0x140 && sy >= -0x40 && sy <= 0x120;
}

// The left stick as a D-pad, 8 directions of 45 degrees each. The stick is
// on past dead (of 32767); an axis gives its direction when it holds at
// least sin 22.5 deg (0.38) of the stick length, so walking with the stick a
// little low does not crouch. held = the bits of the last frame: a held
// stick stays on down to 3/4 of dead and a held direction down to 0.30 of
// the length, so a stick on a sector edge does not flicker.
enum { WIDE_STICK_L = 1, WIDE_STICK_R = 2, WIDE_STICK_U = 4, WIDE_STICK_D = 8 };
static inline int Wide_StickDirs(int x, int y, int dead, int held) {
    long long xx = (long long)x * x, yy = (long long)y * y, m2 = xx + yy;
    long long on = held ? dead - dead / 4 : dead;
    int r = 0;

    if (m2 <= on * on) {
        return 0;
    }
    if (xx * 1000 >=
        (held & (WIDE_STICK_L | WIDE_STICK_R) ? 90 : 147) * m2) {
        r |= x < 0 ? WIDE_STICK_L : WIDE_STICK_R;
    }
    if (yy * 1000 >=
        (held & (WIDE_STICK_U | WIDE_STICK_D) ? 90 : 147) * m2) {
        r |= y < 0 ? WIDE_STICK_U : WIDE_STICK_D;
    }
    return r;
}

// The right stick look: one frame of the view offset cur (game px) on one
// axis. The target is the axis past dead, scaled to 0..range px; the offset
// moves 1/8 of the way there a frame (at least 1 px), so a released stick
// eases the view back to 0.
static inline int Wide_LookStep(int cur, int axis, int dead, int range) {
    int a = axis < 0 ? -axis : axis, t = 0, d, step;

    a = a > 32767 ? 32767 : a;
    if (a > dead && dead < 32767) {
        t = (int)((long long)(a - dead) * range / (32767 - dead));
    }
    t = axis < 0 ? -t : t;
    d = t - cur;
    step = d / 8;
    if (!step && d) {
        step = d > 0 ? 1 : -1;
    }
    return cur + step;
}

// The scroll with the look: base + look, limited so the player p (world,
// the same axis) stays a..b px from the scroll on the screen. The look never
// moves the player further out than the base scroll does.
static inline int Wide_LookClamp(int base, int look, int p, int a, int b) {
    int s = base + look;

    if (look > 0 && s > p - a) {
        s = p - a > base ? p - a : base;
    }
    if (look < 0 && s < p - b) {
        s = p - b < base ? p - b : base;
    }
    return s;
}

#endif
