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
