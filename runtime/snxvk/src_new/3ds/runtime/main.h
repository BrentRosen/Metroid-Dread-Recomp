/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The 3DS driver (main.c): scarletnx-3ds's own program, or a name of scarletnx's (src/multicall) */
#ifndef SNX_CTR_MAIN_H
#define SNX_CTR_MAIN_H

#include <stdbool.h>

int snx_ctr_main(int argc, char** argv); /* extracts, recompiles and plays what the arguments say; its exit status */
/* Whether the last part of path (".exe" dropped) is a 3DS application's title ID, 0004 and 12 more hex digits, as the
   game's folder and its copy of the runtime are named: a Switch title ID starts 01 */
bool snx_ctr_title_name(const char* path);

#endif
