#!/bin/bash
# macOS build (Apple silicon; Intel Macs untested). Native Vulkan through MoltenVK: macOS stops at OpenGL 4.1, the
# OpenGL renderer needs 4.3.
#
#   macos/build.sh                          the runtime: bin/runtime_macos_sdl (and runtime/snxvk/libsnxvk_mac.a)
#   macos/build.sh game DUMP [UPDATE]       setup from your own dump: checks the files, translates and compiles the
#                                           game into games/ (OUT=folder for another place), then installs the runtime
#   macos/build.sh install [FOLDER]         the runtime and its libraries into a set-up game folder (default games/)
#
# Needs the Xcode command line tools (xcode-select --install) and Homebrew:
#   brew install sdl2 molten-vk glslang libvpx
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME="Metroid Dread"
cd "$ROOT"

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing: $1. $2" >&2; exit 1; }; }

deps() {
  [ "$(uname -s)" = Darwin ] || { echo "This script builds on macOS only." >&2; exit 1; }
  need clang "Install the Xcode command line tools: xcode-select --install"
  need python3 "Install the Xcode command line tools: xcode-select --install"
  need glslang "brew install glslang"
  command -v sdl2-config >/dev/null 2>&1 || pkg-config --exists sdl2 2>/dev/null || { echo "Missing: SDL2. brew install sdl2" >&2; exit 1; }
  mvk_path >/dev/null || { echo "Missing: MoltenVK. brew install molten-vk" >&2; exit 1; }
}

brew_prefix() { command -v brew >/dev/null 2>&1 && brew --prefix "$1" 2>/dev/null; }

mvk_path() {
  for p in "$(brew_prefix molten-vk)/lib/libMoltenVK.dylib" /opt/homebrew/lib/libMoltenVK.dylib /usr/local/lib/libMoltenVK.dylib; do
    [ -f "$p" ] && { echo "$p"; return 0; }; done
  return 1
}

runtime() {
  deps
  echo "== native Vulkan renderer (ScarletNX) -> runtime/snxvk/libsnxvk_mac.a"
  python3 work-profiler/build_snxvk2.py
  # libvpx for the game's VP9 videos, linked statically (same ABI as runtime/vp9/include); without it no videos
  vpx="$(brew_prefix libvpx)/lib/libvpx.a"
  if [ -f "$vpx" ]; then mkdir -p runtime/vp9/mac && cp -f "$vpx" runtime/vp9/mac/libvpx.a
  else echo "warning: libvpx not found (brew install libvpx): the game's videos will not play"; fi
  echo "== runtime -> bin/runtime_macos_sdl"
  python3 switchrecomp.py --build-runtime --cc clang
}

# the runtime as "<folder>/Metroid Dread" with its SDL2 and MoltenVK beside it, so the folder runs without Homebrew
install_game() {
  G=${1:-$ROOT/games}
  [ -f "$G/${NAME}_code.dylib" ] || { echo "No ${NAME}_code.dylib in $G: run setup first (macos/build.sh game DUMP)." >&2; exit 1; }
  [ -x bin/runtime_macos_sdl ] || runtime
  EXE="$G/$NAME"
  cp -f bin/runtime_macos_sdl "$EXE"
  # SDL2: the copy the runtime was linked with, referenced from the game folder
  sdl=$(otool -L "$EXE" | awk '/libSDL2/ {print $1; exit}')
  if [ -n "$sdl" ] && [ -f "$sdl" ]; then
    cp -f "$sdl" "$G/libSDL2-2.0.0.dylib"; chmod u+w "$G/libSDL2-2.0.0.dylib"
    install_name_tool -id @executable_path/libSDL2-2.0.0.dylib "$G/libSDL2-2.0.0.dylib" 2>/dev/null
    install_name_tool -change "$sdl" @executable_path/libSDL2-2.0.0.dylib "$EXE" 2>/dev/null
    # Homebrew's sdl2 is sdl2-compat, SDL2's API over SDL3: it opens libSDL3.dylib from its own folder
    if strings "$sdl" | grep -q 'libSDL3.dylib'; then
      sdl3=$(brew_prefix sdl3)/lib/libSDL3.0.dylib
      [ -f "$sdl3" ] || { echo "SDL2 here is sdl2-compat, but SDL3 was not found (brew install sdl3)." >&2; exit 1; }
      cp -f "$sdl3" "$G/libSDL3.dylib"; chmod u+w "$G/libSDL3.dylib"
      install_name_tool -id @loader_path/libSDL3.dylib "$G/libSDL3.dylib" 2>/dev/null
    fi
  fi
  # MoltenVK on its own is a whole Vulkan implementation: backend_sdl.c loads it from the game folder first
  mvk=$(mvk_path); cp -f "$mvk" "$G/libMoltenVK.dylib"; chmod u+w "$G/libMoltenVK.dylib"
  install_name_tool -id @executable_path/libMoltenVK.dylib "$G/libMoltenVK.dylib" 2>/dev/null
  # install_name_tool breaks the signatures; Apple silicon runs nothing unsigned: sign them again (ad hoc)
  for f in "$EXE" "$G/${NAME}_code.dylib" "$G"/libSDL2-2.0.0.dylib "$G"/libSDL3.dylib "$G"/libMoltenVK.dylib; do
    [ -f "$f" ] && codesign -s - -f "$f" 2>/dev/null; done
  # a double-click start from Finder (the game finds its files next to itself; captures/ goes here too)
  cat > "$G/Play $NAME.command" <<'EOF'
#!/bin/bash
cd "$(dirname "$0")" && exec "./Metroid Dread" "$@"
EOF
  chmod +x "$G/Play $NAME.command" "$EXE"
  [ -f "$G/$NAME.ini" ] || printf '[Display]\nfullscreen=0\ndocked=1\n[Graphics]\nrenderer=vulkan_native\n' > "$G/$NAME.ini"
  echo "Installed: $G"
  echo "Start it with \"$G/Play $NAME.command\" (or double-click it in Finder)."
}

case "${1:-runtime}" in
  runtime) runtime ;;
  game)
    [ -n "$2" ] || { echo "usage: macos/build.sh game DUMP_FOLDER [UPDATE_FOLDER]" >&2; exit 1; }
    deps; [ -x bin/runtime_macos_sdl ] || runtime
    OUTDIR=${OUT:-$ROOT/games}; mkdir -p "$OUTDIR"
    args=(--out "$OUTDIR" --name "$NAME" --base "$2"); [ -n "$3" ] && args+=(--update "$3")
    python3 dread/dread_setup.py "${args[@]}" | grep -v '^@@P '
    [ "${PIPESTATUS[0]}" = 0 ] || exit 1
    install_game "$OUTDIR" ;;
  install) install_game "$2" ;;
  *) sed -n '2,13p' "$0"; exit 1 ;;
esac
