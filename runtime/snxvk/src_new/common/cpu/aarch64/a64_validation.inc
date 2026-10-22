/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Shared encoding validity checks: a64.c evaluates them while decoding, a64_emit.c prints the same expressions into
   the reference C. */
#define VALIDATIONS(X)                                                                                                 \
    X(1, ((w >> 31) || !(w & 0x400000UL)) &&                                                                           \
             (((((w >> 22) & 1) << 6) | (~(w >> 10) & 63)) & (((((w >> 22) & 1) << 6) | (~(w >> 10) & 63)) - 1)))      \
    X(2, (w >> 31) || !((w >> 21) & 2))                                                                                \
    X(3, ((w >> 22) & 1) == (w >> 31) && ((w >> 31) || !(w & 0x208000UL)))                                             \
    X(4, ((w >> 22) & 1) == (w >> 31) && ((w >> 31) || !(w & 0x8000UL)))                                               \
    X(5, (w >> 13) & 2)                                                                                                \
    X(6, (w >> 31) || !(w & 0x8000UL))                                                                                 \
    X(7, ((w >> 22) & 3) != 3 && ((w >> 31) || !(w & 0x8000UL)))                                                       \
    X(8, ((w >> 10) & 7) <= 4)                                                                                         \
    X(9, (w >> 31) || ((w >> 10) & 63) >= 32)                                                                          \
    X(10, ((w >> 22) & 3) != 3) /* General-register half FMOV requires later FP16, not A57. */
