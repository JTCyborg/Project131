# ATM ZXBus i486DX expansion module

This directory contains the isolated PC platform layer for the ATM ZXBus
486DX/33 expansion module. It has its own physical address space and does not
use the Spectrum/ATM banked-memory API.

## Implemented in this stage

- Independent 16 MiB RAM allocation.
- Separate 64 KiB system BIOS image and reset-vector alias.
- Separate Trident TVGA8900D Option ROM at physical C0000h.
- Little-endian physical memory accessors.
- Separate 8/16/32-bit x86 I/O port interface.
- PCI Configuration Mechanism #1 at CF8h-CFFh.
- Initial SiS 496/497 host/ISA bridge PCI function at bus 0, device 5,
  function 0 (vendor 1039h, device 0496h, class 0600h).
- Dual 8259A PIC command/data ports with ICW1-ICW4, OCW1-OCW3,
  IRR/ISR selection, EOI, fixed/rotating priority, IRQ edge latching,
  master/slave cascade, pending detection and interrupt-vector acknowledge.
- 8253 PIT channel programming, counter latching and read/write sequencing;
  channel 0 supports one-shot mode 0 and periodic modes 2/3 with IRQ0 generation.
- Initial register blocks for CMOS index/data ports and 8237 DMA register storage.
- The module I/O path dispatches chipset/legacy PC ports before the Trident
  ISA VGA adapter. Unclaimed ports return FFh / ignore writes.

## Important implementation boundary

This is **not yet a bootable PC**. The PCI bridge identity/configuration and
legacy I/O register interfaces are an initial board skeleton. The PIC now arbitrates IRQ requests and exposes an acknowledge/vector API,
but is not yet wired to an executing CPU core; PIT channel 0 still needs an external clock source calling pit_advance(), the
DMA register file does not perform transfers, and CMOS does not yet have a
time source. Keyboard controller, FDC, IDE, serial, parallel, speaker, PCI/ISA
enumeration and interrupt routing still need complete device models.

Most importantly, no x86 instruction execution is connected yet. The PCem
486 interpreter, x87, segmentation, paging/MMU, exception/interrupt delivery,
prefetch/timing and reset logic remain to be ported and integrated. The module
must not be described as a complete 486 implementation until those pieces and
the device set are working together.

## PCem source baseline

The implementation reference is Sarah Walker's PCem `dev` branch, pinned in
`../docs/atm-486-module/PCEM_PORTING.md`. The 486 CPU implementation lives in
PCem's `src/cpu/386.c` and `src/cpu/386_common.c`; the SiS 496/497 chipset
reference is `src/devices/sis496.c` and its machine wiring is in
`src/models/`. Preserve upstream copyright and GPL-2.0 notices when source
files are imported. No PCem CPU source has been imported in this stage.

## BIOS images

- `BIOS.BIN` in the repository root is the **Trident video Option ROM**, not
  the motherboard BIOS. It is loaded independently and mapped at C0000h.
- `i486::load_bios(path)` loads the separate system BIOS image (currently
  exactly 64 KiB). No motherboard BIOS binary is bundled.
- The 486 reset fetch at FFFFFFF0h aliases system BIOS offset FFF0h.

## Build integration

Both project makefiles include the 486 platform sources. The GCC makefile now
also includes `vga_adapter.cpp`, which is required by the 486 port I/O and
ISA memory callbacks.

## Next integration stages

1. Port the PCem i486 interpreter and x87 with a per-module CPU state.
2. Connect CPU physical memory, I/O, IRQ, NMI and cycle callbacks to this board.
3. Replace the preliminary PIC/PIT/DMA/CMOS register stores with functional
   timed devices.
4. Add the PCem ISA/PCI bus, storage, keyboard, serial and other PC peripherals.
5. Connect a PCem-supported VGA device and a separate 3D accelerator
   (for example Voodoo Graphics), with a distinct rendering path from ATM VGA.
6. Implement and test the ZXBus bridge protocol, reset, interrupts and shared
   transfer windows. No ZXBus register protocol is claimed by this commit.
