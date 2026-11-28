# 486 ZXBus module — implementation plan

1. **Standalone 486 PC:** PCem CPU and machine core, private RAM and BIOS,
   chipset, ISA/PCI devices, independent VGA, and BIOS POST.
2. **ZXBus interconnect:** bridge/controller on both sides, register map,
   memory windows, command/status queues, interrupts, reset and block transfers.

The Spectrum Trident TVGA8900D remains attached only to Spectrum. The 486 PC
uses a separate VGA adapter and Option ROM.

## Current limitations

Vendored PCem CPU sources are not integrated into Project131's build or
adapted to per-instance machine state. The platform layer remains a
preliminary skeleton, not a complete PC/AT chipset. No ZXBus protocol or
486 BIOS POST has been implemented or verified.

Root BIOS.BIN is the Spectrum Trident VGA Option ROM, not a motherboard BIOS.
