/*
 * Project131 isolated PCem CPU state and i486DX reset.
 *
 * CPU reset state follows the Intel 486 reset vector semantics:
 * CS selector F000h, hidden CS base FFFF0000h, EIP FFF0h.
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <stdint.h>
#include <stdio.h>
#define I486_X86_STATE_DEFINE
#include "../x86.h"
#include <string.h>
#include "../../i486.h"

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


/*
 * First architectural instruction at the 486 reset vector.
 * This deliberately implements only the reset-vector FAR JMP bootstrap;
 * subsequent instructions must be dispatched by the PCem interpreter.
 */
bool i486::execute_reset_vector_bootstrap(void)
{
    const uint32_t physical = 0xFFFFFFF0U;
    const uint8_t opcode = i486::read8(physical);
    const uint16_t offset = (uint16_t)(i486::read8(physical + 1U) |
        ((uint16_t)i486::read8(physical + 2U) << 8));
    const uint16_t selector = (uint16_t)(i486::read8(physical + 3U) |
        ((uint16_t)i486::read8(physical + 4U) << 8));

    if (opcode != 0xEA)
    {
        i486::log("[I486] RESET BOOTSTRAP STOP: expected FAR JMP opcode EA");
        return false;
    }

    cpu_state.oldpc = cpu_state.pc;
    cpu_state.seg_cs.seg = selector;
    cpu_state.seg_cs.base = ((uint32_t)selector << 4);
    cpu_state.seg_cs.limit = 0xFFFF;
    cpu_state.seg_cs.limit_raw = 0xFFFF;
    cpu_state.seg_cs.limit_low = 0;
    cpu_state.seg_cs.limit_high = 0xFFFF;
    cpu_state.seg_cs.access = 0x9B;
    cpu_state.seg_cs.access2 = 0;
    cpu_state.seg_cs.checked = 1;
    cpu_state.pc = offset;
    cpu_state.op32 = 0;

    char message[160];
    sprintf(message,
        "[I486] BOOTSTRAP EXEC: FAR JMP %04X:%04X (opcode EA)",
        selector, offset);
    i486::log(message);

    sprintf(message,
        "[I486] BOOTSTRAP NEXT: CS=%04X BASE=%08X EIP=%08X PHYS=%08X",
        cpu_state.seg_cs.seg, cpu_state.seg_cs.base, cpu_state.pc,
        cpu_state.seg_cs.base + cpu_state.pc);
    i486::log(message);
    return true;
}
