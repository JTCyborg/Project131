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

Observed in `spectrum_out.txt` (blob SHA `08fcbc39c6b08dce5d8d09e5474ae7cc1a1bda05`):

- The BIOS executes the polling loop at `F000:FA18` and does not reach HLT in this run.
- The exact BIOS bytes at ROM offset `1A18h` are `E4 62 A8 20 E1 FA`: `IN AL,62h`; `TEST AL,20h`; `LOOPZ FA18h`. The loop is polling port `62h`, not `14h`.
- The previous port-`14h` bootstrap stub was based on an incorrect port identification and did not affect this BIOS read. It has been removed.
- The log shows port-`62h` values alternating between `04h` and `14h`, while bit 5 remains clear. Source inspection found that the current port-`62h` handler synthesized bit 4 on every read, which is not the PCem behavior for the VGA/FPU branch.
- PCem reference: `PCem/keyboard_xt.c::keyboard_xt_read()` returns the video-status nibble and ORs bit 5 from `ppispeakon`; the speaker input is driven by PIT channel 2 via `PCem/pit.c::pit_speaker_timer()`.
- The latest log includes PIT2 channel-2 OUT transitions before the BIOS polling loop, but the BIOS poll trace does not show bit 5 set. The precise PIT2/port-`62h` state during the loop still needs runtime capture.

## Current diagnostic patch

Commit `9ec71a4bb68f161b4c4590bee93483cfb0842fb7`:

- Removes the incorrect port-`14h` stub and its dispatch entry.
- Removes the synthetic bit-4 toggle from port `62h`.
- Keeps the VGA/FPU status nibble and returns bit 5 strictly from the PIT2 speaker-input state.
- Adds a one-shot-bounded trace (maximum 64 reads per device reset) containing the actual port-`62h` return value, port-`61h` latch, speaker input, PIT2 OUT, gate and phase remaining.

Build and runtime verification are pending. On the next run, inspect the first `[I486][PPI] IN 0062=` records and correlate them with PIT2 OUT transitions. Do not add another guessed port stub.


## Boot-progress compatibility override (2026-11-14)

Commit `8a67bfd6237d20574dd10b4a40ccb731dd6f5457` changes the XT PPI port-`62h` read path for the current BIOS bootstrap:

- Preserve the VGA video-status bits derived from PPI port-B bit 3.
- Force input bit 5 high so the BIOS speaker-input polling loop at `F000:FA18` exits and POST can continue.
- This deliberately does not emulate speaker/PIT2 timing. Audio fidelity is outside the current milestone.
- Next milestone: observe the first instruction after the polling loop, continue POST to VGA mode setup, then capture the first framebuffer/text output. Do not spend time perfecting PIT2 unless a later boot/video stage demonstrably depends on it.

Build and runtime verification are pending; this is a targeted POST bypass, not a claim that VGA output is already working.
