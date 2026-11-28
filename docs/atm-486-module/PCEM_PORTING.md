# ATM ZXBus 486 Expansion — PCem port baseline

## Decision

The 486 PC side of the ATM expansion is to be based on the implementation in
[sarah-walker-pcem/pcem](https://github.com/sarah-walker-pcem/pcem/tree/dev), branch
`dev`, pinned for this porting baseline to commit
`b3d6ab4cd5016e459832a066e1bea5dd78605715`.

PCem is the source implementation for the PC-compatible CPU, chipset, memory
system, PC buses, standard peripherals, VGA and 3D device models. The ATM ZXBus
connection is a new platform-specific bridge and must not be emulated as a
PCem ISA/PCI peripheral unless the hardware protocol explicitly requires it.

## Target machine profile

Initial board profile:

- Intel 486DX-class CPU (exact stepping and clock to be fixed in the board profile).
- 16 MiB installed RAM.
- Own system BIOS image and reset/POST sequence.
- PC-compatible chipset with northbridge/system-controller and southbridge/I/O
  functions.
- PCI/ISA device topology as supported by the selected PCem 486 machine model.
- VGA device with a 3D accelerator, using PCem's existing video device models.
- Separate ZXBus bridge connecting the PC module to ATM.

The first candidate chipset/platform to evaluate is PCem's Award SiS 496/497
486 machine. PCem's system catalogue also contains AMI 486 and IBM 486
configurations. Select one concrete board profile before wiring individual
devices; do not combine register maps from unrelated chipsets.

## PCem source map

| Subsystem | PCem source area | Porting role |
|---|---|---|
| 486 CPU core | `src/cpu/386.c`, `386_common.c`, `cpu.c`, `x86seg.c`, `x87.c`, `includes/private/cpu/*` | CPU execution, protected mode, segmentation, paging, exceptions, FPU |
| System / machine model | `src/models/*`, `src/pc.c` | Board composition, reset and device initialization |
| Chipset | `src/models/sis496.c`, `src/devices/sis496.c`, plus selected board-specific support | PCI configuration, shadowing, IRQ routing and memory mapping |
| Memory | `src/memory/*` and memory interfaces | Physical memory map, ROM/RAM decode, access timing and mapping |
| PCI / ISA | `src/bus/*` | Bus transactions, configuration space, IRQ routing and device attachment |
| Interrupt/timer/DMA | `src/models/pic.c`, `pit.c`, `dma.c` and related headers | Legacy PC interrupt controllers, PIT and DMA |
| Storage | `src/ide/*`, `src/hdd/*`, `src/floppy/*`, `src/cdrom/*` | IDE, floppy and CD-ROM controllers/devices |
| I/O peripherals | `src/models/serial.c`, `src/models/sio.c`, keyboard, mouse, parallel-port and RTC sources | UART, Super-I/O, keyboard, mouse, LPT, RTC |
| VGA / SVGA | `src/video/*` | VGA register, memory, timing, rendering and RAMDAC implementations |
| 3D acceleration | `src/video/vid_voodoo*.c` | Voodoo 3D device and rendering pipeline |

The exact set of source files must be derived from the chosen PCem machine model
and its CMake source lists, rather than copying every PCem device into the ATM
module.

## Integration rules for Project131

1. Keep the existing ATM/Z80 emulator and Trident adapter operational. The
   486 module is an independent machine instance with its own address space,
   CPU state, reset, BIOS, RAM and device buses.
2. Keep PCem-derived code behind a narrow PC-platform interface. Do not let
   PCem global state, callbacks or device singletons leak into the ATM core.
3. Replace PCem's host-facing timing, display, input, audio, storage and
   threading services with Project131 host adapters. Preserve PCem hardware
   behavior behind those adapters.
4. Implement ZXBus as a separate bridge with explicit register and memory
   windows, interrupt signaling, transfer protocol and reset/power semantics.
   Do not assume a PCI bus transaction is equivalent to a ZXBus transaction.
5. The module BIOS is a distinct board firmware image. Do not substitute a
   PCem machine BIOS or the existing Trident VGA option ROM for it.
6. Bring up in stages: CPU + RAM + BIOS reset vector; chipset and memory decode;
   PIC/PIT/DMA; keyboard/UART/RTC; storage; VGA; 3D; finally ZXBus integration.

## Licensing and provenance

The referenced PCem tree contains a GNU GPL version 2 license (`COPYING`).
PCem-derived source files must retain their original copyright and license
notices, and modifications must be marked with dates/notices as required by
GPL-2.0. If PCem-derived code is distributed as part of Project131, the
distribution and corresponding source obligations of GPL-2.0 must be respected.
The repository's `NOTICE` also records separately licensed third-party
components; each imported component must retain its applicable notices.

Before importing code, add the upstream `COPYING` and relevant `NOTICE`
material to a clearly named third-party directory and record the exact upstream
commit and per-file provenance. Do not copy firmware ROM images from PCem's
external ROM collection; use a separately licensed BIOS image or develop the
module BIOS.

## Current Project131 baseline

The existing `i486/i486.cpp` is only a small memory/BIOS/ISA-VGA integration
shell. It does not yet provide a complete PC-compatible 486 platform: the
current I/O path only dispatches the Trident adapter and returns `0xFF` for
other ports, and there is no complete chipset, PCI/ISA topology, PIC/PIT/DMA,
keyboard, storage or 3D device stack in that module. The PCem port must replace
that shell incrementally; it must not be mistaken for a finished 486 PC.

## Validation gates

- CPU reset fetch reaches the module BIOS reset vector.
- BIOS POST can execute with exactly 16 MiB RAM and chipset memory decode.
- RAM/ROM shadowing and option-ROM decode match the selected chipset.
- PIC/PIT/keyboard and serial interrupts are delivered correctly.
- DMA and IDE/floppy transfers operate through the PC memory/bus interfaces.
- VGA modes and framebuffer access work before enabling 3D.
- 3D command submission, texture/framebuffer paths and synchronization pass
  independent tests.
- ZXBus tests verify host/module isolation, register access, memory windows,
  interrupt delivery, reset and sustained transfers.
