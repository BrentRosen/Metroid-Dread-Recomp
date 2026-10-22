/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_VK_Y2R_H
#define SNX_CTR_VK_Y2R_H
#include "3ds/gpu/transfer.h"
#include "3ds/gpu/y2r.h"
/* The Y2R engine's conversion on the GPU (vk/y2r.c), checked by snx_ctr_y2r_check: each stream the guest pages it
   spans from its address's page, pages[s] their physical addresses (page_count[s] of them, none for a stream the
   format does not use). Recorded, with no wait: the output is the GPU's until the submission is done (write tracking
   waits for it when the CPU reads it). SNX_CTR_XFER_BOUNDS for a page no RAM region holds, or more pages than the
   engine takes at once, SNX_CTR_XFER_UNSUPPORTED without a device */
SnxCtrXferResult snx_ctr_vk_y2r(const SnxCtrY2rConfig* c, const SnxCtrY2rBuffer streams[SNX_CTR_Y2R_STREAMS],
                                const uint32_t* const pages[SNX_CTR_Y2R_STREAMS],
                                const uint32_t page_count[SNX_CTR_Y2R_STREAMS]);
void snx_ctr_vk_y2r_free(void);
#endif
