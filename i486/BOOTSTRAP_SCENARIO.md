# i486 bootstrap diagnostic scenario

Status: instrumentation committed; runtime verification pending.

Reference implementation: `JTCyborg/Project130`, directory `PCem`.
Target: `JTCyborg/Project131`, branch `feature/i486dx-bootstrap`.

## Rules

- Treat Project130/PCem as the behavioral reference for CPU and BIOS execution.
- Do not change CPU, BIOS, VGA, or device behavior while collecting this diagnostic.
- Do not infer a fault from a large or incomplete log.
- Keep the first-HLT register snapshot one-shot.
- Keep i486 module logging bounded to 256 KiB per emulator run.

## Test sequence

- [ ] Start a fresh Project131 emulator run with the current i486 BIOS image.
- [ ] Confirm the reset-vector bytes are logged as `[I486] RESET_BYTES=...`.
- [ ] Confirm the CPU reaches BIOS execution at the expected reset path.
- [ ] Capture the first `[I486][PRE-HLT]` block, emitted immediately before dispatching the first HLT opcode.
- [ ] Confirm the dump includes CS:EIP, hidden CS base/limit/access, EFLAGS/FLAGS, CR0/CR2/CR3, all eight general registers, segment selectors/bases, GDTR, IDTR, LDTR, TR, IF, CPL, USE32 and STACK32.
- [ ] Confirm repeated Spectrum frames do not repeat the PRE-HLT block.
- [ ] Confirm i486 module log messages stop after the 256 KiB per-run budget and emit the suppression marker once.
- [ ] Compare the captured instruction path and register state with the corresponding Project130/PCem execution.

## Diagnostic interpretation

The first-HLT snapshot records the opcode address before the interpreter advances EIP or executes the HLT handler. It is a diagnostic observation only; it does not change guest-visible CPU state.

The 256 KiB limit applies to messages routed through `i486::log()`. If other subsystems append directly to `spectrum_out.txt`, their output is outside this limit and must be checked separately.

## Required evidence before a behavioral fix

Record the BIOS image identity, build identifier, complete first-HLT snapshot, preceding BIOS/CPU trace, and matching Project130/PCem trace. Do not alter instruction semantics or device behavior until a concrete divergence is identified.

## Current result

Runtime build and test have not been run by this commit. The scenario remains **pending runtime verification**.

## Latest runtime observation (2026-11-14)

Observed in `spectrum_out.txt` on `feature/i486dx-bootstrap`:

- The BIOS does not reach HLT: no `HLT` or `[I486][PRE-HLT]` marker is present.
- Execution repeatedly returns to `F000:FA18`: `IN AL,14h`, `TEST AL,imm8`, `LOOPZ`. The reported CX value decreases while the loop continues.
- The log contains 2,865 `[I486][BIOS-POLL]` records. These records continue after the 256 KiB `i486::log()` suppression marker, so this diagnostic stream is not governed by that logger budget.
- Source inspection confirms `i486::in8()` routes non-VGA reads to `devices::in8()`. In `devices.cpp`, port `14h` is not in the handled-port dispatch list; the default read result is `FFh`. This does not match the AL values shown in the BIOS-POLL excerpt (typically `14h`/`04h`). Therefore the BIOS-POLL AL field cannot yet be treated as proof of the actual value returned by the CPU I/O read.

## Next required diagnostic

- Locate the exact producer of `[I486][BIOS-POLL]` and establish whether its AL field is sampled before or after the IN instruction.
- Add a bounded counter at that producer (maximum 64 BIOS-POLL records per run), without changing guest-visible I/O behavior.
- Capture the actual value returned by `devices::in8(0x14)` at the CPU I/O boundary and compare the same instruction path against Project130/PCem.
- Do not add a port-14h device response or alter BIOS/CPU semantics until the AL discrepancy is resolved.

Runtime build/test for this observation has not been run in this environment.
