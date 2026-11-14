# PCem i486 interpreter porting notes

Reference: `JTCyborg/Project130/PCem`, branch `main`. The reference tree is described by Project130 as a read-only CPU/VGA reference. Keep the Project131 port isolated under `i486/`; do not link Project130 runtime code or modify the ATM/Z80/Spectrum subsystems.

## Confirmed interpreter path

The PCem interpreter entry point is `exec386(int cycs)` in `386.c`. It dispatches through `x86_opcodes`, which are populated from the `ops_386` and `ops_386_0f` tables. The interpreter also uses the 16/32-bit x87 opcode tables and REP tables. `386_common.c` contains shared interrupt, privilege-check and arithmetic helpers. `x86seg.c` contains the protected-mode segment, exception, task-switch and SMI implementation; it is not safe to replace with a small segment-register shim.

## Dependencies that must be adapted, not copied blindly

| Reference source | Role | Porting constraint |
| --- | --- | --- |
| `386.c` | CPU interpreter loop | Requires fast memory access, timer budget, PIC/NMI state, exception/reset helpers and opcode tables. It also references PCem machine globals in debug output. |
| `386_common.c/.h` | Shared x86 operations | Preserve the architectural implementation; replace only machine-facing memory/PIC/reset hooks. |
| `x86seg.c` | Segmentation, protected mode, exceptions, task switching, SMI | Current source includes `config.h`, `paths.h`, `i440bx.h` and uses `AT`; isolate those model-specific hooks. |
| `x86.h`, `x86_flags.h` | CPU state and flag helpers | Keep the PCem data layout and semantics consistent across all imported translation units. |
| `386_ops.h` | 386 opcode implementations | Large inline implementation; retain its matching helper/API expectations. |
| `x87.c`, `x87.h`, `x87_ops.h`, `x87_timings.*` | Integrated FPU | Keep the built-in FPU tables; replace the dynarec-only rounding-mode hook with an interpreter-safe implementation. |
| `cpu.c` | PCem model setup and opcode wiring | Do not import wholesale: it depends on motherboard, PCI, model and dynarec state. Extract only fixed i486DX/33 initialization. |
| `mem.c`, `ibm.h` | Physical memory, MMU, lookup tables and machine globals | Do not import wholesale: separate the MMU/lookup engine from PCem RAM/ROM and machine mappings. |
| `timer.c`, `pic.c`, `nmi.c` | Timing and interrupts | Provide a minimal compatible platform adapter first; no physical ATM ports are accessed by the guest CPU. |

## Opcode-table extraction (Project131 adapter)

PCem defines the interpreter opcode tables by including `386_ops.h` from `386_dynarec.c`, not from `386.c`. Importing that whole translation unit would also pull in the dynarec backend. Project131 now has a separate table translation unit at `i486/pcem/adapter/386_opcode_tables.c`. It reuses the PCem 386 addressing helpers and opcode handlers, selects the `ops_386` / `ops_386_0f` tables and integrated x87 tables, and disables the legacy 286/386 prefetch-queue callbacks.

The table adapter is **not yet in either build system** and has not been compiled. Its narrow memory contract is declared in `i486/pcem/adapter/pcem_memory.h`; the next required work is the isolated RAM/MMU/lookup implementation and the remaining CPU platform hooks. Do not link it until those interfaces are implemented and verified.

## Reset-vector correction

PCem's `x86seg_reset()` currently selects the conventional AT reset state (`F0000h` base) when `AT` is enabled. A hardware-reset i486DX must instead begin with hidden CS base `FFFF0000h`, EIP `FFF0h`, yielding physical fetch address `FFFFFFF0h`. The Project131 BIOS mapping already reserves that high 64-KiB window. The port must explicitly initialize and test the hidden CS state; merely mapping the BIOS high is insufficient.

## Fixed first target

- CPU: Intel i486DX/33, integrated FPU.
- Execution: PCem interpreter only; dynarec excluded.
- Memory: isolated physical RAM and BIOS mapping owned by `i486/`.
- I/O: isolated guest port callbacks; never call the Z80/ATM output handler.
- First acceptance test: reset fetch at `FFFFFFF0h`, then a tiny ROM test for 16/32-bit integer instructions, OUT and HLT.

## Import/licensing status

The user approved continuing with a GPL-2.0-based integration. The full upstream license is now included at `i486/pcem/COPYING`, and the unmodified PCem interpreter/FPU source snapshot is stored under `i486/pcem/`; see `i486/pcem/UPSTREAM.md` for its manifest. Preserve upstream notices. Any modified imported source file must carry a prominent change notice and date. The imported files remain excluded from the build until their PCem machine dependencies are adapted.
