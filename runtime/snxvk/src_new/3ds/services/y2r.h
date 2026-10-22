/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_Y2R_SERVICE_H
#define SNX_CTR_Y2R_SERVICE_H
#include "3ds/ipc/ctr_ipc.h"
#include <stdint.h>
struct SnxCtrProcess;
void snx_ctr_y2r_dispatch(SnxCtrIpc* c);
/* The running conversion's end, due at p->y2r.done_ns (snx_ctr_schedule): the engine idle, its event signaled */
void snx_ctr_y2r_done(struct SnxCtrProcess* p);
#endif
