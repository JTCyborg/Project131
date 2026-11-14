# PCem i486 interpreter porting notes

Reference: `JTCyborg/Project130/PCem`, branch `main`. The reference tree is described by Project130 as a read-only CPU/VGA reference. Keep the Project131 port isolated under `i486/`; do not link Project130 runtime code or modify the ATM/Z80/Spectrum subsystems.

## Project131 module boundary

Project131 does **not** emulate a second IBM PC. The i486DX is an ATM expansion compute module, not a standalone PC platform. Keep the module limited to the CPU, its private RAM, BIOS ROM, and the ATM-facing module transport/debug path. Do not add VGA, IDE, floppy, serial, PC keyboard controller, RTC, PCI/ISA, or a general PC chipset merely because the upstream PCem source supports them. Any guest I/O port access must stay inside the i486 module's isolated I/O callbacks; unsupported ports are logged/treated as unimplemented until an actual ATM-side requirement is demonstrated. Never forward guest IN/OUT directly to the Spectrum/ATM port handlers.

PIC/PIT/timer references in the upstream interpreter are implementation dependencies, not a requirement to instantiate those hardware devices. Prefer an instruction-budget/cycle adapter with external interrupts disabled for the initial BIOS bring-up. Add interrupt-controller or timer behavior only if the selected BIOS path demonstrably requires it.

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
| `timer.c`, `pic.c`, `nmi.c` | Timing and interrupt services expected by the full PCem loop | Do not import these PC devices. Replace the loop dependencies with local cycle accounting and disabled external IRQ/NMI handling for the initial bring-up. Add an interrupt source only when required by a demonstrated guest workload. |

## Opcode-table extraction (Project131 adapter)

PCem defines the interpreter opcode tables by including `386_ops.h` from `386_dynarec.c`, not from `386.c`. Importing that whole translation unit would also pull in the dynarec backend. Project131 now has a separate table translation unit at `i486/pcem/adapter/386_opcode_tables.c`. It reuses the PCem 386 addressing helpers and opcode handlers, selects the `ops_386` / `ops_386_0f` tables and integrated x87 tables, and disables the legacy 286/386 prefetch-queue callbacks.

The table adapter exists at `i486/pcem/adapter/386_opcode_tables.c`, but is **not yet in either build system** and has not been compiled. It includes `386_ops.h` and depends on the complete matching PCem helper/state contract; adding this file alone will not produce an executable CPU. Its narrow memory contract is declared in `i486/pcem/adapter/pcem_memory.h`.

The first memory adapter is now implemented in `i486/pcem/adapter/pcem_memory.cpp` and included in both the Makefile and `msvc/unreal.vcxproj`. It builds 4-KiB direct lookup entries for module RAM, read-only low BIOS alias `F0000h–FFFFFh`, and high reset BIOS alias `FFFF0000h–FFFFFFFFh`. Slow reads/writes route through the isolated `i486::read/write` API. This stage supports identity-mapped real mode (and protected mode with paging disabled) only; page-table translation and page-fault reporting are still pending. The memory adapter has not yet been compiled in the user's environment.

The CPU reset/state adapter is now present at `i486/pcem/adapter/pcem_cpu_state.cpp`. It defines `cpu_state`, initializes the 486 hidden reset CS state and provides a temporary reset-vector FAR-JMP bootstrap. That bootstrap is a diagnostic transition only; it does not execute the BIOS instruction through PCem.

### Current dependency audit (2026-11-14)

The opcode-table adapter's actual include chain is `386_opcode_tables.c` → `386_ops.h` → the `x86_ops_*.h` instruction families. The table translation unit also requires the `x86.h` state contract, `x86_flags.h`, `386_common.h` inline memory/ModRM helpers, and all referenced globals/functions to have exactly one definition. The `x87.h` header currently contains global definitions (`x87_pc_off`, `x87_op_off`, `x87_pc_seg`, `x87_op_seg`) rather than declarations, so it must not be included indiscriminately from multiple translation units without first correcting its ownership model.

The opcode handlers use the following shared CPU state beyond the register file: `use32`, `stack32`, `rmdat`, `easeg`, `eal_r/eal_w`, `mod1add/mod1seg`, `znptable8`, `cycles`, `insc`, `pccache/pccache2`, and exception/abort helpers. Some instruction families also require protected-mode helpers and x87 state. These are not satisfied by the current memory and reset adapters alone.

The upstream `exec386()` loop is not suitable for direct inclusion: it also drives PCem timer processing, PIC/NMI/SMI and machine logging. The Project131 runner must be a separate bounded interpreter loop using the real PCem opcode dispatch and instruction handlers, local cycle accounting, and an explicit stop on unimplemented exception paths. Guest `IN/OUT` must terminate at module-local callbacks; no callback may forward to the emulator's Z80/ATM port handlers. External IRQ/NMI remain disabled for this initial bring-up.

**Current verified state:** BIOS loading and reset-vector byte inspection are logged; the temporary bootstrap correctly changes the visible execution location to `F000:E05B`. No PCem opcode handler has yet been executed. The opcode table adapter and this dependency audit are not compiled or runtime-verified. The next code change must establish the shared-state ownership and required real-mode helpers before adding the table adapter to either build system.

## Reset-vector correction

PCem's `x86seg_reset()` currently selects the conventional AT reset state (`F0000h` base) when `AT` is enabled. A hardware-reset i486DX must instead begin with hidden CS base `FFFF0000h`, EIP `FFF0h`, yielding physical fetch address `FFFFFFF0h`. The Project131 BIOS mapping already reserves that high 64-KiB window. The port must explicitly initialize and test the hidden CS state; merely mapping the BIOS high is insufficient.

## Fixed first target

- CPU: Intel i486DX/33, integrated FPU.
- Execution: PCem interpreter only; dynarec excluded.
- Memory: isolated physical RAM and BIOS mapping owned by `i486/`.
- BIOS loader accepts a raw 8-KiB 27C64 image (mapped at offset `E000h` in the 64-KiB BIOS aperture) or a full 64-KiB image. Pass the image path as `-i486bios <path>`; the loader logs the mapped size and the five reset-vector bytes. The 8-KiB mapping places file offset `1FF0h` at physical `FFFFFFF0h`.
- I/O: isolated guest port callbacks; never call the Z80/ATM output handler. Unsupported accesses are diagnostic events, not a reason to add a PC peripheral.
- First acceptance test: dispatch the actual BIOS reset-vector far jump (`EA 5B E0 00 F0`) from `FFFFFFF0h` through the PCem interpreter, then continue through real BIOS instructions with instruction/CS:EIP tracing. The current manual bootstrap does not satisfy this acceptance test.
- The initial target deliberately has no VGA, IDE, floppy, serial, keyboard controller, RTC, PIC, or PIT device models. The ATM/emulator owns host-facing resources; the i486 module does not create a second PC.

## Import/licensing status

The user approved continuing with a GPL-2.0-based integration. The full upstream license is now included at `i486/pcem/COPYING`, and the unmodified PCem interpreter/FPU source snapshot is stored under `i486/pcem/`; see `i486/pcem/UPSTREAM.md` for its manifest. Preserve upstream notices. Any modified imported source file must carry a prominent change notice and date. The imported files remain excluded from the build until their PCem machine dependencies are adapted.
