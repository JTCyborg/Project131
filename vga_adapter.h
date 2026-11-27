#pragma once

// Trident TVGA8900D-compatible VGA/SVGA adapter core.
// Physical VRAM: 1 MiB. The emulator exposes a separate Z80 bridge at C000-FFFF
// (16 KiB window, 64 banks); this bridge is not a Trident ISA register.
// 3DEh: bit 0 software mode, bit 1 aperture enable, bits 2..5 low bank bits,
//       bit 6 legacy 640x480 RGB565 shortcut.
// 3DFh: full aperture bank selector, 0..63.
// 3E0h/3E1h: indexed packed-pixel mode extension:
//   index 0/1 = X resolution low/high; 2/3 = Y resolution low/high;
//   index 4 = bits per pixel (8/15/16/24/32);
//   index 5 = mode control (bit 0 enable); index 6 = aperture bank.
// 3E2h/3E3h: 64-byte indexed 2D engine register bank. 00h command:
// 1 fill rectangle, 2 BitBLT, 3 line; 01h status (1 complete, 80h error).
// 02h ROP: 0 copy, 1 XOR, 2 OR, 3 AND, 4 NOT source, 5 set, 6 clear.
// 04..07 source base, 08..0B destination base, 0C..0D source pitch,
// 0E..0F destination pitch, 10..13 source X/Y, 14..17 destination X/Y,
// 18..1B width/height, 1C..1F colour, 20..23 line end X/Y.
// Cursor: 24..25 X, 26..27 Y, 28..2B 16x16 1bpp bitmap base,
// 2C..2F RGB colour (00RRGGBB), 34h control bit 0 visible.
// All multi-byte registers are little-endian. The packed-pixel framebuffer is linear at VRAM offset 00000h.
// A complete visible frame must fit into the 1 MiB VRAM.
void isa_vga_adapter_set_present(bool present);
bool isa_vga_adapter_port_read(unsigned port, unsigned char &value);
bool isa_vga_adapter_port_write(unsigned port, unsigned char value);
bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value);
bool isa_vga_adapter_mem_write(unsigned address, unsigned char value);
void isa_vga_adapter_capture_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride, unsigned bits_per_pixel, bool yuy2);
void isa_vga_adapter_done();
