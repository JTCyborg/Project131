# Standalone 486 PCem machine profile

This is the Stage 1 reference configuration for the 486 expansion computer.
It deliberately uses PCem's complete machine implementation rather than the
preliminary Project131 board stubs.

## Machine

| Setting | Selection |
|---|---|
| Motherboard | Award SiS 496/497 |
| CPU | Intel 486DX2/66 (or the closest Intel 486DX2 option exposed by this PCem revision) |
| RAM | 16 MiB |
| Primary VGA | Diamond Stealth 3D 2000, S3 ViRGE/325 |
| Optional 3D add-on | 3DFX Voodoo Graphics |
| Storage | Standard IDE |
| Sound | Sound Blaster 16 (optional for initial POST) |

The S3 ViRGE is the PC's own VGA adapter. The Trident TVGA8900D remains
exclusively on the Spectrum/ATM side and is not visible to this PC.

## ROM images

PCem does not distribute copyrighted motherboard or adapter ROMs. Supply
legally obtained ROM images through PCem's normal ROM/configuration UI.

The Award SiS 496/497 motherboard ROM set is documented by upstream PCem as
`sis496/sis496-1.awa`. The S3 ViRGE adapter and optional Voodoo each need
their own appropriate Option ROM where required by PCem. Do not reuse the
Spectrum Trident `BIOS.BIN` as a motherboard or S3 BIOS.

## Build and launch

From a PowerShell prompt in the Project131 checkout:

```powershell
.	oolsuild_486_pcem.ps1
.	oolsun_486_pcem.ps1
```

The build script initializes the pinned `third_party/pcem-full` source and
builds PCem with its SDL display frontend. PCem opens its own machine UI; select
the configuration above and attach the required ROM images there.

## Integration boundary

This Stage 1 launcher runs PCem as an independent process. It proves the
standalone PC baseline using the full upstream machine implementation; it is
not yet an in-process 486 device inside Project131. Stage 2 will replace the
process boundary with the ZXBus controller endpoints and explicit shared
memory/register/interrupt transport. Spectrum Z80 and PC 486 will retain
separate CPU and device address spaces.
