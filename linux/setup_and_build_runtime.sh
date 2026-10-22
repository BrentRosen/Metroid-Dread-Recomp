#!/bin/bash
# Linux build, step 1: copy the runtime and build scripts into WSL's own filesystem (/opt/swr) and build the
# X11/OpenGL runtime. Run inside WSL as root: bash /mnt/c/.../linux/setup_and_build_runtime.sh
set -e
W=${SWR_ROOT:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}   # the repository (this script is in linux/)
mkdir -p /opt/swr
cd /opt/swr
rm -rf runtime
cp -r "$W/runtime" ./runtime
cp "$W"/*.py .
mkdir -p runtime/vp9/lin
cp /usr/lib/x86_64-linux-gnu/libvpx.a runtime/vp9/lin/   # same VP9 ABI (image 5, codec 4+5) as the vendored headers
python3 switchrecomp.py --build-runtime --cc gcc --target linux --backend x11 2>&1 | tail -60
