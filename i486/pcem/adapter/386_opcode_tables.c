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
