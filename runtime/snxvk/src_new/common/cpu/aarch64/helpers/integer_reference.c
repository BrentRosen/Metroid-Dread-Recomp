/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported in part from QEMU (target/arm/tcg/helper-a64.c, translate-a64.c), LGPL-2.1-or-later:
   Copyright (c) 2013 Alexander Graf <agraf@suse.de> */
/* Integer helpers only the C reference interpreter needs: logical-immediate decoding and CRC32. The LLVM backend
   evaluates both at compile time. */

/* DecodeBitMasks(N, imms, immr, immediate = TRUE): the element size is the highest set bit of N:NOT(imms), the
   element imms+1 ones rotated right by immr, replicated over ds bits. All-ones elements are reserved. */
u64 snx_decode_bitmask(u32 N, u32 imms, u32 immr, u32 ds) {
    u32 key = ((N & 1) << 6) | (~imms & 63), esize, s, r, i;
    int len = -1;
    u64 element, result = 0;
    for (i = 0; i < 7; ++i)
        if (key & (1u << i)) len = (int)i;
    if (len < 1) return 0;
    esize = 1u << len;
    s = imms & (esize - 1);
    r = immr & (esize - 1);
    if (s == esize - 1) return 0;
    element = bmask(s + 1);
    if (r) element = ((element >> r) | (element << (esize - r))) & bmask(esize);
    for (i = 0; i < ds; i += esize) result |= element << i;
    return ds == 32 ? (u32)result : result;
}

/* CRC32 (polynomial 0x04C11DB7) and CRC32C (0x1EDC6F41), bit-reflected as the instructions define them, a nibble
   at a time: entry i is i run through four steps of the bit-serial division. Same results as eight conditional
   shifts per byte, which made the interpreter slow (#69). */
static const u32 crc32_nibbles[2][16] = {
    {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
     0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu},
    {0x00000000u, 0x105EC76Fu, 0x20BD8EDEu, 0x30E349B1u, 0x417B1DBCu, 0x5125DAD3u, 0x61C69362u, 0x7198540Du,
     0x82F63B78u, 0x92A8FC17u, 0xA24BB5A6u, 0xB21572C9u, 0xC38D26C4u, 0xD3D3E1ABu, 0xE330A81Au, 0xF36E6F75u}};

u32 snx_crc32(u32 acc, u64 val, int bytes, int castagnoli) {
    const u32* table = crc32_nibbles[castagnoli ? 1 : 0];
    int i;
    for (i = 0; i < bytes; ++i) {
        acc ^= (u32)(val >> (8 * i)) & 0xFF;
        acc = (acc >> 4) ^ table[acc & 15];
        acc = (acc >> 4) ^ table[acc & 15];
    }
    return acc;
}
