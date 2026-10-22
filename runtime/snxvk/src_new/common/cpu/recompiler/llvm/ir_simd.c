/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported in part from QEMU (target/arm/tcg/translate-a64.c, helper-a64.c, vec_helper.c, vfp_helper.c,
   crypto_helper.c), LGPL-2.1-or-later:
   Copyright (c) 2013 Alexander Graf <agraf@suse.de>
   Copyright (c) 2018 Linaro
   Copyright (C) 2013 - 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
   Copyright (c) 2003 Fabrice Bellard */
/* AdvSIMD: snx_a64_fast_simd's shortcuts (a64.c), emitted inline as the C it prints, and every SIMD pattern the C
   reference template would hand to snx_simd, lowered directly here */
#include "common/cpu/recompiler/llvm/ir.h"
#include "common/cpu/recompiler/aes_tables.h"
#include <string.h>

typedef LLVMValueRef V;
#define B ir->b

static LLVMTypeRef lane_type(SnxIr* ir, unsigned size) {
    return size == 0 ? ir->t_i8 : size == 1 ? ir->t_i16 : size == 2 ? ir->t_i32 : ir->t_i64;
}

/* lane index of 1<<size bytes in a V128 at pointer base (a register or a local) */
static V lane_pointer(SnxIr* ir, V base, unsigned size, unsigned index) {
    V offset = ir_i64(ir, (SnxU64)index << size);
    return LLVMBuildInBoundsGEP2(B, ir->t_i8, base, &offset, 1, "");
}
static V get(SnxIr* ir, V base, unsigned size, unsigned index) {
    return ir_guest_order(ir, ir_load(ir, lane_type(ir, size), lane_pointer(ir, base, size, index)));
}
static void set(SnxIr* ir, V base, unsigned size, unsigned index, V value) {
    ir_store(ir, ir_guest_order(ir, ir_cast(ir, value, lane_type(ir, size), 0)), lane_pointer(ir, base, size, index));
}
static V vreg(SnxIr* ir, unsigned n) { return ir_cpu(ir, ir_off_v(n)); }
static V fp_fma(SnxIr* ir, V add, V a, V b, unsigned width) {
    LLVMTypeRef p[5];
    V args[5];
    p[0] = ir->t_ptr;
    p[1] = p[2] = p[3] = ir->t_i64;
    p[4] = ir->t_i32;
    args[0] = ir->c;
    args[1] = ir_u64(ir, add);
    args[2] = ir_u64(ir, a);
    args[3] = ir_u64(ir, b);
    args[4] = ir_i32(ir, width);
    return ir_call(ir, ir_extern(ir, "snx_fp_fma", ir->t_i64, p, 5), args, 5);
}
/* x ^ sign bit of the lane, or x */
static V negate(SnxIr* ir, V x, unsigned sz, int on) {
    if (!on) return x;
    return LLVMBuildXor(B, x, sz ? ir_i64(ir, (SnxU64)1 << 63) : ir_i32(ir, 0x80000000UL), "");
}

/* lanes 0..n-1 of 1<<size bytes at base as one value: iN for one lane, <n x iN> otherwise */
static LLVMTypeRef lanes_type(SnxIr* ir, unsigned size, unsigned n) {
    return n == 1 ? lane_type(ir, size) : LLVMVectorType(lane_type(ir, size), n);
}
static V get_lanes(SnxIr* ir, V base, unsigned size, unsigned n) {
    V load = ir_load(ir, lanes_type(ir, size, n), base);
    LLVMSetAlignment(load, 1);
    return ir_guest_order(ir, load);
}
static void set_lanes(SnxIr* ir, V base, V value) {
    V store = LLVMBuildStore(B, ir_guest_order(ir, value), base);
    LLVMSetAlignment(store, 1);
}
static V broadcast(SnxIr* ir, V scalar, unsigned n) {
    V mask[16];
    V seed;
    unsigned i;
    if (n == 1) return scalar;
    if (LLVMIsConstant(scalar)) {
        V constants[16];
        for (i = 0; i < n; ++i) constants[i] = scalar;
        return LLVMConstVector(constants, n);
    }
    /* One insert and one shuffle. N inserts make the translator build a chain. */
    seed = LLVMBuildInsertElement(B, LLVMGetUndef(LLVMVectorType(LLVMTypeOf(scalar), 1)), scalar, ir_i32(ir, 0), "");
    for (i = 0; i < n; ++i) mask[i] = ir_i32(ir, 0);
    return LLVMBuildShuffleVector(B, seed, seed, LLVMConstVector(mask, n), "");
}
static void write_lanes(SnxIr* ir, unsigned rd, V value, unsigned q);

static V negate_lanes(SnxIr* ir, V x, unsigned sz, unsigned n) {
    return LLVMBuildXor(B, x, broadcast(ir, sz ? ir_i64(ir, (SnxU64)1 << 63) : ir_i32(ir, 0x80000000UL), n), "");
}

int snx_ir_fast_simd(SnxIr* ir, SnxU32 w) {
    unsigned q, u, size, rd, rn, rm, op, hi, sz, lanes, i, scalar, l, h, mb, index, m, imm5, imm4, lane;
    q = (w >> 30) & 1;
    u = (w >> 29) & 1;
    size = (w >> 22) & 3;
    rd = w & 31;
    rn = (w >> 5) & 31;
    rm = (w >> 16) & 31;
    if ((w & 0x9F200400UL) == 0x0E200400UL) {
        op = (w >> 11) & 31;
        if (op == 3) {
            V a0, a1, b0, b1;
            if (!u && size == 2 && rn == rm) { /* MOV (ORR Vd,Vn,Vn) */
                ir_set_vd(ir, rd, 0, ir_vd(ir, rn, 0));
                ir_set_vd(ir, rd, 1, q ? ir_vd(ir, rn, 1) : ir_i64(ir, 0));
                return 1;
            }
            if (u && size) return 0;
            a0 = ir_vd(ir, rn, 0);
            a1 = ir_vd(ir, rn, 1);
            b0 = ir_vd(ir, rm, 0);
            b1 = ir_vd(ir, rm, 1);
            if (!u && (size & 1)) { /* &~, |~ */
                b0 = LLVMBuildNot(B, b0, "");
                b1 = LLVMBuildNot(B, b1, "");
            }
            if (u) {
                ir_set_vd(ir, rd, 0, LLVMBuildXor(B, a0, b0, ""));
                ir_set_vd(ir, rd, 1, q ? LLVMBuildXor(B, a1, b1, "") : ir_i64(ir, 0));
            } else if (size < 2) {
                ir_set_vd(ir, rd, 0, LLVMBuildAnd(B, a0, b0, ""));
                ir_set_vd(ir, rd, 1, q ? LLVMBuildAnd(B, a1, b1, "") : ir_i64(ir, 0));
            } else {
                ir_set_vd(ir, rd, 0, LLVMBuildOr(B, a0, b0, ""));
                ir_set_vd(ir, rd, 1, q ? LLVMBuildOr(B, a1, b1, "") : ir_i64(ir, 0));
            }
            return 1;
        }
        if (op >= 0x18) {
            int operation = -1, acc = 0, negated = 0;
            sz = size & 1;
            hi = size >> 1;
            if (sz && !q) return 0;
            lanes = sz ? 2 : (q ? 4 : 2);
            if (!u && !hi && op == 0x1A)
                operation = 0; /* SNX_FP_ADD */
            else if (!u && hi && op == 0x1A)
                operation = 1; /* SNX_FP_SUB */
            else if (u && !hi && op == 0x1B)
                operation = 2; /* SNX_FP_MUL */
            else if (u && !hi && op == 0x1F)
                operation = 3; /* SNX_FP_DIV */
            else if (!u && op == 0x19) {
                acc = 1;
                negated = (int)hi;
            }
            if (operation < 0 && !acc) return 0;
            /* Load all operands before the destination write; Rd may alias Rn/Rm. */
            {
                unsigned s = sz ? 3 : 2, width = sz ? 64 : 32;
                V va = get_lanes(ir, vreg(ir, rn), s, lanes);
                V vb = get_lanes(ir, vreg(ir, rm), s, lanes);
                V old = acc ? get_lanes(ir, vreg(ir, rd), s, lanes) : NULL;
                LLVMBasicBlockRef slow, join, fast_end, slow_end;
                SnxFpFast f;
                if (acc)
                    ir_fp_fast(ir, SNX_IR_FP_FMA, width, lanes, negated ? negate_lanes(ir, va, sz, lanes) : va, vb, old,
                               &f);
                else
                    ir_fp_fast(ir, (unsigned)operation, width, lanes, va, vb, NULL, &f);
                join = ir_fp_branch(ir, f.ok, &slow);
                fast_end = LLVMGetInsertBlock(B);
                ir_fp_else(ir, join, slow, f.inexact);
                V result = LLVMGetUndef(LLVMTypeOf(va));
                for (i = 0; i < lanes; ++i) {
                    V x = LLVMBuildExtractElement(B, va, ir_i32(ir, i), "");
                    V y = LLVMBuildExtractElement(B, vb, ir_i32(ir, i), "");
                    V value;
                    if (acc) {
                        V add = LLVMBuildExtractElement(B, old, ir_i32(ir, i), "");
                        value = fp_fma(ir, add, negate(ir, x, sz, negated), y, width);
                    } else {
                        value = ir_fp_binary(ir, x, y, width, (unsigned)operation);
                    }
                    result =
                        LLVMBuildInsertElement(B, result, ir_cast(ir, value, lane_type(ir, s), 0), ir_i32(ir, i), "");
                }
                slow_end = LLVMGetInsertBlock(B);
                ir_fp_join(ir, join);
                V merged = LLVMBuildPhi(B, LLVMTypeOf(va), "fp_result");
                V incoming[] = {f.value, result};
                LLVMBasicBlockRef from[] = {fast_end, slow_end};
                LLVMAddIncoming(merged, incoming, from, 2);
                write_lanes(ir, rd, merged, q);
                return 1;
            }
        }
        if (op == 0x10 && (q || size < 3)) { /* ADD, SUB (vector) */
            V a, b;
            lanes = (q ? 16 : 8) >> size;
            a = get_lanes(ir, vreg(ir, rn), size, lanes);
            b = get_lanes(ir, vreg(ir, rm), size, lanes);
            write_lanes(ir, rd, u ? LLVMBuildSub(B, a, b, "") : LLVMBuildAdd(B, a, b, ""), q);
            return 1;
        }
        return 0;
    }
    if ((w & 0x9F000400UL) == 0x0F000000UL || (w & 0xDF000400UL) == 0x5F000000UL) { /* FMLA/FMLS/FMUL by element */
        unsigned s;
        scalar = (w >> 28) & 1;
        op = (w >> 12) & 15;
        if (u || (op != 1 && op != 5 && op != 9) || size < 2) return 0;
        sz = size & 1;
        l = (w >> 21) & 1;
        h = (w >> 11) & 1;
        mb = (w >> 20) & 1;
        if (sz && l) return 0;
        index = sz ? h : ((h << 1) | l);
        m = (mb << 4) | ((w >> 16) & 15);
        if (!scalar && sz && !q) return 0;
        lanes = scalar ? 1 : (sz ? 2 : (q ? 4 : 2));
        s = sz ? 3 : 2;
        {
            V va = get_lanes(ir, vreg(ir, rn), s, lanes);
            V e = get(ir, vreg(ir, m), s, index);
            V ve = broadcast(ir, e, lanes);
            V old = op != 9 ? get_lanes(ir, vreg(ir, rd), s, lanes) : NULL;
            LLVMBasicBlockRef slow, join, fast_end, slow_end;
            SnxFpFast f;
            if (op == 9)
                ir_fp_fast(ir, SNX_IR_FP_MUL, sz ? 64 : 32, lanes, va, ve, NULL, &f);
            else
                ir_fp_fast(ir, SNX_IR_FP_FMA, sz ? 64 : 32, lanes, op == 5 ? negate_lanes(ir, va, sz, lanes) : va, ve,
                           old, &f);
            join = ir_fp_branch(ir, f.ok, &slow);
            fast_end = LLVMGetInsertBlock(B);
            ir_fp_else(ir, join, slow, f.inexact);
            V result = lanes == 1 ? NULL : LLVMGetUndef(LLVMTypeOf(va));
            for (i = 0; i < lanes; ++i) {
                V x = lanes == 1 ? va : LLVMBuildExtractElement(B, va, ir_i32(ir, i), "");
                V add = op != 9 ? (lanes == 1 ? old : LLVMBuildExtractElement(B, old, ir_i32(ir, i), "")) : NULL;
                V value = op == 9 ? ir_fp_binary(ir, x, e, sz ? 64 : 32, 2)
                                  : fp_fma(ir, add, negate(ir, x, sz, op == 5), e, sz ? 64 : 32);
                value = ir_cast(ir, value, lane_type(ir, s), 0);
                result = lanes == 1 ? value : LLVMBuildInsertElement(B, result, value, ir_i32(ir, i), "");
            }
            slow_end = LLVMGetInsertBlock(B);
            ir_fp_join(ir, join);
            V merged = LLVMBuildPhi(B, LLVMTypeOf(va), "fp_element_result");
            V incoming[] = {f.value, result};
            LLVMBasicBlockRef from[] = {fast_end, slow_end};
            LLVMAddIncoming(merged, incoming, from, 2);
            write_lanes(ir, rd, merged, scalar ? 0 : q);
            return 1;
        }
    }
    if ((w & 0x9FE08400UL) == 0x0E000400UL) { /* DUP, INS, UMOV */
        imm5 = (w >> 16) & 31;
        imm4 = (w >> 11) & 15;
        if (!imm5) return 0;
        sz = 0;
        while (!(imm5 & (1U << sz))) ++sz;
        if (sz > 3) return 0;
        index = imm5 >> (sz + 1);
        lane = sz;
        lanes = (q ? 16 : 8) >> sz;
        if (!u && imm4 == 7 && ((sz == 3) == !!q)) {
            if (rd != 31) ir_set_x(ir, rd, ir_u64(ir, ir_ve(ir, rn, lane, index)));
            return 1;
        }
        if (!u && imm4 == 3 && q) {
            ir_set_ve(ir, rd, lane, index, rn == 31 ? ir_i64(ir, 0) : ir_x(ir, rn));
            return 1;
        }
        if (u && q) {
            ir_set_ve(ir, rd, lane, index, ir_ve(ir, rn, lane, imm4 >> sz));
            return 1;
        }
        if (!u && (imm4 == 0 || imm4 == 1) && !(sz == 3 && !q)) {
            V e = !imm4 ? ir_ve(ir, rn, lane, index)
                        : ir_cast(ir, rn == 31 ? ir_i64(ir, 0) : ir_x(ir, rn), lane_type(ir, lane), 0);
            write_lanes(ir, rd, broadcast(ir, e, lanes), q);
            return 1;
        }
        return 0;
    }
    if ((w & 0xFFFFFFE0UL) == 0x2F00E400UL || (w & 0xFFFFFFE0UL) == 0x6F00E400UL) { /* MOVI Vd, #0 */
        ir_set_vd(ir, rd, 0, ir_i64(ir, 0));
        ir_set_vd(ir, rd, 1, ir_i64(ir, 0));
        return 1;
    }
    return 0;
}

/* These families share private lane and vector builders. Compile them as one
   unit: no exported helpers, extra calls, or dynamic dispatch in emitted code. */
#include "ir_neon_memory.inc"
#include "ir_neon_crypto.inc"
#include "ir_neon_arithmetic.inc"
#include "ir_neon_dispatch.inc"
