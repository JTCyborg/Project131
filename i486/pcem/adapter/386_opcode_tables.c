/*
 * PCem i486 interpreter opcode tables.
 *
 * Adapted for Project131 on 2026-11-14 from PCem 386_dynarec.c and 386.c.
 * Opcode handlers and table contents are the upstream PCem implementation.
 * This translation unit contains no dynarec backend and disables the legacy
 * 286/386 prefetch-queue model; i486 uses its internal cache model instead.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <fenv.h>

#include "x86.h"
#include "x86_ops.h"
#include "x87.h"
#include "adapter/pcem_memory.h"

/* CPU-model timing state normally declared by PCem cpu.h.
 * Keep the interpreter isolated from PCem's machine/device manager.
 */
extern int is486;
extern int timing_rr, timing_mr, timing_mrl;
extern int timing_rm, timing_rml, timing_mm, timing_mml;
extern int timing_bt, timing_bnt;
extern int timing_int, timing_int_rm, timing_int_v86, timing_int_pm, timing_int_pm_outer;
extern int timing_iret_rm, timing_iret_v86, timing_iret_pm, timing_iret_pm_outer;
extern int timing_call_rm, timing_call_pm, timing_call_pm_gate, timing_call_pm_gate_inner;
extern int timing_retf_rm, timing_retf_pm, timing_retf_pm_outer;
extern int timing_jmp_rm, timing_jmp_pm, timing_jmp_pm_gate;
extern int timing_misaligned;

/* ModRM decode fields are stored in the PCem CPU state union.
 * Define them before 386_common.h: its inline effective-address helpers
 * reference cpu_rm/cpu_mod/cpu_reg during their definitions.
 */
#define cpu_rm  (cpu_state.rm_data.rm_mod_reg.rm)
#define cpu_mod (cpu_state.rm_data.rm_mod_reg.mod)
#define cpu_reg (cpu_state.rm_data.rm_mod_reg.reg)

#include "386_common.h"

static inline void fetch_ea_32_long(uint32_t rmdat)
{
        eal_r = eal_w = NULL;
        easeg = cpu_state.ea_seg->base;
        if (cpu_rm == 4)
        {
                uint8_t sib = rmdat >> 8;
                
                switch (cpu_mod)
                {
                        case 0: 
                        cpu_state.eaaddr = cpu_state.regs[sib & 7].l; 
                        cpu_state.pc++; 
                        break;
                        case 1: 
                        cpu_state.pc++;
                        cpu_state.eaaddr = ((uint32_t)(int8_t)getbyte()) + cpu_state.regs[sib & 7].l; 
//                        pc++; 
                        break;
                        case 2: 
                        cpu_state.eaaddr = (fastreadl(cs + cpu_state.pc + 1)) + cpu_state.regs[sib & 7].l; 
                        cpu_state.pc += 5; 
                        break;
                }
                /*SIB byte present*/
                if ((sib & 7) == 5 && !cpu_mod) 
                        cpu_state.eaaddr = getlong();
                else if ((sib & 6) == 4 && !cpu_state.ssegs)
                {
                        easeg = ss;
                        cpu_state.ea_seg = &cpu_state.seg_ss;
                }
                if (((sib >> 3) & 7) != 4) 
                        cpu_state.eaaddr += cpu_state.regs[(sib >> 3) & 7].l << (sib >> 6);
        }
        else
        {
                cpu_state.eaaddr = cpu_state.regs[cpu_rm].l;
                if (cpu_mod) 
                {
                        if (cpu_rm == 5 && !cpu_state.ssegs)
                        {
                                easeg = ss;
                                cpu_state.ea_seg = &cpu_state.seg_ss;
                        }
                        if (cpu_mod == 1) 
                        { 
                                cpu_state.eaaddr += ((uint32_t)(int8_t)(rmdat >> 8)); 
                                cpu_state.pc++; 
                        }
                        else          
                        {
                                cpu_state.eaaddr += getlong(); 
                        }
                }
                else if (cpu_rm == 5) 
                {
                        cpu_state.eaaddr = getlong();
                }
        }
        if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
        {
		uint32_t addr = easeg + cpu_state.eaaddr;
                if ( readlookup2[addr >> 12] != -1)
                	eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
                if (writelookup2[addr >> 12] != -1)
                	eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
        }
}

static inline void fetch_ea_16_long(uint32_t rmdat)
{
        eal_r = eal_w = NULL;
        easeg = cpu_state.ea_seg->base;
        if (!cpu_mod && cpu_rm == 6)
        { 
                cpu_state.eaaddr = getword();
        }
        else
        {
                switch (cpu_mod)
                {
                        case 0:
                        cpu_state.eaaddr = 0;
                        break;
                        case 1:
                        cpu_state.eaaddr = (uint16_t)(int8_t)(rmdat >> 8); cpu_state.pc++;
                        break;
                        case 2:
                        cpu_state.eaaddr = getword();
                        break;
                }
                cpu_state.eaaddr += (*mod1add[0][cpu_rm]) + (*mod1add[1][cpu_rm]);
                if (mod1seg[cpu_rm] == &ss && !cpu_state.ssegs)
                {
                        easeg = ss;
                        cpu_state.ea_seg = &cpu_state.seg_ss;
                }
                cpu_state.eaaddr &= 0xFFFF;
        }
        if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC)
        {
		uint32_t addr = easeg + cpu_state.eaaddr;
                if ( readlookup2[addr >> 12] != -1)
                	eal_r = (uint32_t *)(readlookup2[addr >> 12] + addr);
                if (writelookup2[addr >> 12] != -1)
                	eal_w = (uint32_t *)(writelookup2[addr >> 12] + addr);
        }
}

#define fetch_ea_16(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_16_long(rmdat); if (cpu_state.abrt) return 0; } 
#define fetch_ea_32(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_32_long(rmdat); } if (cpu_state.abrt) return 0

#include "x86_flags.h"

/*
 * Minimal PCem machine/codegen compatibility for the interpreter tables.
 * Project131 is a fixed Intel i486DX/33 without dynarec or PCem machine devices.
 */
#define MANU_INTEL 0
#define CPU_BLOCK_END() ((void)0)
#define UNUSED(value) ((void)(value))
static int cpu_manufacturer = MANU_INTEL;
static int CPUID = 0;                 /* Original i486DX/33 has no CPUID instruction. */
static const uint64_t cpu_CR4_mask = 0; /* CR4 is not implemented by this CPU model. */

/* Fixed-model state normally supplied by PCem's machine-dependent cpu.h.
 * These values describe the i486DX interpreter only; no PC devices are attached.
 */
#define CR4_TSD (1U << 2)
#define CR4_PCE (1U << 8)
static const int is386 = 1;             /* i486 includes the 386 instruction set. */
static const int cpu_use_dynarec = 0;   /* This build uses the interpreter only. */
static int ins = 0;                     /* REP instruction accounting. */
static int cpu_cache_int_enabled = 0;   /* Cache is not externally write-through gated. */
static uint64_t tsc = 0;                /* No RDTSC feature; placeholder state is never read. */
#define FPU_8087 0
static const int fpu_type = 1;          /* Integrated 387-compatible i486DX FPU. */

/* Fixed platform-independent i486DX model state. */
static const int cpu_iscyrix = 0;
static const int cpu_16bitbus = 0; /* Intel 486DX has a 32-bit external data bus. */
static const int nmi = 0;          /* No external NMI source in the initial module. */
static const int nmi_mask = 0;
static int pic_intpending = 0;     /* No PC PIC is attached to this CPU module. */
static int mmu_perm = 4;            /* PCem's non-paged default permission mode. */

/* I/O instructions use the isolated i486 port space, never host PC devices. */
extern uint8_t i486_pcem_in8(uint16_t port);
extern uint16_t i486_pcem_in16(uint16_t port);
extern uint32_t i486_pcem_in32(uint16_t port);
extern void i486_pcem_out8(uint16_t port, uint8_t value);
extern void i486_pcem_out16(uint16_t port, uint16_t value);
extern void i486_pcem_out32(uint16_t port, uint32_t value);
static inline uint8_t inb(uint16_t port) { return i486_pcem_in8(port); }
static inline uint16_t inw(uint16_t port) { return i486_pcem_in16(port); }
static inline uint32_t inl(uint16_t port) { return i486_pcem_in32(port); }
static inline void outb(uint16_t port, uint8_t value) { i486_pcem_out8(port, value); }
static inline void outw(uint16_t port, uint16_t value) { i486_pcem_out16(port, value); }
static inline void outl(uint16_t port, uint32_t value) { i486_pcem_out32(port, value); }

/* Paging translation is not ported yet; the active adapter is identity-mapped.
 * Rebuild its direct mappings on PCem cache-flush requests.
 */
static inline void flushmmucache(void) { pcem_memory_rebuild(); }
static inline void flushmmucache_cr3(void) { pcem_memory_rebuild(); }
static inline void mmu_invalidate(uint32_t address) { (void)address; }
static inline void cpu_update_waitstates(void) { /* Fixed i486DX/33 timing model. */ }
static inline void picint(int mask) { (void)mask; } /* Integrated FPU; no legacy PIC. */
static inline void cpu_CPUID(void) { /* Original i486DX has no CPUID instruction. */ }
static int codegen_flags_changed;    /* Dynarec-only notification; intentionally inert. */

void pclog(const char *format, ...);
void fatal(const char *format, ...);

#define getbytef()  ((uint8_t)(fetchdat)); cpu_state.pc++
#define getwordf()  ((uint16_t)(fetchdat)); cpu_state.pc+=2
#define getbyte2f() ((uint8_t)(fetchdat>>8)); cpu_state.pc++
#define getword2f() ((uint16_t)(fetchdat>>8)); cpu_state.pc+=2

/* No legacy external prefetch queue on the i486DX. */
#define PREFETCH_RUN(...)   ((void)0)
#define PREFETCH_PREFIX()  ((void)0)
#define PREFETCH_FLUSH()   ((void)0)

#define OP_TABLE(name) ops_ ## name
#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

/* Fixed Intel i486DX/33 CPU feature compatibility from PCem cpu.h.
 * The 486DX has no RDTSC, MSR, MMX, CR4, VME, CX8, 3DNow or SYSCALL.
 * Keep the opcode handlers compiled, but make unsupported instructions
 * take their normal #UD path through cpu_has_feature().
 */
#define CPU_FEATURE_RDTSC   (1 << 0)
#define CPU_FEATURE_MSR     (1 << 1)
#define CPU_FEATURE_MMX     (1 << 2)
#define CPU_FEATURE_CR4     (1 << 3)
#define CPU_FEATURE_VME     (1 << 4)
#define CPU_FEATURE_CX8     (1 << 5)
#define CPU_FEATURE_3DNOW   (1 << 6)
#define CPU_FEATURE_SYSCALL (1 << 7)

static inline int cpu_has_feature(int feature)
{
    (void)feature;
    return 0;
}

/* Reached only after an MSR feature check; retained as link-safe stubs. */
static inline void cpu_RDMSR(void) {}
static inline void cpu_WRMSR(void) {}

/* Original PCem opcode handlers and tables, including integrated x87. */
#include "386_ops.h"

/* Active interpreter dispatch tables. Dynarec pointers are intentionally unused. */
OpFn *x86_opcodes = ops_386;
OpFn *x86_opcodes_0f = ops_386_0f;
OpFn *x86_opcodes_d8_a16 = ops_fpu_d8_a16;
OpFn *x86_opcodes_d8_a32 = ops_fpu_d8_a32;
OpFn *x86_opcodes_d9_a16 = ops_fpu_d9_a16;
OpFn *x86_opcodes_d9_a32 = ops_fpu_d9_a32;
OpFn *x86_opcodes_da_a16 = ops_fpu_da_a16;
OpFn *x86_opcodes_da_a32 = ops_fpu_da_a32;
OpFn *x86_opcodes_db_a16 = ops_fpu_db_a16;
OpFn *x86_opcodes_db_a32 = ops_fpu_db_a32;
OpFn *x86_opcodes_dc_a16 = ops_fpu_dc_a16;
OpFn *x86_opcodes_dc_a32 = ops_fpu_dc_a32;
OpFn *x86_opcodes_dd_a16 = ops_fpu_dd_a16;
OpFn *x86_opcodes_dd_a32 = ops_fpu_dd_a32;
OpFn *x86_opcodes_de_a16 = ops_fpu_de_a16;
OpFn *x86_opcodes_de_a32 = ops_fpu_de_a32;
OpFn *x86_opcodes_df_a16 = ops_fpu_df_a16;
OpFn *x86_opcodes_df_a32 = ops_fpu_df_a32;
OpFn *x86_opcodes_REPE = ops_REPE;
OpFn *x86_opcodes_REPNE = ops_REPNE;
OpFn *x86_opcodes_3DNOW = ops_3DNOW;

void x86_setopcodes(OpFn *opcodes, OpFn *opcodes_0f,
                    OpFn *dynarec_opcodes, OpFn *dynarec_opcodes_0f)
{
    (void)dynarec_opcodes;
    (void)dynarec_opcodes_0f;
    x86_opcodes = opcodes;
    x86_opcodes_0f = opcodes_0f;
}

void i486_pcem_set_interpreter_tables(void)
{
    x86_opcodes = ops_386;
    x86_opcodes_0f = ops_386_0f;
    x86_opcodes_d8_a16 = ops_fpu_d8_a16;
    x86_opcodes_d8_a32 = ops_fpu_d8_a32;
    x86_opcodes_d9_a16 = ops_fpu_d9_a16;
    x86_opcodes_d9_a32 = ops_fpu_d9_a32;
    x86_opcodes_da_a16 = ops_fpu_da_a16;
    x86_opcodes_da_a32 = ops_fpu_da_a32;
    x86_opcodes_db_a16 = ops_fpu_db_a16;
    x86_opcodes_db_a32 = ops_fpu_db_a32;
    x86_opcodes_dc_a16 = ops_fpu_dc_a16;
    x86_opcodes_dc_a32 = ops_fpu_dc_a32;
    x86_opcodes_dd_a16 = ops_fpu_dd_a16;
    x86_opcodes_dd_a32 = ops_fpu_dd_a32;
    x86_opcodes_de_a16 = ops_fpu_de_a16;
    x86_opcodes_de_a32 = ops_fpu_de_a32;
    x86_opcodes_df_a16 = ops_fpu_df_a16;
    x86_opcodes_df_a32 = ops_fpu_df_a32;
    x86_opcodes_REPE = ops_REPE;
    x86_opcodes_REPNE = ops_REPNE;
    x86_opcodes_3DNOW = ops_3DNOW;
}


/*
 * Project131 bounded PCem interpreter dispatch.
 * The emulator frame loop schedules this CPU-only slice. No PC machine
 * devices, timers, PIC, NMI, dynarec or external host ports are attached.
 */
extern void pclog(const char *format, ...);
extern int i486_pcem_irq_pending(void);
extern int i486_pcem_irq_acknowledge(void);
extern void i486_pcem_pit_advance(unsigned int cpu_cycles);
extern void x86_int(int vector);

unsigned int i486_pcem_execute_instructions(unsigned int instruction_budget)
{
        unsigned int executed = 0;

        if (!instruction_budget || !x86_opcodes)
                return 0;

        pccache = 0xFFFFFFFFU;
        cycles = 0x7fffffff;

        /*
         * Compare the C translation unit's view of the shared state layout
         * with the C++ bootstrap adapter. A stable object address with
         * different member offsets would explain the apparent CS loss.
         */
        pclog("[I486] DISPATCH STATE=%p SIZE=%u CS_OFF=%u CS_ADDR=%p CS=%04X BASE=%08X EIP=%08X",
              (void *)&cpu_state, (unsigned int)sizeof(cpu_state),
              (unsigned int)offsetof(x86_cpu_state, seg_cs),
              (void *)&cpu_state.seg_cs, cpu_state.seg_cs.seg,
              cpu_state.seg_cs.base, cpu_state.pc);

        /*
         * The first dispatch must start exactly at the 27C64D reset target.
         * Refuse to execute zero-filled expansion RAM if the host reset path
         * or segment state has overwritten the bootstrap transfer.
         */
        static int first_fetch_validated = 0;
        if (!first_fetch_validated)
        {
                uint32_t first_physical;
                uint32_t first_fetch;

                if (CS != 0xF000 || cs != 0x000F0000U || cpu_state.pc != 0x0000E05BU)
                {
                        pclog("[I486] BOOT STATE INVALID: CS=%04X BASE=%08X EIP=%08X",
                              CS, cs, cpu_state.pc);
                        return 0;
                }

                first_physical = cs + cpu_state.pc;
                first_fetch = fastreadl(first_physical);
                if ((first_fetch & 0xFFU) != 0xFAU)
                {
                        pclog("[I486] BIOS FETCH INVALID: PHYS=%08X DWORD=%08X EXPECTED=FAxxxxxx",
                              first_physical, first_fetch);
                        return 0;
                }

                first_fetch_validated = 1;
                pclog("[I486] BIOS FETCH VERIFIED: CS=%04X BASE=%08X EIP=%08X PHYS=%08X BYTE=%02X",
                      CS, cs, cpu_state.pc, first_physical, first_fetch & 0xFFU);
                pccache = 0xFFFFFFFFU;
        }

        while (executed < instruction_budget)
        {
                pic_intpending = i486_pcem_irq_pending();
                if (pic_intpending && (cpu_state.flags & I_FLAG))
                {
                        int vector = i486_pcem_irq_acknowledge();
                        if (vector >= 0)
                        {
                                uint8_t pending_opcode = (uint8_t)fastreadl(cs + cpu_state.pc);
                                cpu_state.oldpc = cpu_state.pc + (pending_opcode == 0xF4 ? 1U : 0U);
                                x86_int(vector);
                                pclog("[I486][PIC] IRQ VECTOR=%02X RETURN=%04X:%08X", vector, CS, cpu_state.oldpc);
                                pccache = 0xFFFFFFFFU;
                        }
                }
                uint32_t physical;
                uint8_t opcode;

                if (cpu_state.abrt)
                {
                        pclog("[I486] EXEC STOP: pending abort=%02X at %04X:%08X",
                              cpu_state.abrt, CS, cpu_state.pc);
                        break;
                }

                cpu_state.oldpc = cpu_state.pc;
                cpu_state.op32 = use32;
                cpu_state.ea_seg = &cpu_state.seg_ds;
                cpu_state.ssegs = 0;
                physical = cs + cpu_state.pc;
                fetchdat = fastreadl(physical);

                if (cpu_state.abrt)
                {
                        pclog("[I486] EXEC FETCH ABORT at %04X:%08X PHYS=%08X",
                              CS, cpu_state.pc, physical);
                        break;
                }

                opcode = (uint8_t)fetchdat;
                fetchdat >>= 8;
                trap = cpu_state.flags & T_FLAG;
                cpu_state.pc++;
                x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
                ++executed;
                i486_pcem_pit_advance(4);

                if (executed <= 16)
                        pclog("[I486] EXEC #%u CS:EIP=%04X:%08X PHYS=%08X OP=%02X NEXT=%08X",
                              executed, CS, cpu_state.oldpc, physical, opcode,
                              cpu_state.pc);

                if (cpu_state.abrt)
                {
                        pclog("[I486] EXEC ABORT=%02X ERROR=%08X AT=%04X:%08X",
                              cpu_state.abrt, abrt_error, CS, cpu_state.oldpc);
                        break;
                }

                if (trap)
                {
                        pclog("[I486] EXEC STOP: single-step trap is not wired");
                        break;
                }
        }

        if (executed)
                pclog("[I486] EXEC BATCH: %u instructions CS:EIP=%04X:%08X",
                      executed, CS, cpu_state.pc);
        return executed;
}
