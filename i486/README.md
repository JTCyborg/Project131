# Intel i486DX expansion module

This directory is the isolated platform layer for the Intel i486DX/33 module.
It has independent RAM, BIOS mapping, CPU execution, and PC-compatible I/O
devices. It does not use the Spectrum/ATM banked-memory API.

## CPU and memory

- Independent 16 MiB RAM allocation (configurable at initialization).
- Separate 64 KiB BIOS aperture; 8 KiB 27C64 image mapped at its top.
- BIOS read-only mapping at F0000h-FFFFFh and FFFF0000h-FFFFFFFFh.
- Little-endian physical memory accessors.
- Interpreter-only Intel i486DX/33 execution with integrated x87.

## Initial I/O devices

| Device | I/O range | Current implementation |
|---|---:|---|
| COM1 | 3F8h-3FFh | 16550-compatible register bank, divisor latch, line status, TX capture to emulator log |
| LPT1 | 378h-37Ah | SPP data, status and control registers |
| NE2000 | 300h-31Fh | NE2000 register pages, station MAC, 32 KiB onboard RAM, remote DMA, reset and interrupt-status registers |

Default NE2000 MAC: 02:50:43:31:31:00. Interrupt output is not connected
until the i486 interrupt-controller layer is implemented. Network transmit is
reported as no-carrier until a packet transport backend is attached; the card
does not falsely claim successful transmission.

## Next network stage

The selected host transport is SLiRP/NAT. The NE2000 register core is kept
separate from the transport so that guest-visible hardware behavior remains
independent of host networking. The SLiRP packet bridge, polling/timer service,
and Windows socket dependencies are not yet connected.

## Integration constraints

- Keep the i486 module independent from the Spectrum/ATM banked-memory API.
- Do not route x86 IN/OUT through the Z80/ATM output handler.
- Keep PC-compatible I/O isolated within this module.
- Import PCem code with its original copyright and license notices.
- Do not import PCem motherboard, PCI/ISA, video, or dynarec subsystems.
