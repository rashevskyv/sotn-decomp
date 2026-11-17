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

    puts("wide_test ok");
    return 0;
}
