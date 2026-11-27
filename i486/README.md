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


## Trident TVGA8900D video BIOS

- `BIOS.BIN` is loaded automatically by `i486::init()`, first from the
  process working directory and then from the parent of the executable directory.
- The image must be exactly 64 KiB and start with Option ROM signature `55 AA`.
  Header byte 2 determines the decoded size in 512-byte units; this ROM declares
  a 32-KiB decoded window.
- The declared ROM window is mapped read-only at physical `C0000h` and is visible
  through `i486::read8/read16/read32`. Writes to the window are ignored.
- `load_vga_bios(path)`, `vga_bios_is_loaded()` and `vga_bios_rom_size()`
  provide explicit loading and status.
- The system BIOS remains separate at `F0000h` and the 486 reset alias
  `FFFF0000h`; the Trident ROM does not replace it.
