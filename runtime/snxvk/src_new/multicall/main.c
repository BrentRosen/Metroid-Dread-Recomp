/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* scarletnx's main where it is also the other drivers (SNX_MULTICALL): started as scarletnx-3ds or scarletnx-gnuabi,
   the links of those names installed beside it, it is that driver, as it is under a 3DS title ID, the name of a 3DS
   game's copy, and under any other name the Switch driver, which takes a game's copy's name for the game it runs. One program carries LLVM, SDL and the code they share once */
#include "nx/runtime/entry.h"
#ifdef SNX_MULTICALL_3DS
#include "3ds/runtime/main.h"
#endif
#ifdef SNX_MULTICALL_GNUABI
#include "gnuabi/runtime/main.h"
#endif
#include <string.h>

/* 1 when the last part of path is name */
static int named(const char* path, const char* name) {
    const char* slash = strrchr(path, '/');
    return !strcmp(slash ? slash + 1 : path, name);
}

int main(int argc, char** argv) {
    const char* self = argc > 0 && argv[0] ? argv[0] : "";
#ifdef SNX_MULTICALL_3DS
    if (named(self, "scarletnx-3ds") || snx_ctr_title_name(self)) return snx_ctr_main(argc, argv);
#endif
#ifdef SNX_MULTICALL_GNUABI
    if (named(self, "scarletnx-gnuabi")) return snx_gnuabi_main(argc, argv);
#endif
    return snx_switch_main(argc, argv);
}
