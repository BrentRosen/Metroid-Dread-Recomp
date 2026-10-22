/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* ---- beyond snx_a64_fast_simd: encodings snx_simd handles, emitted inline with its semantics ---- */

static V v128_type_load(SnxIr* ir, V base) { /* the 16 bytes at base as <16 x i8> */
    V load = ir_load(ir, LLVMVectorType(ir->t_i8, 16), base);
    LLVMSetAlignment(load, 1);
    return load;
}
static void zero_high(SnxIr* ir, unsigned n) { ir_set_vd(ir, n, 1, ir_i64(ir, 0)); }

/* LD1/ST1 {Vt..Vt+n-1}: simd_ldst stores each element in turn; for consecutive registers that is one copy per
   register. Post-index adds the size, or Xm */
static int ld1_multiple(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, l = (w >> 22) & 1, post = (w >> 23) & 1, rm = (w >> 16) & 31, rn = (w >> 5) & 31,
             rt = w & 31, op = (w >> 12) & 15, regs, bytes = q ? 16 : 8, r;
    V address;
    regs = op == 7 ? 1 : op == 10 ? 2 : op == 6 ? 3 : op == 2 ? 4 : 0;
    if (!regs) return 0;
    address = ir_x(ir, rn);
    for (r = 0; r < regs; ++r) {
        V a = LLVMBuildAdd(B, address, ir_i64(ir, (SnxU64)r * bytes), "");
        if (l)
            set_lanes(ir, vreg(ir, (rt + r) & 31), ir_rd_bytes(ir, a, bytes));
        else
            ir_wr_bytes(ir, a, get_lanes(ir, vreg(ir, (rt + r) & 31), 0, bytes));
    }
    if (l && !q)
        for (r = 0; r < regs; ++r) zero_high(ir, (rt + r) & 31);
    if (post)
        ir_set_x(ir, rn, LLVMBuildAdd(B, address, rm == 31 ? ir_i64(ir, (SnxU64)regs * bytes) : ir_x(ir, rm), ""));
    return 1;
}

/* LD1R-LD4R and LD1-4/ST1-4 of one lane: sel consecutive elements, one per register (Vt..Vt+sel-1, wrapping),
   accessed one at a time in simd_ldst's order */
static int ld1_single(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, l = (w >> 22) & 1, post = (w >> 23) & 1, rm = (w >> 16) & 31, rn = (w >> 5) & 31,
             rt = w & 31, r = (w >> 21) & 1, op = (w >> 13) & 7, s = (w >> 12) & 1, size = (w >> 10) & 3;
    unsigned sc = op >> 1, sel = (((op & 1) << 1) | r) + 1, esz, idx, n, bytes, k;
    V address;
    address = ir_x(ir, rn);
    if (sc == 3) { /* LDnR: each element in every lane of its register, the rest zero */
        V value;
        if (!l || s) return 0;
        n = (q ? 128u : 64u) >> (3 + size);
        for (k = 0; k < sel; ++k) {
            value = ir_rd(ir, LLVMBuildAdd(B, address, ir_i64(ir, (SnxU64)k << size), ""), 8u << size);
            /* The replicate is one SSA shuffle; no temporary guest V128. */
            write_lanes(ir, (rt + k) & 31, broadcast(ir, value, n), q);
        }
        bytes = sel << size;
    } else {
        if (sc == 0) {
            esz = 0;
            idx = (q << 3) | (s << 2) | size;
        } else if (sc == 1) {
            if (size & 1) return 0;
            esz = 1;
            idx = (q << 2) | (s << 1) | (size >> 1);
        } else {
            if (size & 2) return 0;
            if (size & 1) {
                if (s) return 0;
                esz = 3;
                idx = q;
            } else {
                esz = 2;
                idx = (q << 1) | s;
            }
        }
        for (k = 0; k < sel; ++k) {
            V a = LLVMBuildAdd(B, address, ir_i64(ir, (SnxU64)k << esz), "");
            if (l)
                ir_set_ve(ir, (rt + k) & 31, esz, idx, ir_rd(ir, a, 8u << esz));
            else
                ir_wr(ir, a, 8u << esz, ir_ve(ir, (rt + k) & 31, esz, idx));
        }
        bytes = sel << esz;
    }
    if (post) ir_set_x(ir, rn, LLVMBuildAdd(B, address, rm == 31 ? ir_i64(ir, bytes) : ir_x(ir, rm), ""));
    return 1;
}

/* store <lanes> into Vd with the upper half cleared when Q is 0 (simd_perm and EXT write r from zero) */
static void write_lanes(SnxIr* ir, unsigned rd, V value, unsigned q) {
    LLVMTypeRef type = LLVMTypeOf(value);
    unsigned bits = LLVMGetTypeKind(type) == LLVMVectorTypeKind
                        ? LLVMGetVectorSize(type) * LLVMGetIntTypeWidth(LLVMGetElementType(type))
                        : LLVMGetIntTypeWidth(type);
    LLVMTypeRef integer = LLVMIntTypeInContext(ir->ctx, bits);
    V packed;
    /* the guest's lane 0 is the low bits; a big-endian target's bitcast puts a vector's element 0 in the high ones */
    if (ir->big_endian && LLVMGetTypeKind(type) == LLVMVectorTypeKind && LLVMGetVectorSize(type) > 1) {
        V mask[16];
        unsigned n = LLVMGetVectorSize(type), i;
        for (i = 0; i < n; ++i) mask[i] = ir_i32(ir, n - 1 - i);
        value = LLVMBuildShuffleVector(B, value, value, LLVMConstVector(mask, n), "");
    }
    packed = LLVMGetTypeKind(type) == LLVMVectorTypeKind ? LLVMBuildBitCast(B, value, integer, "") : value;
    V full = bits == 128 ? packed : LLVMBuildZExt(B, packed, ir->t_i128, "");
    /* Scalar and D forms clear the unused upper bits. Q forms preserve all 128 result bits. */
    ir_set_vd(ir, rd, 0, LLVMBuildTrunc(B, full, ir->t_i64, ""));
    ir_set_vd(ir, rd, 1,
              q ? LLVMBuildTrunc(B, LLVMBuildLShr(B, full, LLVMConstInt(ir->t_i128, 64, 0), ""), ir->t_i64, "")
                : ir_i64(ir, 0));
}
static V shuffle(SnxIr* ir, V a, V b, const unsigned* indices, unsigned n) {
    V mask[16];
    unsigned i;
    for (i = 0; i < n; ++i) mask[i] = ir_i32(ir, indices[i]);
    return LLVMBuildShuffleVector(B, a, b, LLVMConstVector(mask, n), "");
}
/* count vectors of n lanes end to end, as one of count*n lanes (at most 64) */
static V concat(SnxIr* ir, V const* parts, unsigned count, unsigned n) {
    V mask[64], all = parts[0], part;
    unsigned have = n, k, i;
    for (k = 1; k < count; ++k) {
        part = parts[k];
        if (have > n) { /* a shuffle's operands share a type: the part widened to have lanes (the extra unused) */
            for (i = 0; i < have; ++i) mask[i] = ir_i32(ir, i < n ? i : 0);
            part = LLVMBuildShuffleVector(B, part, part, LLVMConstVector(mask, have), "");
        }
        for (i = 0; i < have + n; ++i) mask[i] = ir_i32(ir, i); /* all's lanes, then the part's first n */
        all = LLVMBuildShuffleVector(B, all, part, LLVMConstVector(mask, have + n), "");
        have += n;
    }
    return all;
}

/* UZP1/2, TRN1/2, ZIP1/2: simd_perm's element formulas, as one shuffle */
static int permute(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, size = (w >> 22) & 3, rm = (w >> 16) & 31, op = (w >> 12) & 7, rn = (w >> 5) & 31,
             rd = w & 31, n, h, part, i, indices[16];
    if ((size == 3 && !q) || !(op & 3)) return 0;
    n = (q ? 128u : 64u) >> (3 + size);
    h = n / 2;
    part = (op >> 2) & 1;
    for (i = 0; i < n; ++i) {
        unsigned k;
        if ((op & 3) == 1) /* UZP: k < n ? a[k] : b[k-n] */
            k = 2 * i + part;
        else if ((op & 3) == 2) /* TRN */
            k = (i & 1) ? n + i / 2 * 2 + part : i / 2 * 2 + part;
        else /* ZIP */
            k = (i & 1) ? n + part * h + i / 2 : part * h + i / 2;
        indices[i] = k;
    }
    write_lanes(ir, rd,
                shuffle(ir, get_lanes(ir, vreg(ir, rn), size, n), get_lanes(ir, vreg(ir, rm), size, n), indices, n), q);
    return 1;
}

/* EXT: bytes imm4.. of Vn:Vm */
static int extract(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, rm = (w >> 16) & 31, imm4 = (w >> 11) & 15, rn = (w >> 5) & 31, rd = w & 31,
             len = q ? 16 : 8, i, indices[16];
    if (!q && (imm4 & 8)) return 0;
    for (i = 0; i < len; ++i) {
        unsigned source = imm4 + i;
        indices[i] = source < len ? source : 16 + (source - len);
    }
    write_lanes(ir, rd, shuffle(ir, v128_type_load(ir, vreg(ir, rn)), v128_type_load(ir, vreg(ir, rm)), indices, len),
                q);
    return 1;
}
