#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# PSP Maria's gen/*.h headers for the PC build: the maria.bin entry of
# config/assets.pspeu.yaml, written to <out> instead of assets/ and src/.
# Run from the repo root. Usage: maria_gen.sh <sotn-assets> <out dir>
set -e
sa=$1
out=$2
mkdir -p "$out"
# sotn-assets reads the enums from <src_path>/<asset dir name>.h
echo "#include \"$PWD/src/maria/maria.h\"" > "$out/maria.h"
{
    echo "version: pspeu"
    echo "files:"
    awk -v out="$out" '
        { sub(/\r$/, "") } # a CRLF checkout
        /^  - target:/ { on = ($0 ~ /PSPBIN\/maria\.bin$/) }
        on {
            sub("asset_path: assets/maria", "asset_path: " out "/maria")
            sub("src_path: src/maria", "src_path: " out)
            print
        }' config/assets.pspeu.yaml
} > "$out/maria.yaml"
"$sa" extract-assets "$out/maria.yaml"
"$sa" build-assets "$out/maria.yaml"
