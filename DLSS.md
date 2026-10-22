# Adding NVIDIA DLAA (DLSS) to your own build

The game can use NVIDIA DLSS as **DLAA**: anti-aliasing at the game's own resolution (no upscaling). It works with
the **Native Vulkan** renderer on an **NVIDIA RTX** graphics card.

The code that drives it is in this repository (`runtime/snxvk/src_new/nx/gpu/vk/dlss.c`). NVIDIA's files are not,
and the public release is built without DLSS. To use it, you get NVIDIA's file yourself and build the game once
with DLSS switched on, as described below.

> **For your own use only.** The program is GPL-3.0 (because of the ScarletNX renderer) and NVIDIA's DLSS licence
> does not allow DLSS to be shipped with GPL software. A build with DLSS in it, or `nvngx_dlss.dll`, must never be
> shared, uploaded or bundled with anything. The public release stays DLSS-free.

## What you need

- An NVIDIA RTX card (20-series or newer) and a current NVIDIA driver. The driver brings the DLSS runtime itself
  (NGX); nothing else to install for that.
- The game already set up with this project (the Play screen works).
- The build tools from the main [README](README.md): `gcc\`, `python\` and glslang 16.6.0.

## 1. Get NVIDIA's DLSS file

1. Open **https://github.com/NVIDIA/DLSS** (NVIDIA's official DLSS SDK).
2. Read the licence (`LICENSE.txt` in that repository). Downloading the file means you accept it.
3. Download the repository: **Code → Download ZIP** (or pick a version under *Releases*). Extract it anywhere.
4. In the extracted folder, take this one file:

   ```
   lib\Windows_x86_64\rel\nvngx_dlss.dll
   ```

   Use the one in **`rel`**. The one in `dev` is a development build that draws debug text over the picture.
   Tested with version **310.9.16**; newer versions should work the same way.

5. Copy `nvngx_dlss.dll` into your game folder, next to `Metroid Dread.exe`.

## 2. Build the game with DLSS on

From the repository root, the same steps as the README with two differences: **no `SWR_NO_DLSS`** and **no
`SWR_RELEASE`**:

```
python\python.exe work-profiler\build_imgui.py
set GLSLANG=<path to glslang.exe 16.6.0>
python\python.exe work-profiler\build_snxvk2.py
python\python.exe switchrecomp.py --build-runtime --cc gcc
```

`build_snxvk2.py` then builds the renderer with `dlss.c` (`runtime\snxvk\libsnxvk_new.a`), and the last step writes
`bin\runtime_win64_win32.exe`.

Keep a copy of your current `Metroid Dread.exe`, then copy `bin\runtime_win64_win32.exe` over it. Nothing else in the
game folder changes; your saves and settings stay.

## 3. Turn it on

On the Play screen: **Graphics → Renderer → Native Vulkan**. The option **NVIDIA DLAA anti-aliasing** then appears
(only when `nvngx_dlss.dll` is next to the EXE). Switch it on and start the game.

Or in `Metroid Dread.ini`:

```
renderer=vulkan_native
dlss=1
```

It takes effect at the next start.

## Checking that it works

`Metroid Dread.log` in the game folder shows, near the start:

```
dlss: NGX up; DLSS available
```

and, once you are in the game (the size is your internal resolution, here 2x):

```
dlss: DLSS feature 3200x1800 created (quality value 5)
```

## Troubleshooting

| what you see | why |
|---|---|
| no DLAA option on the Play screen | `nvngx_dlss.dll` is not next to `Metroid Dread.exe`, the renderer is not Native Vulkan, or the EXE is the public release (built without DLSS) |
| the log says NGX or DLSS is not available | not an RTX card, or the NVIDIA driver is too old: update it |
| debug text over the picture | the `dev` copy of `nvngx_dlss.dll` was used: take the one from `rel` |
| lower FPS than without DLAA | expected: DLAA costs GPU time, and the renderer records commands on one thread while DLSS is on |

To turn it off again: `dlss=0`, or switch the option off on the Play screen.

## How it works (for developers)

`dlss.c` loads NVIDIA's NGX core from the graphics driver at run time (found through the registry); no NVIDIA SDK
code or header is compiled in. The scene is handed to DLSS before the game's bloom and post effects; motion vectors
come from replaying the game's depth-prepass draws with this and last frame's positions (`shaders/dlss_mv.vert`,
`dlss_mv.frag`, compiled into `dlss_mv_spv.h` by `work-profiler/gen_spv_headers.py`); the camera jitter is a
Halton(2,3) sequence. Without `nvngx_dlss.dll`, or on other GPUs, the game runs without it.
