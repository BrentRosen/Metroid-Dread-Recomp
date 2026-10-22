/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "common/cpu/recompiler/variants.h"

int snx_variant_hash(SnxVariantWordFn word, void* user, const SnxVariantRun* runs, size_t count, SnxU64* h1,
                     SnxU64* h2) {
    SnxU64 a = 0xCBF29CE484222325ULL, b = 0x84222325CBF29CE4ULL, address, v;
    SnxU32 k, n, w;
    size_t i;
    if (!h1 || !h2) return -1;
    *h1 = 0;
    *h2 = 0;
    if (!word || (!runs && count)) return -1;
    for (i = 0; i < count; ++i) {
        address = runs[i].address;
        n = runs[i].words;
        for (k = 0; k < n; ++k, address += 4) {
            if (word(user, address, &w)) return -1;
            v = (address << 32) ^ (SnxU64)w;
            a = (a ^ v) * (SnxU64)0x100000001B3ULL;
            b = (b ^ (v * (SnxU64)0x9E3779B97F4A7C15ULL)) * (SnxU64)0xC2B2AE3D27D4EB4FULL;
        }
    }
    *h1 = a;
    *h2 = b;
    return 0;
}
