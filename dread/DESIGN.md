# Metroid Dread (recompiled) — one-EXE launcher: architecture and parity checklist

## Architecture decision

**One process, one window.** `Metroid Dread.exe` is the runtime itself (the SwitchRecomp runtime + the
Metroid Dread front end). Started without arguments it opens its window and shows the front end in it:

1. **Setup** (first run, or when the compiled game is missing/invalid): pick the game files, validate,
   compile, then go on to Play.
2. **Play / settings** (every later run).
3. **Play** loads the compiled game code (`Metroid Dread_code.dll`) into the same process, and the game
   draws into the same window.

Why the front end is drawn with **Direct3D 11** and not OpenGL:

- A Win32 window's pixel format can be set only once (`SetPixelFormat`).
- The game window gets an OpenGL pixel format for `renderer=opengl`.
- `renderer=vulkan_native` needs a window *without* one (a Vulkan swapchain on a GL-formatted window is
  unreliable; the runtime already avoids it and keeps GL on a hidden window for `renderer=vulkan`).
- A D3D11/DXGI flip swapchain does not set a pixel format and can be released completely.

So the front end renders with D3D11 on the game's own `HWND`, releases its swapchain and device when you
press Play, and the runtime then sets up OpenGL or Vulkan on that same window. No second EXE and no
second window are involved.

**What still runs as a child process: the compiler, during setup only.**

- Recompiling needs the SwitchRecomp translator (Python) and GCC: `python.exe` (bundled), which runs
  `gcc.exe` (bundled).
- They are separate programs by nature. The EXE starts them and shows their output with a progress bar.
- Nothing is extracted or spawned to *play*.

**Where things live.** The release is one folder (one download):

- `Metroid Dread.exe`
- `recompiler\`: Python, GCC (C only), the translator, the Dread v2.1.0 patch set
- `zlib1.dll`, `SDL2.dll`
- licences

Setup writes, next to the EXE:

- `Metroid Dread_code.dll` (compiled game code)
- `Metroid Dread_data\` (exefs copy and merged RomFS, hard-linked when possible, plus saves and mods)
- `Metroid Dread_build\` (the generated C, for resume/rebuild)

No Nintendo files are shipped.

**In-game menu.**

- F1 (or a configurable controller shortcut, default the Home/Guide button) opens a menu drawn with
  Dear ImGui's OpenGL3 backend on the GL thread, right before the swap.
- Input is queued from the window thread to the GL thread.
- While it is open, the game receives neutral input. On close, every button must be released before
  the game sees it again, so no inputs get stuck.
- The game keeps running at normal speed.
- Settings are applied through the runtime's existing INI path, so they are persisted and live where
  the runtime supports it.
- Native Vulkan: the menu is not drawn yet (documented limitation).

## Validation (setup)

- **ExeFS:** `main` must be an NSO with build ID `646761F643AFEBB379EDD5E6A5151AF2CEF93DC1` (v2.1.0). The
  `main.npdm` program ID must be `010093801237C000`.
  - Another title ID: "this is not Metroid Dread".
  - Dread's own base `main` (different build ID): "this is the base game's ExeFS: pick the update's
    ExeFS".
- **RomFS:**
  - The final RomFS is checked against a manifest of the v2.1.0 RomFS: every path and size, and SHA-1 of
    each file (hashes and names only, no content).
  - Inputs accepted:
    - one folder with `exefs\` + an already merged `romfs\`;
    - or a base folder plus an update folder. The update's RomFS files are layered over the base, then
      the result must match the manifest.
  - An update RomFS that cannot be layered (an unmerged BKTR patch) fails the manifest check. The error
    explains how to get a merged RomFS (extract with the update applied, for example an emulator's
    "Extract RomFS" with the update installed).
- **Resume/recovery:** every stage writes a marker. A failed or interrupted compile keeps the generated
  C and the objects that finished; "Retry" continues from there. The full log is `setup.log`, shown
  live.

## Feature parity with `dread_launcher.py`

Game INI keys written by the Python launcher (all must be in the new UI):

| key | page | live in game? |
|---|---|---|
| docked | Display | restart |
| fullscreen | Display | yes (F11 / menu) |
| language | Display | restart |
| aspect, ultrawide | Display | restart (load-time patch) |
| resolution | Graphics | yes |
| dynamic_resolution, dynamic_resolution_min | Graphics | yes |
| game_dynamic_resolution | Graphics | yes |
| shadows | Graphics | yes |
| renderer | Graphics | restart |
| dlss | Graphics (only when nvngx_dlss.dll is present) | restart |
| prebuild_shaders | Advanced | restart |
| hdr, hdr_paper_white | Graphics | restart |
| antialiasing | Graphics | yes |
| anisotropy | Graphics | restart |
| fps_limit | Gameplay | yes |
| forced_vsync | Gameplay | yes |
| fast_loading | Gameplay | yes |
| fast_forward, fast_forward_speed | Gameplay | yes |
| sleep_waits | Advanced | yes |
| texture_threads | Advanced | restart |
| low_priority_shader_compiler | Advanced | restart |
| memory_gb | Advanced | restart |
| audio_eos_sample_count, audio_surround_downmix | Audio | yes |
| controller_map | Controls | yes |
| rumble | Controls | yes |
| controller_api | Controls | restart |
| controller_index | Controls | yes |
| stick_deadzone | Controls | yes |
| smooth_recording, recording_fps | Advanced | yes |
| log, diag, profile | Advanced | partly (profile: restart) |

Launcher-only settings (Python: `%APPDATA%\DreadLauncher\config.json`, migrated into the INI `[Launcher]`
section on first run):

- `sync_shaders` → `wait_for_shaders`
- `loading_60fps` → `loading_60fps`
- `cutscenes_60fps` → `cutscenes_60fps`
- `controller_navigation`, `controller_prompts`, `launcher_bindings` → `launcher_nav`,
  `launcher_prompts`, `launcher_bindings`

New: `volume` (master output level, live), `menu_shortcut`.

Other Python-launcher features:

- [ ] Play page: renderer / resolution / FPS tiles, controller status, save backup + open backups /
      saves, game folder, screenshots.
- [ ] Recommended settings for this PC (GPU tier, screen size, refresh rate, cores).
- [ ] Resolution presets, Native button, effective pixel size *inside the slider* for 16:9 / 21:9 /
      16:10 / handheld.
- [ ] Controls:
  - SDL2 present or not, and an install button;
  - controller list, player-1 pick, live tester, dead zone, rumble with a test pulse;
  - game button mapping with presets;
  - launcher navigation bindings with a lockout-proof reset;
  - button prompt style;
  - keyboard reference.
- [ ] Mods: list, enable/disable, order, remove, add .zip / folder (Atmosphere layout recognised),
      `load_order.txt` format unchanged.
- [ ] Advanced: CPU, shaders (wait vs. background, pre-build, low priority), memory, audio, recording,
      diagnostics, open the INI.
- [ ] Settings migration from the existing installation (INI + launcher JSON) and save import
      (copy, never move).
- [ ] About / Credits.
