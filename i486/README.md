# ATM ZXBus 486 expansion module

The 486 module is a standalone IBM PC-compatible machine with its own CPU,
physical address space, RAM, motherboard BIOS, chipset, interrupt controllers,
I/O devices and video adapter. Spectrum/ATM is a separate computer.

## Stage 1 — standalone PC boot

- 16 MiB private physical RAM.
- Separate 64 KiB motherboard BIOS image, loaded explicitly with
  `i486::load_bios()`.
- Reset-vector alias at FFFFFFF0h and legacy BIOS window at F0000h.
- Isolated x86 memory and I/O entry points.
- Preliminary board-device models in `pc_platform.cpp`.
- PCem 486 CPU source is vendored under `third_party/pcem/`; provenance and
  license are recorded in `third_party/pcem/UPSTREAM.md`.

This is not yet a bootable PC: the imported PCem CPU is not compiled or
connected to a per-machine context, and the preliminary board devices are not
a complete PC/AT implementation. A compatible motherboard BIOS image is also
required. Root `BIOS.BIN` is the Spectrum Trident VGA ROM, never the 486
motherboard BIOS.

Next Stage 1 work: adapt PCem CPU execution and machine services, integrate
chipset, memory/ROM mapping, PIC/PIT, DMA, CMOS/RTC, keyboard, storage and an
independent PC VGA device. Bootability will only be claimed after executing a
real system BIOS and completing POST.

## Video ownership

Trident TVGA8900D belongs exclusively to ATM/Spectrum. The 486 must have its
own PCem-supported VGA device, private VRAM, VGA I/O ports and VGA Option ROM.
A 3D accelerator such as 3Dfx Voodoo is an additional 486-side device. No
Spectrum Trident memory or I/O callback is mapped into the 486.

## Stage 2 — ZXBus link

After standalone 486 boot works, implement ZXBus controllers on both machines:
explicit register and memory windows, command/status exchange, interrupts,
reset/control and block transfers. Neither CPU directly accesses the other's
private RAM or internal device buses.
