/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* scarletnx --mcp: an MCP server over the logs and games of this machine, for an agent looking into a failure (mcp.c).
   The program's entry points (entry.c, driver/main.c) run it before the driver */
#ifndef SNX_MCP_H
#define SNX_MCP_H

/* argv[1] is "--mcp"; "--repo DIR" after it names the data folder. The exit status */
int snx_mcp(int argc, char** argv);

#endif
