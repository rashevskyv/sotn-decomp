#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# New game through the menus, hidden (tools/test/drive.py): title -> file
# select -> name entry -> Player select -> Alucard (ST0) and Richter (NO3).
# The start wipes the whole prim buffer (select.c Upd_Eng_18); 0.8.60 crashed
# there. PASS needs the pick in the log, then the stage's first room.
# usage: tools/test/newgame.sh [<build dir>]   (default: build/linux-static)
# Needs no HD textures and no owner files; the private copy is setup.sh's.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
bash "$HERE/setup.sh" "$@" > /dev/null
OUT=${SOTN_TEST_OUT:-$HOME/sotn/runs/test/newgame}
mkdir -p "$OUT"
printf 'scale = 2\nfullscreen = 0\nlanguage = en\nhd_textures = 0\nmusic_files = 0\noverlay = 1\nfps = 0\n' > "$OUT/ng.ini"
fail=0
# name: the Player select keys (the cursor starts on Alucard), the pick, the room
for t in "alucard::0:ST0" "richter:Left:1:NO3"; do
  IFS=: read -r name keys player room <<< "$t"
  rm -rf "$OUT/$name"
  python3 -I "$HERE/drive.py" 640x480 "$OUT/ng.ini" "$OUT/$name" \
    w:15 f w:2 k:Return w:4 k:x w:3 k:x w:3 k:Return w:3 ${keys:+k:$keys} k:x w:20 > /dev/null || true
  log="$OUT/$name/game.log"
  if grep -q "\[charsel\] start: player $player" "$log" && grep -q "^\[room\] $room" "$log"; then
    echo "PASS newgame $name"
  else
    echo "FAIL newgame $name (log $log)"; fail=1
  fi
done
exit $fail
