#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Music files for the native port from the official soundtrack (KICA-7760).

    python3 -I ost_map.py copy OST_DIR OUT_DIR
    python3 -I ost_map.py verify OST_DIR XA_STR1

copy: OUT_DIR/<ID>.mp3 for each PS1 track id (3 upper hex digits, the start
id of the track: 30F.mp3), and OUT_DIR/loops.txt with the loop points that
verify measured. Put OUT_DIR as the folder `music` next to the game.
OST_DIR holds the 34 files "NN Title.mp3"; only the track number counts.

verify: decodes each track of XA_STR1 (the US disc file data/SD/XA_STR1) and
each soundtrack file, finds the soundtrack start of the PS1 intro and loop by
cross-correlation, and prints the durations, the ratios and the loop points.
Needs ffmpeg and numpy. Nothing is written.

No audio is in this repository. The owner rips or buys the soundtrack.
"""
import glob
import os
import re
import shutil
import subprocess
import sys

# PS1 start id -> soundtrack track number, matched by title (step 89 table),
# then checked by verify (correlation, durations).
MAP = {
    0x301: 20, 0x303: 22, 0x305: 15, 0x307: 18, 0x309: 11, 0x30B: 16,
    0x30D: 8, 0x30F: 6, 0x311: 25, 0x313: 9, 0x315: 12, 0x317: 21,
    0x319: 17, 0x31B: 27, 0x31D: 13, 0x31F: 3, 0x321: 2, 0x323: 24,
    0x325: 26, 0x327: 1, 0x328: 30, 0x329: 33, 0x32E: 7, 0x330: 23,
    0x332: 5, 0x334: 28, 0x336: 29, 0x338: 31, 0x33A: 32, 0x33C: 34,
    0x33D: 19, 0x33E: 14, 0x33F: 10, 0x340: 4,
}

# verify results: id -> (start, loop start, loop end) in 44100 Hz frames of
# the decoded file. start skips the lead-in that the PS1 track does not have.
# loop end 0: the track plays once. level: gain in 1/256 to the XA's
# loudness. Filled from `verify` (step 97). Loop start = start: no repeat
# found, the file loops from its start to the start of its fade-out.
LOOPS = {
    0x301: (34678, 34678, 4343850, 177),
    0x303: (32438, 32438, 3064950, 186),
    0x305: (33853, 33853, 5600700, 159),
    0x307: (31578, 912765, 6998614, 220),
    0x309: (34689, 496825, 6548850, 214),
    0x30B: (35200, 35200, 5137650, 174),
    0x30D: (33808, 33808, 3461850, 173),
    0x30F: (34174, 34174, 4718700, 196),
    0x311: (37446, 37446, 10539900, 165),
    0x313: (34132, 34132, 7122150, 195),
    0x315: (30181, 30181, 3550050, 230),
    0x317: (34784, 34784, 6328350, 171),
    0x319: (33929, 33929, 5644800, 217),
    0x31B: (36340, 36340, 4630500, 250),
    0x31D: (34667, 2243454, 4348046, 195),
    0x31F: (35039, 35039, 3219300, 161),
    0x321: (30185, 30185, 3770550, 156),
    0x323: (34790, 1367402, 7011900, 137),
    0x325: (34394, 34394, 4542300, 185),
    0x327: (55295, 0, 0, 192),
    0x328: (32435, 0, 0, 254),
    0x329: (33546, 0, 0, 231),
    0x32E: (31759, 31759, 4696650, 202),
    0x330: (34986, 34986, 5380200, 187),
    0x332: (36685, 36685, 2447550, 171),
    0x334: (32458, 32458, 4189500, 198),
    0x336: (36161, 36161, 4035150, 215),
    0x338: (33968, 33968, 13406400, 223),
    0x33A: (34265, 3798960, 7277322, 167),
    0x33C: (25097, 0, 0, 147),
    0x33D: (30362, 0, 0, 191),
    0x33E: (30296, 0, 0, 243),
    0x33F: (37926, 0, 0, 197),
    0x340: (24763, 0, 0, 173),
}

RATE = 44100
XA_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "../../src/dra/gen/us/xa_music_config.h")


def ost_files(ost_dir):
    out = {}
    for p in glob.glob(os.path.join(ost_dir, "*")):
        m = re.match(r"(\d+)\s", os.path.basename(p))
        if m and p.lower().endswith((".mp3", ".flac")):
            out[int(m.group(1))] = p
    return out


def copy(ost_dir, out_dir):
    files = ost_files(ost_dir)
    os.makedirs(out_dir, exist_ok=True)
    lines = ["# id start loop_start loop_end (44100 Hz frames) level (1/256),"
             " ost_map.py"]
    for tid, track in sorted(MAP.items()):
        src = files.get(track)
        if not src:
            print("%03X: track %d missing, the game plays the XA" % (tid, track))
            continue
        ext = os.path.splitext(src)[1].lower()
        shutil.copyfile(src, os.path.join(out_dir, "%03X%s" % (tid, ext)))
        if tid in LOOPS:
            lines.append("%03X %d %d %d %d" % ((tid,) + LOOPS[tid]))
        print("%03X <- %s" % (tid, os.path.basename(src)))
    with open(os.path.join(out_dir, "loops.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


def xa_config():
    rows = []
    for line in open(XA_CONFIG):
        m = re.match(r"\{(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),"
                     r"\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}", line.strip())
        if m:
            a, fr, fi, ch, vol, _, mode = (int(x, 0) for x in m.groups())
            rows.append(dict(addr=a, frames=fr, file=fi, ch=ch, mode=mode))
    return rows


def pcm(args):
    import numpy as np
    out = subprocess.run(["ffmpeg", "-v", "error"] + args +
                         ["-f", "s16le", "-ac", "1", "-ar", str(RATE), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(out, "<i2").astype(np.float32)


def ncc(w, s):
    """normalised cross-correlation of s over every lag of w"""
    import numpy as np
    n = 1 << int(np.ceil(np.log2(len(w) + len(s))))
    c = np.fft.irfft(np.fft.rfft(w, n) * np.conj(np.fft.rfft(s, n)), n)
    c = c[:len(w) - len(s) + 1]
    e = np.concatenate([[0.0], np.cumsum(w.astype(np.float64) ** 2)])
    e = e[len(s):] - e[:len(e) - len(s)]
    return c / (np.sqrt(np.maximum(e, 1e-9)) * np.sqrt((s.astype(np.float64)
                                                        ** 2).sum()) + 1e-9)


def find(w, s, lo, hi):
    """best lag of s in w[lo:hi + len(s)], and its score"""
    lo, hi = max(0, lo), min(len(w) - len(s), hi)
    if hi <= lo:
        return None, 0.0
    c = ncc(w[lo:hi + len(s)], s)
    k = int(c.argmax())
    return lo + k, float(c[k])


def verify(ost_dir, xa_path):
    import numpy as np
    rows, files = xa_config(), ost_files(ost_dir)
    chans, S = {}, RATE
    W = 8 * S  # correlation window
    print("| ID | OST | OST s | PS1 s | ratio | start s | score | loop s "
          "| PS1 loop s | loop score | result |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for tid, track in sorted(MAP.items()):
        r = rows[tid - 0x300]
        if r["ch"] not in chans:
            chans.clear()  # one channel in memory at a time (100 MB)
            chans[r["ch"]] = pcm(["-f", "psxstr", "-i", xa_path,
                                  "-map", "0:%d" % r["ch"]])
        xa = chans[r["ch"]]
        x0 = r["addr"] // 8 * 2352  # 2016 frames at 37800 Hz per sector
        ost = pcm(["-i", files[track]])
        loop = r["mode"] == 1
        nxt = rows[tid - 0x300 + 1] if loop else None
        intro = (nxt["addr"] - r["addr"]) // 8 * 2352 if loop else 0
        xa_len = (intro + nxt["frames"] * 735) if loop else r["frames"] * 735
        start, s0 = find(ost, xa[x0:x0 + W], 0, 40 * S)
        res, ls, le, sl = "plays once", 0, 0, 0.0
        if loop:
            ls, le, sl, res = seam(ost, xa[x0 + intro:x0 + intro + W],
                                   start + intro, nxt["frames"] * 735)
            if not le:
                # no repeat found: the whole file from the start point to the
                # start of the fade-out (the game crossfades into the start)
                ls, le = max(start, 0), fade_start(ost)
        print("| %03X | %d | %.1f | %.1f | %.2f | %.3f | %.2f | %s | %.1f | %.2f | %s |"
              % (tid, track, len(ost) / S, xa_len / S, len(ost) / xa_len,
                 start / S, s0, "%.2f-%.2f" % (ls / S, le / S) if le else "-",
                 nxt["frames"] / 60 if loop else 0, sl, res))
        # level: the XA's loudness over the same 30 s of music (the soundtrack
        # is mastered louder; with the CD mix gain of the game it would clip)
        n = min(30 * S, len(ost) - max(start, 0), len(xa) - x0)
        a = xa[x0:x0 + n].astype(np.float64)
        b = ost[max(start, 0):max(start, 0) + n].astype(np.float64)
        gain = np.sqrt((a ** 2).mean() / max((b ** 2).mean(), 1e-9))
        print("LOOP 0x%03X: (%d, %d, %d, %d), # level %.2f"
              % (tid, max(start, 0), ls, le, round(gain * 256), gain),
              flush=True)


def fade_start(ost):
    """end of the last half second at 0.6 of the median level or more: the
    fade-out of the soundtrack edit starts there"""
    import numpy as np
    blk = RATE // 2
    r = np.sqrt((ost[:len(ost) // blk * blk].reshape(-1, blk) ** 2).mean(1))
    return int(np.where(r > 0.6 * np.median(r))[0][-1] + 1) * blk


def seam(ost, seg, la, period):
    """loop points (a, b): the music at b is the music at a again, so the
    jump from b to a keeps the phrase. Tried in this order:
    1. the PS1 loop start (seg, expected at la) once more in the soundtrack,
       later or earlier;
    2. a passage before the fade that repeats one PS1 loop period earlier;
    3. any passage before the fade that repeats, with a strong match.
    None: (0, 0): the game loops the whole file."""
    import numpy as np
    S, W = RATE, 6 * RATE
    ls, sl = find(ost, seg, la - 3 * S, la + 3 * S)
    if ls is not None and sl > 0.5:
        l2, s2 = find(ost, seg, ls + 20 * S, len(ost))
        if l2 is not None and s2 > 0.5:
            return ls, l2, sl, "PS1 loop start again %.2f" % s2
        e2, se = find(ost, seg, 0, ls - 20 * S)
        if e2 is not None and se > 0.5:
            return e2, ls, se, "PS1 loop start earlier %.2f" % se
    fade = fade_start(ost)
    d = ost[:len(ost) // 4 * 4].reshape(-1, 4).mean(1)  # 11025 Hz search
    per, any_ = (0.0, 0, 0), (0.0, 0, 0)
    for b in range(fade - W - 25 * S, fade - W, S // 2):
        if b - period - S > 0:
            a, sc = find(d, d[b // 4:(b + W) // 4], (b - period) // 4 - S // 40,
                         (b - period) // 4 + S // 40)
            if a is not None and sc > per[0]:
                per = (sc, a * 4, b)
        if b > 40 * S:
            a, sc = find(d, d[b // 4:(b + W) // 4], 0, (b - 30 * S) // 4)
            if a is not None and sc > any_[0]:
                any_ = (sc, a * 4, b)
    for (sc, a, b), need, what in ((per, 0.35, "PS1 period"),
                                   (any_, 0.75, "repeat")):
        if sc >= need:
            a, sc = find(ost, ost[b:b + W], a - 100, a + 100)  # to the frame
            return a, b, sc, "%s %.2f" % (what, sc)
    return 0, 0, 0.0, "whole file"


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "copy":
        copy(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "verify":
        verify(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(1)
