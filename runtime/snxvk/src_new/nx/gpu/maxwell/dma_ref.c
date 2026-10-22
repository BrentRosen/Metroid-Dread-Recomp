/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported from Eden (src/video_core/engines/maxwell_dma.cpp, src/video_core/textures/decoders.cpp):
   SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
   SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project */
/* A launch of the DMA copy engine done on the CPU in guest memory, as maxwell/dma.c did it before it went to the GPU:
   the reference the tests hold the renderer's dma_copy to (tests/vulkan_headless.c). The runtime copies on the GPU
   (vk/dma.c, docs/GPU_OWNERSHIP.md), so this file isn't built into it */
#include <stdlib.h>
#include <string.h>
#include "common/utilities/endian.h"
#include "nx/gpu/gpu.h"

static u64 span_of(const GpuDmaSide* s, u32 lines, u32 line) {
    if (!s->bl) return (u64)s->pitch * (lines - 1) + line;
    u64 gobs_x = ((u64)s->wb + 63) >> 6, rows = ((((u64)s->h + 7) >> 3) + (1u << s->bh) - 1) >> s->bh;
    return gobs_x * rows * ((u64)512 << (s->bh + s->bd)) * ((u64)(s->z >> s->bd) + 1);
}
/* lines of line bytes of an operand into buf (line bytes apart). from: a copy of a pitch linear operand */
static void ref_read(const GpuDmaSide* s, u32 lines, u32 line, const u8* from, u8* buf) {
    u32 y, x;
    if (from) {
        for (y = 0; y < lines; y++) memcpy(buf + (u64)y * line, from + (u64)y * s->pitch, line);
        return;
    }
    for (y = 0; y < lines; y++) {
        u8* d = buf + (u64)y * line;
        u64 row;
        if (!s->bl) {
            gmem_read_raw(s->addr + (u64)y * s->pitch, d, line);
            continue;
        }
        if (s->oy + y >= s->h) { /* past the surface */
            memset(d, 0, line);
            continue;
        }
        row = bl_offset(0, s->oy + y, s->z, s->wb, s->h, s->bh, s->bd);
        for (x = 0; x < line;) {
            u32 at = s->ox + x, k = 16 - (at & 15);
            if (k > line - x) k = line - x;
            gmem_read_raw(s->addr + row + bl_offset(at, 0, 0, s->wb, s->h, s->bh, s->bd), d + x, k);
            x += k;
        }
    }
}
static void ref_write(const GpuDmaSide* s, u32 lines, u32 line, const u8* buf) {
    u32 y, x;
    for (y = 0; y < lines; y++) {
        const u8* src = buf + (u64)y * line;
        u64 row;
        if (!s->bl) {
            gmem_write_raw(s->addr + (u64)y * s->pitch, src, line);
            continue;
        }
        if (s->oy + y >= s->h) break;
        row = bl_offset(0, s->oy + y, s->z, s->wb, s->h, s->bh, s->bd);
        for (x = 0; x < line;) {
            u32 at = s->ox + x, k = 16 - (at & 15);
            if (k > line - x) k = line - x;
            gmem_write_raw(s->addr + row + bl_offset(at, 0, 0, s->wb, s->h, s->bh, s->bd), src + x, k);
            x += k;
        }
    }
    gpu_mem_written(s->addr, span_of(s, lines, line));
}
/* the remap: each destination component from a source component, constant A or B, or left as it is */
static void ref_remap(const GpuDmaOp* o, const u8* in, u8* out) {
    u64 i, n = (u64)o->width * o->lines;
    u32 k;
    for (i = 0; i < n; i++) {
        const u8* s = in ? in + i * o->cs * o->ns : NULL;
        u8* d = out + i * o->cs * o->nd;
        for (k = 0; k < o->nd; k++) {
            u32 sel = o->sel[k];
            if (sel == 4 || sel == 5) { /* the constant's low bytes, as guest memory orders them */
                u8 c[4];
                snx_st_le32(c, sel == 4 ? o->const_a : o->const_b);
                memcpy(d + k * o->cs, c, o->cs);
            } else if (sel < 4) {
                if (s && sel < o->ns)
                    memcpy(d + k * o->cs, s + sel * o->cs, o->cs);
                else
                    memset(d + k * o->cs, 0, o->cs);
            }
        }
    }
}
void dma_copy_ref(const GpuDmaOp* o) {
    u64 in_bytes = o->fill ? 0 : (u64)o->line_in * o->lines;
    u64 out_bytes = o->remap && (o->fill || o->shuffle) ? (u64)o->line_out * o->lines : 0;
    u8 *buf = (u8*)malloc((size_t)(in_bytes + out_bytes) + 1), *out;
    if (!buf) return;
    if (!o->fill) ref_read(&o->src, o->lines, o->line_in, o->snap, buf);
    out = buf;
    if (out_bytes) {
        out = buf + in_bytes;
        if (o->keep)
            ref_read(&o->dst, o->lines, o->line_out, NULL, out);
        else
            memset(out, 0, (size_t)out_bytes);
        ref_remap(o, o->fill ? NULL : buf, out);
    }
    ref_write(&o->dst, o->lines, o->line_out, out);
    free(buf);
}
