/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A 2D engine blit from memory done on the CPU in guest memory, pixel by pixel: the reference the tests hold the
   renderer's blit_mem to (tests/fermi2d_blit.c, tests/vulkan_headless.c). The runtime blits on the GPU (vk/blit.c,
   docs/GPU_OWNERSHIP.md), so this file isn't built into it */
#include "nx/gpu/gpu.h"

/* the byte offset of pixel (x, y) of a plane */
static u64 px_off(const GpuDmaSide* s, u32 bpp, u32 x, u32 y) {
    return s->bl ? bl_offset(x * bpp, y, s->z, s->wb, s->h, s->bh, s->bd) : (u64)y * s->pitch + (u64)x * bpp;
}
void blit_mem_ref(const GpuBlit* b) {
    u32 x, y;
    u8 px[16];
    for (y = b->y0; y < b->y1; y++)
        for (x = b->x0; x < b->x1; x++) {
            gmem_read_raw(b->src.addr + px_off(&b->src, b->bpp, b->scol[x - b->x0], b->srow[y - b->y0]), px, b->bpp);
            gmem_write_raw(b->dst.addr + px_off(&b->dst, b->bpp, x, y), px, b->bpp);
        }
}
