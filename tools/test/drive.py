#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Hidden test run of the private Linux build copy (tools/test/setup.sh makes it).

usage: drive.py <W>x<H> <ini> <outdir> step...
steps: k:KEY[,KEY] tap | h:KEY[+KEY]:SEC hold | w:SEC wait | s:NAME screenshot | f: fit the window
       m:X,Y mouse move | c:X,Y mouse move and left click (screen pixels)
       (KEY: xdotool names, for example Return, Right, z)
env:   SOTN_ARGS     more game arguments, for example "--stage st0"
       SOTN_TEST_DIR the private copy (default ~/sotn/runs/test/t)
       SOTN_TEST_DISPLAY the Xvfb display (default :100)
The game runs under Xvfb with the dummy audio driver: no window, no sound.
Its output goes to <outdir>/game.log. Exit code 2: the game ended before the last step.
"""
import os
import shlex
import subprocess
import sys
import time

T = os.path.expanduser(os.environ.get("SOTN_TEST_DIR", "~/sotn/runs/test/t"))
DISP = os.environ.get("SOTN_TEST_DISPLAY", ":100")

if len(sys.argv) < 4 or "x" not in sys.argv[1]:
    sys.exit(__doc__)
W, H = sys.argv[1].split("x")
ini, out = sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
if os.path.exists("/tmp/.X11-unix/X" + DISP.lstrip(":")):
    sys.exit(f"display {DISP} is in use: set SOTN_TEST_DISPLAY")
env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "DISPLAY")}
env.update(DISPLAY=DISP, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="x11")
xv = subprocess.Popen(["Xvfb", DISP, "-screen", "0", "%sx%sx24" % (W, H), "-nolisten", "tcp"],
                      stderr=subprocess.DEVNULL)
time.sleep(1.5)
log = open(os.path.join(out, "game.log"), "w")
cmd = [T + "/sotn", "--ini", ini] + shlex.split(os.environ.get("SOTN_ARGS", ""))
g = subprocess.Popen(cmd, cwd=T, env=env, stdout=log, stderr=subprocess.STDOUT)


def x(*a):
    subprocess.run(["xdotool"] + list(a), env=env, stderr=subprocess.DEVNULL)


died = False
try:
    for st in sys.argv[4:]:
        if g.poll() is not None:
            died = True
            break
        op, _, arg = st.partition(":")
        if op == "k":
            for key in arg.split(","):
                x("keydown", key); time.sleep(0.12); x("keyup", key); time.sleep(0.25)
        elif op == "h":  # KEY may be KEY+KEY: held together
            key, _, sec = arg.partition(":")
            keys = key.split("+")
            x("keydown", *keys); time.sleep(float(sec or 1)); x("keyup", *keys); time.sleep(0.25)
        elif op == "f":  # no window manager under Xvfb: size the window to the screen
            wid = subprocess.run(["xdotool", "search", "--pid", str(g.pid)], env=env, capture_output=True,
                                 text=True).stdout.split()
            for w_ in wid:
                x("windowmove", w_, "0", "0")
                x("windowsize", w_, W, H)
            print("fit", wid, flush=True)
        elif op in ("m", "c"):  # mouse move, or move and left click
            mx, _, my = arg.partition(",")
            x("mousemove", mx, my)
            if op == "c":
                x("click", "1")
            time.sleep(0.25)
        elif op == "w":
            time.sleep(float(arg))
        elif op == "s":
            subprocess.run(["import", "-display", DISP, "-window", "root", os.path.join(out, arg + ".png")], env=env)
            print("shot", arg, flush=True)
        else:
            print("unknown step", st, flush=True)
    died = died or g.poll() is not None
finally:
    g.terminate()
    try:
        g.wait(2)
    except subprocess.TimeoutExpired:
        g.kill()
    xv.terminate()
if died:
    print("game exited early, code", g.returncode, flush=True)
    sys.exit(2)
