# Metroid Dread on macOS

> **Early, untested in game.** The macOS build compiles, starts, opens its window and runs recompiled Switch code
> on Apple silicon (tested on an M5, macOS 26.5), but nobody has played Metroid Dread with it yet. Expect problems,
> and please attach `Metroid Dread.log` from the game folder to any report.

macOS stops at OpenGL 4.1 and the OpenGL renderer needs 4.3, so the Mac build always draws with the **native Vulkan
renderer** (ScarletNX), running on Metal through [MoltenVK](https://github.com/KhronosGroup/MoltenVK). The window,
controllers and sound come from SDL2.

## What you need

- A Mac with Apple silicon and macOS 12 or later. Intel Macs may work but have not been tried.
- The Xcode command line tools, for clang and Python: `xcode-select --install`
- [Homebrew](https://brew.sh), then: `brew install sdl2 molten-vk glslang libvpx`
- Your own dump of Metroid Dread **2.1.0**, both the ExeFS and the RomFS (see "Setting up the game" in the main
  [README](../README.md)).
- About 10 GB free, and some time: setup translates about 7 million instructions to C and compiles them.

## Build and set up

From the repository folder:

```
macos/build.sh                                  # the runtime: bin/runtime_macos_sdl
macos/build.sh game "/path/to/your dump"        # setup: checks, translates and compiles the game into games/
macos/build.sh game "/path/to/base" "/path/to/update"   # base game and 2.1.0 update as separate dumps
```

Setup is the same as on Windows (`dread/dread_setup.py`): every RomFS file is checked against the 2.1.0 file list,
the verified patches are applied, and the game code is compiled, here with clang into `Metroid Dread_code.dylib`.
`OUT=/some/folder macos/build.sh game ...` puts the game somewhere other than `games/`.

The game folder then holds the program, the game code and data, and copies of SDL2 and MoltenVK, so it runs without
Homebrew. Start it with **`Play Metroid Dread.command`** (double-click it in Finder, or run it in Terminal). After
rebuilding the runtime, `macos/build.sh install [FOLDER]` copies it into a game folder again.

## Controls

A controller works as on the Switch (buttons by position). On the keyboard:

| key | | key | |
|---|---|---|---|
| Arrows | D-Pad | W A S D | left stick |
| X or Space | A | I J K L | right stick |
| Z | B | Q / E | L / R |
| C | X | 1 / 3 | ZL / ZR |
| V | Y | Enter / Backspace | + / − |

F1 help, F3 screenshot (in `captures/`), F4 FPS limit 60 / unlimited, F5 thread dump, Cmd+F (or Cmd+Enter, F11)
fullscreen, Esc or Cmd+Q quit. On a Mac keyboard the F keys may need the fn key. Hold − to skip a cutscene.

## Settings

There is no Play screen or in-game menu on the Mac yet: the Play screen is Windows-only (Direct3D 11), and the
in-game menu needs the OpenGL renderer. Settings are in `Metroid Dread.ini` in the game folder, which the game rewrites on its
first start with every option explained. `renderer` is ignored: it is always native Vulkan here.

## Differences from Windows

- Native Vulkan is the only renderer, and it is still marked experimental on Windows too.
- No write watch: macOS can't report which pages the game wrote, so the renderer's caches compare memory contents
  instead. This works but may cost speed in busy scenes.
- MoltenVK reports a per-stage limit of 16 samplers while the renderer's layout declares 64 textures per stage. With
  Metal argument buffers (MoltenVK's default) the real limit is far higher, so this should be fine, but it is the
  first thing to check if shaders or pipelines fail: the log says "pipelines may fail" at start either way.
- Not built for macOS: DLSS, HDR output, F7 video recording, H.264 video decoding (OpenH264), frame generation.
- Guest memory uses 4 KB pages inside the Mac's 16 KB pages (`runtime/plat.c`).

## How the port is put together

| file | what |
|---|---|
| `runtime/backend_sdl.c` | SDL2 window with a Vulkan surface, keyboard, controllers, test hooks (the macOS counterpart of `backend_x11.c`) |
| `runtime/snxvk_glue.c` | the native Vulkan renderer's adapter, now for POSIX as well as Windows |
| `runtime/plat.c` | macOS condition variables, 16 KB host pages, crash handler, dialogs, thread QoS |
| `runtime/posix_compat.h` | force-included into the game code: maps the patches' few Windows calls (as `linux/lx_compat.h`) |
| `work-profiler/build_snxvk2.py` | builds `runtime/snxvk/libsnxvk_mac.a` with clang; Apple's `ld -r` does the `snxv_` renaming |
| `switchrecomp.py` | `--target macos`, `--backend sdl` (the defaults on a Mac), `_code.dylib` |
| `third_party/khronos/` | the OpenGL headers macOS lacks: the OpenGL renderer is compiled in but never started |
