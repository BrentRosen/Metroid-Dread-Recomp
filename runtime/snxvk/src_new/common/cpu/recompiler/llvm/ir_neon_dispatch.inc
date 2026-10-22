/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* ---- the integer and data-movement rest of the reference's snx_simd ---- */

/* DUP (element, scalar), the MOV Bd/Hd/Sd/Dd, Vn.T[i] alias: the element alone in Vd */
static int scalar_dup(SnxIr* ir, SnxU32 w) {
    unsigned imm5 = (w >> 16) & 31, rn = (w >> 5) & 31, rd = w & 31, size = 0;
    if (!imm5) return 0;
    while (!((imm5 >> size) & 1)) size++;
    if (size > 3) return 0;
    write_lanes(ir, rd, get(ir, vreg(ir, rn), size, imm5 >> (size + 1)), 0);
    return 1;
}

/* SMOV: the element sign-extended into Xd, or Wd zero-extended above it (fast_simd has UMOV) */
static int smov(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, u = (w >> 29) & 1, imm5 = (w >> 16) & 31, imm4 = (w >> 11) & 15, rn = (w >> 5) & 31,
             rd = w & 31, size = 0;
    V v;
    if (u || imm4 != 5 || !imm5) return 0;
    while (!((imm5 >> size) & 1)) size++;
    if (size >= 3 || (size == 2 && !q)) return 0;
    v = LLVMBuildSExt(B, get(ir, vreg(ir, rn), size, imm5 >> (size + 1)), ir->t_i64, "");
    if (!q) v = LLVMBuildZExt(B, LLVMBuildTrunc(B, v, ir->t_i32, ""), ir->t_i64, "");
    if (rd != 31) ir_set_x(ir, rd, v);
    return 1;
}

/* TBL, TBX: bytes of the 1-4 table registers Vn.. (wrapping) by Vm's indexes; an index past the table gives 0 (TBL)
   or keeps Vd's byte (TBX). On x86 one PSHUFB per table register, kept where the index falls in its 16 bytes (an
   in-range index is below 64, so PSHUFB's zeroing bit 7 is clear and its low 4 bits pick the byte); elsewhere one
   variable extract per byte from the table laid end to end */
static int table_lookup(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, rm = (w >> 16) & 31, len = (w >> 13) & 3, tbx = (w >> 12) & 1, rn = (w >> 5) & 31,
             rd = w & 31, n = q ? 16 : 8, k, i;
    LLVMTypeRef t = LLVMVectorType(ir->t_i8, 16);
    V idx = v128_type_load(ir, vreg(ir, rm)), tables[4], r;
    for (k = 0; k <= len; ++k) tables[k] = v128_type_load(ir, vreg(ir, (rn + k) & 31));
    r = tbx ? v128_type_load(ir, vreg(ir, rd)) : LLVMConstNull(t);
    if (ir->features->x86 && ir->features->sse42) { /* SSE4.2 implies SSSE3's PSHUFB */
        for (k = 0; k <= len; ++k) {
            V args[2], in;
            args[0] = tables[k];
            args[1] = idx;
            in = LLVMBuildICmp(B, LLVMIntULT, LLVMBuildSub(B, idx, broadcast(ir, ir_i8(ir, 16 * k), 16), ""),
                               broadcast(ir, ir_i8(ir, 16), 16), "");
            r = LLVMBuildSelect(B, in, ir_intrinsic(ir, "llvm.x86.ssse3.pshuf.b.128", NULL, args, 2), r, "");
        }
    } else {
        V all = concat(ir, tables, len + 1, 16);
        for (i = 0; i < n; ++i) {
            V x = LLVMBuildExtractElement(B, idx, ir_i32(ir, i), ""), e;
            V in = LLVMBuildICmp(B, LLVMIntULT, x, ir_i8(ir, 16 * (len + 1)), "");
            /* an index past the table reads byte 0 instead: LLVM's extract past the end is poison */
            e = LLVMBuildExtractElement(
                B, all, LLVMBuildZExt(B, LLVMBuildSelect(B, in, x, ir_i8(ir, 0), ""), ir->t_i32, ""), "");
            r = LLVMBuildInsertElement(B, r,
                                       LLVMBuildSelect(B, in, e, LLVMBuildExtractElement(B, r, ir_i32(ir, i), ""), ""),
                                       ir_i32(ir, i), "");
        }
    }
    write_lanes(ir, rd, low_lanes(ir, r, n), q);
    return 1;
}

/* LD2-4/ST2-4 (multiple structures): element e of register s is at byte (e*sel + s)*esize. simd_ldst moves an
   element at a time in address order; here, as LD1 does, the guest memory moves a register's size at a time, in
   address order, interleaved or deinterleaved by shuffles */
static int ld_structures(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, l = (w >> 22) & 1, post = (w >> 23) & 1, rm = (w >> 16) & 31, rn = (w >> 5) & 31,
             rt = w & 31, op = (w >> 12) & 15, size = (w >> 10) & 3, sel, bytes, eby, s, i, k, map[64];
    V address, parts[4], all;
    sel = op == 0 ? 4 : op == 4 ? 3 : op == 8 ? 2 : 0;
    if (!sel || (size == 3 && !q)) return 0;
    bytes = q ? 16 : 8;
    eby = 1u << size;
    address = ir_x(ir, rn);
    if (l) {
        for (k = 0; k < sel; ++k)
            parts[k] = ir_rd_bytes(ir, LLVMBuildAdd(B, address, ir_i64(ir, (SnxU64)k * bytes), ""), bytes);
        all = concat(ir, parts, sel, bytes);
        for (s = 0; s < sel; ++s) { /* byte i of register s: element i/eby, byte i%eby of it */
            for (i = 0; i < bytes; ++i) map[i] = (i / eby * sel + s) * eby + i % eby;
            set_lanes(ir, vreg(ir, (rt + s) & 31), shuffle(ir, all, all, map, bytes));
        }
        if (!q)
            for (s = 0; s < sel; ++s) zero_high(ir, (rt + s) & 31);
    } else {
        for (s = 0; s < sel; ++s) parts[s] = get_lanes(ir, vreg(ir, (rt + s) & 31), 0, bytes);
        all = concat(ir, parts, sel, bytes);
        for (i = 0; i < sel * bytes; ++i) { /* memory byte i: element m = i/eby, of register m%sel, lane m/sel */
            unsigned m = i / eby;
            map[i] = m % sel * bytes + m / sel * eby + i % eby;
        }
        for (k = 0; k < sel; ++k)
            ir_wr_bytes(ir, LLVMBuildAdd(B, address, ir_i64(ir, (SnxU64)k * bytes), ""),
                        shuffle(ir, all, all, map + k * bytes, bytes));
    }
    if (post) ir_set_x(ir, rn, LLVMBuildAdd(B, address, rm == 31 ? ir_i64(ir, (SnxU64)sel * bytes) : ir_x(ir, rm), ""));
    return 1;
}

/* three-different (vector): simd_3diff's long, wide and narrowing-high forms; PMULL stays with pmull_1q and
   snx_simd */
static int three_different(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, rm = (w >> 16) & 31, op = (w >> 12) & 15,
             rn = (w >> 5) & 31, rd = w & 31, m;
    V a, b, z = NULL, r;
    if (size == 3) return 0;
    m = 8u >> size;
    switch (op) {
        case 4:
        case 6: /* ADDHN, SUBHN, RADDHN, RSUBHN: the high half of each wide sum or difference */
            a = vlanes(ir, vreg(ir, rn), size + 1, m);
            b = vlanes(ir, vreg(ir, rm), size + 1, m);
            r = op == 4 ? LLVMBuildAdd(B, a, b, "") : LLVMBuildSub(B, a, b, "");
            if (u) r = LLVMBuildAdd(B, r, splat_like(ir, r, int_min(8u << size)), "");
            write_narrow(ir, rd, narrow(ir, LLVMBuildLShr(B, r, splat_like(ir, r, 8u << size), "")), q);
            return 1;
        case 1:
        case 3: /* [SU]ADDW, [SU]SUBW: Vn's wide lanes and the half of Vm widened */
            a = vlanes(ir, vreg(ir, rn), size + 1, m);
            b = widen(ir, half_lanes(ir, rm, size, q), u);
            set_lanes(ir, vreg(ir, rd), op == 1 ? LLVMBuildAdd(B, a, b, "") : LLVMBuildSub(B, a, b, ""));
            return 1;
        case 14: /* PMULL (bytes; pmull_1q has the 1Q form) */
            if (u || size) return 0;
            set_lanes(ir, vreg(ir, rd), poly_multiply(ir, half_lanes(ir, rn, 0, q), half_lanes(ir, rm, 0, q)));
            return 1;
        case 9:
        case 11:
        case 13:
            if (u || !size) return 0;
            break;
        case 0:
        case 2:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12: break;
        default: return 0;
    }
    a = half_lanes(ir, rn, size, q);
    b = half_lanes(ir, rm, size, q);
    if (op == 5 || op == 8 || op == 9 || op == 10 || op == 11) z = vlanes(ir, vreg(ir, rd), size + 1, m);
    set_lanes(ir, vreg(ir, rd), long_op(ir, op, u, a, b, z));
    return 1;
}

/* three-different (scalar): SQDMLAL, SQDMLSL, SQDMULL on lane 0, the rest of Vd zero */
static int scalar_three_different(SnxIr* ir, SnxU32 w) {
    unsigned u = (w >> 29) & 1, size = (w >> 22) & 3, rm = (w >> 16) & 31, op = (w >> 12) & 15, rn = (w >> 5) & 31,
             rd = w & 31;
    if (u || (op != 9 && op != 11 && op != 13) || (size != 1 && size != 2)) return 0;
    write_lanes(ir, rd,
                long_op(ir, op, 0, vlanes(ir, vreg(ir, rn), size, 1), vlanes(ir, vreg(ir, rm), size, 1),
                        op == 13 ? NULL : vlanes(ir, vreg(ir, rd), size + 1, 1)),
                0);
    return 1;
}

/* two-register misc, integer: simd_2misc's REV16/32/64, CLS, CLZ, ABS, NEG, SQABS, SQNEG, SUQADD, USQADD, the
   compares against zero, [SU]ADDLP, [SU]ADALP, XTN, SQXTN, UQXTN, SQXTUN and SHLL (byte_misc has CNT, NOT and RBIT).
   Scalar, as simd_2misc allows: the saturating ones of any size, ABS, NEG and the compares on D registers */
static int int_misc(SnxIr* ir, SnxU32 w, unsigned scalar) {
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31,
             rn = (w >> 5) & 31, rd = w & 31, eb = 8u << size, n = scalar ? 1 : (q ? 128u : 64u) / eb, i, indices[16],
             k = (u << 5) | op;
    LLVMTypeRef t;
    V a, r, zero;
    if (scalar && op != 0x12 && op != 0x14 && size != 3 && op != 0x03 && op != 0x07) return 0;
    switch (op) {
        case 0x12:
        case 0x14:                                            /* XTN (wraps), SQXTUN, SQXTN, UQXTN */
            if (size == 3 || (scalar && k == 0x12)) return 0; /* XTN has no scalar form */
            a = vlanes(ir, vreg(ir, rn), size + 1, scalar ? 1 : 64 / eb);
            r = k == 0x12 ? narrow(ir, a) : saturate_narrow(ir, a, k != 0x34, k == 0x14);
            write_narrow(ir, rd, r, q);
            return 1;
        case 0x03: { /* SUQADD (signed Vd + unsigned Vn), USQADD (unsigned Vd + signed Vn): exact in twice the width */
            V z;
            if (size == 3 && !q && !scalar) return 0;
            a = vlanes(ir, vreg(ir, rn), size, n);
            z = vlanes(ir, vreg(ir, rd), size, n);
            write_lanes(ir, rd, saturate_narrow(ir, LLVMBuildAdd(B, widen(ir, z, u), widen(ir, a, !u), ""), 1, !u), q);
            return 1;
        }
        case 0x13: /* SHLL: the half widened, shifted by the element size */
            if (!u || size == 3 || scalar) return 0;
            r = widen(ir, half_lanes(ir, rn, size, q), 1);
            set_lanes(ir, vreg(ir, rd), LLVMBuildShl(B, r, splat_like(ir, r, eb), ""));
            return 1;
        case 0x02:
        case 0x06: { /* [SU]ADDLP, [SU]ADALP: adjacent pairs summed in twice the width */
            unsigned even[16], odd[16];
            if (size == 3 || scalar) return 0;
            a = vlanes(ir, vreg(ir, rn), size, n);
            for (i = 0; i < n / 2; ++i) {
                even[i] = 2 * i;
                odd[i] = 2 * i + 1;
            }
            r = LLVMBuildAdd(B, widen(ir, shuffle(ir, a, a, even, n / 2), u),
                             widen(ir, shuffle(ir, a, a, odd, n / 2), u), "");
            if (op == 6) r = LLVMBuildAdd(B, r, vlanes(ir, vreg(ir, rd), size + 1, n / 2), "");
            write_lanes(ir, rd, r, q);
            return 1;
        }
        default: break;
    }
    if (size == 3 && !q && !scalar) return 0;
    a = vlanes(ir, vreg(ir, rn), size, n);
    t = LLVMTypeOf(a);
    zero = LLVMConstNull(t);
    switch (k) {
        case 0x00:
        case 0x20:
        case 0x01: { /* REV64, REV32, REV16: the elements of each container reversed */
            unsigned container = op == 1 ? 16 : u ? 32 : 64, per;
            if (eb >= container || scalar) return 0;
            per = container / eb;
            for (i = 0; i < n; ++i) indices[i] = i - i % per + per - 1 - i % per;
            r = shuffle(ir, a, a, indices, n);
            break;
        }
        case 0x04:   /* CLS: the leading zeros of x ^ (x >> 1), less one */
        case 0x24: { /* CLZ */
            V args[2];
            if (size == 3) return 0;
            args[0] = u ? a : LLVMBuildXor(B, a, LLVMBuildAShr(B, a, splat_like(ir, a, 1), ""), "");
            args[1] = LLVMConstInt(ir->t_i1, 0, 0); /* a zero lane counts the lane's width */
            r = ir_intrinsic(ir, "llvm.ctlz", t, args, 2);
            if (!u) r = LLVMBuildSub(B, r, splat_like(ir, a, 1), "");
            break;
        }
        case 0x07: /* SQABS, SQNEG: INT_MIN saturates to INT_MAX with QC */
        case 0x27: {
            V min = LLVMBuildICmp(B, LLVMIntEQ, a, splat_like(ir, a, int_min(eb)), ""),
              negated = LLVMBuildSub(B, zero, a, "");
            r = u ? negated : LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntSLT, a, zero, ""), negated, a, "");
            r = LLVMBuildSelect(B, min, splat_like(ir, a, int_max(eb)), r, "");
            set_qc(ir, min);
            break;
        }
        case 0x08: r = mask_of(ir, LLVMBuildICmp(B, LLVMIntSGT, a, zero, ""), t); break;
        case 0x28: r = mask_of(ir, LLVMBuildICmp(B, LLVMIntSGE, a, zero, ""), t); break;
        case 0x09: r = mask_of(ir, LLVMBuildICmp(B, LLVMIntEQ, a, zero, ""), t); break;
        case 0x29: r = mask_of(ir, LLVMBuildICmp(B, LLVMIntSLE, a, zero, ""), t); break;
        case 0x0A: r = mask_of(ir, LLVMBuildICmp(B, LLVMIntSLT, a, zero, ""), t); break;
        case 0x0B:
            r = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntSLT, a, zero, ""), LLVMBuildSub(B, zero, a, ""), a, "");
            break;
        case 0x2B: r = LLVMBuildSub(B, zero, a, ""); break;
        default: return 0;
    }
    write_lanes(ir, rd, r, q);
    return 1;
}

/* across lanes: simd_across's ADDV, [SU]MAXV, [SU]MINV, [SU]ADDLV (the long sum fits twice the width), and FMAXV,
   FMINV, FMAXNMV, FMINNMV through the helper in its reduction order, (0,1), (2,3), then the two */
static int across(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31, rn = (w >> 5) & 31,
             rd = w & 31, n;
    V a, r;
    if (op == 0x0C || op == 0x0F) {
        unsigned mn = size >> 1, numeric = op == 0x0C;
        V p0, p1;
        if (!u || !q || (size & 1)) return 0;
        p0 = ir_fp_minmax(ir, get(ir, vreg(ir, rn), 2, 0), get(ir, vreg(ir, rn), 2, 1), 32, mn, numeric);
        p1 = ir_fp_minmax(ir, get(ir, vreg(ir, rn), 2, 2), get(ir, vreg(ir, rn), 2, 3), 32, mn, numeric);
        write_lanes(ir, rd, ir_cast(ir, ir_fp_minmax(ir, p0, p1, 32, mn, numeric), ir->t_i32, 0), 0);
        return 1;
    }
    if (size == 3 || (size == 2 && !q)) return 0;
    n = (q ? 128u : 64u) >> (3 + size);
    a = vlanes(ir, vreg(ir, rn), size, n);
    switch (op) {
        case 0x03:
            a = widen(ir, a, u);
            r = ir_intrinsic(ir, "llvm.vector.reduce.add", LLVMTypeOf(a), &a, 1);
            break;
        case 0x0A:
            r = ir_intrinsic(ir, u ? "llvm.vector.reduce.umax" : "llvm.vector.reduce.smax", LLVMTypeOf(a), &a, 1);
            break;
        case 0x1A:
            r = ir_intrinsic(ir, u ? "llvm.vector.reduce.umin" : "llvm.vector.reduce.smin", LLVMTypeOf(a), &a, 1);
            break;
        case 0x1B:
            if (u) return 0;
            r = ir_intrinsic(ir, "llvm.vector.reduce.add", LLVMTypeOf(a), &a, 1);
            break;
        default: return 0;
    }
    write_lanes(ir, rd, r, 0);
    return 1;
}

/* scalar pairwise: ADDP Dd, Vn.2D, and FADDP, FMAXP, FMINP, FMAXNMP, FMINNMP of the two lanes */
static int scalar_pairwise(SnxIr* ir, SnxU32 w) {
    unsigned u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31, rn = (w >> 5) & 31, rd = w & 31,
             s = size & 1 ? 3 : 2;
    FpOp o;
    if (!u) {
        if (op != 0x1B || size != 3) return 0;
        write_lanes(ir, rd, LLVMBuildAdd(B, ir_vd(ir, rn, 0), ir_vd(ir, rn, 1), ""), 0);
        return 1;
    }
    memset(&o, 0, sizeof(o));
    if (op == 0x0D && !(size >> 1))
        o.kind = FP_LANES_ADD;
    else if (op == 0x0C || op == 0x0F) {
        o.kind = FP_LANES_MINMAX;
        o.param.minmax.minimum = size >> 1;
        o.param.minmax.numeric = op == 0x0C;
    } else
        return 0;
    fp_lanes(ir, &o, s, 1, get(ir, vreg(ir, rn), s, 0), get(ir, vreg(ir, rn), s, 1), rd, 0);
    return 1;
}

/* by element, beyond fast_simd's FMLA/FMLS/FMUL: FMULX, and simd_elem's integer MUL, MLA, MLS, SQDMULH, SQRDMULH and
   long forms ([SU]MULL, [SU]MLAL, [SU]MLSL, SQDMULL, SQDMLAL, SQDMLSL), vector and scalar as simd_elem allows */
static int by_element(SnxIr* ir, SnxU32 w, unsigned scalar) {
    /* the by-element long opcodes as three-different numbers them (long_op) */
    static const unsigned char long_ops[16] = {0, 0, 8, 9, 0, 0, 10, 11, 0, 0, 12, 13, 0, 0, 0, 0};
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, l = (w >> 21) & 1,
             mb = (w >> 20) & 1, rm = (w >> 16) & 15, op = (w >> 12) & 15, h = (w >> 11) & 1, rn = (w >> 5) & 31,
             rd = w & 31, idx, m, n, eb = 8u << size;
    V a, b, z = NULL, r;
    if (u ? op == 9 : (op == 1 || op == 5 || op == 9)) { /* FP */
        unsigned sz = size & 1, s = sz ? 3 : 2;
        FpOp o;
        if (!u || size < 2 || (sz && l) || (sz && !q && !scalar)) return 0; /* (fast_simd has FMLA/FMLS/FMUL) */
        n = scalar ? 1 : (q ? 16u : 8u) >> s;
        memset(&o, 0, sizeof(o));
        o.kind = FP_LANES_MULX;
        b = get(ir, vreg(ir, (mb << 4) | rm), s, sz ? h : (h << 1) | l);
        fp_lanes(ir, &o, s, n, get_lanes(ir, vreg(ir, rn), s, n), broadcast(ir, b, n), rd, q);
        return 1;
    }
    if (size != 1 && size != 2) return 0;
    if (size == 1) {
        idx = (h << 2) | (l << 1) | mb;
        m = rm;
    } else {
        idx = (h << 1) | l;
        m = (mb << 4) | rm;
    }
    b = get(ir, vreg(ir, m), size, idx);
    if (op == 2 || op == 3 || op == 6 || op == 7 || op == 10 || op == 11) {
        unsigned saturating_op = op == 3 || op == 7 || op == 11;
        if ((u && saturating_op) || (scalar && !saturating_op)) return 0;
        n = scalar ? 1 : 64 / eb;
        a = scalar ? vlanes(ir, vreg(ir, rn), size, 1) : half_lanes(ir, rn, size, q);
        if (op != 10 && op != 11) z = vlanes(ir, vreg(ir, rd), size + 1, n);
        r = long_op(ir, long_ops[op], u, a, vsplat(ir, b, n), z);
        if (scalar)
            write_lanes(ir, rd, r, 0);
        else
            set_lanes(ir, vreg(ir, rd), r);
        return 1;
    }
    if (u ? op != 0 && op != 4 : op != 8 && op != 12 && op != 13) return 0;
    if (scalar && op < 12) return 0;
    n = scalar ? 1 : (q ? 128u : 64u) / eb;
    a = vlanes(ir, vreg(ir, rn), size, n);
    b = vsplat(ir, b, n);
    if (op >= 12)
        r = doubling_high(ir, a, b, op == 13);
    else {
        r = LLVMBuildMul(B, a, b, "");
        if (op != 8) {
            z = vlanes(ir, vreg(ir, rd), size, n);
            r = op == 0 ? LLVMBuildAdd(B, z, r, "") : LLVMBuildSub(B, z, r, "");
        }
    }
    write_lanes(ir, rd, r, q);
    return 1;
}

/* three-same integer (scalar): simd_3same's SQADD, UQADD, SQSUB, UQSUB, SQSHL, UQSHL, SQRSHL, UQRSHL, SQDMULH and
   SQRDMULH, and on D registers ADD, SUB, the compares and SSHL/USHL/SRSHL/URSHL */
static int scalar_three_same(SnxIr* ir, SnxU32 w) {
    unsigned u = (w >> 29) & 1, size = (w >> 22) & 3, rm = (w >> 16) & 31, op = (w >> 11) & 31, rn = (w >> 5) & 31,
             rd = w & 31;
    V a, b, r;
    LLVMTypeRef t;
    switch (op) {
        case 0x01:
        case 0x05:
        case 0x09:
        case 0x0B: break;
        case 0x16:
            if (size != 1 && size != 2) return 0;
            break;
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x0A:
        case 0x10:
        case 0x11:
            if (size != 3) return 0;
            break;
        default: return 0;
    }
    a = vlanes(ir, vreg(ir, rn), size, 1);
    b = vlanes(ir, vreg(ir, rm), size, 1);
    t = LLVMTypeOf(a);
    switch (op) {
        case 0x01:
        case 0x05: r = saturating(ir, u, op == 0x05, 8u << size, 1, a, b); break;
        case 0x16: r = doubling_high(ir, a, b, u); break;
        case 0x06: r = mask_of(ir, LLVMBuildICmp(B, u ? LLVMIntUGT : LLVMIntSGT, a, b, ""), t); break;
        case 0x07: r = mask_of(ir, LLVMBuildICmp(B, u ? LLVMIntUGE : LLVMIntSGE, a, b, ""), t); break;
        case 0x10: r = u ? LLVMBuildSub(B, a, b, "") : LLVMBuildAdd(B, a, b, ""); break;
        case 0x11:
            r = u ? mask_of(ir, LLVMBuildICmp(B, LLVMIntEQ, a, b, ""), t)
                  : mask_of(ir, LLVMBuildICmp(B, LLVMIntNE, LLVMBuildAnd(B, a, b, ""), LLVMConstNull(t), ""), t);
            break;
        default: r = register_shift(ir, a, b, u, op == 0x0A || op == 0x0B, op == 0x09 || op == 0x0B); break;
    }
    write_lanes(ir, rd, r, 0);
    return 1;
}

/* in the C reference's snx_simd order, each group's emitters in turn; 0 is an unallocated encoding */
int snx_ir_more_simd(SnxIr* ir, SnxU32 w) {
    if ((w & 0xBF800000UL) == 0x0C000000UL || (w & 0xBF800000UL) == 0x0C800000UL)
        return ld1_multiple(ir, w) || ld_structures(ir, w);
    if ((w & 0xBF000000UL) == 0x0D000000UL) return ld1_single(ir, w);
    if ((w & 0x9F200400UL) == 0x0E200400UL) return three_same(ir, w) || fp_three_same(ir, w, 0);
    if ((w & 0xDF200400UL) == 0x5E200400UL) return scalar_three_same(ir, w) || fp_three_same(ir, w, 1);
    if ((w & 0xBFE0FC00UL) == 0x0EE0E000UL) return pmull_1q(ir, w);
    if ((w & 0x9F200C00UL) == 0x0E200000UL) return three_different(ir, w);
    if ((w & 0xDF200C00UL) == 0x5E200000UL) return scalar_three_different(ir, w);
    if ((w & 0x9F3E0C00UL) == 0x0E200800UL)
        return byte_misc(ir, w) || fp_sign_misc(ir, w) || fp_misc(ir, w, 0) || fp_narrow(ir, w, 0) ||
               int_misc(ir, w, 0);
    if ((w & 0xDF3E0C00UL) == 0x5E200800UL) return fp_misc(ir, w, 1) || fp_narrow(ir, w, 1) || int_misc(ir, w, 1);
    if ((w & 0x9F3E0C00UL) == 0x0E300800UL) return across(ir, w);
    if ((w & 0xDF3E0C00UL) == 0x5E300800UL) return scalar_pairwise(ir, w);
    if ((w & 0x9FE08400UL) == 0x0E000400UL) return smov(ir, w);
    if ((w & 0xFFE0FC00UL) == 0x5E000400UL) return scalar_dup(ir, w);
    if ((w & 0x9FF80400UL) == 0x0F000400UL) return modified_immediate(ir, w);
    if ((w & 0x9F800400UL) == 0x0F000400UL) return fp_fixed(ir, w, 0) || shift_immediate(ir, w, 0);
    if ((w & 0xDF800400UL) == 0x5F000400UL) return fp_fixed(ir, w, 1) || shift_immediate(ir, w, 1);
    if ((w & 0x9F000400UL) == 0x0F000000UL) return by_element(ir, w, 0);
    if ((w & 0xDF000400UL) == 0x5F000000UL) return by_element(ir, w, 1);
    if ((w & 0xBF208C00UL) == 0x0E000800UL) return permute(ir, w);
    if ((w & 0xBFE08400UL) == 0x2E000000UL) return extract(ir, w);
    if ((w & 0xBFE08C00UL) == 0x0E000000UL) return table_lookup(ir, w);
    if ((w & 0xFFFF0C00UL) == 0x4E280800UL) return aes(ir, w);
    if ((w & 0xFFE08C00UL) == 0x5E000000UL) return sha(ir, w, (w >> 12) & 7);
    if ((w & 0xFFFF0C00UL) == 0x5E280800UL) return ((w >> 12) & 31) < 3 && sha(ir, w, 0x10 + ((w >> 12) & 31));
    return 0;
}
