# ATM ZXBus 486 expansion module

The 486 expansion is a standalone IBM PC-compatible computer. It owns its CPU,
RAM, motherboard BIOS, chipset, buses, peripherals and primary video adapter.
The ATM/Spectrum computer is separate; Trident TVGA8900D belongs only to
Spectrum.

## Stage 1 — standalone PC

The full upstream PCem source is pinned as the `third_party/pcem-full`
submodule. It provides the 486 CPU, PC/AT machine framework, SiS 496/497
motherboard, ISA/PCI, memory, interrupt/timer, storage and VGA implementations.
The earlier `third_party/pcem/` directory is a small verbatim CPU-source
snapshot for provenance, not a complete PCem build.

Build and launch the independent 486 PC:

```powershell
.\tools\build_486_pcem.ps1
.\tools\run_486_pcem.ps1
```

The fixed machine profile is `i486/pcem_486.cfg`: Award SiS 496/497,
Intel i486DX2/66, 16 MiB RAM, S3 ViRGE primary VGA and 3DFX Voodoo Graphics
as a separate 3D accelerator. ROM requirements and locations are documented
in `docs/atm-486-module/PCEM_MACHINE_PROFILE.md`. PCem does not include
copyrighted ROM images. Supply the motherboard and S3 video ROMs; the
Spectrum Trident `BIOS.BIN` is not a PC BIOS.

The PCem build requires CMake, SDL2, OpenAL and OpenGL development packages.
On Windows use MinGW-w64 GCC/G++ and Ninja; the build script checks for them.
The launcher currently runs PCem as a separate process. This provides the
standalone Stage 1 machine implementation; it is not yet an in-process PC
device inside Project131. The old `i486/i486.cpp` shell is not a substitute
for the PCem machine.

## Stage 2 — ZXBus

After the standalone PC completes BIOS POST, implement a controller endpoint
on both sides. The link will provide explicit register and memory windows,
command/status exchange, interrupts, reset/control and block transfers. The
two CPUs and their internal device buses remain independent. The PCem process
boundary will be replaced by explicit ZXBus transport; Trident and ATM RAM
will not be mapped into the PC address space.
