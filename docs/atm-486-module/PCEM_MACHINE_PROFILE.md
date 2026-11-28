# Standalone 486 PCem machine profile

The reference machine is a complete independent PC using PCem's machine,
CPU, chipset, bus, storage and video implementations.

## Selected hardware

| Setting | Selection |
|---|---|
| Motherboard | Award SiS 496/497 |
| CPU | Intel i486DX2/66 |
| RAM | 16 MiB |
| Primary VGA | Diamond Stealth 3D 2000, S3 ViRGE/325 |
| 3D add-on | 3DFX Voodoo Graphics |
| Storage | Disabled for first POST |

The S3 ViRGE is the PC's own PCI VGA adapter. The Trident TVGA8900D remains
exclusively on the Spectrum/ATM side.

## ROM images required

PCem does not distribute copyrighted ROM images. Obtain ROMs legally and place
them in the portable data directory created beside the PCem executable:

- `.pcem/roms/sis496/sis496-1.awa` — Award SiS 496/497 motherboard BIOS
- `.pcem/roms/s3virge.bin` — S3 ViRGE Option ROM

Do not use the Spectrum Trident `BIOS.BIN` for either image. Voodoo Graphics
is enabled as a separate 3D accelerator; it does not replace the S3 VGA card.

## Build prerequisites

- Git (for initializing the pinned source submodule)
- CMake
- MinGW-w64 GCC/G++ and Ninja on Windows
- SDL2, OpenAL and OpenGL development packages discoverable by CMake

The PCem build is a separate native build. Project131's existing MSVC Makefile
does not compile PCem's C sources as part of the emulator.

## Build and launch

From the Project131 checkout:

```powershell
.\tools\build_486_pcem.ps1
.\tools\run_486_pcem.ps1
```

The launcher passes `i486/pcem_486.cfg` to PCem, selecting the machine above.
The build script creates the ROM directories but does not download copyrighted
ROMs. If required images are missing, PCem cannot complete BIOS startup.

## Integration boundary

This is the standalone Stage 1 PCem process. It is not yet an in-process 486
device inside Project131. Stage 2 will replace the process boundary with
explicit ZXBus controller endpoints and memory/register/interrupt transport.
