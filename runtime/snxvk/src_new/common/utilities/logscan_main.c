/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx-logscan: scarletnx --scan-log as a program of its own, for what runs without a whole ScarletNX build: the game
   reports workflow (scripts/game-reports.py) and the tests */
#include "common/utilities/demangle.h"
#include "common/utilities/logscan.h"

int main(int argc, char** argv) { return snx_logscan_main(argc, argv, snx_demangle); }
