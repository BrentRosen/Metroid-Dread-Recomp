/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "gnuabi/signals/signals.h"
#include "gnuabi/process/process.h"
#include "gnuabi/abi/errno/errno.h"
#include "common/utilities/endian.h"
/* a sigaction between the guest's little-endian words and the host's numbers (the same both ways) */
static GnuSignalAction guest_action(GnuSignalAction a) {
    a.handler = snx_le64(a.handler);
    a.flags = snx_le64(a.flags);
    a.restorer = snx_le64(a.restorer);
    a.mask = snx_le64(a.mask);
    return a;
}
int64_t gnu_sigaction(GnuProcess* p, const uint64_t a[6]) {
    if (a[0] < 1 || a[0] > 64 || (a[1] && (a[0] == 9 || a[0] == 19)) || a[3] != 8) return -GNU_EINVAL;
    GnuSignalAction action, old;
    if (a[1] && gnu_memory_read(&p->memory, a[1], &action, sizeof action)) return -GNU_EFAULT;
    /* in the guest's little-endian words */
    if (a[2] && gnu_memory_write(&p->memory, a[2],
                                 SNX_HOST_BE ? (old = guest_action(p->actions[a[0]]), &old) : &p->actions[a[0]],
                                 sizeof action))
        return -GNU_EFAULT;
    if (a[1]) {
        if (SNX_HOST_BE) action = guest_action(action);
        action.mask &= ~((1ull << 8) | (1ull << 18));
        p->actions[a[0]] = action;
        gnu_spawn_signal_disposition((int)a[0], action.handler);
    }
    return 0;
}
int64_t gnu_sigprocmask(GnuProcess* p, const uint64_t a[6]) {
    if (a[3] != 8) return -GNU_EINVAL;
    uint64_t mask = 0, old;
    if (a[1] && (a[0] > 2 || gnu_memory_read(&p->memory, a[1], &mask, 8))) return a[0] > 2 ? -GNU_EINVAL : -GNU_EFAULT;
    /* in the guest's little-endian word */
    if (a[2] &&
        gnu_memory_write(&p->memory, a[2], SNX_HOST_BE ? (old = snx_le64(p->signal_mask), &old) : &p->signal_mask, 8))
        return -GNU_EFAULT;
    mask = snx_le64(mask);
    mask &= ~((1ull << 8) | (1ull << 18));
    if (a[1]) {
        if (a[0] == 0)
            p->signal_mask |= mask;
        else if (a[0] == 1)
            p->signal_mask &= ~mask;
        else
            p->signal_mask = mask;
    }
    return 0;
}
