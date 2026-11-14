/*
 * Project131 isolated PCem CPU state and i486DX reset.
 *
 * CPU reset state follows the Intel 486 reset vector semantics:
 * CS selector F000h, hidden CS base FFFF0000h, EIP FFF0h.
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <stdint.h>
#define I486_X86_STATE_DEFINE
#include "../x86.h"
#include <string.h>

static void reset_segment(x86seg *segment, uint16_t selector, uint32_t base)
{
    memset(segment, 0, sizeof(*segment));
    segment->seg = selector;
    segment->base = base;
    segment->limit = 0xFFFF;
    segment->limit_raw = 0xFFFF;
    segment->limit_low = 0;
    segment->limit_high = 0xFFFF;
    segment->access = 0x02;
    segment->access2 = 0;
    segment->checked = 1;
}

void i486_pcem_cpu_reset(void)
{
    unsigned int i;

    memset(&cpu_state, 0, sizeof(cpu_state));

    reset_segment(&cpu_state.seg_cs, 0xF000, 0xFFFF0000U);
    reset_segment(&cpu_state.seg_ds, 0, 0);
    reset_segment(&cpu_state.seg_es, 0, 0);
    reset_segment(&cpu_state.seg_ss, 0, 0);
    reset_segment(&cpu_state.seg_fs, 0, 0);
    reset_segment(&cpu_state.seg_gs, 0, 0);

    /* Intel 486 reset state: protected mode is disabled. */
    cpu_state.CR0.l = 0x60000010U;
    cpu_state.flags = 0x0002;
    cpu_state.eflags = 0x0002;
    cpu_state.pc = 0x0000FFF0U;
    cpu_state.oldpc = 0x0000FFF0U;
    cpu_state.op32 = 0;

    /* x87 register stack is empty after reset. */
    for (i = 0; i < 8; ++i)
        cpu_state.tag[i] = 3;
}
