/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported from QEMU (target/arm/tcg/translate-a64.c), LGPL-2.1-or-later:
   Copyright (c) 2013 Alexander Graf <agraf@suse.de> */
/* AdvSIMD loads and stores for the C reference interpreter (SNX_A64_REFERENCE): LD1-LD4/ST1-ST4 of multiple
   structures, of a single structure, and LD1R-LD4R, with no offset or post-indexed. Lanes are little-endian in guest
   memory as in the registers. This file also has the reference dispatcher, snx_simd, below simd_ldst. */

static u64 mem_get(u8* M, u64 a, u32 size) {
    switch (size) {
        case 0: return RD8(a);
        case 1: return RD16(a);
        case 2: return RD32(a);
        default: return RD64(a);
    }
}
static void mem_put(u8* M, u64 a, u32 size, u64 v) {
    switch (size) {
        case 0: WR8(a, v); break;
        case 1: WR16(a, v); break;
        case 2: WR32(a, v); break;
        default: WR64(a, v); break;
    }
}

/* the post-index form's writeback: the register's value, or the bytes transferred for Rm = 31 */
static void ldst_writeback(CPU* c, u32 w, u32 rn, u64 bytes) {
    u32 rm = (w >> 16) & 31;
    if (!((w >> 23) & 1)) return;
    c->x[rn] += rm == 31 ? bytes : c->x[rm];
}

static int simd_ldst(CPU* c, u8* M, u32 w) {
    u32 q = (w >> 30) & 1, load = (w >> 22) & 1, rn = (w >> 5) & 31, rt = w & 31;
    u64 base = c->x[rn], offset = 0; /* Rn = 31 is SP */
    u32 i, s, r;
    /* bits 20:16 of the forms without offset, and bit 21 of the multiple-structure ones, are not decoded: the
       emitters take those words as the encodings with them zero, and an unallocated word must be one for both */
    if (!((w >> 24) & 1)) { /* multiple structures */
        u32 size = (w >> 10) & 3, opcode = (w >> 12) & 15, rpt, selem, elements = (q ? 16u : 8u) >> size;
        switch (opcode) {
            case 0x0: rpt = 1, selem = 4; break;
            case 0x2: rpt = 4, selem = 1; break;
            case 0x4: rpt = 1, selem = 3; break;
            case 0x6: rpt = 3, selem = 1; break;
            case 0x7: rpt = 1, selem = 1; break;
            case 0x8: rpt = 1, selem = 2; break;
            case 0xA: rpt = 2, selem = 1; break;
            default: return 0;
        }
        if (size == 3 && !q && selem != 1) return 0;
        if (load && !q)
            for (r = 0; r < rpt * selem; ++r) c->v[(rt + r) & 31].d[1] = 0;
        for (r = 0; r < rpt; ++r)
            for (i = 0; i < elements; ++i)
                for (s = 0; s < selem; ++s) {
                    V128* v = &c->v[(rt + r + s) & 31];
                    if (load)
                        eset(v, size, i, mem_get(M, base + offset, size));
                    else
                        mem_put(M, base + offset, size, eget(v, size, i));
                    offset += (u64)1 << size;
                }
        ldst_writeback(c, w, rn, offset);
        return 1;
    } else { /* single structure */
        u32 R = (w >> 21) & 1, opcode = (w >> 13) & 7, S = (w >> 12) & 1, size = (w >> 10) & 3;
        u32 selem = (((opcode & 1) << 1) | R) + 1, scale = opcode >> 1, index = 0;
        switch (scale) {
            case 3: /* LD1R-LD4R */
                if (!load || S) return 0;
                scale = size;
                for (s = 0; s < selem; ++s) {
                    V128 v;
                    u64 x = mem_get(M, base + offset, scale);
                    vzero(&v);
                    for (i = 0; i < ((q ? 16u : 8u) >> scale); ++i) eset(&v, scale, i, x);
                    c->v[(rt + s) & 31] = v;
                    offset += (u64)1 << scale;
                }
                ldst_writeback(c, w, rn, offset);
                return 1;
            case 0: index = (q << 3) | (S << 2) | size; break;
            case 1:
                if (size & 1) return 0;
                index = (q << 2) | (S << 1) | (size >> 1);
                break;
            default:
                if (size & 2) return 0;
                if (size & 1) {
                    if (S) return 0;
                    scale = 3;
                    index = q;
                } else {
                    index = (q << 1) | S;
                }
                break;
        }
        for (s = 0; s < selem; ++s) {
            V128* v = &c->v[(rt + s) & 31];
            if (load)
                eset(v, scale, index, mem_get(M, base + offset, scale));
            else
                mem_put(M, base + offset, scale, eget(v, scale, index));
            offset += (u64)1 << scale;
        }
        ldst_writeback(c, w, rn, offset);
        return 1;
    }
}
/* AdvSIMD, crypto and SIMD load/store words, by the encoding groups of the Arm ARM's top-level A64 decode */
int snx_simd(CPU* c, u8* M, u32 w) {
    if ((w & 0xBF800000UL) == 0x0C000000UL || (w & 0xBF800000UL) == 0x0C800000UL ||
        (w & 0xBF000000UL) == 0x0D000000UL)
        return simd_ldst(c, M, w);
    if ((w & 0xFFFE0C00UL) == 0x4E280800UL) { /* AESE AESD AESMC AESIMC */
        u32 op = (w >> 12) & 31, rd = w & 31, rn = (w >> 5) & 31;
        V128 s = c->v[rn];
        if (op < 4 || op > 7) return 0;
        if (op < 6)
            v_aes(&c->v[rd], &s, op == 5);
        else
            v_aesmc(&c->v[rd], &s, op == 7);
        return 1;
    }
    if ((w & 0xFFE08C00UL) == 0x5E000000UL) { /* SHA1C/P/M, SHA1SU0, SHA256H/H2, SHA256SU1 */
        u32 op = (w >> 12) & 7, rd = w & 31, rn = (w >> 5) & 31, rm = (w >> 16) & 31;
        if (op == 7) return 0;
        if (op < 4)
            v_sha1(c, op, rd, rn, rm);
        else
            v_sha256(c, op, rd, rn, rm);
        return 1;
    }
    if ((w & 0xFFFE0C00UL) == 0x5E280800UL) { /* SHA1H, SHA1SU1, SHA256SU0 */
        u32 op = (w >> 12) & 31, rd = w & 31, rn = (w >> 5) & 31;
        if (op > 2) return 0;
        if (op < 2)
            v_sha1(c, 0x10 + op, rd, rn, rn);
        else
            v_sha256(c, 0x12, rd, rn, rn);
        return 1;
    }
    if ((w & 0x9F200400UL) == 0x0E200400UL || (w & 0xDF200400UL) == 0x5E200400UL) return simd_3same(c, w);
    if ((w & 0x9F200C00UL) == 0x0E200000UL || (w & 0xDF200C00UL) == 0x5E200000UL) return simd_3diff(c, w);
    if ((w & 0x9F3E0C00UL) == 0x0E200800UL || (w & 0xDF3E0C00UL) == 0x5E200800UL) return simd_2misc(c, w);
    if ((w & 0x9F3E0C00UL) == 0x0E300800UL) return simd_across(c, w);
    if ((w & 0xDF3E0C00UL) == 0x5E300800UL) return simd_scalar_pairwise(c, w);
    if ((w & 0x9FE08400UL) == 0x0E000400UL || (w & 0xDFE08400UL) == 0x5E000400UL) return simd_copy(c, w);
    if ((w & 0x9FF80400UL) == 0x0F000400UL) return simd_modimm(c, w);
    if ((w & 0x9F800400UL) == 0x0F000400UL || (w & 0xDF800400UL) == 0x5F000400UL) return simd_shimm(c, w);
    if ((w & 0x9F000400UL) == 0x0F000000UL || (w & 0xDF000400UL) == 0x5F000000UL) return simd_elem(c, w);
    if ((w & 0xBF208C00UL) == 0x0E000800UL) return simd_perm(c, w);
    if ((w & 0xBF208400UL) == 0x2E000000UL) return simd_ext(c, w);
    if ((w & 0xBF208C00UL) == 0x0E000000UL) { /* TBL, TBX */
        if ((w >> 22) & 3) return 0;
        v_tbl(c, w & 31, (w >> 5) & 31, (w >> 13) & 3, (w >> 16) & 31, (int)((w >> 30) & 1), (int)((w >> 12) & 1));
        return 1;
    }
    return 0;
}
