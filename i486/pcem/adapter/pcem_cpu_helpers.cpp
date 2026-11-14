/*
 * Project131 i486DX interpreter helpers.
 *
 * Implements the real-mode subset needed by the first BIOS bring-up.
 * Protected-mode transitions and non-Intel extensions are deliberately
 * rejected rather than emulated with incomplete PCem machine state.
 *
 * Adapted against PCem interpreter contracts; SPDX-License-Identifier: GPL-2.0
 */
#include <stdint.h>
#include <limits.h>
#include "../x86.h"
#include "../x86_flags.h"

#include "pcem_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t abrt_error = 0;

static void i486_raise_gp(uint16_t error)
{
    cpu_state.abrt = ABRT_GPF;
    abrt_error = error;
}

static void i486_load_real_cs(uint16_t selector)
{
    cpu_state.seg_cs.seg = selector;
    cpu_state.seg_cs.base = ((uint32_t)selector << 4);
    cpu_state.seg_cs.limit = 0xFFFF;
    cpu_state.seg_cs.limit_raw = 0xFFFF;
    cpu_state.seg_cs.limit_low = 0;
    cpu_state.seg_cs.limit_high = 0xFFFF;
    cpu_state.seg_cs.access = 0x9B;
    cpu_state.seg_cs.access2 = 0;
    cpu_state.seg_cs.checked = 1;
}

static void i486_push16(uint16_t value)
{
    SP = (uint16_t)(SP - 2);
    writememwl(ss + SP, value);
}

static uint16_t i486_pop16(void)
{
    uint16_t value = readmemwl(ss + SP);
    SP = (uint16_t)(SP + 2);
    return value;
}

void x86illegal(void)
{
    /* #UD is architectural; deliver it through the real-mode IVT. */
    x86_int(6);
}

void x86gpf(char *reason, uint16_t error)
{
    (void)reason;
    i486_raise_gp(error);
}

void x86gpf_expected(char *reason, uint16_t error)
{
    (void)reason;
    cpu_state.abrt = ABRT_GPF | ABRT_EXPECTED;
    abrt_error = error;
}

int loadseg(uint16_t selector, x86seg *segment)
{
    if (!segment)
    {
        i486_raise_gp(selector & 0xFFFC);
        return 1;
    }
    if (msw & 1)
    {
        i486_raise_gp(selector & 0xFFFC);
        return 1;
    }

    segment->seg = selector;
    segment->base = ((uint32_t)selector << 4);
    segment->limit = 0xFFFF;
    segment->limit_raw = 0xFFFF;
    segment->limit_low = 0;
    segment->limit_high = 0xFFFF;
    segment->access = 0x03;
    segment->access2 = 0;
    segment->checked = 1;
    return 0;
}

void loadcs(uint16_t selector)
{
    if (msw & 1)
    {
        i486_raise_gp(selector & 0xFFFC);
        return;
    }
    i486_load_real_cs(selector);
}

void loadcscall(uint16_t selector, uint32_t old_pc)
{
    if (msw & 1)
    {
        i486_raise_gp(selector & 0xFFFC);
        return;
    }
    i486_push16(CS);
    i486_push16((uint16_t)old_pc);
    i486_load_real_cs(selector);
}

void loadcsjmp(uint16_t selector, uint32_t old_pc)
{
    (void)old_pc;
    loadcs(selector);
}

void pmoderetf(int is32, uint16_t offset)
{
    (void)is32;
    (void)offset;
    /* This helper is only valid after entering protected mode. */
    i486_raise_gp(0);
}

void pmodeiret(int is32)
{
    (void)is32;
    i486_raise_gp(0);
}

static void i486_deliver_real_interrupt(int vector, uint32_t return_pc)
{
    uint32_t entry;
    uint16_t new_ip, new_cs;

    if (msw & 1)
    {
        /* No protected-mode IDT/task-gate implementation is wired yet. */
        i486_raise_gp((uint16_t)(vector * 8 + 2));
        return;
    }

    entry = idt.base + ((uint32_t)(vector & 0xFF) * 4U);
    if (((uint32_t)(vector & 0xFF) * 4U + 3U) > idt.limit)
    {
        cpu_state.abrt = ABRT_GPF;
        abrt_error = 0;
        return;
    }

    flags_rebuild();
    i486_push16(cpu_state.flags);
    i486_push16(CS);
    i486_push16((uint16_t)return_pc);

    cpu_state.flags &= (uint16_t)~(I_FLAG | T_FLAG);
    cpu_state.eflags &= (uint16_t)~(I_FLAG | T_FLAG);
    new_ip = readmemwl(entry);
    new_cs = readmemwl(entry + 2U);
    cpu_state.pc = new_ip;
    i486_load_real_cs(new_cs);
}

void x86_int(int vector)
{
    i486_deliver_real_interrupt(vector, cpu_state.oldpc);
}

void x86_int_sw(int vector)
{
    i486_deliver_real_interrupt(vector, cpu_state.pc);
}

int x86_int_sw_rm(int vector)
{
    if (msw & 1)
    {
        i486_raise_gp((uint16_t)(vector * 8 + 2));
        return 1;
    }
    i486_deliver_real_interrupt(vector, cpu_state.pc);
    return cpu_state.abrt ? 1 : 0;
}

void sysenter(void)
{
    /* SYSENTER/SYSEXIT are not part of the original Intel i486DX ISA. */
    x86illegal();
}

void sysexit(void)
{
    x86illegal();
}

static void i486_divide_error(void)
{
    x86_int(0);
}

int divl(uint32_t divisor)
{
    uint64_t dividend, quotient;
    uint32_t remainder;

    if (!divisor)
    {
        i486_divide_error();
        return 1;
    }
    dividend = ((uint64_t)EDX << 32) | EAX;
    quotient = dividend / divisor;
    remainder = (uint32_t)(dividend % divisor);
    if (quotient > 0xFFFFFFFFULL)
    {
        i486_divide_error();
        return 1;
    }
    EAX = (uint32_t)quotient;
    EDX = remainder;
    return 0;
}

int idivl(int32_t divisor)
{
    int64_t dividend, quotient, remainder;

    if (!divisor)
    {
        i486_divide_error();
        return 1;
    }
    dividend = (int64_t)(((uint64_t)EDX << 32) | EAX);
    if (dividend == INT64_MIN && divisor == -1)
    {
        i486_divide_error();
        return 1;
    }
    quotient = dividend / divisor;
    remainder = dividend % divisor;
    if (quotient < INT32_MIN || quotient > INT32_MAX)
    {
        i486_divide_error();
        return 1;
    }
    EAX = (uint32_t)(int32_t)quotient;
    EDX = (uint32_t)(int32_t)remainder;
    return 0;
}

void x86_smi_leave(void)
{
    /* SMM/SMI is not enabled in this initial isolated CPU profile. */
    x86illegal();
}

void cyrix_load_seg_descriptor(uint32_t address, x86seg *segment)
{
    (void)address;
    (void)segment;
    /* Cyrix-specific descriptor instructions are not an i486DX feature. */
    x86illegal();
}

void cyrix_write_seg_descriptor(uint32_t address, x86seg *segment)
{
    (void)address;
    (void)segment;
    x86illegal();
}

enum { I486_TAG_EMPTY = 0, I486_TAG_VALID = 1, I486_TAG_UINT64 = 0x80 };

uint16_t x87_gettag(void)
{
    uint16_t result = 0;
    unsigned int i;
    for (i = 0; i < 8; ++i)
    {
        unsigned int index = i;
        uint16_t tag;
        if (cpu_state.tag[index] == I486_TAG_EMPTY)
            tag = 3;
        else if (cpu_state.tag[index] & I486_TAG_UINT64)
            tag = 2;
        else if (cpu_state.ST[index] == 0.0 && !cpu_state.ismmx)
            tag = 1;
        else
            tag = 0;
        result |= (uint16_t)(tag << (i * 2));
    }
    return result;
}

void x87_settag(uint16_t new_tag)
{
    unsigned int i;
    for (i = 0; i < 8; ++i)
    {
        unsigned int index = (unsigned int)((cpu_state.TOP + (int)i) & 7);
        unsigned int tag = (new_tag >> (i * 2)) & 3;
        if (tag == 3)
            cpu_state.tag[index] = I486_TAG_EMPTY;
        else if (tag == 2)
            cpu_state.tag[index] = I486_TAG_VALID | I486_TAG_UINT64;
        else
            cpu_state.tag[index] = I486_TAG_VALID;
    }
}

int checkio(int port)
{
    (void)port;
    if (!(msw & 1))
        return 0; /* Real mode: no TSS I/O-permission bitmap applies. */

    /* Protected-mode TSS bitmap is not implemented; deny with #GP. */
    i486_raise_gp(0);
    return 1;
}

#ifdef __cplusplus
}
#endif
