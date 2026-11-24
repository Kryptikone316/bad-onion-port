#!/bin/bash
# SPDX-License-Identifier: Unlicense
# Assembles package/ (what goes on the card under roms/ports) from build/ and the author's game files.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${BUILD:-$HERE/build-sf3500}"
GAME_DIR="$HERE/game"
OUT="${PKG:-$HERE/package}"
rm -rf "$OUT"; mkdir -p "$OUT/bad-onion"
P="$OUT/bad-onion"
cp "$BUILD/fxi.mipsel" "$P/"
# the author's files, byte for byte (not the Windows interpreter and DLLs, not badonion.bat)
for f in badonion.dcb font1.fnt readme.txt; do cp "$GAME_DIR/$f" "$P/"; done
cp "$GAME_DIR"/data*.fpg "$GAME_DIR"/*.wav "$GAME_DIR"/*.xm "$P/"
cp "$HERE/package-src/run.sh" "$P/run.sh"; chmod +x "$P/run.sh"
cp "$HERE/package-src/Bad Onion.sh" "$OUT/Bad Onion.sh"; chmod +x "$OUT/Bad Onion.sh"
[ -f "$HERE/package-src/boxart.png" ] && cp "$HERE/package-src/boxart.png" "$P/boxart.png"
[ -f "$HERE/package-src/README.md" ] && cp "$HERE/package-src/README.md" "$P/README.md"
mkdir -p "$P/licenses"; cp "$HERE"/licenses/*.txt "$P/licenses/"; cp "$HERE/NOTICE.md" "$P/NOTICE.md"
echo "package: $OUT"; (cd "$OUT" && find . -type f | sort | wc -l); du -sh "$OUT"
