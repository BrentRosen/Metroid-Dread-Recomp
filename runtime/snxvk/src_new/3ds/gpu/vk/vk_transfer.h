/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_VK_TRANSFER_H
#define SNX_CTR_VK_TRANSFER_H
#include "3ds/gpu/transfer.h"
/* The transfer engine's commands recorded on the GPU (vk/transfer.c), over physical addresses: done once
   snx_ctr_vk_finish returns. SNX_CTR_XFER_BOUNDS for a span outside one RAM region, SNX_CTR_XFER_UNSUPPORTED for a
   mode the reference has not either, or without a device; empty ones do nothing */
SnxCtrXferResult snx_ctr_vk_fill(uint32_t pa, uint32_t size, uint32_t value, uint32_t control);
SnxCtrXferResult snx_ctr_vk_display_transfer(const SnxCtrDisplayTransfer* t, uint32_t input, uint32_t output);
SnxCtrXferResult snx_ctr_vk_texture_copy(const SnxCtrTextureCopy* t, uint32_t input, uint32_t output);
void snx_ctr_vk_transfer_free(void);
#endif
