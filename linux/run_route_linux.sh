#!/bin/bash
# Linux build: replay a Windows test route (frame-indexed CSV) under WSLg with fresh COPIES of the test saves.
# Usage: run_route_linux.sh LABEL ROUTE.csv EXIT_FRAME CAPTURES(comma) [extra env...]
set -e
LABEL=$1; ROUTE=$2; EXITF=$3; CAPS=$4; shift 4
W=${SWR_ROOT:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}   # the repository (this script is in linux/)
G=/opt/swr/game
OUT=/opt/swr/runs/$LABEL
mkdir -p "$OUT"
rm -rf "$G/Metroid Dread_data/save"
cp -r "$W/backups/eos_verified_20261015_151535/save" "$G/Metroid Dread_data/save"
cd "$G"
rm -f "Metroid Dread.log"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export SWR_TEST_SCRIPT="$ROUTE" SWR_TEST_EXIT_FRAME=$EXITF SWR_TEST_CAPTURE_FRAMES=$CAPS SWR_TEST_CAPTURE_PREFIX="$OUT/image"
for kv in "$@"; do export "$kv"; done
timeout 1500 "./Metroid Dread" --fullscreen=0 > "$OUT/stdout.txt" 2>&1 || echo "exit code $?"
cp "Metroid Dread.log" "$OUT/"
grep -h "test complete\|test capture\|gpu: frames=" "$OUT/Metroid Dread.log" | tail -4
ls "$OUT"
