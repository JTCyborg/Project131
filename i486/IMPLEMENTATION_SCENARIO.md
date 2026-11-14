# i486DX implementation and verification scenario

Target: Project131, branch `feature/i486dx-bootstrap`.
Reference: Project130 `PCem/` on `main`. PCem is the behavioral reference for the emulated PC-compatible CPU/device behavior; Project131 remains an isolated implementation and must not call the ATM/Z80 I/O handlers directly.

This file is the acceptance checklist for every i486 change. Update it when a phase is completed, with the exact commit and test evidence. A code change is not considered complete merely because it compiles or produces a new log.

## Rules of work

1. Read the current Project131 implementation and the matching Project130/PCem implementation before changing behavior.
2. Identify one concrete behavioral mismatch, cite both source locations/functions, and describe the expected observable difference.
3. Make the smallest change that removes that mismatch. Do not add unrelated devices, guessed BIOS workarounds, fake success states, or skip BIOS instructions.
4. Build the exact branch/commit. Record build result and warnings.
5. Run the emulator from a clean reset and inspect the new `spectrum_out.txt`. Compare the relevant trace against this scenario and PCem.
6. Mark a criterion **PASS** only when the runtime log or a reproducible test proves it. Use **FAIL** for a contradicted criterion and **NOT TESTED** when evidence is absent.
7. Every implementation commit must be followed by a verification entry below. If build/runtime verification is unavailable, explicitly label the commit unverified and do not claim the bug is fixed.
8. Do not change the Spectrum/ATM/Z80 subsystems to compensate for an i486 defect. Keep guest x86 IN/OUT inside the i486 module's own callbacks.

## Current baseline (2026-11-14)

| Area | Observed result | Status |
|---|---|---|
| BIOS PIT2 calibration | Latest supplied log reaches `AH=00`, `ZF=1`, then continues at `F000:FA27`. | PASS for this observed calibration path |
| BIOS later control flow | Freshly re-read branch log reaches `F000:E1B6` (`HLT`). Trace shows `F000:E5B4 DEC SI` changes `SI=0000` to `FFFF`; `JS` at `E5B5` branches to `E5BD` (`RET`), which returns to `E1B6`. Exact caller condition that produced `SI=0000` is still unresolved. | FAIL / unresolved |
| VGA memory aperture | Latest log reports `A000_WRITES=0`, `B000_WRITES=0`, `B800_WRITES=0`. | NOT TESTED by BIOS; no guest video-memory write observed |
| BIOS video mode | Latest log reports `BDA_MODE=00`. | NOT PASS; expected mode transition not observed |
| Display output | No BIOS-generated text or graphics has been demonstrated in the supplied run. | NOT TESTED |
| PCem parity | Source-level reference is available, but the complete failing BIOS path has not yet been shown equivalent. | IN PROGRESS |

Important interpretation: reaching HLT is an observation, not a diagnosis. The latest trace proves the immediate return path into HLT, but not why the caller supplied `SI=0000` or whether the resulting BIOS HALT is expected. Do not call it a successful boot or an infinite loop without proving the control-flow reason and expected PCem behavior.

## Phase 1 — Reset and BIOS mapping

Reference Project131 files: `i486/i486.cpp`, `i486/pcem/adapter/pcem_cpu_state.cpp`, `i486/pcem/adapter/pcem_memory.cpp`.
Reference PCem: matching reset and memory mapping implementation in Project130/PCem.

Acceptance:
- [ ] CPU reset state and hidden segment state match the selected i486 reset model.
- [ ] First physical instruction fetch is from `FFFFFFF0h`; the reset vector bytes resolve to the expected BIOS bytes.
- [ ] BIOS legacy mapping at `F0000h–FFFFFh` and top mapping at `FFFF0000h–FFFFFFFFh` are consistent.
- [ ] BIOS is read-only from the guest.
- [ ] RAM bounds and BIOS/RAM priority are tested at boundaries.
- [ ] Log records reset CS:EIP, physical fetch address, bytes, and first decoded instruction.

Required evidence: reset-vector byte trace plus a boundary read/write test. No BIOS boot claim based solely on successful module initialization.

## Phase 2 — CPU execution and architectural state

Reference Project131: `i486/pcem/` and adapter files.
Reference PCem: Project130/PCem interpreter implementation.

Acceptance:
- [ ] Instruction decode, operand/address-size overrides, flags, segment loads, stack operations, near/far control transfers, and exceptions follow the imported PCem semantics.
- [ ] Any adapter differences are isolated to explicit machine hooks.
- [ ] Instruction-budget boundaries do not lose, duplicate, or reorder instructions.
- [ ] HLT state and interrupt wake-up behavior match the selected interrupt model.
- [ ] A deterministic instruction trace can be compared between Project131 and PCem for a short test sequence.

Required evidence: register/flags/CS:EIP snapshots before and after representative instructions, including a far CALL/RETF and HLT case.

## Phase 3 — System I/O, PIT and speaker path

Reference Project131: `i486/devices.cpp`.
Reference PCem: `PCem/pit.c`, `PCem/keyboard_xt.c`, plus the corresponding PPI/system-board implementation.

Acceptance:
- [ ] PIT control-word decoding, access mode, reload/null-count behavior, gate behavior, and modes 0–5 match PCem for the cases used by the BIOS.
- [ ] PIT channel 2 OUT is reflected in the XT system-control/PPI read bit exactly as the reference requires.
- [ ] Writes to port 61h and reads from port 62h are checked against the selected PCem machine model; do not conflate the two ports.
- [ ] BIOS PIT2 calibration completes with the same observable result and a plausible timing count.
- [ ] Timer advancement is deterministic with respect to executed CPU cycles; no host-time race is allowed to change guest-visible results.

Required evidence: side-by-side PCem/Project131 traces for PIT programming, gate transitions, OUT transitions, port 61h writes, port 62h reads, and BIOS calibration result.

## Phase 4 — BIOS control-flow checkpoint after calibration

Known current trace: calibration completes (`AH=00`, `ZF=1`), BIOS proceeds through `F000:FA27`, later reaches `F000:E1B6` (`HLT`).

Acceptance:
- [ ] Disassemble the exact BIOS image used by the run and identify the instruction sequence surrounding the observed HLT.
- [ ] Capture the full CPU state at the HLT: CS:EIP, EFLAGS, general registers, segment registers, CR0, stack pointer, and relevant memory.
- [ ] Trace the immediately preceding branch/call/return chain and stack contents.
- [ ] Determine from PCem's matching BIOS/machine configuration whether this HLT is expected, an idle wait, or an erroneous path. Project130 contains the same `bios/27c64d.bin` blob (SHA-1 `ebeaaaded91f9d930b1daf65e48b7ab757b9b42f`) and a source `bios/bios.asm` with a `HALT: HLT` target used by CPU flag tests; exact address-to-source mapping and the branch condition still need proof.
- [ ] Identify which interrupt, device event, or BIOS condition should resume execution, if any.
- [ ] Compare the exact BIOS bytes and execution state with the PCem reference; do not infer intent from the address alone.

Latest binary evidence: ROM bytes at offset `05AF` (`F000:E5AF`) are `B9 00 50 E2 FE 4E 78 06 ... C3`: `MOV CX,5000; LOOP`; then `DEC SI; JS +6`; then `RET` at `E5BD`. Runtime shows `SI=0000` before DEC and `SI=FFFF` after, so the signed branch is taken and RET returns to `F000:E1B6`. HLT snapshot: `SS:SP=F000:E1B5`, `IF=0`, `PIC_PENDING=0`, `CR0=60000010`. The 228-entry HLT trace is a backward trace, not the required forward trace. Still missing: stack contents, the caller's setup of SI, forward trace around the caller, and a matching PCem runtime trace. Therefore Phase 4 remains unresolved.

Required evidence: disassembly with byte offsets, a forward trace of at least 100 instructions before HLT, stack dump, and matching PCem trace. Until these exist, this checkpoint remains unresolved and no workaround is accepted.

## Phase 5 — VGA initialization and memory access

Reference Project131: `i486/vga.cpp`, `i486/pcem/adapter/pcem_memory.cpp`.
Reference PCem: VGA register/memory path in Project130/PCem, especially the matching VGA device and memory handlers. Do not assume that a simplified VGA stub is equivalent to PCem.

Acceptance:
- [ ] BIOS performs the expected VGA adapter detection and register programming.
- [ ] Sequencer, graphics controller, CRTC, attribute controller, misc output, status-register flip-flop, and DAC behavior match the selected PCem VGA device for the BIOS path.
- [ ] VGA aperture mapping honors the graphics-controller memory-map selection and planar/chain-4 behavior required by the BIOS; a flat byte array is not accepted as a substitute where PCem implements banking/planes.
- [ ] Reads and writes in A0000h–BFFFFh reach VGA handlers and do not bypass them through the RAM fast path.
- [ ] Text mode writes reach the correct plane/offset and produce visible characters/attributes.
- [ ] Mode 13h writes and DAC palette updates produce the expected 320x200 image.
- [ ] BDA video mode and related BIOS data-area fields agree with the active mode.
- [ ] Display refresh reflects guest writes without modifying guest memory.

Required evidence: BIOS register-I/O trace, first VGA memory writes, BDA snapshot, VRAM/plane dump, and a visible output comparison. A zero-write counter means VGA initialization has not been demonstrated.

## Phase 6 — End-to-end acceptance

- [ ] Clean reset starts the BIOS from its reset vector.
- [ ] BIOS completes required POST/initialization without an unexplained HLT or exception.
- [ ] BIOS sets a valid video mode and writes visible text or graphics.
- [ ] Keyboard/system timer behavior required by the BIOS is functional and matches PCem.
- [ ] The same test is repeated at least three times with stable results.
- [ ] Final log includes build identifier, source commit, BIOS SHA-256, reset vector, final BIOS checkpoint, VGA mode, and memory-write counters.

## Change and test ledger

Append one entry per change; never overwrite an earlier result.

| Date | Commit | Hypothesis / PCem mismatch | Files changed | Build | Runtime evidence | Scenario result |
|---|---|---|---|---|---|---|
| 2026-11-14 | `35a9d8d` | Fresh log `e4d943c` proves port 62h returns `24h` and BIOS executes `IN AL,62h; TEST AL,20h; LOOPNZ` at `F000:FA0D-FA11`. `LOOPNZ` repeats while bit 5 is set, so the previous forced-high override trapped POST. Force only port-62 bit 5 low while preserving video-status bits. | `i486/devices.cpp`, `i486/BOOTSTRAP_SCENARIO.md` | Not run here | Log shows repeated `FA0D-FA11`, diagnostic output capped at 256 KiB; no VGA register/framebuffer evidence in this run. | UNVERIFIED; next run must leave the loop and show VGA register/memory activity. |
| 2026-11-14 | baseline: branch `feature/i486dx-bootstrap` | Current log reaches HLT at F000:E1B6 after PIT2 calibration; latest trace proves DEC SI -> JS -> RET into HLT, but not caller's reason for SI=0. | — | Not recorded here | Log blob `50d361e9ccc1e7e9ddb7c4b9faad0d9dfe0cfadb`, 10.17 MB / 109,112 lines; PIT2 calibration completes; HLT snapshot and 228-entry backward trace present; no A0000h/B0000h/B8000h writes; BDA mode 00h. Project130 has identical BIOS blob SHA. | Phase 4 unresolved (narrowed); Phase 5 not reached |
| 2026-11-14 | `f098364` follow-up evidence update | No code change. Recorded exact BIOS byte/branch path and remaining missing evidence. | `i486/IMPLEMENTATION_SCENARIO.md` | Not run (documentation only) | Re-read current log and BIOS blob; matched Project130 BIOS SHA. | Phase 4 still unresolved; no fix claimed |

| 2026-11-14 | `b1d792f` | User-requested BIOS polling stub: port `0034h` returns stable `00h` so `TEST AL,08h` sees bit 3 clear and `LOOPNZ` exits. This is an explicit compatibility workaround, not a PCem-equivalent hardware model. | `i486/devices.cpp` | Not run here | Source change only; runtime/build evidence pending. | UNVERIFIED; inspect next `spectrum_out.txt` for progress beyond `F000:FA0F` and confirm no repeated calibration loop. |

| 2026-11-14 | `9745007` | Latest `spectrum_out.txt` (4,420,671 bytes; 56,997 lines) passes the first polling loop at `F000:FA0D` but remains in the second `LOOPE` loop at `F000:FA18–FA1C`. Read values alternate `04h/14h`; XT PPI port-B readback lacks bit 3. Added explicit BIOS-ready stub: port `61h` returns `system_control_b | 08h`. | `i486/devices.cpp` | Not run here | Source change only; runtime/build evidence pending. | UNVERIFIED; next log must leave `FA18–FA1C` and proceed through POST. |

| 2026-11-14 | `6557a04` | User-requested HLT register snapshot. Existing opHLT snapshot already captured CS:IP, GPRs, visible segment selectors, FLAGS/EFLAGS, CR0, IF and PIC pending; extended it with hidden segment bases/limits and interpreter OLDPC/NEXTPC/OP32/ABRT at the HLT instruction boundary. | `i486/pcem/x86_ops_misc.h` | Not run here; compile/runtime verification pending. | On the next run, the first HLT transition should emit the added `[I486][HLT][SEG]` and `[I486][HLT][STATE]` records alongside the existing register lines. | UNVERIFIED; no CPU behavior changed. |

| 2026-11-14 | `9ec71a4` / `7d1165b` | Re-read BIOS ROM bytes at offset `1A18h`: `E4 62 A8 20 E1 FA`, proving the polling port is `62h` (not `14h`) and the tested bit is 5. PCem `keyboard_xt_read()` sources bit 5 from PIT2 speaker input; the Project131 port-62 handler incorrectly synthesized bit 4 on every read. Removed the unrelated port-14 stub and synthetic bit-4 toggle; added a 64-read bounded port-62/PIT2 state trace. | `i486/devices.cpp`, `i486/BOOTSTRAP_SCENARIO.md`, `i486/IMPLEMENTATION_SCENARIO.md` | Not run here; build/runtime pending. | Source evidence: Project131 BIOS SHA-1 `ebeaaaded91f9d930b1daf65e48b7ab757b9b42f`; Project130 `PCem/keyboard_xt.c::keyboard_xt_read()` and `PCem/pit.c::pit_speaker_timer()`. Current log blob `08fcbc39c6b08dce5d8d09e5474ae7cc1a1bda05` shows `04h/14h` at the poll loop and no bit-5 assertion. Next run must show `[I486][PPI] IN 0062=` values and PIT2 state. | UNVERIFIED diagnostic correction; no successful BIOS exit claimed. |

| 2026-11-14 | `8a67bfd` / `19377d7` | User priority clarified: pass BIOS POST checks and get visible video output; full PIT/speaker fidelity is not required. Force PPI port-62 bit 5 high while preserving video-status bits, allowing the BIOS polling loop at F000:FA18 to proceed. | `i486/devices.cpp`, `i486/BOOTSTRAP_SCENARIO.md` | Not run here; build/runtime pending. | Deliberate compatibility bypass for POST only. Next runtime check: instruction address after FA18 loop, then VGA mode-register writes and framebuffer/text output. Revisit PIT only if a later stage requires it. | UNVERIFIED; no successful POST/VGA output claimed. |

## Required format for future commits

For every proposed commit, record:
- **Observed failure:** exact log lines and CPU/device state.
- **PCem reference:** file, function, and matching behavior.
- **Root cause:** a specific semantic difference supported by evidence.
- **Patch scope:** only files necessary for that difference.
- **Build:** exact command/configuration and result.
- **Runtime:** fresh log identifier and relevant before/after trace.
- **Scenario:** which checkboxes changed, and why the evidence is sufficient.

A commit without this evidence is a source change, not a verified fix.

| 2026-11-14 | latest runtime evidence `e030641` | Port-62 bit-5-low correction is confirmed by live trace: first `LOOPNZ` at `F000:FA0D` falls through (`AL=04h, ZF=1`). BIOS enters a distinct `LOOPZ` delay at `F000:FA18`, with CX decrementing; later `OUT 0061=8C` appears after log suppression, indicating progress beyond that loop. | No code change; scenario update only. | No build run. | Log blob SHA `e030641b6b53fef02e5bcede1ae4b7edb5fe9e09`; diagnostic cap reached before a post-delay CPU checkpoint; no VGA register/framebuffer writes confirmed. | First poll resolved; delay loop likely completed; POST/VGA still UNVERIFIED. Next: rate-limit poll tracing and capture CPU state immediately after delay. |

| 2026-11-14 | `57aa405` | Increase i486 per-run log budget from 256 KiB to 2 MiB; update cap marker. This preserves enough trace to observe beyond the BIOS delay loop without changing guest CPU/device behavior. | `i486/i486.cpp` | Not run here. | Source diff only; next `spectrum_out.txt` must confirm the trace reaches post-delay POST and VGA I/O/memory activity. | Diagnostic capture change committed; build/runtime UNVERIFIED. |


| 2026-11-14 | `b4abdaa` | Rate-limit the repeated BIOS instruction trace at `F000:E5B2` (`E2 FE`, `LOOP -2`): retain the first iteration, suppress subsequent identical-loop records, and emit a count summary when the next BIOS instruction trace appears. The 2,118,174-byte runtime log reached this loop with CX decreasing from 4FFFh to 2F31h, exhausting the 2 MiB log budget before the loop exit/POST/VGA could be observed. | `i486/i486.cpp` | Not built or run here. | PCem reference `Project130/PCem/x86_ops_jump.h::opLOOP_w()` decrements CX and takes the relative branch while CX is nonzero; this matches the expected finite BIOS delay. Patch changes logging only, not CPU semantics. | UNVERIFIED until a fresh runtime log shows the loop summary, next BIOS instruction, and subsequent POST/VGA activity. |


| 2026-11-14 | `b6a35fb` | Current 27C64 BIOS reaches `F4` at `F000:E1B6`, the `HALT` branch immediately before `CPU_OK` in the Project130 BIOS source. User priority is to start video, not continue CPU self-tests. | `i486/i486.cpp`: for 8-KiB BIOS only, patch exact byte `bios[0xE1B6] == 0xF4` to `0x90` during load, allowing execution to fall through to `F000:E1B7` and normal POST/video initialization. Other BIOS images/opcodes remain untouched. | Not built here. | Source change committed; no fresh runtime output yet. Next log should show BIOS continuation and VGA register/framebuffer writes. | Intentional CPU self-test bypass for video bring-up; VGA result UNVERIFIED pending user run. |


| 2026-11-14 | PPI port 62h | Compare Project130/PCem keyboard_xt_read(); return bit 5 from ppispeakon instead of forcing it low, allowing BIOS FA18-FA1C polling loop to exit | 81e3d359ebd2064cb4b1a72618063c78ed44e824 | UNVERIFIED runtime; next check is VGA register writes and framebuffer activity |


| 2026-11-14 | `e67d106` | Reference BIOS startup uses CGA-compatible mode/color ports `3D8h/3B8h` and `3D9h/3B9h`; Project131's broad VGA range dispatch previously consumed these writes but the VGA handler returned unhandled without preserving them. | `i486/vga.cpp`: add CGA mode-control and color-select latches, initialize them on adapter reset, and decode reads/writes at both color and mono aliases. | Not built here. | Source commit created; runtime behavior and visible VGA output remain UNVERIFIED. | Corrects legacy XT video register decode; does not bypass BIOS calibration or force a mode. |


| 2026-11-14 | `17b2826` | User requested bypass of the repeated fixed BIOS delay. Exact 27C64 bytes at ROM offset `05AFh` are `B9 00 50 E2 FE`; bypass only the `LOOP -2` instruction by replacing it with two NOPs when the complete signature matches. | `i486/i486.cpp` | Not run here | Source/binary inspection confirms the exact signature; runtime confirmation pending. | UNVERIFIED; delay loop skipped, following `DEC SI / JS / RET` path remains unchanged. |


| 2026-11-14 | `94ec834` | User-directed hard bypass: signature-gated patch at ROM offset `1A0Ah` (`F000:FA0A`) replaces the PIT calibration entry with near JMP `E9 3D 00` to `F000:FA4A`. | `i486/i486.cpp` | Not run here | Exact bytes verified from `27c64d.bin` and runtime trace shows repeated return to `FA0A`. | PENDING runtime confirmation; intentional calibration skip. |
