/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The Switch driver's entry (entry.c): the program's main, or under scarletnx's own names in a build where it is also
   the other drivers (src/multicall) */
#ifndef SNX_ENTRY_H
#define SNX_ENTRY_H

int snx_switch_main(int argc, char** argv); /* the MCP server, the driver's commands, else a game; its exit status */

#endif
