#!/bin/bash
# Linux build, step 2: the installed game code (games/Metroid Dread_build/*.c, every patch included) compiled with
# Linux gcc into "Metroid Dread_code.so". Same flags as switchrecomp.py compile_all for Linux, plus the compat header.
# Run inside WSL as root after setup_and_build_runtime.sh.
set -e
W=${SWR_ROOT:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}   # the repository (this script is in linux/)
B=/opt/swr/build
mkdir -p "$B" /opt/swr/out
rsync -a --include='*.c' --exclude='*' "$W/games/Metroid Dread_build/" "$B/" 2>/dev/null || cp "$W/games/Metroid Dread_build/"*.c "$B/"
cp "$W/linux/lx_compat.h" /opt/swr/lx_compat.h
cd "$B"
FLAGS="-O2 -fno-strict-aliasing -fwrapv -w -I /opt/swr/runtime -foptimize-sibling-calls -fno-semantic-interposition -fPIC -fvisibility=hidden -include /opt/swr/lx_compat.h"
ls *.c | sed 's/\.c$//' | xargs -P "$(nproc)" -I{} sh -c '[ -f {}.lx.o ] && [ {}.lx.o -nt {}.c ] || gcc '"$FLAGS"' -c {}.c -o {}.lx.o.tmp && mv -f {}.lx.o.tmp {}.lx.o 2>/dev/null || true'
n_c=$(ls *.c | wc -l); n_o=$(ls *.lx.o | wc -l)
echo "compiled $n_o of $n_c"
[ "$n_c" = "$n_o" ] || { echo "missing objects"; exit 1; }
gcc -shared -o "/opt/swr/out/Metroid Dread_code.so" *.lx.o -lm -ldl
ls -la "/opt/swr/out/Metroid Dread_code.so"
