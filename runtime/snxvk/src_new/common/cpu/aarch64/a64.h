/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_TOOL_A64_H
#define SNX_TOOL_A64_H

#include "common/utilities/common.h"
#include <stddef.h>
#include <stdio.h>

enum SnxFlow {
    SNX_FLOW_UNDEF,
    SNX_FLOW_PLAIN,
    SNX_FLOW_B,
    SNX_FLOW_BL,
    SNX_FLOW_BC,
    SNX_FLOW_BR,
    SNX_FLOW_BLR,
    SNX_FLOW_RET,
    SNX_FLOW_SVC,
    SNX_FLOW_BRK
};

/* One instruction-family ID per encoding; LLVM consumes it directly. */
typedef enum {
    SNX_A64_UNKNOWN,
    SNX_A64_ADR,
    SNX_A64_ADRP,
    SNX_A64_ADDSUB_IMM,
    SNX_A64_LOGIC_IMM,
    SNX_A64_MOVW,
    SNX_A64_BITFIELD,
    SNX_A64_EXTR,
    SNX_A64_HINT,
    SNX_A64_CLREX,
    SNX_A64_BARRIER,
    SNX_A64_SYS,
    SNX_A64_MRS,
    SNX_A64_MSR,
    SNX_A64_LDST_UIMM,
    SNX_A64_LDST_IMM9,
    SNX_A64_LDST_REG,
    SNX_A64_LDR_LIT,
    SNX_A64_LDSTP,
    SNX_A64_LDSTX,
    SNX_A64_LOGIC_REG,
    SNX_A64_ADDSUB_SREG,
    SNX_A64_ADDSUB_EXT,
    SNX_A64_ADC,
    SNX_A64_CCMP,
    SNX_A64_CSEL,
    SNX_A64_DP1,
    SNX_A64_DP2,
    SNX_A64_DP3,
    SNX_A64_FP2,
    SNX_A64_FP1,
    SNX_A64_FCMP,
    SNX_A64_FCCMP,
    SNX_A64_FCSEL,
    SNX_A64_FMOV_IMM,
    SNX_A64_FP3,
    SNX_A64_FCVT_INT,
    SNX_A64_FCVT_FIX,
    SNX_A64_SIMD_LDST,
    SNX_A64_SIMD_LDST1,
    SNX_A64_SIMD_DP,
    SNX_A64_SIMD_DP2,
    SNX_A64_BCOND,
    SNX_A64_CBZ,
    SNX_A64_TBZ,
    SNX_A64_BR,
    SNX_A64_BLR,
    SNX_A64_RET,
    SNX_A64_B,
    SNX_A64_BL,
    SNX_A64_SVC,
    SNX_A64_BRK,
    SNX_A64_MSR_IMM
} SnxA64Kind;

typedef struct {
    enum SnxFlow kind;
    const char *decl, *first, *second;
} SnxA64Case;

typedef struct {
    unsigned lo, width;
} SnxA64Field;

typedef struct {
    SnxA64Kind kind;
    const char* name;
    SnxU32 mask, value;
    unsigned validation, field_count;
    SnxA64Field fields[4];
    const char* index_expr;
    const SnxA64Case* cases;
    size_t case_count;
} SnxA64Pattern;

/* Keep the original C case for runtime generation; LLVM uses the same typed decode. */
typedef struct {
    const SnxA64Case* instruction;
    const SnxA64Pattern* pattern;
    size_t index;
} SnxA64Decoded;

SnxA64Decoded snx_a64_decode_inst(SnxU32 word);
const SnxA64Case* snx_a64_decode(SnxU32 word);
/* The pattern a decoded case belongs to and its index in the pattern's case table, for backends that emit each
   specialization themselves instead of printing the case's C (NULL for the shared undefined case) */
const SnxA64Pattern* snx_a64_case_pattern(const SnxA64Case* instruction, size_t* index);
const SnxA64Pattern* snx_a64_patterns(size_t* count);
SnxU64 snx_a64_target(SnxU32 word, SnxU64 pc);

#endif
