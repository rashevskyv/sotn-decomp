#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Makes the private copy of a Linux build for hidden tests (tools/test/drive.py).
# usage: tools/test/setup.sh [<build dir>]   (default: build/linux-static of this repo)
# The copy is SOTN_TEST_DIR (default ~/sotn/runs/test/t). It links the big data,
# copies the program and the memory cards, and has its own empty data/textures/.
# It never touches the owner's play folder.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
B=$(cd "${1:-$REPO/build/linux-static}" && pwd)
T=${SOTN_TEST_DIR:-$HOME/sotn/runs/test/t}
[ -x "$B/sotn" ] || { echo "no program: $B/sotn"; exit 1; }
mkdir -p "$T/data/textures/uk" "$T/data/textures/en" "$T/data/textures/common"
cp "$B/sotn" "$T/sotn"
for e in "$B"/*; do n=$(basename "$e")
  case $n in sotn|data|bu00|bu10|recordings|CMakeFiles|psyz_vita_ovl|Testing|*.ninja*|sotn.ini|sotn*.log) ;;
    *) ln -sfn "$e" "$T/$n";; esac
done
for e in "$B"/data/*; do n=$(basename "$e")
  [ "$n" = textures ] || ln -sfn "$e" "$T/data/$n"
done
mkdir -p "$T/bu00" "$T/bu10" "$T/recordings"
cp -r "$B/bu00/." "$T/bu00/" 2>/dev/null || true
cp -r "$B/bu10/." "$T/bu10/" 2>/dev/null || true
echo "private copy: $T (from $B)"
