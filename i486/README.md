# ATM ZXBus 486 expansion module

The 486 expansion is a standalone IBM PC-compatible computer. It owns its CPU,
RAM, motherboard BIOS, chipset, buses, peripherals and primary video adapter.
The ATM/Spectrum computer is a separate machine; Trident TVGA8900D belongs
only to Spectrum.

## Stage 1 — standalone PC

The full upstream PCem source is pinned as the `third_party/pcem-full`
submodule. The PCem revision includes the 486 CPU, PC/AT machine framework,
SiS 496/497 motherboard, ISA/PCI, memory, interrupt/timer, storage and VGA
implementations. The earlier `third_party/pcem/` directory is a small
verbatim CPU-source provenance snapshot, not a complete PCem build.

Build and launch the independent PCem machine:

```powershell
.\tools\build_486_pcem.ps1
.\tools\run_486_pcem.ps1
```

Use the reference profile in
`docs/atm-486-module/PCEM_MACHINE_PROFILE.md`: Award SiS 496/497, Intel
486DX2/66, 16 MiB RAM, S3 ViRGE primary VGA, and optional 3DFX Voodoo.
PCem does not include copyrighted BIOS ROM images. A legally obtained
motherboard BIOS and the relevant video Option ROMs must be supplied through
PCem's configuration UI. The Spectrum Trident `BIOS.BIN` is not a PC BIOS.

The launcher currently runs PCem as a separate process. It is the standalone
PC baseline, not yet an in-process PC device in Project131. The previous
`i486/i486.cpp` memory/I/O shell is not used as a substitute for the PCem
machine implementation.

## Stage 2 — ZXBus

After the standalone PC configuration boots through BIOS POST, implement a
controller endpoint on both sides. The link will provide explicit register
and memory windows, command/status exchange, interrupts, reset/control and
block transfers. The two CPUs and their internal device buses remain
independent. The PCem process boundary will be replaced by an explicit ZXBus
transport, not by mapping Trident or ATM RAM into the PC's address space.
