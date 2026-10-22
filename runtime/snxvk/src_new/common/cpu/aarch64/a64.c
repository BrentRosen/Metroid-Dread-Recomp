/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Ported in part from QEMU (target/arm/tcg/translate-a64.c, helper-a64.c, vec_helper.c, vfp_helper.c,
   crypto_helper.c), LGPL-2.1-or-later:
   Copyright (c) 2013 Alexander Graf <agraf@suse.de>
   Copyright (c) 2018 Linaro
   Copyright (C) 2013 - 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
   Copyright (c) 2003 Fabrice Bellard */
#include "common/cpu/aarch64/a64.h"
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "common/cpu/aarch64/a64_templates.inc"
#include "common/cpu/aarch64/a64_validation.inc"

static int valid_word(unsigned validation, SnxU32 w) {
    switch (validation) {
#define CHECK(id, expr)                                                                                                \
    case id: return !!(expr);
        VALIDATIONS(CHECK)
#undef CHECK
        default: return 1;
    }
}

/* Each high-11-bit bucket stores a contiguous span of candidate patterns.
   The table is immutable once published; there is no writable decode state and
   no race between the first compiler workers. It is ~9 KiB for today's ISA. */
enum { BUCKET_COUNT = 2048, PATTERN_COUNT = sizeof(patterns) / sizeof(patterns[0]) };
_Static_assert(PATTERN_COUNT <= UINT8_MAX, "decoder indices need a wider type");

typedef struct {
    uint32_t offsets[BUCKET_COUNT + 1];
    uint8_t indices[];
} SnxA64Buckets;

static _Atomic(SnxA64Buckets*) shared_buckets;
static const SnxA64Case undefined_case = {SNX_FLOW_UNDEF, "", "", ""};

static const SnxA64Buckets* decode_buckets(void) {
    SnxA64Buckets* buckets = atomic_load_explicit(&shared_buckets, memory_order_acquire);
    if (buckets) return buckets;

    uint32_t offsets[BUCKET_COUNT + 1] = {0};
    for (unsigned b = 0; b < BUCKET_COUNT; ++b) {
        SnxU32 top = (SnxU32)b << 21;
        for (unsigned i = 0; i < PATTERN_COUNT; ++i) {
            SnxU32 mask = patterns[i].mask & 0xFFE00000u;
            offsets[b + 1] += (top & mask) == (patterns[i].value & mask);
        }
        offsets[b + 1] += offsets[b];
    }
    size_t count = offsets[BUCKET_COUNT];
    if (count > SIZE_MAX - sizeof(*buckets)) return NULL;
    SnxA64Buckets* fresh = malloc(sizeof(*buckets) + count);
    if (!fresh) return NULL;
    memcpy(fresh->offsets, offsets, sizeof(offsets));
    for (unsigned b = 0; b < BUCKET_COUNT; ++b) {
        SnxU32 top = (SnxU32)b << 21;
        uint32_t index = offsets[b];
        for (unsigned i = 0; i < PATTERN_COUNT; ++i) {
            SnxU32 mask = patterns[i].mask & 0xFFE00000u;
            if ((top & mask) == (patterns[i].value & mask)) fresh->indices[index++] = (uint8_t)i;
        }
    }
    SnxA64Buckets* empty = NULL;
    if (!atomic_compare_exchange_strong_explicit(&shared_buckets, &empty, fresh, memory_order_release,
                                                 memory_order_acquire)) {
        free(fresh);
        return empty;
    }
    return fresh;
}

static const SnxA64Case* decode_pattern(SnxU32 word, const SnxA64Pattern* pattern,
                                        const SnxA64Pattern** owner, size_t* specialization) {
    if (!valid_word(pattern->validation, word)) return &undefined_case;
    size_t index = 0;
    for (unsigned n = 0; n < pattern->field_count; ++n) {
        SnxA64Field field = pattern->fields[n];
        index = (index << field.width) | ((word >> field.lo) & ((1u << field.width) - 1));
    }
    if (owner) *owner = pattern;
    if (specialization) *specialization = index;
    return &pattern->cases[index];
}

const SnxA64Pattern* snx_a64_patterns(size_t* count) {
    *count = sizeof(patterns) / sizeof(patterns[0]);
    return patterns;
}

/* A pointer-only entry remains cheap for discovery, which needs only flow kind.
   Compiler backends request the same decode plus its typed specialization. */
static const SnxA64Case* decode_word(SnxU32 word, const SnxA64Pattern** owner, size_t* index) {
    const SnxA64Buckets* buckets = decode_buckets();
    if (buckets) {
        unsigned b = word >> 21;
        for (uint32_t i = buckets->offsets[b]; i < buckets->offsets[b + 1]; ++i) {
            const SnxA64Pattern* pattern = &patterns[buckets->indices[i]];
            if ((word & pattern->mask) == pattern->value) return decode_pattern(word, pattern, owner, index);
        }
    } else { /* no allocation may change the decoder's behavior */
        for (unsigned i = 0; i < PATTERN_COUNT; ++i)
            if ((word & patterns[i].mask) == patterns[i].value)
                return decode_pattern(word, &patterns[i], owner, index);
    }
    return &undefined_case;
}

const SnxA64Case* snx_a64_decode(SnxU32 word) { return decode_word(word, NULL, NULL); }

SnxA64Decoded snx_a64_decode_inst(SnxU32 word) {
    SnxA64Decoded decoded = {0};
    decoded.instruction = decode_word(word, &decoded.pattern, &decoded.index);
    return decoded;
}

const SnxA64Pattern* snx_a64_case_pattern(const SnxA64Case* instruction, size_t* index) {
    uintptr_t address = (uintptr_t)instruction;
    for (unsigned i = 0; i < PATTERN_COUNT; ++i) {
        uintptr_t first = (uintptr_t)patterns[i].cases;
        uintptr_t last = (uintptr_t)(patterns[i].cases + patterns[i].case_count);
        if (address >= first && address < last) {
            *index = (address - first) / sizeof(*instruction);
            return &patterns[i];
        }
    }
    *index = 0;
    return NULL;
}

SnxU64 snx_a64_target(SnxU32 word, SnxU64 pc) {
    unsigned width, shift;
    SnxU64 offset;
    if ((word & 0x7C000000UL) == 0x14000000UL) {
        width = 26;
        shift = 0;
    } else if ((word & 0x7E000000UL) == 0x36000000UL) {
        width = 14;
        shift = 5;
    } else {
        width = 19;
        shift = 5;
    }
    offset = (word >> shift) & ((1UL << width) - 1);
    if (offset & (1UL << (width - 1))) offset -= (SnxU64)1 << width;
    return pc + offset * 4;
}
