#!/bin/bash
# Linux build, step 4: boot the test folder under WSLg for N seconds (default 150) with Mesa's software OpenGL
# (WSLg's D3D12 GL stops at 4.2; the runtime needs 4.3), then a screenshot. Log: /opt/swr/game/Metroid Dread.log
cd /opt/swr/game
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
S=${1:-150}
timeout $((S + 60)) "./Metroid Dread" --seconds=$S --shot=/opt/swr/game/shot_end --fullscreen=0 > run_stdout.txt 2>&1
echo "exit=$?"
tail -3 run_stdout.txt
grep -h "code:\|RomFS\|audio:\|vp9\|VP9\|video\|gpu: frames=\|renderer\|OpenGL\|GL_\|error\|fatal\|WARN" "Metroid Dread.log" | head -40
ls -la shot_end* 2>/dev/null
