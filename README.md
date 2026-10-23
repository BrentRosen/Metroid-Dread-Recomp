# Metroid Dread — recompiled for PC

> **Work in progress.** This project was shared on Reddit and elsewhere before it was ready: the planned first proper
> release is 1.0.0, and current builds (0.x) are early previews. Expect crashes, setup problems and rough edges on
> hardware it has not been tested on. If something breaks, please open an issue and attach the `.log` file from the
> game folder, plus your CPU, GPU, RAM and Windows version.

One Windows EXE that sets up, configures and runs a native PC build of **your own** copy of Metroid Dread 2.1.0.
It is the SwitchRecomp runtime (by MZ Labs) with a Metroid Dread front end built in: first-run setup that compiles
the game with a bundled GCC, a Play / settings screen, and an in-game menu (F1 or the Home / Guide button).

**No Nintendo files are in this repository**: no game code, no RomFS, no keys, no generated C.

- `dread/dread_v210_patches.json` holds small anchored edits that setup applies to the C it generates from the player's ExeFS.
- `dread/romfs_v210.manifest` lists RomFS file names, sizes and SHA-1 hashes, so setup can verify the player's files.

## Layout

| path | what |
|---|---|
| `switchrecomp.py`, `codegen.py`, `a64.py`, `nxloader.py`, `metroid_perf.py` | SwitchRecomp: ARM64 → C translator (Apache-2.0) |
| `runtime/` | the runtime: kernel, services, GPU → OpenGL, audio, video, input |
| `runtime/dread_ui/` | front end: `fe.cpp` (setup, Play, settings), `ov.cpp` (in-game menu), `ui.cpp` (theme, widgets, settings table) |
| `runtime/snxvk/` | ScarletNX's native Vulkan renderer (GPL-3.0, main b5f4f14) in `src_new/`, vendored, symbols prefixed `snxv_`; our DLSS code in `src_new/nx/gpu/vk/dlss.c` |
| `dread/` | setup driver run by the EXE, patch set, RomFS manifest, release script, design notes |
| `third_party/imgui-1.92.5/` | Dear ImGui (MIT) |
| `linux/` | scripts for the Linux / Steam Deck build |

## Build (Windows)

You need two things in the repository root:

- `gcc\` — winlibs GCC 14.2.0, UCRT, POSIX threads, in `gcc\Library\...`
- `python\` — Python 3.12 for Windows
- glslang 16.6.0 (Vulkan SDK or KhronosGroup release), for the renderer's compute shaders: `GLSLANG=` or on PATH

```
python\python.exe work-profiler\build_imgui.py
set GLSLANG=<path to glslang.exe 16.6.0> && set SWR_NO_DLSS=1 && python\python.exe work-profiler\build_snxvk2.py
set SWR_RELEASE=1 && python\python.exe switchrecomp.py --build-runtime --cc gcc
python\python.exe dread\make_release.py release <empty folder> --exe bin\runtime_win64_win32.exe
```

The release step also needs `redist\zlib1.dll` (zlib 1.3.1, x64) and `redist\SDL2.dll` (SDL 2.x x64 release), the two
DLLs the game ships beside it. They are not in this repository.

## Contributors

- **yozander2009-stack** — project lead
- **Claude** (Anthropic) — AI pair programmer: runtime fixes and performance work, the front end and in-game menu,
  setup, the Linux build, testing and documentation

Button-icon packs (Controls > Button icons) are community mods downloaded from GameBanana when a player asks for
them, not included here: Switch to XBOX Layout by Krydar, Switch to PS4 Layout by Nova77, Switch to PS5 Layout by
xSeRosiSx, with PlayStation art from Xelu's FREE Controller Prompts.

The program icon is the [Metroid icon created by Smashicons - Flaticon](https://www.flaticon.com/free-icons/metroid)
(Flaticon licence, free with attribution).

Third-party work this builds on is credited below and on the game's Credits page.
DISCLAIMER
AI was used

## Licences

- SwitchRecomp: Apache-2.0. The credit "SwitchRecomp by MZ Labs" and NOTICE.txt must be kept.
- ScarletNX renderer: GPL-3.0, which makes the combined EXE GPL-3.0.
- Dear ImGui: MIT.
- libvpx and OpenH264: BSD.
- SDL2 and zlib: zlib licence.

NVIDIA DLAA: the code is here (`dlss.c`), NVIDIA's files are not, and the release is built without it. To add it to
your own build, see [DLSS.md](DLSS.md). A build with DLSS is for your own use only: never share it.

Metroid Dread is © Nintendo. This project is not affiliated with or endorsed by Nintendo.
