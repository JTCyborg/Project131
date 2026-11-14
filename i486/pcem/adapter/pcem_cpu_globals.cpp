/*
 * Project131 PCem interpreter shared CPU state.
 *
 * The data layout and lazy-flag/ModRM conventions follow the GPL-2.0 PCem
 * interpreter. This file owns the shared globals used by the interpreter
 * translation units; it does not instantiate PCem machine devices.
 *
 * Project131 adaptation: 2026-11-14.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <fenv.h>
#include "../../i486.h"
#include <string.h>
#include "../x86.h"
#include "../x86_flags.h"
#include "../x87.h"
#include "../x87_timings.h"

extern "C" {
x86seg gdt, ldt, idt, tr;

uint32_t cr2, cr3, cr4;
uint32_t dr[8];

uint16_t sysenter_cs;
uint32_t sysenter_eip, sysenter_esp;

uint32_t use32;
int stack32;
int optype;
int trap;

uint32_t rmdat;
uint32_t easeg;
uint32_t *eal_r, *eal_w;

int oldcpl;
uint32_t oldss;
int nmi_enable;
int cpl_override;
int fpucount;
uint16_t cpu_cur_status;

uint32_t pccache;
uint8_t *pccache2;

/* PCem interpreter state required by opcode handlers. The first real-mode
 * bring-up does not enter protected mode or use the legacy dynarec gates.
 */
int cgate16 = 0;
int cgate32 = 0;
int x86_was_reset = 0;

/* x87 execution timing is accounted by the interpreter. The fixed 486 timing
 * profile is initialized separately when the timing table is wired in.
 */
x87_timings_t x87_timings = { 0 };

uint8_t znptable8[256];
uint16_t *mod1add[2][8];
uint32_t *mod1seg[8];

/* abrt_error and cgate16/cgate32 are owned by the segment/exception core
 * (upstream x86seg.c); do not define them in this shared-state adapter.
 */
int insc;
float mips, flops;
int cpu_end_block_after_ins;
int tempc;
int cycles_main;

}

void i486_pcem_cpu_core_init(void)
{
    static const uint8_t reg_a[8] = { 3, 3, 5, 5, 6, 7, 5, 3 };
    static const uint8_t reg_b[8] = { 6, 7, 6, 7, 0, 0, 0, 0 };
    static uint16_t zero_offset = 0;
    unsigned int i;

    /* PCem's lazy parity table: P_FLAG is set for even parity. */
    for (i = 0; i < 256; ++i)
    {
        unsigned int v = i;
        unsigned int ones = 0;
        while (v)
        {
            ones ^= 1U;
            v &= v - 1U;
        }
        znptable8[i] = ones ? 0 : P_FLAG;
    }

    /*
     * 16-bit ModRM effective-address register pairs:
     * BX+SI, BX+DI, BP+SI, BP+DI, SI, DI, (disp16/BP), BX.
     * The second register is unused for SI/DI/BP/BX-only forms.
     */
    for (i = 0; i < 8; ++i)
    {
        mod1add[0][i] = &cpu_state.regs[reg_a[i]].w;
        mod1add[1][i] = (i < 4) ? &cpu_state.regs[reg_b[i]].w : &zero_offset;
        mod1seg[i] = (i == 2 || i == 3 || i == 6)
            ? &cpu_state.seg_ss.base
            : &cpu_state.seg_ds.base;
    }

    use32 = 0;
    stack32 = 0;
    optype = 0;
    trap = 0;
    rmdat = 0;
    easeg = 0;
    eal_r = 0;
    eal_w = 0;
    oldcpl = 0;
    oldss = 0;
    nmi_enable = 0; /* External NMI is disabled for initial module bring-up. */
    cpl_override = 0;
    fpucount = 0;
    cpu_cur_status = 0;
    pccache = 0xFFFFFFFFU;
    pccache2 = 0;
    insc = 0;
    mips = flops = 0.0f;
    cpu_end_block_after_ins = 0;
    tempc = 0;
    cycles_main = 0;

    memset(&gdt, 0, sizeof(gdt));
    memset(&ldt, 0, sizeof(ldt));
    memset(&idt, 0, sizeof(idt));
    memset(&tr, 0, sizeof(tr));
    idt.limit = 0x03FF;
}


/* Fixed Intel i486DX timing model (PCem CPU_i486DX profile).
 * The interpreter has no runtime CPU-model manager: this module is always
 * an i486DX, so initialize the timing globals directly.
 */
extern "C" {
int is486 = 1;
int timing_rr = 1;
int timing_mr = 3, timing_mrl = 3;
int timing_rm = 2, timing_rml = 2;
int timing_mm = 3, timing_mml = 3;
int timing_bt = 2, timing_bnt = 1;
int timing_int = 4;
int timing_int_rm = 26, timing_int_v86 = 82, timing_int_pm = 44, timing_int_pm_outer = 71;
int timing_iret_rm = 15, timing_iret_v86 = 36, timing_iret_pm = 20, timing_iret_pm_outer = 36;
int timing_call_rm = 18, timing_call_pm = 20, timing_call_pm_gate = 35, timing_call_pm_gate_inner = 69;
int timing_retf_rm = 13, timing_retf_pm = 17, timing_retf_pm_outer = 35;
int timing_jmp_rm = 17, timing_jmp_pm = 19, timing_jmp_pm_gate = 32;
int timing_misaligned = 3;
}


/* Minimal logging bridge for PCem interpreter handlers. */
extern "C" void pclog(const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    i486::log(message);
}

extern "C" void fatal(const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    i486::log("[I486] PCem fatal: ");
    i486::log(message);
}

/* Isolated PCem port-I/O bridge. The module's current port map returns FFh
 * for reads and ignores writes; no UnrealSpeccy/ATM devices are touched.
 */
extern "C" uint8_t i486_pcem_in8(uint16_t port) { return i486::in8(port); }
extern "C" uint16_t i486_pcem_in16(uint16_t port) { return i486::in16(port); }
extern "C" uint32_t i486_pcem_in32(uint16_t port) { return i486::in32(port); }
extern "C" void i486_pcem_out8(uint16_t port, uint8_t value) { i486::out8(port, value); }
extern "C" void i486_pcem_out16(uint16_t port, uint16_t value) { i486::out16(port, value); }
extern "C" void i486_pcem_out32(uint16_t port, uint32_t value) { i486::out32(port, value); }


/* The interpreter has no dynarec code generator. Preserve x87 rounding semantics
 * by applying the requested mode to the host floating-point environment.
 */
extern "C" void codegen_set_rounding_mode(int mode)
{
    int host_mode = FE_TONEAREST;
    switch (mode)
    {
        case X87_ROUNDING_DOWN: host_mode = FE_DOWNWARD; break;
        case X87_ROUNDING_UP: host_mode = FE_UPWARD; break;
        case X87_ROUNDING_CHOP: host_mode = FE_TOWARDZERO; break;
        default: host_mode = FE_TONEAREST; break;
    }
    (void)fesetround(host_mode);
}
