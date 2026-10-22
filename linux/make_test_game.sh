#!/bin/bash
# Linux build, step 3: a test game folder in WSL (/opt/swr/game). exefs and romfs are links to the Windows install
# (read only); the saves are a COPY of the test-save backup, never the user's real saves.
set -e
W=${SWR_ROOT:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}   # the repository (this script is in linux/)
G=/opt/swr/game
mkdir -p "$G/Metroid Dread_data"
cp -f /opt/swr/bin/runtime_linux_x11 "$G/Metroid Dread"
cp -f "/opt/swr/out/Metroid Dread_code.so" "$G/"
ln -sfn "$W/games/Metroid Dread_data/exefs" "$G/Metroid Dread_data/exefs"
ln -sfn "$W/games/Metroid Dread_data/romfs" "$G/Metroid Dread_data/romfs"
rm -rf "$G/Metroid Dread_data/save"
cp -r "$W/backups/eos_verified_20261015_151535/save" "$G/Metroid Dread_data/save"
[ -f "$G/Metroid Dread.ini" ] || printf '[Display]\nfullscreen=0\ndocked=1\n[Graphics]\nrenderer=opengl\n' > "$G/Metroid Dread.ini"
ls -la "$G" "$G/Metroid Dread_data"
