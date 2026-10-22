/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* ---- integer lanes as vectors: the helpers' sx/zx arithmetic in a wider lane where it needs the room ---- */

/* n lanes of 1<<size bytes at base, always a vector (<1 x iN> for the scalar forms, so one emitter serves both) */
static V vlanes(SnxIr* ir, V base, unsigned size, unsigned n) {
    V load = ir_load(ir, LLVMVectorType(lane_type(ir, size), n), base);
    LLVMSetAlignment(load, 1);
    return ir_guest_order(ir, load);
}
/* the 64-bit half of Vn that Q selects (the "2" forms read the upper one) */
static V half_lanes(SnxIr* ir, unsigned rn, unsigned size, unsigned upper) {
    return vlanes(ir, ir_cpu(ir, ir_off_v(rn) + (upper ? 8 : 0)), size, 8u >> size);
}
static unsigned lanes_of(V x) { return LLVMGetVectorSize(LLVMTypeOf(x)); }
static unsigned bits_of(V x) { return LLVMGetIntTypeWidth(LLVMGetElementType(LLVMTypeOf(x))); }
static V vsplat(SnxIr* ir, V scalar, unsigned n) { /* broadcast, but a vector for one lane too */
    return n == 1 ? LLVMBuildInsertElement(B, LLVMGetUndef(LLVMVectorType(LLVMTypeOf(scalar), 1)), scalar,
                                           ir_i32(ir, 0), "")
                  : broadcast(ir, scalar, n);
}
/* v in every lane of a vector shaped like x */
static V splat_like(SnxIr* ir, V x, SnxU64 v) {
    return vsplat(ir, LLVMConstInt(LLVMGetElementType(LLVMTypeOf(x)), v, 0), lanes_of(x));
}
/* lanes zero- or sign-extended to bits, or truncated to them (LLVM's casts refuse the same width: x itself) */
static V extend(SnxIr* ir, V x, unsigned u, unsigned bits) {
    LLVMTypeRef t = LLVMVectorType(LLVMIntTypeInContext(ir->ctx, bits), lanes_of(x));
    if (bits == bits_of(x)) return x;
    if (bits < bits_of(x)) return LLVMBuildTrunc(B, x, t, "");
    return u ? LLVMBuildZExt(B, x, t, "") : LLVMBuildSExt(B, x, t, "");
}
static V widen(SnxIr* ir, V x, unsigned u) { return extend(ir, x, u, 2 * bits_of(x)); }
static V narrow(SnxIr* ir, V x) {
    return LLVMBuildTrunc(B, x, LLVMVectorType(LLVMIntTypeInContext(ir->ctx, bits_of(x) / 2), lanes_of(x)), "");
}
static SnxU64 int_min(unsigned eb) { return (SnxU64)1 << (eb - 1); }
static SnxU64 int_max(unsigned eb) { return int_min(eb) - 1; }
static SnxU64 uint_max(unsigned eb) { return eb == 64 ? ~(SnxU64)0 : ((SnxU64)1 << eb) - 1; }

/* SQADD, UQADD, SQSUB, UQSUB (vector): saturating, QC when a lane saturates (sqadd_ and friends). For 32/64-bit
   signed lanes on AVX-512 hosts, wunkolo's vpternlog test: a lane overflowed when the inputs' sign bits agree and the
   sum's differs (add, 0x42) or the inputs' differ and the difference's differs from the first (sub, 0x18); the
   saturated value is the result's sign spread and its top bit flipped */
static V saturating(SnxIr* ir, unsigned u, unsigned sub, unsigned eb, unsigned n, V a, V b) {
    static const char* const names[4] = {"llvm.sadd.sat", "llvm.uadd.sat", "llvm.ssub.sat", "llvm.usub.sat"};
    LLVMTypeRef t = LLVMTypeOf(a);
    V wrap = sub ? LLVMBuildSub(B, a, b, "") : LLVMBuildAdd(B, a, b, ""), args[2], r;
    if (!u && eb >= 32 && n * eb == 128 && host_ternlog(ir)) {
        unsigned table = sub ? (TERN_A ^ TERN_B) & (TERN_A ^ TERN_C) : ~(TERN_A ^ TERN_B) & (TERN_A ^ TERN_C);
        V check = LLVMBuildBitCast(B, ternlog(ir, a, b, wrap, table, eb), t, "");
        V overflow = LLVMBuildICmp(B, LLVMIntSLT, check, LLVMConstNull(t), "");
        V spread = LLVMBuildAShr(B, wrap, broadcast(ir, LLVMConstInt(LLVMGetElementType(t), eb - 1, 0), n), "");
        V value = LLVMBuildXor(B, spread,
                               broadcast(ir, LLVMConstInt(LLVMGetElementType(t), (SnxU64)1 << (eb - 1), 0), n), "");
        set_qc(ir, overflow);
        return LLVMBuildSelect(B, overflow, value, wrap, "");
    }
    args[0] = a;
    args[1] = b;
    r = ir_intrinsic(ir, names[sub * 2 + u], t, args, 2); /* PADDS/PADDUS/PSUBS/PSUBUS for 8/16-bit lanes on x86 */
    set_qc(ir, LLVMBuildICmp(B, LLVMIntNE, r, wrap, ""));
    return r;
}

/* SHADD, UHADD, SRHADD, URHADD: (a + b [+ 1]) >> 1 without overflow. Unsigned 8/16-bit rounding is PAVGB/PAVGW on
   x86; signed goes through it too, biased by the lanes' sign bit (wunkolo, "pavgb: most-significant-bit constant"):
   (a ^ 0x80) and (b ^ 0x80) are a + 128 and b + 128, so their average is the signed one plus 128 */
static V halving(SnxIr* ir, unsigned u, unsigned rounding, unsigned size, unsigned n, V a, V b) {
    if (size < 2) {
        /* Keep the widened form: LLVM recognizes 8/16-bit PAVG on x86.
           A bitwise average is correct, but lowers to many more instructions. */
        LLVMTypeRef wide = LLVMVectorType(lane_type(ir, size + 1), n);
        V bias = NULL, x, y, r;
        if (!u) {
            bias = broadcast(ir, LLVMConstInt(lane_type(ir, size), (SnxU64)1 << ((8u << size) - 1), 0), n);
            a = LLVMBuildXor(B, a, bias, "");
            b = LLVMBuildXor(B, b, bias, "");
            u = 1;
        }
        x = LLVMBuildZExt(B, a, wide, "");
        y = LLVMBuildZExt(B, b, wide, "");
        r = LLVMBuildAdd(B, x, y, "");
        if (rounding) r = LLVMBuildAdd(B, r, broadcast(ir, LLVMConstInt(lane_type(ir, size + 1), 1, 0), n), "");
        r = LLVMBuildLShr(B, r, broadcast(ir, LLVMConstInt(lane_type(ir, size + 1), 1, 0), n), "");
        r = LLVMBuildTrunc(B, r, LLVMTypeOf(a), "");
        return bias ? LLVMBuildXor(B, r, bias, "") : r;
    }
    /* For 32-bit lanes this stays in their native width. No wider vector
       extension is required, and signed floor rounding uses arithmetic shift. */
    V xor = LLVMBuildXor(B, a, b, "");
    V one = splat_like(ir, a, 1);
    V half = u ? LLVMBuildLShr(B, xor, one, "") : LLVMBuildAShr(B, xor, one, "");
    return rounding ? LLVMBuildSub(B, LLVMBuildOr(B, a, b, ""), half, "")
                    : LLVMBuildAdd(B, LLVMBuildAnd(B, a, b, ""), half, "");
}

/* the narrowing forms' result: Vd's low half with the upper half zero, or (the "2" forms) its upper half with the
   low half kept (simd_2misc's and simd_shimm's o = Q ? d : r) */
static void write_narrow(SnxIr* ir, unsigned rd, V value, unsigned upper) {
    if (upper)
        set_lanes(ir, ir_cpu(ir, ir_off_v(rd) + 8), value);
    else
        write_lanes(ir, rd, value, 0);
}

/* wide lanes clamped to half their width, QC for any lane that changed: signed to signed (ssat, SQXTN), signed to
   unsigned (usat, SQXTUN) or unsigned to unsigned (UQXTN) */
static V saturate_narrow(SnxIr* ir, V x, unsigned from_signed, unsigned to_signed) {
    unsigned eb = bits_of(x) / 2;
    V hi = splat_like(ir, x, to_signed ? int_max(eb) : uint_max(eb)), over, under, r;
    over = LLVMBuildICmp(B, from_signed ? LLVMIntSGT : LLVMIntUGT, x, hi, "");
    r = LLVMBuildSelect(B, over, hi, x, "");
    if (from_signed) { /* INT_MIN sign-extended, for lanes of 128 bits too (SUQADD's) */
        V lo = to_signed ? vsplat(ir, LLVMConstInt(LLVMGetElementType(LLVMTypeOf(x)), (SnxU64)0 - int_min(eb), 1),
                                  lanes_of(x))
                         : splat_like(ir, x, 0);
        under = LLVMBuildICmp(B, LLVMIntSLT, x, lo, "");
        r = LLVMBuildSelect(B, under, lo, r, "");
        over = LLVMBuildOr(B, over, under, "");
    }
    set_qc(ir, over);
    return narrow(ir, r);
}

/* SQDMULL and the doubling in SQDMLAL/SQDMLSL: 2*a*b in twice the width. Only INT_MIN * INT_MIN overflows it, and
   saturates with QC */
static V doubling_long(SnxIr* ir, V a, V b) {
    unsigned eb = bits_of(a);
    V x = widen(ir, a, 0), p = LLVMBuildShl(B, LLVMBuildMul(B, x, widen(ir, b, 0), ""), splat_like(ir, x, 1), "");
    V min = splat_like(ir, a, int_min(eb));
    V both = LLVMBuildAnd(B, LLVMBuildICmp(B, LLVMIntEQ, a, min, ""), LLVMBuildICmp(B, LLVMIntEQ, b, min, ""), "");
    set_qc(ir, both);
    return LLVMBuildSelect(B, both, splat_like(ir, p, int_max(2 * eb)), p, "");
}
/* SQDMULH, SQRDMULH: the high half of 2*a*b (plus 1<<(eb-1) rounding), computed in twice the width where it cannot
   overflow but for INT_MIN * INT_MIN, which saturates with QC */
static V doubling_high(SnxIr* ir, V a, V b, unsigned round) {
    unsigned eb = bits_of(a);
    V x = widen(ir, a, 0), p, min, both;
    p = LLVMBuildShl(B, LLVMBuildMul(B, x, widen(ir, b, 0), ""), splat_like(ir, x, 1), "");
    if (round) p = LLVMBuildAdd(B, p, splat_like(ir, x, int_min(eb)), "");
    p = narrow(ir, LLVMBuildAShr(B, p, splat_like(ir, x, eb), ""));
    min = splat_like(ir, a, int_min(eb));
    both = LLVMBuildAnd(B, LLVMBuildICmp(B, LLVMIntEQ, a, min, ""), LLVMBuildICmp(B, LLVMIntEQ, b, min, ""), "");
    set_qc(ir, both);
    return LLVMBuildSelect(B, both, splat_like(ir, a, int_max(eb)), p, "");
}

/* PMUL, PMULL (bytes): carry-less products of byte lanes, 16 bits wide: b shifted by each set bit of a, xored */
static V poly_multiply(SnxIr* ir, V a, V b) {
    V x = widen(ir, a, 1), y = widen(ir, b, 1), p = splat_like(ir, x, 0), bit;
    unsigned k;
    for (k = 0; k < 8; ++k) {
        bit = LLVMBuildAnd(B, LLVMBuildLShr(B, x, splat_like(ir, x, k), ""), splat_like(ir, x, 1), "");
        p = LLVMBuildXor(
            B, p, LLVMBuildAnd(B, LLVMBuildNeg(B, bit, ""), LLVMBuildShl(B, y, splat_like(ir, y, k), ""), ""), "");
    }
    return p;
}

/* the long forms on narrow lanes a and b, by three-different's opcode: [SU]ADDL (0), [SU]SUBL (2), [SU]ABAL (5),
   [SU]ABDL (7), [SU]MLAL (8), SQDMLAL (9), [SU]MLSL (10), SQDMLSL (11), [SU]MULL (12), SQDMULL (13); z the
   accumulator's wide lanes. Every result fits the wide lane, so the helper's 64-bit sums truncate to the same bits */
static V long_op(SnxIr* ir, unsigned op, unsigned u, V a, V b, V z) {
    V x, y, r;
    if (op == 9 || op == 11 || op == 13) {
        r = doubling_long(ir, a, b);
        return op == 13 ? r : saturating(ir, 0, op == 11, 2 * bits_of(a), lanes_of(a), z, r);
    }
    x = widen(ir, a, u);
    y = widen(ir, b, u);
    switch (op) {
        case 0: return LLVMBuildAdd(B, x, y, "");
        case 2: return LLVMBuildSub(B, x, y, "");
        case 5:
        case 7:
            r = LLVMBuildSelect(B, LLVMBuildICmp(B, u ? LLVMIntUGT : LLVMIntSGT, a, b, ""), LLVMBuildSub(B, x, y, ""),
                                LLVMBuildSub(B, y, x, ""), "");
            return op == 5 ? LLVMBuildAdd(B, z, r, "") : r;
        case 8: return LLVMBuildAdd(B, z, LLVMBuildMul(B, x, y, ""), "");
        case 10: return LLVMBuildSub(B, z, LLVMBuildMul(B, x, y, ""), "");
        default: return LLVMBuildMul(B, x, y, "");
    }
}

/* SSHL, USHL, SRSHL, URSHL, SQSHL, UQSHL, SQRSHL, UQRSHL (register): vshl (helpers/simd.c) on 64-bit copies of the
   lanes, the shift the signed low byte of b's lane. Every shift amount is clamped below 64 and the cases it would
   change are selected away: LLVM's shifts by the width or more are poison. QC lanes go to *saturated */
static V register_shift(SnxIr* ir, V a, V b, unsigned u, unsigned round, unsigned sat) {
    unsigned eb = bits_of(a);
    V x = extend(ir, a, u, 64), sh = extend(ir, extend(ir, b, 1, 8), 0, 64);
    V zero = splat_like(ir, x, 0), k63 = splat_like(ir, x, 63), keb = splat_like(ir, x, eb), left, right, r;
    V amount, shifted, fits, negative, back;
    /* left: sh >= 0 */
    amount = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntUGT, sh, k63, ""), k63, sh, "");
    shifted = LLVMBuildShl(B, x, amount, "");
    if (!sat)
        left = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntSGE, sh, keb, ""), zero, shifted, "");
    else {
        /* no bit lost: shifting back gives x, and the result fits the lane (vshl's checks) */
        back = u ? LLVMBuildLShr(B, shifted, amount, "") : LLVMBuildAShr(B, shifted, amount, "");
        fits = LLVMBuildICmp(B, LLVMIntEQ, extend(ir, extend(ir, shifted, u, eb), u, 64), shifted, "");
        fits = LLVMBuildAnd(B, fits, LLVMBuildICmp(B, LLVMIntEQ, back, x, ""), "");
        fits = LLVMBuildAnd(B, fits, LLVMBuildICmp(B, LLVMIntSLT, sh, keb, ""), "");
        fits = LLVMBuildOr(B, fits, LLVMBuildICmp(B, LLVMIntEQ, x, zero, ""), "");
        left = LLVMBuildSelect(B, fits, shifted,
                               u ? splat_like(ir, x, uint_max(eb))
                                 : LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntSLT, x, zero, ""),
                                                   splat_like(ir, x, int_min(eb)), splat_like(ir, x, int_max(eb)), ""),
                               "");
        set_qc(ir, LLVMBuildAnd(B, LLVMBuildICmp(B, LLVMIntSGE, sh, zero, ""), LLVMBuildNot(B, fits, ""), ""));
    }
    /* right: n = -sh, 1..128; a shift by 64 or more leaves the sign (signed) or 0 */
    negative = LLVMBuildSub(B, zero, sh, "");
    amount = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntUGT, negative, k63, ""), k63, negative, "");
    if (u)
        right = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntUGT, negative, k63, ""), zero,
                                LLVMBuildLShr(B, x, amount, ""), "");
    else
        right = LLVMBuildAShr(B, x, amount, "");
    if (round) { /* + the last bit shifted out, bit n-1; past a shift of 64 there is nothing left */
        V last = LLVMBuildSub(B, negative, splat_like(ir, x, 1), "");
        V bit;
        last = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntUGT, last, k63, ""), k63, last, "");
        bit = LLVMBuildAnd(B, LLVMBuildLShr(B, x, last, ""), splat_like(ir, x, 1), "");
        right = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntUGT, negative, splat_like(ir, x, 64), ""), zero,
                                LLVMBuildAdd(B, right, bit, ""), "");
    }
    r = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntSLT, sh, zero, ""), right, left, "");
    return extend(ir, r, 1, eb);
}

/* three-same integer (vector): simd_3same for BSL/BIT/BIF, compares, MIN/MAX, ABD/ABA, MLA/MLS, MUL, pairwise
   ADDP/MAXP/MINP */
static int three_same(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, rm = (w >> 16) & 31, op = (w >> 11) & 31,
             rn = (w >> 5) & 31, rd = w & 31, eb = 8u << size, n, i;
    LLVMTypeRef t;
    V a, b, z, r;
    if (op == 3) { /* BSL, BIT, BIF (fast_simd has the others): one VPTERNLOGQ on AVX-512 hosts */
        LLVMTypeRef t = LLVMVectorType(ir->t_i64, 2);
        V x, y, d, r;
        if (!u || !size) return 0;
        x = ir_load(ir, t, vreg(ir, rn));
        y = ir_load(ir, t, vreg(ir, rm));
        d = ir_load(ir, t, vreg(ir, rd));
        LLVMSetAlignment(x, 8); /* V128 is 8-aligned in the CPU structure */
        LLVMSetAlignment(y, 8);
        LLVMSetAlignment(d, 8);
        if (host_ternlog(ir)) {
            unsigned table = size == 1   ? (TERN_C & TERN_A) | (~TERN_C & TERN_B)  /* BSL: (d & n) | (~d & m) */
                             : size == 2 ? (TERN_C & ~TERN_B) | (TERN_A & TERN_B)  /* BIT: (d & ~m) | (n & m) */
                                         : (TERN_C & TERN_B) | (TERN_A & ~TERN_B); /* BIF: (d & m) | (n & ~m) */
            r = LLVMBuildBitCast(B, ternlog(ir, x, y, d, table, 64), t, "");
        } else if (size == 1)
            r = LLVMBuildOr(B, LLVMBuildAnd(B, d, x, ""), LLVMBuildAnd(B, LLVMBuildNot(B, d, ""), y, ""), "");
        else if (size == 2)
            r = LLVMBuildOr(B, LLVMBuildAnd(B, d, LLVMBuildNot(B, y, ""), ""), LLVMBuildAnd(B, x, y, ""), "");
        else
            r = LLVMBuildOr(B, LLVMBuildAnd(B, d, y, ""), LLVMBuildAnd(B, x, LLVMBuildNot(B, y, ""), ""), "");
        if (!q) r = LLVMBuildInsertElement(B, r, ir_i64(ir, 0), ir_i32(ir, 1), "");
        LLVMSetAlignment(LLVMBuildStore(B, r, vreg(ir, rd)), 8);
        return 1;
    }
    if (op >= 0x18 || (size == 3 && !q)) return 0;
    n = (q ? 128u : 64u) / eb;
    t = LLVMVectorType(lane_type(ir, size), n);
    a = get_lanes(ir, vreg(ir, rn), size, n);
    b = get_lanes(ir, vreg(ir, rm), size, n);
    switch (op) {
        case 0x00:
        case 0x02:
            if (eb == 64) return 0;
            r = halving(ir, u, op == 0x02, size, n, a, b);
            break;
        case 0x01:
        case 0x05: r = saturating(ir, u, op == 0x05, eb, n, a, b); break;
        case 0x04: /* SHSUB, UHSUB: the difference in twice the width, halved arithmetically */
            if (eb == 64) return 0;
            r = narrow(ir, LLVMBuildAShr(B, LLVMBuildSub(B, widen(ir, a, u), widen(ir, b, u), ""),
                                         splat_like(ir, widen(ir, a, u), 1), ""));
            break;
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B: r = register_shift(ir, a, b, u, op == 0x0A || op == 0x0B, op == 0x09 || op == 0x0B); break;
        case 0x16:
            if (size != 1 && size != 2) return 0;
            r = doubling_high(ir, a, b, u);
            break;
        case 0x06: r = mask_of(ir, LLVMBuildICmp(B, u ? LLVMIntUGT : LLVMIntSGT, a, b, ""), t); break;
        case 0x07: r = mask_of(ir, LLVMBuildICmp(B, u ? LLVMIntUGE : LLVMIntSGE, a, b, ""), t); break;
        case 0x11:
            r = u ? mask_of(ir, LLVMBuildICmp(B, LLVMIntEQ, a, b, ""), t)
                  : mask_of(ir, LLVMBuildICmp(B, LLVMIntNE, LLVMBuildAnd(B, a, b, ""), LLVMConstNull(t), ""), t);
            break;
        case 0x0C:
        case 0x0D:
            if (eb == 64) return 0;
            r = LLVMBuildSelect(
                B,
                LLVMBuildICmp(B, op == 0x0C ? (u ? LLVMIntUGT : LLVMIntSGT) : (u ? LLVMIntULT : LLVMIntSLT), a, b, ""),
                a, b, "");
            break;
        case 0x0E:
        case 0x0F:
            if (eb == 64) return 0;
            r = LLVMBuildSelect(B, LLVMBuildICmp(B, u ? LLVMIntUGT : LLVMIntSGT, a, b, ""), LLVMBuildSub(B, a, b, ""),
                                LLVMBuildSub(B, b, a, ""), "");
            if (op == 0x0F) r = LLVMBuildAdd(B, get_lanes(ir, vreg(ir, rd), size, n), r, "");
            break;
        case 0x12:
            if (eb == 64) return 0;
            z = get_lanes(ir, vreg(ir, rd), size, n);
            r = u ? LLVMBuildSub(B, z, LLVMBuildMul(B, a, b, ""), "")
                  : LLVMBuildAdd(B, z, LLVMBuildMul(B, a, b, ""), "");
            break;
        case 0x13: /* MUL, PMUL (bytes) */
            if (eb == 64 || (u && size)) return 0;
            r = u ? narrow(ir, poly_multiply(ir, a, b)) : LLVMBuildMul(B, a, b, "");
            break;
        case 0x14:
        case 0x15:
        case 0x17: {
            /* pairwise: lanes 0..n/2-1 from pairs of a, the rest from pairs of b */
            unsigned even[16], odd[16];
            V x, y;
            if ((op != 0x17 && eb == 64) || (op == 0x17 && u)) return 0;
            for (i = 0; i < n; ++i) {
                even[i] = i < n / 2 ? 2 * i : n + 2 * (i - n / 2);
                odd[i] = even[i] + 1;
            }
            x = shuffle(ir, a, b, even, n);
            y = shuffle(ir, a, b, odd, n);
            if (op == 0x17)
                r = LLVMBuildAdd(B, x, y, "");
            else
                r = LLVMBuildSelect(
                    B,
                    LLVMBuildICmp(B, op == 0x14 ? (u ? LLVMIntUGT : LLVMIntSGT) : (u ? LLVMIntULT : LLVMIntSLT), x, y,
                                  ""),
                    x, y, "");
            break;
        }
        default: return 0;
    }
    write_lanes(ir, rd, r, q);
    return 1;
}

/* byte-lane shifts by immediate through GF2P8AFFINEQB: simd_shimm's semantics for eb 8 */
static int shift_bytes_gfni(SnxIr* ir, unsigned q, unsigned u, unsigned op, unsigned shr, unsigned shl, unsigned rn,
                            unsigned rd) {
    unsigned n = q ? 16 : 8;
    V a = v128_type_load(ir, vreg(ir, rn)), r, z;
    switch (op) {
        case 0x00:
        case 0x02:
        case 0x04:
        case 0x06: /* [SU][R]SHR, [SU][R]SRA */
            r = gf_affine(ir, a, u ? gf_lshr(shr) : gf_ashr(shr));
            if (op & 4) /* + the last bit shifted out (signed or not, it is source bit shr-1) */
                r = LLVMBuildAdd(B, r, gf_affine(ir, a, gf_bit(shr - 1)), "");
            if (op & 2) r = LLVMBuildAdd(B, v128_type_load(ir, vreg(ir, rd)), r, "");
            break;
        case 0x08: /* SRI */
            if (!u) return 0;
            z = v128_type_load(ir, vreg(ir, rd));
            if (shr >= 8)
                r = z;
            else
                r = LLVMBuildOr(B, LLVMBuildAnd(B, z, broadcast(ir, ir_i8(ir, ~(0xFFu >> shr)), 16), ""),
                                gf_affine(ir, a, gf_lshr(shr)), "");
            break;
        case 0x0A:
            r = gf_affine(ir, a, gf_shl(shl));
            if (u) { /* SLI */
                z = v128_type_load(ir, vreg(ir, rd));
                r = LLVMBuildOr(B, LLVMBuildAnd(B, z, broadcast(ir, ir_i8(ir, ~(0xFFu << shl)), 16), ""), r, "");
            }
            break;
        default: return 0;
    }
    write_lanes(ir, rd, low_lanes(ir, r, n), q);
    return 1;
}

/* the narrowing shifts right on the wide lanes of Vn, shr 1..eb: SHRN and RSHRN wrap, the saturating ones clamp
   with QC (and have scalar forms: one lane, the rest of Vd zero). Rounding adds the last bit shifted out (simd_shimm:
   the constant first would overflow 64 bits) */
static int shift_narrow(SnxIr* ir, unsigned q, unsigned u, unsigned op, unsigned size, unsigned shr, unsigned rn,
                        unsigned rd, unsigned scalar) {
    unsigned rnd = op & 1;
    V x, v, bit;
    if (scalar && !u && op < 0x12) return 0;
    x = vlanes(ir, vreg(ir, rn), size + 1, scalar ? 1 : 8u >> size);
    if (!u && op < 0x12) { /* SHRN, RSHRN: the bits kept end below the wide lane's top, so its carry is not needed */
        if (rnd) x = LLVMBuildAdd(B, x, splat_like(ir, x, (SnxU64)1 << (shr - 1)), "");
        write_narrow(ir, rd, narrow(ir, LLVMBuildLShr(B, x, splat_like(ir, x, shr), "")), q);
        return 1;
    }
    /* U with op 0x12/0x13: UQSHRN, UQRSHRN (unsigned); else SQSHRN, SQRSHRN, SQSHRUN, SQRSHRUN (signed source) */
    v = u && op >= 0x12 ? LLVMBuildLShr(B, x, splat_like(ir, x, shr), "")
                        : LLVMBuildAShr(B, x, splat_like(ir, x, shr), "");
    if (rnd) {
        bit = LLVMBuildAnd(B, LLVMBuildLShr(B, x, splat_like(ir, x, shr - 1), ""), splat_like(ir, x, 1), "");
        v = LLVMBuildAdd(B, v, bit, "");
    }
    write_narrow(ir, rd, saturate_narrow(ir, v, !(u && op >= 0x12), !u), q);
    return 1;
}

/* shift by immediate: simd_shimm's SSHR/USHR, SSRA/USRA, SRSHR/URSHR, SRSRA/URSRA, SHL, SLI, SRI, SQSHL/UQSHL,
   SQSHLU, SSHLL/USHLL and the narrowing shifts; scalar, the 64-bit forms and the saturating left shifts of any size
   (as simd_shimm allows) */
static int shift_immediate(SnxIr* ir, SnxU32 w, unsigned scalar) {
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, immh = (w >> 19) & 15, immb = (w >> 16) & 7,
             op = (w >> 11) & 31, rn = (w >> 5) & 31, rd = w & 31, size = 3, eb, imm, shr, shl, n;
    LLVMTypeRef t;
    V a, r;
    if (!immh) return 0;
    while (!((immh >> size) & 1)) size--;
    eb = 8u << size;
    imm = (immh << 3) | immb;
    shr = 2 * eb - imm;
    shl = imm - eb;
    if (op == 0x14 || (op >= 0x10 && op <= 0x13)) {
        if (size == 3) return 0;
        if (op != 0x14) return shift_narrow(ir, q, u, op, size, shr, rn, rd, scalar);
        if (scalar) return 0;
        /* SSHLL, USHLL: the low or high half widened, shifted left */
        r = widen(ir, half_lanes(ir, rn, size, q), u);
        set_lanes(ir, vreg(ir, rd), LLVMBuildShl(B, r, splat_like(ir, r, shl), ""));
        return 1;
    }
    if (size == 3 && !q && !scalar) return 0;
    if (scalar && size != 3 && op != 0x0C && op != 0x0E) return 0;
    if (!size && !scalar && host_gfni(ir) && shift_bytes_gfni(ir, q, u, op, shr, shl, rn, rd)) return 1;
    n = scalar ? 1 : (q ? 128u : 64u) / eb;
    t = lanes_type(ir, size, n);
    a = get_lanes(ir, vreg(ir, rn), size, n);
#define SPLAT(v) broadcast(ir, LLVMConstInt(lane_type(ir, size), (v), 0), n)
    switch (op) {
        case 0x00:
        case 0x02:
        case 0x04:
        case 0x06: /* [SU][R]SHR, [SU][R]SRA: a shift by the element size gives 0 (unsigned) or the sign */
            if (u)
                r = shr >= eb ? LLVMConstNull(t) : LLVMBuildLShr(B, a, SPLAT(shr), "");
            else
                r = LLVMBuildAShr(B, a, SPLAT(shr >= eb ? eb - 1 : shr), "");
            if (op & 4) { /* rounding: + bit shr-1 of the source (for shr = eb, 0 signed, the top bit unsigned) */
                V bit = LLVMBuildAnd(B, LLVMBuildLShr(B, a, SPLAT(shr - 1), ""), SPLAT(1), "");
                r = shr >= eb && !u ? LLVMConstNull(t) : LLVMBuildAdd(B, r, bit, "");
            }
            if (op & 2) r = LLVMBuildAdd(B, get_lanes(ir, vreg(ir, rd), size, n), r, "");
            break;
        case 0x08: /* SRI */
            if (!u) return 0;
            if (shr >= eb)
                r = get_lanes(ir, vreg(ir, rd), size, n);
            else {
                SnxU64 m = (eb == 64 ? ~(SnxU64)0 : ((SnxU64)1 << eb) - 1) >> shr;
                r = LLVMBuildOr(B, LLVMBuildAnd(B, get_lanes(ir, vreg(ir, rd), size, n), SPLAT(~m), ""),
                                LLVMBuildLShr(B, a, SPLAT(shr), ""), "");
            }
            break;
        case 0x0A:
            if (!u) /* SHL */
                r = LLVMBuildShl(B, a, SPLAT(shl), "");
            else { /* SLI */
                SnxU64 m = (eb == 64 ? ~(SnxU64)0 : ((SnxU64)1 << eb) - 1) << shl;
                r = LLVMBuildOr(B, LLVMBuildAnd(B, get_lanes(ir, vreg(ir, rd), size, n), SPLAT(~m), ""),
                                LLVMBuildAnd(B, LLVMBuildShl(B, a, SPLAT(shl), ""), SPLAT(m), ""), "");
            }
            break;
        case 0x0C:   /* SQSHLU: a negative lane gives 0, then as UQSHL */
        case 0x0E: { /* SQSHL, UQSHL: saturated when shifting back does not give the lane (no bit lost, no sign
                        change), QC */
            V shifted = LLVMBuildShl(B, a, SPLAT(shl), ""), lost, negative = NULL, top;
            if (op == 0x0C && !u) return 0;
            if (!u) {
                lost = LLVMBuildICmp(B, LLVMIntNE, LLVMBuildAShr(B, shifted, SPLAT(shl), ""), a, "");
                top = LLVMBuildSelect(B, LLVMBuildICmp(B, LLVMIntSLT, a, LLVMConstNull(t), ""), SPLAT(int_min(eb)),
                                      SPLAT(int_max(eb)), "");
            } else {
                lost = LLVMBuildICmp(B, LLVMIntNE, LLVMBuildLShr(B, shifted, SPLAT(shl), ""), a, "");
                top = SPLAT(uint_max(eb));
            }
            r = LLVMBuildSelect(B, lost, top, shifted, "");
            if (op == 0x0C) {
                negative = LLVMBuildICmp(B, LLVMIntSLT, a, LLVMConstNull(t), "");
                r = LLVMBuildSelect(B, negative, LLVMConstNull(t), r, "");
                lost = LLVMBuildOr(B, lost, negative, "");
            }
            set_qc(ir, lost);
            break;
        }
        default: return 0;
    }
#undef SPLAT
    write_lanes(ir, rd, r, q);
    return 1;
}

/* FABS, FNEG (vector): sign bits only */
static int fp_sign_misc(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31, rn = (w >> 5) & 31,
             rd = w & 31, sz = size & 1;
    SnxU64 sign = sz ? (SnxU64)1 << 63 : 0x8000000080000000ull;
    V lo, hi;
    if (op != 0x0F || !(size >> 1) || (sz && !q)) return 0;
    lo = ir_vd(ir, rn, 0);
    hi = ir_vd(ir, rn, 1);
    if (u) {
        lo = LLVMBuildXor(B, lo, ir_i64(ir, sign), "");
        hi = LLVMBuildXor(B, hi, ir_i64(ir, sign), "");
    } else {
        lo = LLVMBuildAnd(B, lo, ir_i64(ir, ~sign), "");
        hi = LLVMBuildAnd(B, hi, ir_i64(ir, ~sign), "");
    }
    ir_set_vd(ir, rd, 0, lo);
    ir_set_vd(ir, rd, 1, q ? hi : ir_i64(ir, 0));
    return 1;
}

typedef enum {
    FP_LANES_ADD,
    FP_LANES_ABD,
    FP_LANES_MULX,
    FP_LANES_STEP,
    FP_LANES_MINMAX,
    FP_LANES_COMPARE,
    FP_LANES_SQRT,
    FP_LANES_ESTIMATE,
    FP_LANES_ROUND,
    FP_LANES_RECPX,
    FP_LANES_URECPE,
    FP_LANES_URSQRTE,
    FP_LANES_TO_INT,
    FP_LANES_FROM_INT
} FpLaneKind;

/* Only one parameter shape is meaningful for a decoded FP opcode. Keep the
   translation-time descriptor typed; no extra branches reach guest code. */
typedef struct {
    unsigned is_unsigned, rounding, fractional_bits;
} FpConvert;

typedef struct {
    FpLaneKind kind;
    union {
        struct {
            unsigned minimum, numeric;
        } minmax;
        struct {
            unsigned mode, exact;
        } round;
        unsigned predicate;
        unsigned inverse;
        FpConvert convert;
    } param;
} FpOp;

/* the unsigned estimate helpers (fp_arithmetic.c), u32 to u32: plain symbols of the runtime library */
static V u32_helper(SnxIr* ir, const char* name, V x) {
    LLVMTypeRef p[1] = {ir->t_i32};
    return ir_call(ir, ir_extern(ir, name, ir->t_i32, p, 1), &x, 1);
}

/* one lane through the helper simd_3same's and simd_2misc's loops call (helpers/simd.c), as iN */
static V fp_lane(SnxIr* ir, const FpOp* op, V a, V b, unsigned width) {
    switch (op->kind) {
        case FP_LANES_ADD: return ir_fp_binary(ir, a, b, width, 0);  /* SNX_FP_ADD */
        case FP_LANES_ABD: return ir_fp_binary(ir, a, b, width, 5);  /* SNX_FP_ABD */
        case FP_LANES_MULX: return ir_fp_binary(ir, a, b, width, 4); /* SNX_FP_MULX */
        case FP_LANES_STEP: return ir_fp_binary(ir, a, b, width, op->param.inverse ? 7 : 6);
        case FP_LANES_MINMAX: return ir_fp_minmax(ir, a, b, width, op->param.minmax.minimum, op->param.minmax.numeric);
        case FP_LANES_COMPARE: return ir_fp_compare_mask(ir, op->param.predicate, a, b, width);
        case FP_LANES_SQRT: return ir_fp_unary(ir, a, width, 0); /* SNX_FP_SQRT */
        case FP_LANES_ESTIMATE: return ir_fp_unary(ir, a, width, op->param.inverse ? 2 : 1);
        case FP_LANES_ROUND: return ir_fp_round_to(ir, a, width, op->param.round.mode, op->param.round.exact);
        case FP_LANES_RECPX: return ir_fp_unary(ir, a, width, 3); /* SNX_FP_RECPX */
        case FP_LANES_URECPE: return u32_helper(ir, "urecpe32", a);
        case FP_LANES_URSQRTE: return u32_helper(ir, "ursqrte32", a);
        case FP_LANES_TO_INT:
            return ir_fp_to_int(ir, a, width, width, op->param.convert.is_unsigned, op->param.convert.fractional_bits,
                                op->param.convert.rounding);
        default:
            return ir_fp_from_int(ir, a, width, width, op->param.convert.is_unsigned,
                                  op->param.convert.fractional_bits);
    }
}

/* the operation on n lanes of 1<<s bytes (x, and y for two operands) into Vd: every lane at once on the host's
   vector unit when the fast path's guard holds for all of them (ir_fp.c's fast paths), otherwise the helper a lane
   at a time. One lane is a scalar form: the rest of Vd is zero */
static void fp_lanes(SnxIr* ir, const FpOp* op, unsigned s, unsigned n, V x, V y, unsigned rd, unsigned q) {
    unsigned width = 8u << s, i;
    LLVMBasicBlockRef slow, join = NULL;
    SnxFpFast f;
    V r;
    switch (op->kind) {
        case FP_LANES_ADD: ir_fp_fast(ir, SNX_IR_FP_ADD, width, n, x, y, NULL, &f); break;
        case FP_LANES_ABD: /* |x - y|: snx_fp_binary clears the difference's sign */
            ir_fp_fast(ir, SNX_IR_FP_SUB, width, n, x, y, NULL, &f);
            f.value = LLVMBuildAnd(
                B, f.value, broadcast(ir, LLVMConstInt(lane_type(ir, s), ((SnxU64)1 << (width - 1)) - 1, 0), n), "");
            break;
        case FP_LANES_MULX: /* FMUL's result but for infinity times zero, which the guard keeps out */
            ir_fp_fast(ir, SNX_IR_FP_MUL, width, n, x, y, NULL, &f);
            break;
        case FP_LANES_STEP: ir_fp_step_fast(ir, op->param.inverse, width, n, x, y, &f); break;
        case FP_LANES_MINMAX: ir_fp_minmax_fast(ir, width, n, x, y, op->param.minmax.minimum, &f); break;
        case FP_LANES_COMPARE: ir_fp_compare_fast(ir, op->param.predicate, width, n, x, y, &f); break;
        case FP_LANES_SQRT: ir_fp_fast(ir, SNX_IR_FP_SQRT, width, n, x, NULL, NULL, &f); break;
        case FP_LANES_ESTIMATE: ir_fp_estimate_fast(ir, op->param.inverse, width, n, x, &f); break;
        case FP_LANES_ROUND:
        case FP_LANES_RECPX:
        case FP_LANES_URECPE:
        case FP_LANES_URSQRTE:
            f.ok = NULL; /* the helpers only */
            break;
        case FP_LANES_TO_INT:
            if (!ir_fp_to_int_fast(ir, width, n, x, width, op->param.convert.is_unsigned, op->param.convert.rounding,
                                   op->param.convert.fractional_bits, &f))
                f.ok = NULL;
            break;
        default:
            ir_fp_from_int_fast(ir, width, n, x, op->param.convert.is_unsigned, op->param.convert.fractional_bits, &f);
            break;
    }
    if (f.ok) { /* else no fast path: the helper alone */
        join = ir_fp_branch(ir, f.ok, &slow);
        write_lanes(ir, rd, f.value, q);
        ir_fp_else(ir, join, slow, f.inexact);
    }
    r = n == 1 ? NULL : LLVMGetUndef(LLVMTypeOf(x));
    for (i = 0; i < n; ++i) {
        V a = n == 1 ? x : LLVMBuildExtractElement(B, x, ir_i32(ir, i), ""), b = NULL, v;
        if (y) b = n == 1 ? y : LLVMBuildExtractElement(B, y, ir_i32(ir, i), "");
        v = ir_cast(ir, fp_lane(ir, op, a, b, width), lane_type(ir, s), 0);
        r = n == 1 ? v : LLVMBuildInsertElement(B, r, v, ir_i32(ir, i), "");
    }
    write_lanes(ir, rd, r, q);
    if (join) ir_fp_join(ir, join);
}

/* FP three-same beyond fast_simd's arithmetic: simd_3same's FADDP, FABD, FMULX, FRECPS, FRSQRTS, FMAX/FMIN/FMAXNM/
   FMINNM and their pairwise forms, FCMEQ/FCMGE/FCMGT and FACGE/FACGT; the scalar forms of FABD, FMULX, FRECPS,
   FRSQRTS and the compares (simd_3same refuses the scalar pairwise, accumulating and min/max ones) */
static int fp_three_same(SnxIr* ir, SnxU32 w, unsigned scalar) {
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, sz = (w >> 22) & 1, hi = (w >> 23) & 1,
             rm = (w >> 16) & 31, op = (w >> 11) & 31, rn = (w >> 5) & 31, rd = w & 31, s = sz ? 3 : 2,
             n = scalar ? 1
                 : sz   ? 2
                        : (q ? 4 : 2),
             i;
    unsigned pair = 0, absolute = 0;
    FpOp o = {0};
    V x, y;
    if (!scalar && sz && !q) return 0;
    o.param.minmax.minimum = hi;
    o.param.minmax.numeric = op == 0x18;
    switch (op) {
        case 0x18: /* FMAXNM, FMINNM */
        case 0x1E: /* FMAX, FMIN; with U, the pairwise FMAXNMP, FMINNMP, FMAXP, FMINP */
            o.kind = FP_LANES_MINMAX;
            pair = u;
            break;
        case 0x1A: /* FADDP, FABD (FADD and FSUB are fast_simd's) */
            if (!u) return 0;
            o.kind = hi ? FP_LANES_ABD : FP_LANES_ADD;
            pair = !hi;
            break;
        case 0x1B: /* FMULX (FMUL is fast_simd's) */
            if (u || hi) return 0;
            o.kind = FP_LANES_MULX;
            break;
        case 0x1F: /* FRECPS, FRSQRTS (FDIV is fast_simd's) */
            if (u) return 0;
            o.kind = FP_LANES_STEP;
            o.param.inverse = hi;
            break;
        case 0x1C:
            if (!u && hi) return 0;
            o.kind = FP_LANES_COMPARE;
            o.param.predicate = !u ? SNX_IR_CMP_EQ : hi ? SNX_IR_CMP_GT : SNX_IR_CMP_GE;
            break;
        case 0x1D: /* FACGE, FACGT: the comparison of the magnitudes */
            if (!u) return 0;
            o.kind = FP_LANES_COMPARE;
            o.param.predicate = hi ? SNX_IR_CMP_GT : SNX_IR_CMP_GE;
            absolute = 1;
            break;
        default: return 0;
    }
    if (scalar && o.kind != FP_LANES_ABD && o.kind != FP_LANES_MULX && o.kind != FP_LANES_STEP &&
        o.kind != FP_LANES_COMPARE)
        return 0;
    x = get_lanes(ir, vreg(ir, rn), s, n);
    y = get_lanes(ir, vreg(ir, rm), s, n);
    if (pair) { /* lanes 0..n/2-1 from pairs of Vn, the rest from pairs of Vm: in Vn:Vm, elements 2i and 2i+1 */
        unsigned even[4], odd[4];
        V a = x;
        for (i = 0; i < n; ++i) {
            even[i] = 2 * i;
            odd[i] = 2 * i + 1;
        }
        x = shuffle(ir, a, y, even, n);
        y = shuffle(ir, a, y, odd, n);
    }
    if (absolute) {
        V magnitude = broadcast(ir, LLVMConstInt(lane_type(ir, s), sz ? ~((SnxU64)1 << 63) : 0x7FFFFFFFUL, 0), n);
        x = LLVMBuildAnd(B, x, magnitude, "");
        y = LLVMBuildAnd(B, y, magnitude, "");
    }
    fp_lanes(ir, &o, s, n, x, y, rd, q);
    return 1;
}

/* FP two-register misc: simd_2misc's FCMGT, FCMGE, FCMEQ, FCMLE, FCMLT #0.0, FCVT[NPMZA][SU], SCVTF, UCVTF, FRECPE
   and FRSQRTE (vector and scalar) and FSQRT (vector) */
static int fp_misc(SnxIr* ir, SnxU32 w, unsigned scalar) {
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31,
             rn = (w >> 5) & 31, rd = w & 31, sz = size & 1, s = sz ? 3 : 2,
             n = scalar ? 1
                 : sz   ? 2
                        : (q ? 4 : 2);
    unsigned k = u << 6 | (size >> 1) << 5 | op;
    FpOp o = {0};
    V x;
    if (!scalar && sz && !q) return 0;
    o.param.convert.is_unsigned = u;
    if (op >= 0x0C && op <= 0x0E && (size >> 1)) {
        if (op == 0x0C)
            o.param.predicate = u ? SNX_IR_CMP_GE : SNX_IR_CMP_GT;
        else if (op == 0x0D)
            o.param.predicate = u ? SNX_IR_CMP_LE : SNX_IR_CMP_EQ;
        else if (!u)
            o.param.predicate = SNX_IR_CMP_LT;
        else
            return 0;
        o.kind = FP_LANES_COMPARE;
        fp_lanes(ir, &o, s, n, get_lanes(ir, vreg(ir, rn), s, n), LLVMConstNull(lanes_type(ir, s, n)), rd, q);
        return 1;
    }
    switch (k) {
        /* FRINTN, FRINTM, FRINTP, FRINTZ, FRINTA, FRINTX and FRINTI: the mode the interpreter's key picks (the size
           bit and U together name it: 0-4 or the FPCR's), on the vector group only, and no "2" forms in the helper */
        case 0x18:
        case 0x19:
        case 0x38:
        case 0x39:
        case 0x58:
        case 0x59:
        case 0x79:
            if (scalar) return 0;
            o.kind = FP_LANES_ROUND;
            switch (k) {
                case 0x18: o.param.round.mode = 0; break;                                          /* FRINTN */
                case 0x19: o.param.round.mode = 2; break;                                          /* FRINTM */
                case 0x38: o.param.round.mode = 1; break;                                          /* FRINTP */
                case 0x39: o.param.round.mode = 3; break;                                          /* FRINTZ */
                case 0x58: o.param.round.mode = 4; break;                                          /* FRINTA */
                case 0x59: o.param.round.mode = SNX_IR_FP_RM_FPCR, o.param.round.exact = 1; break; /* FRINTX */
                default: o.param.round.mode = SNX_IR_FP_RM_FPCR; break;                            /* FRINTI */
            }
            break;
        case 0x3C: /* URECPE (32-bit lanes only), URSQRTE */
        case 0x7C:
            if (scalar || sz) return 0;
            o.kind = k == 0x3C ? FP_LANES_URECPE : FP_LANES_URSQRTE;
            break;
        case 0x3F: /* FRECPX (AdvSIMD scalar only) */
            if (!scalar) return 0;
            o.kind = FP_LANES_RECPX;
            break;
        case 0x1A: /* FCVTNS, FCVTPS, FCVTMS, FCVTZS, FCVTAS and the unsigned ones: snx_fp_to_int's mode */
        case 0x3A:
        case 0x1B:
        case 0x3B:
        case 0x1C:
        case 0x5A:
        case 0x7A:
        case 0x5B:
        case 0x7B:
        case 0x5C:
            o.kind = FP_LANES_TO_INT;
            o.param.convert.rounding = (k & 0x1F) == 0x1C   ? 4
                                       : (k & 0x1F) == 0x1A ? (size >> 1 ? 1 : 0)
                                                            : (size >> 1 ? 3 : 2);
            break;
        case 0x1D: /* SCVTF, UCVTF */
        case 0x5D: o.kind = FP_LANES_FROM_INT; break;
        case 0x3D: /* FRECPE, FRSQRTE */
        case 0x7D:
            o.kind = FP_LANES_ESTIMATE;
            o.param.inverse = u;
            break;
        case 0x7F:
            if (scalar) return 0; /* FSQRT has no scalar AdvSIMD form */
            o.kind = FP_LANES_SQRT;
            break;
        default: return 0;
    }
    x = get_lanes(ir, vreg(ir, rn), s, n);
    fp_lanes(ir, &o, s, n, x, NULL, rd, q);
    return 1;
}

/* SCVTF, UCVTF, FCVTZS, FCVTZU (vector and scalar, fixed point): simd_shimm's conversions with fbits = 2*esize - imm */
static int fp_fixed(SnxIr* ir, SnxU32 w, unsigned scalar) {
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, immh = (w >> 19) & 15, immb = (w >> 16) & 7,
             op = (w >> 11) & 31, rn = (w >> 5) & 31, rd = w & 31, s, n;
    FpOp o = {0};
    if ((op != 0x1C && op != 0x1F) || immh < 4) return 0; /* 32 and 64-bit lanes only */
    s = immh & 8 ? 3 : 2;
    if (s == 3 && !q && !scalar) return 0;
    n = scalar ? 1 : (q ? 128u : 64u) >> (3 + s);
    o.kind = op == 0x1C ? FP_LANES_FROM_INT : FP_LANES_TO_INT;
    o.param.convert = (FpConvert){u, 3, 2 * (8u << s) - ((immh << 3) | immb)};
    fp_lanes(ir, &o, s, n, get_lanes(ir, vreg(ir, rn), s, n), NULL, rd, q);
    return 1;
}

/* FCVTN, FCVTN2 (doubles to floats, floats to halves, FPCR's rounding) and FCVTXN, FCVTXN2 (doubles to floats,
   round to odd, also scalar): simd_2misc's snx_fp_convert lanes, into Vd's low half with the upper half zero, its
   upper half with the low half kept (the "2" forms), or for the scalar FCVTXN its low 32 bits. FCVTL and FCVTL2
   (halves to floats, floats to doubles) read the upper half of Vn and write all of Vd */
static int fp_narrow(SnxIr* ir, SnxU32 w, unsigned scalar) {
    unsigned q = scalar ? 0 : (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31,
             rn = (w >> 5) & 31, rd = w & 31, from = size & 1 ? 64 : 32, s = size & 1 ? 3 : 2,
             n = scalar ? 1 : 128 / from, i;
    LLVMTypeRef t = lane_type(ir, s - 1);
    LLVMBasicBlockRef slow, join;
    SnxFpFast f;
    V x, r;
    if (op == 0x17) { /* FCVTL, FCVTL2: the q half of Vn's lanes widened, into all of Vd */
        unsigned element = size & 1 ? 2 : 1, lanes = size & 1 ? 2 : 4, lane;
        LLVMTypeRef wide = lane_type(ir, element + 1);
        V v;
        if (u || scalar || (size >> 1)) return 0;
        v = LLVMGetUndef(LLVMVectorType(wide, lanes));
        for (lane = 0; lane < lanes; ++lane) {
            V bits = ir_fp_wide(ir, get(ir, vreg(ir, rn), element, lane + lanes * q), element * 16);
            v = LLVMBuildInsertElement(B, v, ir_cast(ir, bits, wide, 0), ir_i32(ir, lane), "");
        }
        write_lanes(ir, rd, v, 1);
        return 1;
    }
    if (op != 0x16 || (size >> 1) || (scalar && !u) || (u && from != 64)) return 0;
    x = get_lanes(ir, vreg(ir, rn), s, n);
    ir_fp_narrow_fast(ir, from, n, x, u, &f);
    join = ir_fp_branch(ir, f.ok, &slow);
    write_narrow(ir, rd, f.value, q);
    ir_fp_else(ir, join, slow, f.inexact);
    r = n == 1 ? NULL : LLVMGetUndef(LLVMVectorType(t, n));
    for (i = 0; i < n; ++i) {
        V lane =
            ir_cast(ir, ir_fp_narrow(ir, n == 1 ? x : LLVMBuildExtractElement(B, x, ir_i32(ir, i), ""), from, u), t, 0);
        r = n == 1 ? lane : LLVMBuildInsertElement(B, r, lane, ir_i32(ir, i), "");
    }
    write_narrow(ir, rd, r, q);
    ir_fp_join(ir, join);
    return 1;
}
