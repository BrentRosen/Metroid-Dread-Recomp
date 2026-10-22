#!/bin/bash
# Linux build, step 5: stop any test instance and assemble the Steam Deck package (program + game code + Deck .ini +
# run.sh + README) in linux/dist/MetroidDread-Linux on the Windows side, plus a .tar.gz of it. No game data inside.
W=${SWR_ROOT:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}   # the repository (this script is in linux/)
pkill -9 -x "Metroid Dread" 2>/dev/null
P=/opt/swr/pkg/MetroidDread-Linux
rm -rf /opt/swr/pkg && mkdir -p "$P"
cp /opt/swr/bin/runtime_linux_x11 "$P/Metroid Dread"
cp "/opt/swr/out/Metroid Dread_code.so" "$P/"
cp "$W/linux/dist/run.sh" "$W/linux/dist/README-SteamDeck.txt" "$W/linux/dist/Metroid Dread.ini" "$P/"
cp "$W/games/Metroid Dread.hints" "$P/" 2>/dev/null || true
sed -i 's/\r$//' "$P/run.sh" "$P/README-SteamDeck.txt" "$P/Metroid Dread.ini"
chmod +x "$P/run.sh" "$P/Metroid Dread"
strip --strip-unneeded "$P/Metroid Dread_code.so" -o "$P/code.tmp" && mv "$P/code.tmp" "$P/Metroid Dread_code.so"
cd /opt/swr/pkg && tar czf MetroidDread-Linux.tar.gz MetroidDread-Linux
rm -rf "$W/linux/dist/MetroidDread-Linux" && cp -r "$P" "$W/linux/dist/" && cp MetroidDread-Linux.tar.gz "$W/linux/dist/"
ls -la "$P" /opt/swr/pkg/MetroidDread-Linux.tar.gz
sha256sum "$P/Metroid Dread" "$P/Metroid Dread_code.so"
