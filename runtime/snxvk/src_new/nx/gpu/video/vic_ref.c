/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported from Eden (src/video_core/host1x/vic.cpp):
   SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
   SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project */
/* The VIC composition on the CPU, byte by byte as vic.comp does it: the reference the tests hold the shader to
   (tests/glsl/vic_jobs.c, tests/vic_compose.c). The runtime composes every job on the GPU (docs/GPU_OWNERSHIP.md),
   so this file isn't built into it */
#include <string.h>
#include "nx/gpu/video/vic.h"

static u64 plane_off(const VicPlane* p, u32 x, u32 y) {
    return p->blk ? bl_offset(x, y, 0, p->pitch, p->h, p->bh, 0) : (u64)y * p->pitch + x;
}
/* bytes past the mapping read as 0 and take no writes */
static u32 plane_rd(const VicPlane* p, u32 x, u32 y) {
    u64 o = plane_off(p, x, y);
    return o < p->lim ? p->p[o] : 0;
}
static void plane_wr(const VicPlane* p, u64 o, u8 v) {
    if (o < p->lim) p->p[o] = v;
}

static void slot_pixel(const VicSlot* sl, u32 x, u32 y, u32 px[4]) {
    u32 sx = sl->sx0 + (x - sl->dx0), sy = sl->sy0 + (y - sl->dy0), kc = sx & ~1u, c;
    int q[3];
    q[0] = (int)(plane_rd(&sl->iy, sx, sy) << 2);
    q[1] = (int)(plane_rd(&sl->ic, kc + sl->vu, sy >> 1) << 2);
    q[2] = (int)(plane_rd(&sl->ic, kc + 1 - sl->vu, sy >> 1) << 2);
    for (c = 0; c < 3; c++) {
        int v = q[c];
        if (sl->mat_enable) {
            v = q[0] * sl->mat[c][0] + q[1] * sl->mat[c][1] + q[2] * sl->mat[c][2];
            v = ((v >> sl->mat_shift) + sl->mat[c][3]) >> 8;
            v = v < (int)sl->clamp_lo ? (int)sl->clamp_lo : v > (int)sl->clamp_hi ? (int)sl->clamp_hi : v;
        }
        px[c] = (u32)v;
    }
    px[3] = sl->alpha;
}
void vic_compose_ref(const VicJob* j) {
    u32 x, y, s, k, yuv = j->fmt == FMT_NV12 || j->fmt == FMT_NV21, vu = j->fmt == FMT_NV21;
    for (y = 0; y < j->sh; y++)
        for (x = 0; x < j->sw; x++) {
            u32 px[4];
            int covered = x >= j->tx0 && x - j->tx0 < j->tw && y >= j->ty0 && y - j->ty0 < j->th;
            memcpy(px, j->bg, sizeof px);
            for (s = 0; s < 8; s++) {
                const VicSlot* sl = &j->slots[s];
                if (!sl->on || x < sl->dx0 || x - sl->dx0 >= sl->w || y < sl->dy0 || y - sl->dy0 >= sl->h) continue;
                slot_pixel(sl, x, y, px);
                covered = 1;
            }
            if (!covered) continue;
            if (!yuv) {
                u64 o = plane_off(&j->oy, 4 * x, y);
                u32 rb = j->fmt == FMT_ARGB ? 2 : 0; /* A8R8G8B8 stores R and B the other way around */
                plane_wr(&j->oy, o + rb, (u8)(px[0] >> 2));
                plane_wr(&j->oy, o + 1, (u8)(px[1] >> 2));
                plane_wr(&j->oy, o + 2 - rb, (u8)(px[2] >> 2));
                plane_wr(&j->oy, o + 3, (u8)(px[3] >> 2));
                continue;
            }
            plane_wr(&j->oy, plane_off(&j->oy, x, y), (u8)(px[0] >> 2));
            /* a pair's chroma comes from its even pixel on the even row */
            if (!(x & 1) && !(y & 1) && j->oc.p) {
                u64 o = plane_off(&j->oc, x, y >> 1);
                for (k = 0; k < 2; k++) plane_wr(&j->oc, o + (k ^ vu), (u8)(px[1 + k] >> 2));
            }
        }
}
