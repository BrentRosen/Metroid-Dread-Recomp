/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_A64_EMIT_H
#define SNX_A64_EMIT_H

#include "common/cpu/aarch64/a64.h"

/* The C emitter: the reference the LLVM backend is compared against, and the interpreter the old AOT and fallback
   paths used. It is built into snx_a64_reference for tests, not into the production library. */
void snx_a64_hex(FILE* out, SnxU64 value);
void snx_a64_literal(FILE* out, SnxU64 value);
void snx_a64_scope(FILE* out, SnxU32 word, SnxU64 pc, const SnxA64Case* instruction);
void snx_a64_body(FILE* out, const char* body, const char* undefined);
void snx_a64_interp(FILE* out, int step_mode);
void snx_a64_interp_checked(FILE* out);
void snx_a64_static_step(FILE* out, SnxU32 word, SnxU64 pc, const char* name);
int snx_a64_fast_simd(FILE* out, SnxU32 word);

#endif
