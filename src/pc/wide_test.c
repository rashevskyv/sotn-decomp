// SPDX-License-Identifier: AGPL-3.0-or-later
// Check of the wide view maths. Run: cc src/pc/wide_test.c && ./a.out
#include <assert.h>
#include <stdio.h>
#include "wide.h"

static void range(int start, int end, int fb, int m, int p0, int p1, int lo,
                  int hi) {
    int a, b;
    Wide_ScrollRange(start, end, fb, m, p0, p1, &a, &b);
    if (a != lo || b != hi) {
        printf("range %d..%d m %d: got %d..%d, want %d..%d\n", start, end, m,
               a, b, lo, hi);
    }
    assert(a == lo && b == hi);
}

// A stage on a w x h screen part at the asked scale: pixel size k, view
// width, g_WideX and the black pillar on each side (as src/pc/wide.c).
static void stage(int w, int h, int ask, int k, int view, int wx, int pillar) {
    int lo, hi, s, v;
    Wide_ScaleRange(w, h, 207, &lo, &hi);
    s = ask < lo ? lo : ask > hi ? hi : ask;
    v = Wide_ViewW(w, s);
    if (s != k || v != view || (v - 256) / 2 != wx || (w - v * s) / 2 != pillar) {
        printf("stage %dx%d ask %d: got %d %d %d %d\n", w, h, ask, s, v,
               (v - 256) / 2, (w - v * s) / 2);
    }
    assert(s == k && v == view && (v - 256) / 2 == wx);
    assert((w - v * s) / 2 == pillar);
}

int main(void) {
    int x, y, w, h;

    // 4:3 (margin 0) keeps the original clamps
    range(0, 1024, 256, 0, 0, 256, 0, 768);
    range(0, 512, 240, 0, 4, 252, 4, 260);
    // 426x240 view in a 1024 px room: the view stops at the room edges
    range(0, 1024, 256, 85, 0, 256, 85, 683);
    // 640x360 view in a 512 px room: centred, 64 px black on each side
    range(0, 512, 256, 192, 0, 256, 128, 128);
    // 640x360 view in a 256x256 room: centred on both axes
    range(0, 256, 256, 192, 0, 256, 0, 0);
    range(0, 256, 240, 60, 4, 252, 8, 8);
    // 360 rows in a 768 px tall room
    range(0, 768, 240, 60, 4, 252, 60, 468);

    // the two fade halves of the 256x240 frame cover the 640x360 view
    x = 0, y = 0, w = 128, h = 240;
    Wide_StretchRect(&x, &y, &w, &h, 192, 60);
    assert(x == -192 && w == 320 && y == -60 && h == 360);
    x = 128, y = 0, w = 128, h = 240;
    Wide_StretchRect(&x, &y, &w, &h, 192, 60);
    assert(x == 128 && w == 320 && y == -60 && h == 360);
    // small tiles stay as they are
    x = 0, y = 0, w = 16, h = 16;
    Wide_StretchRect(&x, &y, &w, &h, 192, 60);
    assert(x == 0 && w == 16 && y == 0 && h == 16);

    // prologue stairs, 640x360 view (my 60) in the 0x500x0x200 room
    range(0, 512, 240, 60, 4, 252, 60, 212);
    // room load with the player low in the room: the view stops at the
    // room bottom and the player (y 384) stays inside the 360 rows
    y = Wide_FollowY(0, 384, 60, 212, 0, 1);
    assert(y == 212 && 384 - y < 240 + 60);
    // a scroll left below the range snaps back, the player stays in view
    assert(Wide_FollowY(300, 300, 60, 212, 0, 0) == 212);
    // a move up is still smoothed by 4 px a frame
    assert(Wide_FollowY(200, 300, 60, 212, 0, 0) == 196);
    assert(Wide_FollowY(200, 180, 60, 212, 0, 0) == 196);
    // a move down snaps
    assert(Wide_FollowY(100, 300, 60, 212, 0, 0) == 164);
    // the one-shot lock at x 640 (height 0x100) in a 426 px view
    // (mx 85): the wide window creates it at scroll 704 + 85, while the
    // player is still on the stairs (x > 880). It must wait for the PS1
    // window, scroll 704, when the player is on the top platform.
    assert(!Wide_InPsxSpawnWindow(640 - (704 + 85), 128 - 128));
    assert(Wide_InPsxSpawnWindow(640 - 704, 128 - 128));
    // the locked room is 256 rows: centred, the platform (y 160) in view
    range(0, 256, 240, 60, 4, 252, 8, 8);
    y = Wide_FollowY(100, 160, 8, 8, 0, 0);
    assert(y == 8 && 160 - y < 240 + 60);

    // Saturn band 16/16: the 426x240 view (my 0) in the 512-row garden
    // shows rows 16..496, never the empty rows
    {
        int lo, hi;
        Wide_BandRange(0, 512, 240, 0, 4, 252, 16, 16, &lo, &hi);
        assert(lo == 16 && hi == 256 && hi + 240 == 496);
        // 640x360 (my 60): the margin is kept inside the band too
        Wide_BandRange(0, 768, 240, 60, 4, 252, 16, 16, &lo, &hi);
        assert(lo - 60 == 16 && hi + 240 + 60 == 752);
        // a one-screen room (256 rows, 224 of art) is centred: rows 8..248,
        // the clip then shows only 16..240 (frame rows 8..232)
        Wide_BandRange(0, 256, 240, 0, 4, 252, 16, 16, &lo, &hi);
        assert(lo == 8 && hi == 8);
        // no band: the PS1 clamps stay
        Wide_BandRange(0, 512, 240, 0, 4, 252, 0, 0, &lo, &hi);
        assert(lo == 4 && hi == 260);
    }

    // PS1 bars only where the room ends (clip rows 20..227)
    {
        int y0, y1;
        // a one-screen room (lo == hi == 4): the PS1 bars
        y0 = 0 - 4, y1 = 256 - 4;
        Wide_PsxRows(4, 4, 4, 20, 227, &y0, &y1);
        assert(y0 == 20 && y1 == 227);
        // a 512-row room in the middle: no bars
        y0 = 0 - 130, y1 = 512 - 130;
        Wide_PsxRows(4, 260, 130, 20, 227, &y0, &y1);
        assert(y0 < 0 && y1 > 240);
        // at its top: the top bar only; 10 px down it is 10 px thinner
        y0 = 0 - 4, y1 = 512 - 4;
        Wide_PsxRows(4, 260, 4, 20, 227, &y0, &y1);
        assert(y0 == 20 && y1 > 240);
        y0 = 0 - 14;
        Wide_PsxRows(4, 260, 14, 20, 227, &y0, &y1);
        assert(y0 == 10);
        // at its bottom: the bottom bar
        y0 = 0 - 260, y1 = 512 - 260;
        Wide_PsxRows(4, 260, 260, 20, 227, &y0, &y1);
        assert(y0 < 0 && y1 == 227);
    }

    // edge fill: a true mirror image, the art flipped next to the edge,
    // upright again past one room width
    {
        int f;
        assert(Wide_MirrorColumn(16, -1, &f) == 0 && f);
        assert(Wide_MirrorColumn(16, -3, &f) == 2 && f);
        assert(Wide_MirrorColumn(16, -16, &f) == 15 && f);
        assert(Wide_MirrorColumn(16, -17, &f) == 15 && !f);
        assert(Wide_MirrorColumn(16, 16, &f) == 15 && f);
        assert(Wide_MirrorColumn(16, 18, &f) == 13 && f);
        assert(Wide_MirrorColumn(16, 32, &f) == 0 && !f);
        assert(Wide_MirrorColumn(16, 5, &f) == 5 && !f);
        assert(Wide_MirrorColumn(1, -2, &f) == 0 && !f);
    }

    // stage pixel size and view on a PC window (the whole window) and
    // on the 16:9 Switch screens; 99 = max
    stage(3440, 1440, 2, 4, 860, 302, 0);
    stage(3440, 1440, 3, 4, 860, 302, 0);
    stage(3440, 1440, 5, 5, 688, 216, 0);
    stage(3440, 1440, 99, 6, 572, 158, 4);
    stage(2560, 1080, 2, 3, 852, 298, 2);
    stage(2560, 1080, 4, 4, 640, 192, 0);
    stage(2560, 1080, 99, 5, 512, 128, 0);
    stage(1920, 1080, 1, 2, 960, 352, 0);
    stage(1920, 1080, 3, 3, 640, 192, 0);
    stage(1920, 1080, 4, 4, 480, 112, 0);
    stage(1920, 1080, 99, 5, 384, 64, 0);
    stage(1280, 720, 2, 2, 640, 192, 0);
    stage(1280, 720, 99, 3, 426, 85, 1);
    stage(5120, 1440, 3, 6, 852, 298, 4);
    stage(1228, 1033, 2, 2, 614, 179, 0);
    stage(1228, 1033, 99, 4, 306, 25, 2);
    stage(2000, 607, 3, 2, 960, 352, 40); // the 960 budget: pillars
    // a 16:9 screen keeps the old range, where the rows also counted
    for (w = 256; w <= 4096; w += 16) {
        int lo, hi, old_hi, old_lo;
        h = w * 9 / 16;
        Wide_ScaleRange(w, h, 207, &lo, &hi);
        old_hi = w / 256 < h / 207 ? w / 256 : h / 207;
        old_hi = old_hi < 1 ? 1 : old_hi;
        old_lo = (w + 959) / 960 > (h + 539) / 540 ? (w + 959) / 960
                                                   : (h + 539) / 540;
        old_lo = old_lo > old_hi ? old_hi : old_lo;
        assert(lo == old_lo && hi == old_hi);
    }

    // the left stick as a D-pad: dead zone, 45 degree sectors, hysteresis
    {
        enum { L = WIDE_STICK_L, R = WIDE_STICK_R, U = WIDE_STICK_U,
               D = WIDE_STICK_D };
        int dead = 11468; // 35 %
        assert(Wide_StickDirs(0, 0, dead, 0) == 0);
        assert(Wide_StickDirs(11000, 0, dead, 0) == 0); // in the dead zone
        assert(Wide_StickDirs(32767, 0, dead, 0) == R);
        assert(Wide_StickDirs(-32767, 0, dead, 0) == L);
        assert(Wide_StickDirs(0, -32767, dead, 0) == U); // SDL: up < 0
        assert(Wide_StickDirs(0, 32767, dead, 0) == D);
        assert(Wide_StickDirs(23170, 23170, dead, 0) == (R | D)); // 45 deg
        assert(Wide_StickDirs(-23170, -23170, dead, 0) == (L | U));
        // walking right with the stick 15 deg low: no crouch
        assert(Wide_StickDirs(31650, 8481, dead, 0) == R);
        // 30 deg low: the diagonal
        assert(Wide_StickDirs(28377, 16384, dead, 0) == (R | D));
        // hysteresis: 10000 stays on only when held; 20 deg low keeps down
        assert(Wide_StickDirs(10000, 0, dead, 0) == 0);
        assert(Wide_StickDirs(10000, 0, dead, R) == R);
        assert(Wide_StickDirs(30791, 11207, dead, 0) == R);
        assert(Wide_StickDirs(30791, 11207, dead, R | D) == (R | D));
    }

    // the right stick look: proportional target, eased, back to 0
    {
        int cur = 0, i;
        assert(Wide_LookStep(0, 3000, 6553, 48) == 0); // dead zone
        for (i = 0; i < 200; i++) {
            cur = Wide_LookStep(cur, 32767, 6553, 48);
        }
        assert(cur == 48);
        for (i = 0; i < 200; i++) {
            cur = Wide_LookStep(cur, -32768, 6553, 48);
        }
        assert(cur == -48);
        cur = Wide_LookStep(-48, 0, 6553, 48);
        assert(cur > -48 && cur < 0); // eases, no jump
        for (i = 0; i < 200; i++) {
            cur = Wide_LookStep(cur, 0, 6553, 48);
        }
        assert(cur == 0);
        for (i = 0; i < 200; i++) { // half way past the dead zone: half range
            cur = Wide_LookStep(cur, 6553 + (32767 - 6553) / 2, 6553, 48);
        }
        assert(cur == 23 || cur == 24);
        assert(Wide_LookStep(0, 32767, 6553, 0) == 0); // range 0: off
        // the player stays 32..224 px from the scroll: free in the middle,
        // limited near the edges, never pushed out by the look
        assert(Wide_LookClamp(1000, 48, 1128, 32, 224) == 1048);
        assert(Wide_LookClamp(1000, -48, 1128, 32, 224) == 952);
        assert(Wide_LookClamp(1000, 48, 1062, 32, 224) == 1030);
        assert(Wide_LookClamp(1000, 48, 1020, 32, 224) == 1000);
        assert(Wide_LookClamp(1000, -48, 1200, 32, 224) == 976);
        assert(Wide_LookClamp(1000, -48, 1250, 32, 224) == 1000);
    }

    puts("wide_test ok");
    return 0;
}
