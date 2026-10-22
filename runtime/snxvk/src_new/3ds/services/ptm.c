/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "3ds/runtime/internal.h"
#include "3ds/services/fs.h"
#include "3ds/services/ptm.h"
#include "services/ptm.swipc.inc"
#include <stdio.h>
#include <string.h>
void snx_ctr_ptm_system_data(SnxCtrProcess* p) {
    /* Azahar's PTM: the archive formatted with an empty ArchiveFormatInfo, then gamecoin.dat holding its
       default_game_coin (magic 0x4F00, 42 coins, none today, no steps, 2014-12-29; 3dbrew's GameCoin layout) */
    static const unsigned char metadata[16] = {0};
    static const unsigned char coins[20] = {0x00, 0x4f, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xde, 0x07, 12, 29};
    static const char path[] = "nand/data/00000000000000000000000000000000/extdata/00048000/f000000b/user/gamecoin.dat";
    pm_lock(&p->tree->lock);
    FsNode* node = NULL;
    if (fs_tree_lookup(p->tree, NULL, path, &node) == FS_TREE_NOTFOUND &&
        !snx_ctr_fs_make_extdata(p, true, 0xf0000000u, 0xf000000bu, metadata, NULL, 0) &&
        !fs_tree_create(p->tree, NULL, path, 0, sizeof coins, &node)) {
        FILE* f = fs_tree_open(p->tree, node, "r+b");
        if (f) {
            (void)fwrite(coins, sizeof coins, 1, f);
            fclose(f);
        }
    }
    pm_unlock(&p->tree->lock);
}
static uint32_t snx_ctr_ptm_CheckNew3DS(SnxCtrIpc* c, uint32_t* is_new) {
    *is_new = snx_ctr_memory_profile(c->process->memory)->model == SNX_CTR_NEW;
    return 0;
}
static uint32_t snx_ctr_ptm_ConfigureNew3DSCPU(SnxCtrIpc* c, uint32_t configuration) {
    /* The board implements only its base-rate ARM11 configuration, without
       accelerated L2/804-MHz modes. Requesting that existing configuration is
       idempotent; any request to enable an unimplemented mode stops. */
    if (configuration) c->state = SNX_CTR_IPC_UNSUPPORTED;
    return 0;
}
