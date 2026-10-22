/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_Y2R_H
#define SNX_CTR_Y2R_H
#include <stdbool.h>
#include <stdint.h>
/* The Y2R engine's conversion of YUV pictures to RGB (y2r.c, the CPU reference; vk/y2r.c, the GPU's): Azahar's
   service/cam/y2r_u.h and hw/y2r.cpp. Formats and modes by their wire values */
enum { SNX_CTR_Y2R_422_8, SNX_CTR_Y2R_420_8, SNX_CTR_Y2R_422_16, SNX_CTR_Y2R_420_16, SNX_CTR_Y2R_YUYV };
enum { SNX_CTR_Y2R_RGBA8, SNX_CTR_Y2R_RGB8, SNX_CTR_Y2R_RGB5A1, SNX_CTR_Y2R_RGB565 };
/* A stream the engine reads or writes by DMA: transfer_unit bytes, then gap bytes skipped, from its start */
typedef struct {
    uint32_t address, image_size;
    uint16_t transfer_unit, gap;
} SnxCtrY2rBuffer;
typedef struct {
    uint8_t input_format, output_format, rotation, block_alignment; /* rotation 0-3 (x 90 degrees), alignment 1 8x8 */
    uint16_t input_line_width, input_lines;
    int16_t coefficients[8]; /* 0-4 unsigned 2.8 matrix entries, 5-7 signed 11.5 offsets */
    uint16_t alpha;
} SnxCtrY2rConfig;
/* The streams by index: Y (or YUYV, interleaved), U, V, then the output */
enum { SNX_CTR_Y2R_Y, SNX_CTR_Y2R_U, SNX_CTR_Y2R_V, SNX_CTR_Y2R_DST, SNX_CTR_Y2R_STREAMS };
/* Whether the engine converts c from these streams: a width of whole tiles (at most 1024), 8x8 output of whole strips,
   and streams whose transfer units each hold whole samples or pixels and fill each strip exactly, as Azahar's DMA
   assumes. *spans gets the bytes each stream reaches from its start, 0 for one the format does not use */
bool snx_ctr_y2r_check(const SnxCtrY2rConfig* c, const SnxCtrY2rBuffer streams[SNX_CTR_Y2R_STREAMS],
                       uint64_t spans[SNX_CTR_Y2R_STREAMS]);
/* The CPU reference over each stream's bytes from its start (src[SNX_CTR_Y2R_U] and [V] unused for YUYV), the checked
   conversion written into dst */
void snx_ctr_y2r_convert(const SnxCtrY2rConfig* c, const SnxCtrY2rBuffer streams[SNX_CTR_Y2R_STREAMS],
                         const unsigned char* const src[3], unsigned char* dst);
/* Azahar's four standard coefficient sets: ITU-R BT.601, BT.709, and both scaled to the video range */
extern const int16_t snx_ctr_y2r_standard[4][8];
#endif
