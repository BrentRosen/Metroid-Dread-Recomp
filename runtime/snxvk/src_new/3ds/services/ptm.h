/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_PTM_H
#define SNX_CTR_PTM_H
struct SnxCtrProcess;
/* The shared ExtSaveData PTM keeps the Play Coins in (0xF000000B, gamecoin.dat), made with Azahar's default coins when
   the title's NAND has none, as the console's system made it long before a game asks for it. At the mount */
void snx_ctr_ptm_system_data(struct SnxCtrProcess* p);
#endif
