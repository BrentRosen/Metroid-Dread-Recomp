/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* ---- GFNI: GF2P8AFFINEQB as a per-byte bit matrix ----
   (wunkolo, "gf2p8affineqb: int8 shifting", 2020.) Byte 7-i of the 64-bit matrix builds result bit i, its bit j
   selecting source bit j: the identity is 0x0102040810204080. Shifting the identity right by 8n bytes is a logical
   shift left by n, left by 8n a logical shift right; an arithmetic shift right also ORs n bytes of 0x80 back in,
   replicating bit 7. x86 has no 8-bit shifts, so each of these replaces a shift-and-mask sequence. */
#define GF_IDENTITY 0x0102040810204080ull
static SnxU64 gf_shl(unsigned n) { return n >= 8 ? 0 : GF_IDENTITY >> (8 * n); }
static SnxU64 gf_lshr(unsigned n) { return n >= 8 ? 0 : GF_IDENTITY << (8 * n); }
static SnxU64 gf_ashr(unsigned n) {
    SnxU64 sign = 0x8080808080808080ull;
    if (n > 7) n = 7;
    return n ? (GF_IDENTITY << (8 * n)) | (~(~(SnxU64)0 << (8 * n)) & sign) : GF_IDENTITY;
}
/* result bit 0 = source bit k, the rest 0 */
static SnxU64 gf_bit(unsigned k) { return ((SnxU64)1 << k) << 56; }

/* the host's GF2P8AFFINEQB applies, from CPUID (snx_ir_host): an x86-64 target with GFNI */
static int host_gfni(SnxIr* ir) { return ir->features->x86 && ir->features->gfni; }

/* GF2P8AFFINEQB x, matrix, 0 on 16 bytes */
static V gf_affine(SnxIr* ir, V x, SnxU64 matrix) {
    V bytes[16], args[3];
    unsigned i;
    for (i = 0; i < 16; ++i) bytes[i] = ir_i8(ir, (unsigned)(matrix >> (8 * (i & 7))));
    args[0] = x;
    args[1] = LLVMConstVector(bytes, 16);
    args[2] = ir_i8(ir, 0);
    return ir_intrinsic(ir, "llvm.x86.vgf2p8affineqb.128", NULL, args, 3);
}

/* the low n lanes of a <16 x i8> */
static V low_lanes(SnxIr* ir, V x, unsigned n) {
    unsigned indices[16], i;
    if (n == 16) return x;
    for (i = 0; i < n; ++i) indices[i] = i;
    return shuffle(ir, x, x, indices, n);
}

/* CNT, NOT, RBIT (byte lanes): llvm.ctpop and llvm.bitreverse become VPOPCNTB and GF2P8AFFINEQB on x86 hosts with
   AVX512_BITALG and GFNI, a shuffle-table sequence otherwise, CNT and RBIT on AArch64 */
static int byte_misc(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, u = (w >> 29) & 1, size = (w >> 22) & 3, op = (w >> 12) & 31, rn = (w >> 5) & 31,
             rd = w & 31, n = q ? 16 : 8;
    V a, r;
    LLVMTypeRef t;
    if (op != 5 || !(u ? size < 2 : size == 0)) return 0;
    t = LLVMVectorType(ir->t_i8, n);
    a = get_lanes(ir, vreg(ir, rn), 0, n);
    if (!u)
        r = ir_intrinsic(ir, "llvm.ctpop", t, &a, 1);
    else if (!size)
        r = LLVMBuildNot(B, a, "");
    else if (host_gfni(ir)) /* wunkolo, "gf2p8affineqb: Bit reversal": the matrix 0x8040201008040201 */
        r = low_lanes(ir, gf_affine(ir, v128_type_load(ir, vreg(ir, rn)), 0x8040201008040201ull),
                      n);
    else
        r = ir_intrinsic(ir, "llvm.bitreverse", t, &a, 1);
    write_lanes(ir, rd, r, q);
    return 1;
}

/* v_expand_imm (helpers/simd.c), evaluated here */
static SnxU64 expand_imm(unsigned op, unsigned cmode, unsigned imm8) {
    SnxU64 i = imm8, r = 0;
    unsigned k;
    switch (cmode >> 1) {
        case 0: r = i | (i << 32); break;
        case 1: r = (i << 8) | (i << 40); break;
        case 2: r = (i << 16) | (i << 48); break;
        case 3: r = (i << 24) | (i << 56); break;
        case 4:
            r = i | (i << 16);
            r |= r << 32;
            break;
        case 5:
            r = (i << 8) | (i << 24);
            r |= r << 32;
            break;
        case 6:
            r = !(cmode & 1) ? (i << 8) | 0xFF : (i << 16) | 0xFFFF;
            r |= r << 32;
            break;
        default:
            if (!(cmode & 1) && !op) {
                for (k = 0; k < 8; k++) r |= i << (k * 8);
            } else if (!(cmode & 1) && op) {
                for (k = 0; k < 8; k++)
                    if ((i >> k) & 1) r |= (SnxU64)0xFF << (k * 8);
            } else if ((cmode & 1) && !op) {
                SnxU32 b =
                    (SnxU32)((i & 0x80) << 24) | ((i & 0x40) ? 0x3E000000u : 0x40000000u) | ((SnxU32)(i & 0x3F) << 19);
                r = b | ((SnxU64)b << 32);
            } else {
                r = ((i & 0x80) << 56) | ((i & 0x40) ? 0x3FC0000000000000ull : 0x4000000000000000ull) |
                    ((i & 0x3F) << 48);
            }
            break;
    }
    return r;
}

/* MOVI, MVNI, ORR, BIC (vector, immediate): simd_modimm with the immediate a constant */
static int modified_immediate(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, op = (w >> 29) & 1, cmode = (w >> 12) & 15, rd = w & 31, o2 = (w >> 11) & 1;
    unsigned imm8 = (((w >> 16) & 7) << 5) | ((w >> 5) & 31);
    SnxU64 imm;
    if (o2 || (cmode == 15 && op && !q)) return 0;
    imm = expand_imm(op, cmode, imm8);
    if (cmode < 12 && (cmode & 1)) { /* ORR, BIC */
        V k = ir_i64(ir, op ? ~imm : imm);
        if (!op) {
            ir_set_vd(ir, rd, 0, LLVMBuildOr(B, ir_vd(ir, rd, 0), k, ""));
            ir_set_vd(ir, rd, 1, q ? LLVMBuildOr(B, ir_vd(ir, rd, 1), k, "") : ir_i64(ir, 0));
        } else {
            ir_set_vd(ir, rd, 0, LLVMBuildAnd(B, ir_vd(ir, rd, 0), k, ""));
            ir_set_vd(ir, rd, 1, q ? LLVMBuildAnd(B, ir_vd(ir, rd, 1), k, "") : ir_i64(ir, 0));
        }
        return 1;
    }
    if (op && cmode != 14 && cmode != 15) imm = ~imm; /* MVNI */
    ir_set_vd(ir, rd, 0, ir_i64(ir, imm));
    ir_set_vd(ir, rd, 1, ir_i64(ir, q ? imm : 0));
    return 1;
}

/* the module's copy of a table, made once: a private constant */
static V aes_table(SnxIr* ir, const unsigned char* bytes, const char* name) {
    V table = LLVMGetNamedGlobal(ir->mod, name), values[256];
    unsigned i;
    if (table) return table;
    for (i = 0; i < 256; ++i) values[i] = ir_i8(ir, bytes[i]);
    table = LLVMAddGlobal(ir->mod, LLVMArrayType(ir->t_i8, 256), name);
    LLVMSetInitializer(table, LLVMConstArray(ir->t_i8, values, 256));
    LLVMSetGlobalConstant(table, 1);
    LLVMSetLinkage(table, LLVMPrivateLinkage);
    LLVMSetUnnamedAddress(table, LLVMGlobalUnnamedAddr);
    return table;
}
/* a <16 x i8> of b */
static V aes_splat(SnxIr* ir, unsigned b) {
    V v[16];
    unsigned i;
    for (i = 0; i < 16; ++i) v[i] = ir_i8(ir, b);
    return LLVMConstVector(v, 16);
}
/* xtime on each byte: x*2 in GF(2^8), x << 1 ^ (x's top bit ? 0x1b : 0) */
static V aes_xtime(SnxIr* ir, V x) {
    V top = LLVMBuildICmp(B, LLVMIntSLT, x, aes_splat(ir, 0), "");
    return LLVMBuildXor(B, LLVMBuildShl(B, x, aes_splat(ir, 1), ""),
                        LLVMBuildSelect(B, top, aes_splat(ir, 0x1b), aes_splat(ir, 0), ""), "");
}
/* each column's bytes rotated by n: byte col*4+row takes byte col*4+(row+n)%4 */
static V aes_rotate(SnxIr* ir, V x, unsigned n) {
    unsigned indices[16], i;
    for (i = 0; i < 16; ++i) indices[i] = (i & ~3u) | ((i + n) & 3);
    return shuffle(ir, x, x, indices, 16);
}
/* crypto.c's v_aes and v_aesmc on the bytes: AESE/AESD (op 4, 5) Vd = ShiftRows(SubBytes(Vd ^ Vn)) and the inverse,
   AESMC/AESIMC (6, 7) Vd = MixColumns(Vn) and the inverse */
static int aes_portable(SnxIr* ir, unsigned op, unsigned rn, unsigned rd) {
    LLVMTypeRef t8 = LLVMVectorType(ir->t_i8, 16), t_table = LLVMArrayType(ir->t_i8, 256);
    V d = ir_load(ir, t8, vreg(ir, rd)), x = ir_load(ir, t8, vreg(ir, rn)), r;
    unsigned i;
    LLVMSetAlignment(d, 8);
    LLVMSetAlignment(x, 8);
    if (op < 6) {
        V table = op == 4 ? aes_table(ir, AES_SBOX, "snx_aes_sbox") : aes_table(ir, AES_ISBOX, "snx_aes_isbox");
        V state = LLVMBuildXor(B, d, x, "");
        r = LLVMGetUndef(t8);
        for (i = 0; i < 16; ++i) {
            /* byte (col, row) of the result: AESE's ShiftRows takes column (col + row) % 4, AESD's (col - row) % 4 */
            unsigned col = i / 4, row = i % 4, from = (op == 4 ? (col + row) % 4 : (col + 4 - row) % 4) * 4 + row;
            V index[2], byte = LLVMBuildExtractElement(B, state, ir_i32(ir, from), "");
            index[0] = ir_i64(ir, 0);
            index[1] = LLVMBuildZExt(B, byte, ir->t_i64, "");
            byte = LLVMBuildLoad2(B, ir->t_i8, LLVMBuildInBoundsGEP2(B, t_table, table, index, 2, ""), "");
            r = LLVMBuildInsertElement(B, r, byte, ir_i32(ir, i), "");
        }
    } else {
        V x2 = aes_xtime(ir, x);
        if (op == 6) /* 2a ^ rot1(3a) ^ rot2(a) ^ rot3(a) */
            r = LLVMBuildXor(B, LLVMBuildXor(B, x2, aes_rotate(ir, LLVMBuildXor(B, x2, x, ""), 1), ""),
                             LLVMBuildXor(B, aes_rotate(ir, x, 2), aes_rotate(ir, x, 3), ""), "");
        else { /* 14a ^ rot1(11a) ^ rot2(13a) ^ rot3(9a) */
            V x4 = aes_xtime(ir, x2), x8 = aes_xtime(ir, x4), x9 = LLVMBuildXor(B, x8, x, "");
            V x11 = LLVMBuildXor(B, x9, x2, ""), x13 = LLVMBuildXor(B, x9, x4, "");
            V x14 = LLVMBuildXor(B, LLVMBuildXor(B, x8, x4, ""), x2, "");
            r = LLVMBuildXor(B, LLVMBuildXor(B, x14, aes_rotate(ir, x11, 1), ""),
                             LLVMBuildXor(B, aes_rotate(ir, x13, 2), aes_rotate(ir, x9, 3), ""), "");
        }
    }
    LLVMSetAlignment(LLVMBuildStore(B, r, vreg(ir, rd)), 8);
    return 1;
}

/* AESE/AESD/AESMC/AESIMC. With AES-NI (x86, CPUID aes): Arm's AESE is ShiftRows(SubBytes(d^k)), x86's AESENCLAST
   ShiftRows(SubBytes(s))^rk: AESENCLAST(d^k, 0). MixColumns alone is AESENC(AESDECLAST(x, 0), 0), the inverse
   steps cancelling. Elsewhere crypto.c's: SubBytes and ShiftRows as one lookup per byte in the module's S-box,
   MixColumns as xtime on the vector of bytes and rotations of each column */
static int aes(SnxIr* ir, SnxU32 w) {
    unsigned op = (w >> 12) & 31, rn = (w >> 5) & 31, rd = w & 31;
    LLVMTypeRef t = LLVMVectorType(ir->t_i64, 2);
    V args[2], zero = LLVMConstNull(t), x, r;
    if (op < 4 || op > 7) return 0;
    if (!ir->features->x86 || !ir->features->aes) return aes_portable(ir, op, rn, rd);
    x = ir_load(ir, t, vreg(ir, rn));
    LLVMSetAlignment(x, 8);
    if (op < 6) {
        V d = ir_load(ir, t, vreg(ir, rd));
        LLVMSetAlignment(d, 8);
        args[0] = LLVMBuildXor(B, d, x, "");
        args[1] = zero;
        r = ir_intrinsic(ir, op == 4 ? "llvm.x86.aesni.aesenclast" : "llvm.x86.aesni.aesdeclast", NULL, args, 2);
    } else if (op == 6) {
        args[0] = x;
        args[1] = zero;
        args[0] = ir_intrinsic(ir, "llvm.x86.aesni.aesdeclast", NULL, args, 2);
        r = ir_intrinsic(ir, "llvm.x86.aesni.aesenc", NULL, args, 2);
    } else
        r = ir_intrinsic(ir, "llvm.x86.aesni.aesimc", NULL, &x, 1);
    LLVMSetAlignment(LLVMBuildStore(B, r, vreg(ir, rd)), 8);
    return 1;
}

/* the 128-bit carry-less product of x and y (i64) from integer multiplies, for hosts without PCLMULQDQ (the
   masked-multiply technique of BearSSL's ghash_ctmul, Thomas Pornin): with both operands masked to every fourth bit,
   each bit of the integer product counts the pairs of set bits whose positions sum to it, and the count's parity is
   the carry-less bit. Taking x 32 bits at a time keeps every count at 8 or below (8 bits of a half in each residue
   class), so it never carries into the next position of its class: XORing the products of the class pairs and
   masking to the class gives that class of the product. 32 multiplies of 64 by 64 bits to 128 */
static V carryless_multiply(SnxIr* ir, V x, V y) {
    static const SnxU64 m = 0x1111111111111111ull;
    V yb[4], xa[4], r = NULL, half, part, z, k;
    unsigned h, a, c;
    for (a = 0; a < 4; ++a) yb[a] = LLVMBuildZExt(B, LLVMBuildAnd(B, y, ir_i64(ir, m << a), ""), ir->t_i128, "");
    for (h = 0; h < 2; ++h) {
        half = h ? LLVMBuildLShr(B, x, ir_i64(ir, 32), "") : LLVMBuildAnd(B, x, ir_i64(ir, 0xFFFFFFFFUL), "");
        for (a = 0; a < 4; ++a) xa[a] = LLVMBuildZExt(B, LLVMBuildAnd(B, half, ir_i64(ir, m << a), ""), ir->t_i128, "");
        part = NULL;
        for (c = 0; c < 4; ++c) {
            z = LLVMBuildMul(B, xa[0], yb[c], "");
            for (a = 1; a < 4; ++a) z = LLVMBuildXor(B, z, LLVMBuildMul(B, xa[a], yb[(c - a) & 3], ""), "");
            k = LLVMConstInt(ir->t_i128, m << c, 0);
            k = LLVMBuildOr(B, k, LLVMBuildShl(B, k, LLVMConstInt(ir->t_i128, 64, 0), ""), "");
            z = LLVMBuildAnd(B, z, k, "");
            part = part ? LLVMBuildOr(B, part, z, "") : z;
        }
        r = h ? LLVMBuildXor(B, r, LLVMBuildShl(B, part, LLVMConstInt(ir->t_i128, 32, 0), ""), "") : part;
    }
    return r;
}

/* PMULL/PMULL2 Vd.1Q (64x64 carry-less): PCLMULQDQ where CPUID has it (x86), carryless_multiply otherwise */
static int pmull_1q(SnxIr* ir, SnxU32 w) {
    unsigned q = (w >> 30) & 1, rm = (w >> 16) & 31, rn = (w >> 5) & 31, rd = w & 31;
    LLVMTypeRef t = LLVMVectorType(ir->t_i64, 2);
    V args[3], r;
    if (!ir->features->x86 || !ir->features->pclmul) {
        r = carryless_multiply(ir, ir_vd(ir, rn, q), ir_vd(ir, rm, q));
        ir_set_vd(ir, rd, 0, LLVMBuildTrunc(B, r, ir->t_i64, ""));
        ir_set_vd(ir, rd, 1,
                  LLVMBuildTrunc(B, LLVMBuildLShr(B, r, LLVMConstInt(ir->t_i128, 64, 0), ""), ir->t_i64, ""));
        return 1;
    }
    args[0] = ir_load(ir, t, vreg(ir, rn));
    args[1] = ir_load(ir, t, vreg(ir, rm));
    LLVMSetAlignment(args[0], 8);
    LLVMSetAlignment(args[1], 8);
    args[2] = ir_i8(ir, q ? 0x11 : 0x00);
    r = ir_intrinsic(ir, "llvm.x86.pclmulqdq", NULL, args, 3);
    LLVMSetAlignment(LLVMBuildStore(B, r, vreg(ir, rd)), 8);
    return 1;
}

/* ---- SHA1 and SHA256: crypto.c's v_sha1 and v_sha256 (the Arm ARM's SHA1C/P/M, SHA1SU0/SU1, SHA1H, SHA256H/H2,
   SHA256SU0/SU1) on 32-bit words; every operand is read before Vd is written ---- */

static V rol32(SnxIr* ir, V x, unsigned n) {
    return LLVMBuildOr(B, LLVMBuildShl(B, x, ir_i32(ir, n), ""), LLVMBuildLShr(B, x, ir_i32(ir, 32 - n), ""), "");
}
/* SHA256's sigmas: ror(x, a) ^ ror(x, b) ^ (c ? ror(x, c) : x >> d) */
static V sigma(SnxIr* ir, V x, unsigned a, unsigned b, unsigned c, unsigned d) {
    V r = LLVMBuildXor(B, rol32(ir, x, 32 - a), rol32(ir, x, 32 - b), "");
    return LLVMBuildXor(B, r, c ? rol32(ir, x, 32 - c) : LLVMBuildLShr(B, x, ir_i32(ir, d), ""), "");
}
static V add3(SnxIr* ir, V a, V b, V c) { return LLVMBuildAdd(B, LLVMBuildAdd(B, a, b, ""), c, ""); }

/* snx_simd's SHA ops: three registers (0-6, (w >> 12) & 7) and two (0x10-0x12) */
static int sha(SnxIr* ir, SnxU32 w, unsigned op) {
    unsigned rm = (w >> 16) & 31, rn = (w >> 5) & 31, rd = w & 31, i;
    V x[4], y[4], m[4], r[4], t;
    for (i = 0; i < 4; ++i) {
        x[i] = get(ir, vreg(ir, rd), 2, i);
        y[i] = get(ir, vreg(ir, rn), 2, i);
        m[i] = op < 0x10 ? get(ir, vreg(ir, rm), 2, i) : NULL;
    }
    switch (op) {
        case 0: /* SHA1C, SHA1P, SHA1M: four rounds on the hash in Vd, E in Sn, the words in Vm */
        case 1:
        case 2: {
            V e = y[0], f, next;
            for (i = 0; i < 4; ++i) {
                if (op == 0) /* Choose */
                    f = LLVMBuildOr(B, LLVMBuildAnd(B, x[1], x[2], ""),
                                    LLVMBuildAnd(B, LLVMBuildNot(B, x[1], ""), x[3], ""), "");
                else if (op == 1) /* Parity */
                    f = LLVMBuildXor(B, LLVMBuildXor(B, x[1], x[2], ""), x[3], "");
                else /* Majority */
                    f = LLVMBuildOr(
                        B, LLVMBuildOr(B, LLVMBuildAnd(B, x[1], x[2], ""), LLVMBuildAnd(B, x[1], x[3], ""), ""),
                        LLVMBuildAnd(B, x[2], x[3], ""), "");
                e = LLVMBuildAdd(B, add3(ir, e, rol32(ir, x[0], 5), f), m[i], "");
                next = x[3];
                x[3] = x[2];
                x[2] = rol32(ir, x[1], 30);
                x[1] = x[0];
                x[0] = e;
                e = next;
            }
            memcpy(r, x, sizeof(r));
            break;
        }
        case 3: /* SHA1SU0: Vn's low half above Vd's upper half, ^ Vd ^ Vm */
            for (i = 0; i < 4; ++i)
                r[i] = LLVMBuildXor(B, LLVMBuildXor(B, i < 2 ? x[i + 2] : y[i - 2], x[i], ""), m[i], "");
            break;
        case 4: /* SHA256H (the hash's first half in Vd, the second in Vn), SHA256H2 (the other way round) */
        case 5: {
            V *a = op == 4 ? x : y, *b = op == 4 ? y : x, ch, maj, a3;
            for (i = 0; i < 4; ++i) {
                ch = LLVMBuildXor(B, LLVMBuildAnd(B, b[0], b[1], ""),
                                  LLVMBuildAnd(B, LLVMBuildNot(B, b[0], ""), b[2], ""), "");
                maj = LLVMBuildXor(
                    B, LLVMBuildXor(B, LLVMBuildAnd(B, a[0], a[1], ""), LLVMBuildAnd(B, a[0], a[2], ""), ""),
                    LLVMBuildAnd(B, a[1], a[2], ""), "");
                t = LLVMBuildAdd(B, add3(ir, b[3], sigma(ir, b[0], 6, 11, 25, 0), ch), m[i], "");
                a3 = LLVMBuildAdd(B, a[3], t, "");
                t = add3(ir, t, sigma(ir, a[0], 2, 13, 22, 0), maj);
                /* the eight words rotate up one: a = (t, a0, a1, a2), b = (a3 + t1, b0, b1, b2) */
                b[3] = b[2];
                b[2] = b[1];
                b[1] = b[0];
                b[0] = a3;
                a[3] = a[2];
                a[2] = a[1];
                a[1] = a[0];
                a[0] = t;
            }
            memcpy(r, op == 4 ? a : b, sizeof(r));
            break;
        }
        case 6: /* SHA256SU1: the next schedule words; the upper two use the lower two just computed */
            for (i = 0; i < 4; ++i)
                r[i] = add3(ir, x[i], sigma(ir, i < 2 ? m[i + 2] : r[i - 2], 17, 19, 0, 10), i < 3 ? y[i + 1] : m[0]);
            break;
        case 0x10: /* SHA1H: ROL(Sn, 30) alone */ write_lanes(ir, rd, rol32(ir, y[0], 30), 0); return 1;
        case 0x11: /* SHA1SU1: (Vd ^ Vn >> 32) rotated by one, the top word also ^ the first rotated by two */
            for (i = 0; i < 4; ++i) r[i] = i < 3 ? LLVMBuildXor(B, x[i], y[i + 1], "") : x[3];
            t = rol32(ir, r[0], 2);
            for (i = 0; i < 4; ++i) r[i] = rol32(ir, r[i], 1);
            r[3] = LLVMBuildXor(B, r[3], t, "");
            break;
        case 0x12: /* SHA256SU0 */
            for (i = 0; i < 4; ++i) r[i] = LLVMBuildAdd(B, x[i], sigma(ir, i < 3 ? x[i + 1] : y[0], 7, 18, 0, 3), "");
            break;
        default: return 0;
    }
    for (i = 0; i < 4; ++i) set(ir, vreg(ir, rd), 2, i, r[i]);
    return 1;
}

/* ---- vpternlog: any function of three bit vectors in one instruction (AVX512F+VL, from CPUID) ----
   (wunkolo, "vpternlog: Signed Saturation", 2025.) The 8-bit table is the function itself applied to the constants
   A=0xF0, B=0xCC, C=0xAA: each bit position of those is one combination of the three inputs */
enum { TERN_A = 0xF0, TERN_B = 0xCC, TERN_C = 0xAA };
static int host_ternlog(SnxIr* ir) { return ir->features->x86 && ir->features->avx512vl; }
/* VPTERNLOG{D,Q} on 128 bits: lanes of 32 or 64 bits (only the masking granularity differs) */
static V ternlog(SnxIr* ir, V a, V b, V c, unsigned table, unsigned bits) {
    V args[4];
    LLVMTypeRef t = LLVMVectorType(bits == 64 ? ir->t_i64 : ir->t_i32, bits == 64 ? 2 : 4);
    args[0] = LLVMBuildBitCast(B, a, t, "");
    args[1] = LLVMBuildBitCast(B, b, t, "");
    args[2] = LLVMBuildBitCast(B, c, t, "");
    args[3] = ir_i32(ir, table & 0xFF);
    return ir_intrinsic(ir, bits == 64 ? "llvm.x86.avx512.pternlog.q.128" : "llvm.x86.avx512.pternlog.d.128", NULL,
                        args, 4);
}

/* FPSR.QC (bit 27) |= any lane of an i1 vector (or an i1, for the scalar forms) */
static void set_qc(SnxIr* ir, V lanes) {
    V any = LLVMGetTypeKind(LLVMTypeOf(lanes)) == LLVMVectorTypeKind
                ? ir_intrinsic(ir, "llvm.vector.reduce.or", LLVMTypeOf(lanes), &lanes, 1)
                : lanes;
    size_t off = ir_off_fpcr() + 4;
    V fpsr = ir_cpu_load(ir, ir->t_i32, off);
    ir_cpu_store(ir, LLVMBuildOr(B, fpsr, LLVMBuildShl(B, ir_bool32(ir, any), ir_i32(ir, 27), ""), ""), off);
}

/* <n x iN> all-ones or zero per lane, as the compares write them */
static V mask_of(SnxIr* ir, V condition, LLVMTypeRef lanes) { return LLVMBuildSExt(B, condition, lanes, ""); }
