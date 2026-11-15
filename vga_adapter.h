#pragma once

// Software ISA VGA adapter. 1 MiB VRAM, exposed through a 16 KiB Z80 aperture: C000-FFFF.
// Port 3DE: bit 0 = software mode, bit 1 = aperture enable,
//          bits 2..5 = low four bank bits, bit 6 = 640x480 RGB565 mode.
// Port 3DF: direct VRAM bank select (0..63). In RGB565 mode the framebuffer
//          is linear at VRAM offset 00000h (640 * 480 * 2 bytes).
bool isa_vga_adapter_port_read(unsigned port, unsigned char &value);
bool isa_vga_adapter_port_write(unsigned port, unsigned char value);
bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value);
bool isa_vga_adapter_mem_write(unsigned address, unsigned char value);
void isa_vga_adapter_capture_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride, unsigned bits_per_pixel, bool yuy2);
void isa_vga_adapter_done();
