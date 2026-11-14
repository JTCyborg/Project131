# PCem source snapshot

These files are an unmodified reference import from `JTCyborg/Project130`, path `PCem/`, branch `main`, fetched on 2026-11-13:

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

The upstream PCem project publishes its source under GPL-2.0; the full license text is in `COPYING`. Preserve all copyright and license notices. Any future modifications to imported files must be marked with a prominent change notice and date, as required by GPL-2.0.

## Integration status

**Reference snapshot only — not part of the Project131 build yet.**

Do not add these translation units to the Project131 `Makefile` until the PCem machine dependencies have been isolated. In particular, `386.c` expects PCem memory fast paths, timer/PIC/NMI globals and reset helpers; `x86seg.c` references AT/motherboard/configuration facilities. The interpreter must not be linked against Project131's Spectrum memory or ATM port handlers.

The intended integration is a fixed i486DX/33 interpreter configuration, integrated x87 FPU, and no dynarec. Keep platform adaptation in the `i486/` subsystem.
