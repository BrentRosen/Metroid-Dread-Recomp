/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* depth.comp's SPIR-V. The words are in depth_spv.c, which the build makes from the shader with glslang
   (cmake/glsl.cmake): nothing compiled is checked in. */
#ifndef SNX_CTR_DEPTH_SPV_H
#define SNX_CTR_DEPTH_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 snx_ctr_depth_spv[];
extern const u64 snx_ctr_depth_spv_size; /* in bytes */

#endif
