"""Compile runtime/snxvk/src/nx/gpu/vk/shaders/hdr_out.{vert,frag} with glslang into hdr_spv.h (as dlss_mv_spv.h was)."""
import subprocess, struct, os
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SH = ROOT / 'runtime/snxvk/src/nx/gpu/vk/shaders'
import shutil
GLSLANG = (os.environ.get('GLSLANG') or shutil.which('glslang')   # glslang 16.6.0
           or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'third_party', 'glslang', 'bin', 'glslang.exe'))
out = ['/* generated from hdr_out.vert / hdr_out.frag by glslang 16.6.0 (glslang -V); see work-profiler/gen_hdr_spv.py */']
for stage, name in (('vert', 'g_hdr_out_vert'), ('frag', 'g_hdr_out_frag')):
    spv = SH / f'hdr_out.{stage}.spv'
    subprocess.run([GLSLANG, '-V', str(SH / f'hdr_out.{stage}'), '-o', str(spv)], check=True)
    d = spv.read_bytes(); os.remove(spv)
    w = struct.unpack(f'<{len(d) // 4}I', d)
    out.append(f'static const unsigned int {name}[{len(w)}] = {{')
    for i in range(0, len(w), 8): out.append('    ' + ', '.join(f'0x{x:08x}' for x in w[i:i + 8]) + ',')
    out.append('};')
(ROOT / 'runtime/snxvk/src/nx/gpu/vk/hdr_spv.h').write_text('\n'.join(out) + '\n')
print('hdr_spv.h written')
