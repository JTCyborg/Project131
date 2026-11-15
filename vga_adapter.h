#pragma once

// Software ISA VGA/SVGA adapter.
// Physical VRAM: 1 MiB. Z80 aperture: C000-FFFF (16 KiB), 64 banks.
// 3DEh: bit 0 software mode, bit 1 aperture enable, bits 2..5 low bank bits,
//       bit 6 legacy 640x480 RGB565 shortcut.
// 3DFh: full aperture bank selector, 0..63.
// 3E0h/3E1h: indexed packed-pixel mode extension:
//   index 0/1 = X resolution low/high; 2/3 = Y resolution low/high;
//   index 4 = bits per pixel (8/15/16/24/32);
//   index 5 = mode control (bit 0 enable); index 6 = aperture bank.
// The packed-pixel framebuffer is linear at VRAM offset 00000h.
// A complete visible frame must fit into the 1 MiB VRAM.
bool isa_vga_adapter_port_read(unsigned port, unsigned char &value);
bool isa_vga_adapter_port_write(unsigned port, unsigned char value);
bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value);
bool isa_vga_adapter_mem_write(unsigned address, unsigned char value);
void isa_vga_adapter_capture_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride, unsigned bits_per_pixel, bool yuy2);
void isa_vga_adapter_done();
