# 486 ZXBus module — staged implementation

## Stage 1: independent 486 PC

Use the complete PCem source at the pinned upstream revision
`b3d6ab4cd5016e459832a066e1bea5dd78605715`. Build and run the standalone
PCem frontend with the Award SiS 496/497 / Intel 486DX2 / 16 MiB profile,
S3 ViRGE primary VGA and optional Voodoo accelerator.

The Stage 1 launcher is a separate PCem process. It establishes a real
standalone PC machine implementation before any ZXBus coupling is attempted.
BIOS ROM images are not included; legally obtained motherboard and video ROMs
must be supplied by the operator.

## Stage 2: ZXBus interconnect

Only after a successful BIOS POST, implement the Spectrum-side and PC-side
controller endpoints, a documented register map, memory windows, command and
status exchange, interrupt lines, reset control, and block transfers. Do not
share the machines' private RAM or conflate their video devices.

## Validation boundary

The submodule is pinned to the upstream PCem commit. Project131's native
application build and a BIOS POST have not yet been verified in this change.
