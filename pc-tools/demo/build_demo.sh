#!/bin/bash
# Build the demo ROM: the same launcher, with the card from make_demo_card.py packed inside it.
# Usage (from MSYS2 MINGW64): pc-tools/demo/build_demo.sh <demo card folder, e.g. /c/n64/demo-rom-fs>
# Builds into build-demo/ and output-demo/, so the normal build is untouched. Writes output-demo/SC64-Launcher-Demo.z64.
set -e
STAGE="$1"
[ -d "$STAGE/menu" ] || { echo "No demo card at '$STAGE' (run make_demo_card.py first)"; exit 1; }
export N64_INST=${N64_INST:-/c/n64/toolchain}
export PATH="$N64_INST/bin:$PATH"
cd "$(dirname "$0")/../.."
rm -f build-demo/*.dfs     # The card may have changed; the DFS rule doesn't know to look.
make all -j"$(nproc)" BUILD_DIR=build-demo OUTPUT_DIR=output-demo N64_MKDFS_ROOT="$STAGE" > build-demo.log 2>&1 || {
    grep -E 'error' build-demo.log | head; exit 1; }
cp output-demo/N64FlashcartMenu.n64 output-demo/SC64-Launcher-Demo.z64
size=$(stat -c%s output-demo/SC64-Launcher-Demo.z64)
echo "output-demo/SC64-Launcher-Demo.z64: $((size / 1048576)) MB (an N64 ROM can be at most 64 MB)"
[ "$size" -le 67108864 ] || { echo "Too big: take games or music out of the demo card"; exit 1; }
