#!/bin/bash
# Linux: open the in-game menu (Home, replayed) on the title screen under WSLg software GL and screenshot it
set -e
W=${SWR_ROOT:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}   # the repository (this script is in linux/)
bash "$W/linux/make_test_game.sh" > /dev/null
cd /opt/swr/game
rm -f menu-m*.ppm
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export SWR_TEST_SCRIPT="$W/linux/menu_test.csv" SWR_TEST_EXIT_FRAME=140 SWR_OV_SHOTS=3,30 SWR_OV_SHOT_PREFIX=/opt/swr/game/menu
timeout 900 "./Metroid Dread" --fullscreen=0 > run_stdout.txt 2>&1 || echo "exit=$?"
grep -E "menu:|test complete|fatal|FATAL|rror" "Metroid Dread.log" | head -10
ls -la menu-m*.ppm 2>/dev/null && cp menu-m*.ppm "$W/linux/"
echo MENUTESTDONE
