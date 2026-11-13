# Intel i486DX expansion module

This directory is the isolated platform layer for the planned Intel i486DX/33
module. It deliberately does not use the Spectrum/ATM banked-memory API.

## Initial platform layer

- Independent 16 MiB RAM allocation (configurable at initialization).
- Separate 64 KiB BIOS image.
- BIOS read-only mapping at F0000h-FFFFFh and FFFF0000h-FFFFFFFFh.
- Little-endian physical memory accessors.
- Separate 8/16/32-bit I/O port interface.

## Current status

This is only the platform/bootstrap layer. It does **not** yet execute x86
instructions. The PCem interpreter, x87 tables, reset/segment logic, MMU,
interrupt controller, and timer integration are the next implementation
stage. The selected CPU model remains Intel i486DX/33 with integrated FPU;
dynarec is intentionally excluded.

The initial BIOS loader expects exactly 64 KiB. The 486 reset fetch at
FFFFFFF0h resolves to BIOS offset FFF0h.

## Integration constraints

- Keep this module independent from the Spectrum/ATM banked-memory API.
- Do not route x86 IN/OUT through the Z80/ATM output handler.
- Import PCem code with its original copyright and license notices.
- Do not import PCem motherboard, PCI/ISA, video, or dynarec subsystems.
