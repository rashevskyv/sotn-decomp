// SPDX-License-Identifier: AGPL-3.0-or-later
// Wide view maths. No game headers, so src/pc/wide_test.c can check it.
#ifndef WIDE_H
#define WIDE_H

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
// tiles (tx < 0 or tx >= w). The row continues with the smallest period p
// of 1, 2, 4 or 8 that its first (last) columns repeat: checked on up to 8
// columns past the edge period, so a two-column brick wall or a floor of
// four tiles goes on without a seam. A row without such a period is
// mirrored at the edge.
static inline int Wide_EdgeColumn(const unsigned short* row, int w, int tx) {
    int right = tx >= w, d = right ? tx - w : -1 - tx; // 0: next to the edge
    int p, x, a, b;

    for (p = 1; p <= 8 && 2 * p <= w; p *= 2) {
        for (x = 0; x < 8 && x + p < w; x++) {
            a = right ? w - 1 - x : x;
            b = right ? a - p : a + p;
            if (row[a] != row[b]) {
                break;
            }
        }
        if (x == 8 || x + p == w) {
            return right ? w - p + d % p : p - 1 - d % p;
        }
    }
    d = d < w ? d : w - 1;
    return right ? w - 1 - d : d;
}

// The PS1 creates a layout entity when it is in this screen window. The wide
// view creates entities earlier; an entity that must act at its original
// moment checks this window itself.
static inline int Wide_InPsxSpawnWindow(int sx, int sy) {
    return sx >= -0x40 && sx <= 0x140 && sy >= -0x40 && sy <= 0x120;
}

#endif
