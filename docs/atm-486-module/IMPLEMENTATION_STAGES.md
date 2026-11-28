# 486 ZXBus module — staged implementation

## Stage 1: independent 486 PC

The standalone machine profile is pinned to PCem revision
`b3d6ab4cd5016e459832a066e1bea5dd78605715`: Award SiS 496/497, Intel
i486DX2/66, 16 MiB RAM, S3 ViRGE primary VGA and 3DFX Voodoo Graphics
as a separate 3D accelerator.

Build and launch with `tools/build_486_pcem.ps1` and
`tools/run_486_pcem.ps1`. The launcher loads `i486/pcem_486.cfg`.
PCem ROM images are not included; the motherboard BIOS and S3 ViRGE Option
ROM must be supplied in the portable ROM directory described in
`PCEM_MACHINE_PROFILE.md`.

Stage 1 currently uses PCem as a separate process. It establishes a standalone
PC baseline before ZXBus coupling; it is not yet an in-process 486 device
inside Project131.

## Stage 2: ZXBus interconnect

Only after a successful BIOS POST, implement the Spectrum-side and PC-side
controller endpoints, a documented register map, memory windows, command and
status exchange, interrupt lines, reset control, and block transfers. Do not
share the machines' private RAM or conflate their video devices.

## Validation boundary

The PCem submodule is pinned to the upstream revision. Project131's native
application build and a BIOS POST have not yet been verified in this change.
