# PCem upstream source import

- Project: https://github.com/sarah-walker-pcem/pcem
- Branch: `dev`
- Exact upstream commit: `b3d6ab4cd5016e459832a066e1bea5dd78605715`
- License: GNU General Public License, version 2 (see `COPYING`).
- Imported files are kept under `third_party/pcem/` without local edits.

## CPU implementation files

- `src/cpu/386.c`
- `src/cpu/386_common.c`
- `src/cpu/cpu.c`
- `src/cpu/cpu_tables.c`
- `src/cpu/x86seg.c`
- `src/cpu/x87.c`
- `src/cpu/x87_timings.c`

## Private CPU headers

All 50 headers from upstream `includes/private/cpu/` are imported verbatim,
including the 386 opcode tables, x86 operation headers, x87 operations and
timing definitions.

## Integration status

The upstream sources are staged for porting. They are **not yet added to the
Project131 build**, and no claim is made that they compile against Project131.
PCem's CPU implementation depends on its machine-wide globals and services
(memory access, I/O, timers, PIC, NMI, FDC, logging, model configuration and
code-generation infrastructure). The next work is to replace those dependencies
with a per-instance PC context and callbacks to the isolated ATM ZXBus PC board.

Do not remove upstream license notices. Any distributed binary or source
release containing these files must comply with GPL-2.0 and preserve notices.
