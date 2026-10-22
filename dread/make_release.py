"""Build the Metroid Dread release and the source-only folder.

  make_release.py release OUT_DIR [--exe PATH]   -> OUT_DIR\\Metroid Dread\\ (the game EXE + recompiler + licences) and a .zip
  make_release.py source  OUT_DIR                -> a clean source tree for GitHub (no game files, no generated code, no binaries
                                                    we don't have the right to publish, no DLSS)

The release EXE is built with SWR_RELEASE=1 (no DLSS, no frame generation): python\\python.exe switchrecomp.py --build-runtime
--cc gcc with SWR_RELEASE=1 set, after work-profiler\\build_imgui.py and SWR_NO_DLSS=1 work-profiler\\build_snxvk.py.
Nothing from Nintendo is copied: setup compiles the player's own files on their PC.
"""
import os, sys, shutil, zipfile, hashlib, fnmatch, time, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GCC = os.path.join(ROOT, 'gcc', 'Library')
GV = '14.2.0'; TRIPLE = 'x86_64-w64-mingw32'
RECOMPILER_PY = ['switchrecomp.py', 'codegen.py', 'a64.py', 'nxloader.py', 'metroid_perf.py']


def cp(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(src, dst)


def cptree(src, dst, ignore=None):
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=ignore)


def slim_gcc(dst):
    """only what compiling C and linking one DLL needs (the full package has clang, gdb, cmake ...: 1.5 GB)"""
    b = os.path.join(GCC, 'bin'); lx = os.path.join(GCC, 'libexec', 'gcc', TRIPLE, GV); lg = os.path.join(GCC, 'lib', 'gcc', TRIPLE, GV)
    for f in ('gcc.exe', 'as.exe', 'ld.exe', 'ld.bfd.exe', 'libiconv-2.dll', 'libwinpthread-1.dll', 'libintl-8.dll', 'zlib1.dll', 'libzstd.dll'):
        cp(os.path.join(b, f), os.path.join(dst, 'bin', f))
    for f in os.listdir(lx):
        if f in ('cc1.exe', 'collect2.exe', 'lto-wrapper.exe', 'lto1.exe', 'liblto_plugin.dll') or f.endswith('.dll'):
            cp(os.path.join(lx, f), os.path.join(dst, 'libexec', 'gcc', TRIPLE, GV, f))
    for f in ('crtbegin.o', 'crtend.o', 'crtfastmath.o', 'libgcc.a', 'libgcc_eh.a'):
        cp(os.path.join(lg, f), os.path.join(dst, 'lib', 'gcc', TRIPLE, GV, f))
    for d in ('include', 'include-fixed'):
        cptree(os.path.join(lg, d), os.path.join(dst, 'lib', 'gcc', TRIPLE, GV, d))
    cptree(os.path.join(GCC, TRIPLE, 'include'), os.path.join(dst, TRIPLE, 'include'))
    cptree(os.path.join(GCC, TRIPLE, 'lib'), os.path.join(dst, TRIPLE, 'lib'))
    for f in ('as.exe', 'ld.exe', 'ld.bfd.exe', 'libiconv-2.dll', 'libintl-8.dll', 'libwinpthread-1.dll', 'libzstd.dll', 'zlib1.dll'):
        cp(os.path.join(GCC, TRIPLE, 'bin', f), os.path.join(dst, TRIPLE, 'bin', f))
    cp(os.path.join(GCC, 'version_info.txt'), os.path.join(dst, 'version_info.txt'))


LICENSES = [
    ('LICENSE.txt', 'SwitchRecomp-Apache-2.0.txt'), ('NOTICE.txt', 'SwitchRecomp-NOTICE.txt'),
    ('runtime/snxvk/LICENSE', 'ScarletNX-GPL-3.0.txt'), ('runtime/snxvk/NOTICE', 'ScarletNX-NOTICE.txt'),
    ('runtime/imgui/LICENSE.txt', 'DearImGui-MIT.txt'), ('runtime/vp9/LICENSE-libvpx.txt', 'libvpx-BSD.txt'),
    ('runtime/oh264/LICENSE', 'OpenH264-BSD.txt'), ('runtime/bcn/LICENSE_texture2ddecoder.txt', 'texture2ddecoder.txt'),
    ('python/LICENSE_PYTHON.txt', 'Python-PSF.txt'),
]
ZLIB_TXT = """zlib (zlib1.dll) and SDL2 (SDL2.dll) are under the zlib licence:

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held
liable for any damages arising from the use of this software. Permission is granted to anyone to use this software for
any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following
restrictions:
1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If
   you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

zlib: Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler.  SDL2: Copyright (C) 1997-2024 Sam Lantinga.
"""
GCC_TXT = """recompiler\\gcc is GCC %s and GNU binutils (MinGW-w64, built by Brecht Sanders / winlibs.com).
GCC and binutils are GPL-3.0; the GCC runtime library exception covers code they compile. The MinGW-w64 runtime and headers
are under the ZPL / public domain / MIT-style terms listed in their headers. Source: https://gcc.gnu.org, https://sourceware.org/binutils,
https://www.mingw-w64.org, https://github.com/brechtsanders/winlibs_mingw (the exact version is in recompiler\\gcc\\version_info.txt).
""" % GV

README = """METROID DREAD - recompiled for PC
=================================

This is not the game. It turns YOUR OWN copy of Metroid Dread (version 2.1.0) into a native Windows game:
setup compiles it on your PC. No Nintendo files, keys or firmware are included.

1. Extract your game with your own keys, with the 2.1.0 update merged, for example:
       hactool -k prod.keys --basenca=base.nca --exefsdir=exefs --romfsdir=romfs update.nca
   (or use an emulator's "Extract RomFS" / "Extract ExeFS" with the update installed).
2. Start "Metroid Dread.exe". The first time it asks for that folder (and, if you extracted the update separately,
   the update folder), checks every file against the 2.1.0 file list and compiles the game with the bundled GCC.
   That takes a few minutes and about 7 GB of space next to the EXE (less when your game files are on the same drive).
3. Then it opens to the Play screen. Settings, controller set-up, mods and save backups are all there.
   In the game, F1 (or the Home / Guide button) opens the menu; most settings change right away.

Controllers: Xbox controllers work as they are; SDL2.dll (included) adds Switch Pro, Joy-Con, DualShock 4, DualSense
and 8BitDo over USB or Bluetooth. Keyboard: arrows = D-pad, WASD = stick, X/Z/C/V = A/B/X/Y, Enter = +, Backspace = -.

Credits: SwitchRecomp by MZ Labs (recompiler and runtime); ScarletNX (native Vulkan renderer); theboy181 (60 FPS cutscenes,
21:9); Dear ImGui; SDL2; zlib; libvpx; OpenH264; GCC / MinGW-w64; Python. Licences are in the licenses folder; the EXE is
GPL-3.0 (it contains the ScarletNX renderer) and its source is published with this release.
Metroid Dread is (c) Nintendo. This project is not affiliated with or endorsed by Nintendo.
"""


def redist(name):
    """a DLL the game ships beside it: redist\\ (a clone's), else games\\ (the development folder's)"""
    for d in ('redist', 'games'):
        p = os.path.join(ROOT, d, name)
        if os.path.isfile(p): return p
    raise SystemExit('%s not found: put it in %s. zlib1.dll: zlib 1.3.1 built for x64 (MinGW); SDL2.dll: SDL 2.x x64 '
                     'release (github.com/libsdl-org/SDL/releases)' % (name, os.path.join(ROOT, 'redist')))


def release(out, exe):
    dst = os.path.join(out, 'Metroid Dread')
    if os.path.exists(dst):
        raise SystemExit('%s exists: choose a new folder' % dst)
    os.makedirs(dst)
    cp(exe, os.path.join(dst, 'Metroid Dread.exe'))
    for f in ('zlib1.dll', 'SDL2.dll'):
        cp(redist(f), os.path.join(dst, f))
    rc = os.path.join(dst, 'recompiler')
    for f in RECOMPILER_PY:
        cp(os.path.join(ROOT, f), os.path.join(rc, f))
    cp(os.path.join(ROOT, 'dread', 'dread_setup.py'), os.path.join(rc, 'dread_setup.py'))
    for f in ('dread_v210_patches.json', 'romfs_v210.manifest'):
        cp(os.path.join(ROOT, 'dread', f), os.path.join(rc, 'dread', f))
    for f in ('cpu.h', 'cpu_helpers.c'):
        cp(os.path.join(ROOT, 'runtime', f), os.path.join(rc, 'runtime', f))
    cptree(os.path.join(ROOT, 'python'), os.path.join(rc, 'python'), ignore=shutil.ignore_patterns('__pycache__'))
    slim_gcc(os.path.join(rc, 'gcc', 'Library'))
    lic = os.path.join(dst, 'licenses')
    for s, d in LICENSES:
        cp(os.path.join(ROOT, s), os.path.join(lic, d))
    open(os.path.join(lic, 'zlib-and-SDL2.txt'), 'w').write(ZLIB_TXT)
    open(os.path.join(lic, 'GCC-binutils-MinGW.txt'), 'w').write(GCC_TXT)
    open(os.path.join(lic, 'theboy181-patches.txt'), 'w').write(
        'The 60 FPS cutscene and 21:9 patches are ports of theboy181\'s Metroid Dread v2.1.0 patches\n'
        '(https://github.com/theboy181/switch-ptchtxt-mods); 21:9 commissioned by Nikorasu081.\n')
    open(os.path.join(dst, 'README.txt'), 'w', newline='\r\n').write(README)
    # one zip to download
    z = os.path.join(out, 'MetroidDread-PC-%s.zip' % time.strftime('%Y%m%d'))
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for dp, dn, fn in os.walk(dst):
            for n in fn:
                p = os.path.join(dp, n); zf.write(p, os.path.relpath(p, out))
    size = sum(os.path.getsize(os.path.join(dp, n)) for dp, dn, fn in os.walk(dst) for n in fn)
    print('release: %s (%.0f MB), zip %s (%.0f MB)' % (dst, size / 2**20, z, os.path.getsize(z) / 2**20))


SRC_IGNORE = ['*.o', '*.a', '*.obj', '*.exe', '*.dll', '__pycache__', 'obj', 'obj_nodlss', 'obj_new', 'obj_new_nodlss',
              '*.tmp', '*.before*', '*.orig', '*.rej', 'externals.txt',
              'lsfg.c', 'lsfg_spvc.cpp', 'spvc']   # no frame generation (it loads Lossless Scaling's DLL) in the public source


def src_ignore(d, names):
    out = set(shutil.ignore_patterns(*SRC_IGNORE)(d, names))
    n = os.path.normpath(d)
    if n == os.path.normpath(os.path.join(ROOT, 'runtime')): out |= {'imgui'} & set(names)   # made by build_imgui.py
    if n == os.path.normpath(os.path.join(ROOT, 'runtime', 'snxvk')): out |= {'src'} & set(names)   # the old renderer: src_new is it
    # the renderer's dependencies: only what the build reads, and their licences. Their docs and tools are not needed,
    # and their deep paths passed Windows' 260-character limit in a clone (MEASURED 2026-10-22)
    if os.path.dirname(n) == os.path.normpath(os.path.join(ROOT, 'runtime', 'snxvk', 'deps')):
        keep = {'include', 'LICENSE.md', 'LICENSE.txt', 'LICENSES', 'README.md', 'volk.c', 'volk.h'}
        out |= set(names) - keep
    # ScarletNX's game-file driver (container extraction and hactool's decryption): none of it is in our build, only
    # its headers are included on the way to fs.h, so the public source carries the headers and not the code
    drv = os.path.normpath(os.path.join(ROOT, 'runtime', 'snxvk', 'src_new', 'nx', 'runtime', 'driver'))
    if n == drv or n.startswith(drv + os.sep):
        out |= {x for x in names if x.endswith('.c')}
    return out


def source(out):
    if os.path.exists(out) and os.listdir(out):
        raise SystemExit('%s is not empty' % out)
    os.makedirs(out, exist_ok=True)
    for f in RECOMPILER_PY + ['LICENSE.txt', 'NOTICE.txt']:
        cp(os.path.join(ROOT, f), os.path.join(out, f))
    cptree(os.path.join(ROOT, 'runtime'), os.path.join(out, 'runtime'), ignore=src_ignore)
    # prebuilt third-party libraries the build links (BSD licences, sources linked in their READMEs)
    for rel in ('runtime/vp9/win/libvpx.a', 'runtime/oh264/win/libdecoder.a', 'runtime/oh264/win/libcommon.a'):
        if os.path.isfile(os.path.join(ROOT, rel)):
            cp(os.path.join(ROOT, rel), os.path.join(out, rel))
    for f in ('dread_setup.py', 'make_release.py', 'make_patches.py', 'make_manifest.py', 'dread_v210_patches.json', 'romfs_v210.manifest', 'DESIGN.md'):
        cp(os.path.join(ROOT, 'dread', f), os.path.join(out, 'dread', f))
    im = os.path.join(ROOT, 'third_party', 'imgui-1.92.5')
    for f in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui.h', 'imconfig.h', 'imgui_internal.h',
              'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h', 'LICENSE.txt'):
        cp(os.path.join(im, f), os.path.join(out, 'third_party', 'imgui-1.92.5', f))
    for f in ('imgui_impl_win32', 'imgui_impl_opengl3', 'imgui_impl_dx11'):
        for ext in ('.cpp', '.h'):
            cp(os.path.join(im, 'backends', f + ext), os.path.join(out, 'third_party', 'imgui-1.92.5', 'backends', f + ext))
    cp(os.path.join(im, 'backends', 'imgui_impl_opengl3_loader.h'), os.path.join(out, 'third_party', 'imgui-1.92.5', 'backends', 'imgui_impl_opengl3_loader.h'))
    for f in ('build_imgui.py', 'build_snxvk2.py', 'gen_spv_headers.py'):
        cp(os.path.join(ROOT, 'work-profiler', f), os.path.join(out, 'work-profiler', f))
    for f in os.listdir(os.path.join(ROOT, 'linux')):
        p = os.path.join(ROOT, 'linux', f)
        if os.path.isfile(p) and (f.endswith('.sh') or f.endswith('.h') or f.endswith('.txt')):
            cp(p, os.path.join(out, 'linux', f))
    cp(os.path.join(ROOT, 'runtime', 'snxvk', 'LICENSE'), os.path.join(out, 'LICENSE-GPL-3.0.txt'))
    open(os.path.join(out, '.gitignore'), 'w').write('\n'.join(['gcc/', 'python/', 'bin/', 'games/', '*_build/', '*_data/', '*_code.dll', '*.o', '*.a',
        '!runtime/vp9/win/libvpx.a', '!runtime/oh264/win/*.a', 'runtime/imgui/', 'redist/', 'cleanbuild_test/', 'runtime/snxvk/obj*/', '__pycache__/', 'release/', '*.log', 'shader_cache/', 'vulkan_native/', '']))
    # line endings exactly as written (some runtime files are CRLF by rule, the Linux scripts must stay LF)
    open(os.path.join(out, '.gitattributes'), 'w', newline='\n').write('* -text\n')
    open(os.path.join(out, 'README.md'), 'w').write(SOURCE_README)
    cp(os.path.join(ROOT, 'dread', 'DLSS.md'), os.path.join(out, 'DLSS.md'))
    n = sum(len(fn) for dp, dn, fn in os.walk(out))
    print('source: %s (%d files)' % (out, n))


SOURCE_README = """# Metroid Dread — recompiled for PC

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

- `gcc\\` — winlibs GCC 14.2.0, UCRT, POSIX threads, in `gcc\\Library\\...`
- `python\\` — Python 3.12 for Windows
- glslang 16.6.0 (Vulkan SDK or KhronosGroup release), for the renderer's compute shaders: `GLSLANG=` or on PATH

```
python\\python.exe work-profiler\\build_imgui.py
set GLSLANG=<path to glslang.exe 16.6.0> && set SWR_NO_DLSS=1 && python\\python.exe work-profiler\\build_snxvk2.py
set SWR_RELEASE=1 && python\\python.exe switchrecomp.py --build-runtime --cc gcc
python\\python.exe dread\\make_release.py release <empty folder> --exe bin\\runtime_win64_win32.exe
```

The release step also needs `redist\\zlib1.dll` (zlib 1.3.1, x64) and `redist\\SDL2.dll` (SDL 2.x x64 release), the two
DLLs the game ships beside it. They are not in this repository.

## Licences

- SwitchRecomp: Apache-2.0. The credit "SwitchRecomp by MZ Labs" and NOTICE.txt must be kept.
- ScarletNX renderer: GPL-3.0, which makes the combined EXE GPL-3.0.
- Dear ImGui: MIT.
- libvpx and OpenH264: BSD.
- SDL2 and zlib: zlib licence.

NVIDIA DLAA: the code is here (`dlss.c`), NVIDIA's files are not, and the release is built without it. To add it to
your own build, see [DLSS.md](DLSS.md). A build with DLSS is for your own use only: never share it.

Metroid Dread is © Nintendo. This project is not affiliated with or endorsed by Nintendo.
"""

if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    if sys.argv[1] == 'release':
        exe = os.path.join(ROOT, 'bin', 'runtime_win64_win32.exe')
        for a in sys.argv[3:]:
            if a.startswith('--exe='):
                exe = a[6:]
        release(os.path.abspath(sys.argv[2]), exe)
    elif sys.argv[1] == 'source':
        source(os.path.abspath(sys.argv[2]))
    else:
        raise SystemExit(__doc__)
