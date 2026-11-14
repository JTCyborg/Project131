# PCem source snapshot

These files are unmodified reference imports from `JTCyborg/Project130`, path `PCem/`, branch `main`. The original CPU/FPU files were fetched on 2026-11-13; the additional opcode implementation headers were fetched on 2026-11-14.

## CPU and FPU translation units / core headers

- `386.c`
- `386_common.c`
- `386_common.h`
- `386_ops.h`
- `x86.h`
- `x86_flags.h`
- `x86_ops.h`
- `x86seg.c`
- `x87.c`
- `x87.h`
- `x87_ops.h`
- `x87_timings.c`
- `x87_timings.h`

## Opcode implementation headers

- `x86_ops_3dnow.h`
- `x86_ops_arith.h`
- `x86_ops_atomic.h`
- `x86_ops_bcd.h`
- `x86_ops_bit.h`
- `x86_ops_bitscan.h`
- `x86_ops_call.h`
- `x86_ops_cyrix.h`
- `x86_ops_flag.h`
- `x86_ops_fpu.h`
- `x86_ops_inc_dec.h`
- `x86_ops_int.h`
- `x86_ops_io.h`
- `x86_ops_jump.h`
- `x86_ops_misc.h`
- `x86_ops_mmx.h`
- `x86_ops_mmx_arith.h`
- `x86_ops_mmx_cmp.h`
- `x86_ops_mmx_logic.h`
- `x86_ops_mmx_mov.h`
- `x86_ops_mmx_pack.h`
- `x86_ops_mmx_shift.h`
- `x86_ops_mov.h`
- `x86_ops_mov_ctrl.h`
- `x86_ops_mov_seg.h`
- `x86_ops_movx.h`
- `x86_ops_msr.h`
- `x86_ops_mul.h`
- `x86_ops_pmode.h`
- `x86_ops_prefix.h`
- `x86_ops_rep.h`
- `x86_ops_ret.h`
- `x86_ops_set.h`
- `x86_ops_shift.h`
- `x86_ops_stack.h`
- `x86_ops_string.h`
- `x86_ops_xchg.h`
- `x87_ops_arith.h`
- `x87_ops_loadstore.h`
- `x87_ops_misc.h`

## Project131 adapter files (modified, not upstream imports)

- `adapter/386_opcode_tables.c` — extracted opcode-table translation unit; based on PCem `386_dynarec.c` and `386.c`, with dynarec removed and i486 prefetch callbacks disabled.
- `adapter/pcem_memory.h` — narrow memory/MMU interface required by the interpreter.
- `adapter/pcem_memory.cpp` — initial identity-mapped RAM and BIOS lookup adapter; paging is not implemented yet.
- `adapter/interpreter_tables.h` — declaration for fixed interpreter-table selection.

These adapter files are not upstream files. The table translation unit is not included in the build yet and has not been compiled.

The upstream PCem project publishes its source under GPL-2.0; the full license text is in `COPYING`. Preserve all copyright and license notices. Any future modifications to imported files must be marked with a prominent change notice and date.

## Integration status

**Reference snapshot only — not part of the Project131 build yet.**

Do not add PCem translation units to the Project131 build until the PCem machine dependencies have been isolated. In particular, `386.c` expects PCem memory fast paths, timer/PIC/NMI globals and reset helpers; `x86seg.c` references AT/motherboard/configuration facilities. The interpreter must not be linked against Project131's Spectrum memory or ATM port handlers.

The intended integration is a fixed i486DX/33 interpreter configuration, integrated x87 FPU, and no dynarec. Keep platform adaptation in the `i486/` subsystem.
