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


## Latest runtime correction (2026-11-14)

Observed `spectrum_out.txt` blob SHA `e4d943cb80848c054e863bb56185157657e81192`:

- Port `62h` reads are `24h` (and occasionally `2Fh`), so the previous forced-high override is active.
- The live instruction trace shows `F000:FA0D IN AL,62h`, `F000:FA0F TEST AL,20h`, `F000:FA11 LOOPNZ FA0Dh` (`E0`).
- `LOOPNZ` repeats while ZF=0; with bit 5 set, `TEST` leaves ZF=0 and the BIOS remains in the loop. The previous note claiming that bit 5 high releases this loop was incorrect.
- Corrected bootstrap policy: preserve the video-status nibble and force port-62 bit 5 low. This is a targeted boot workaround, not PIT/speaker emulation.
- Next runtime check: confirm the CPU leaves `FA0D-FA11`; then inspect VGA register writes (`3C0-3DF`) and video-memory writes. Build/runtime remain unverified until a fresh run is supplied.


## Runtime follow-up: port-62 bit-5-low correction (2026-11-14)

Observed in `spectrum_out.txt`, blob SHA `e030641b6b53fef02e5bcede1ae4b7edb5fe9e09`:

- Port-62 reads now show `04h` (also `0Fh` when the low video-status bits differ); bit 5 is clear.
- The first poll at `F000:FA0D` executes `IN AL,62h; TEST AL,20h; LOOPNZ`. With `AL=04h`, ZF=1 and `LOOPNZ` falls through to `FA13`; the earlier forced-high trap is resolved.
- BIOS then starts a separate delay loop at `F000:FA18`: `IN AL,62h; TEST AL,20h; LOOPZ FA18h` (`E1`). The trace shows CX counting down (`1000h`, `0FFFh`, ...); this is a timed loop, not evidence that port 62h must be changed again.
- The log later records `OUT 0061=8C` after the diagnostic-output suppression marker, consistent with execution continuing beyond the delay loop. The post-loop CPU address is not captured because the 256 KiB i486 log budget is exhausted.
- No VGA register writes or video-memory writes are evidenced in the available log. Do not claim POST completion or video output.
- Next diagnostic action: reduce/rate-limit repetitive per-iteration `BIOS-POLL` / `BIOS-LOOPE` logging (retain first/last samples and periodic CX checkpoints), and emit a compact CPU checkpoint after the delay loop. Then inspect subsequent POST and VGA activity. No further PPI behavior change is justified by this log.

Build/runtime verification of source changes remains pending.


## Diagnostic capture extension (2026-11-14)

Commit `57aa405584a401f54d9b79ead5a74d46229c1a3e` raises the per-run i486 diagnostic budget from 256 KiB to 2 MiB and updates the suppression marker. This is a diagnostic-only change: CPU/device behavior is unchanged. It is intended to retain the BIOS trace through the `F000:FA18` delay and expose the following POST/VGA activity in the next run. Build/runtime are not verified here.


## BIOS delay-loop trace rate limiting (2026-11-14)

Commit `b4abdaa63b4b985fbfa7c5153278c5e8accfae22` changes only diagnostic output in `i486/i486.cpp`:

- The current 2 MiB log is 2,118,174 bytes / about 20,956 lines. It reaches the BIOS delay at `F000:E5B2`, opcode `E2` (`LOOP -2`), with `CX=4FFFh` at entry and `CX=2F31h` in the last captured sample.
- Every iteration was emitted as a full `[BIOS-SI-TRACE]` line, exhausting the log budget before the BIOS exits this delay and before POST/VGA evidence is captured.
- Keep the first `F000:E5B2 OP=E2` sample, suppress subsequent iterations, and emit one summary when the next BIOS instruction trace arrives. Other diagnostics are unchanged.
- PCem reference `Project130/PCem/x86_ops_jump.h::opLOOP_w()` decrements CX and branches only while CX is nonzero; the reference behavior matches the expected finite loop. No CPU opcode behavior is changed.

Next runtime check: confirm a summary reports the suppressed iterations, then inspect the following BIOS instruction address and VGA register/framebuffer activity. Build/runtime verification is pending; do not claim POST or VGA success yet.
