#pragma once

// Software ISA VGA adapter. 16 KiB Z80 aperture: C000-FFFF.
// Port 3DE: bit 0 = software mode, bit 1 = aperture enable, bits 2..5 = VRAM bank.
// Port 3DF: direct VRAM bank select (0..15).
bool isa_vga_adapter_port_read(unsigned port, unsigned char &value);
bool isa_vga_adapter_port_write(unsigned port, unsigned char value);
bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value);
bool isa_vga_adapter_mem_write(unsigned address, unsigned char value);
void isa_vga_adapter_capture_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride, unsigned bits_per_pixel, bool yuy2);
void isa_vga_adapter_done();
