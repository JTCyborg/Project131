# ISA VGA/SVGA Adapter — implementation and programming guide

## Enable or disable the adapter in unreal.ini

The adapter can be installed or removed from the emulated machine without changing source code. Add this section to `unreal.ini`:

```ini
[ISA_VGA]
Enabled=1
MemoryKB=1024
```

- `Enabled=1` — adapter is present. Its ports, memory aperture and display capture are active.
- `Enabled=0` — adapter is absent. Reads/writes to its ports and memory aperture are not claimed by the adapter, and its window is hidden.
- `MemoryKB=256`, `512`, or `1024` — installed Trident VRAM size. The default is `1024`; other values are ignored and fall back to 1024 KB.
- If the section or key is missing, the default is `1` to preserve the previous behavior.

The setting is read during emulator startup. Restart Unreal Speccy after changing it; it is not hot-reloaded while the emulator is running. The startup console reports whether the adapter is enabled or disabled.

## Hardware model
The emulator exposes a Trident TVGA8900D-compatible VGA/SVGA adapter in a separate window. It has configurable 256/512 KiB or 1 MiB VRAM and a 16 KiB Z80-side bridge at C000h-FFFFh. The bridge is banked: 64 banks × 16 KiB = 1 MiB. This bridge is an emulator integration path, not an ISA register of the physical Trident card. The standard VGA ports and Trident extensions are modeled separately.

There are two independent paths:

- Transparent mode: captures the final image produced by the selected Unreal Speccy renderer. It does not emulate VGA memory or VGA BIOS.
- Software mode: guest code programs the adapter registers and writes VRAM. This is the path described below.

## Implemented legacy VGA modes

| Mode | Resolution | Pixel format | Notes |
|---|---:|---|---|
| Text | 80×25 characters | character + attribute | Rendered in a 720×400 window |
| Mode 13h-like | 320×200 | 8-bit indexed, 256 palette entries | Chain-4-style storage |
| Planar VGA | 640×480 | 4 planes, 16 colours | Basic planar read/write and DAC |

The legacy path is a simplified VGA register model. It is not a complete IBM VGA clone and does not include an INT 10h BIOS implementation.

### VGA text memory

The 80×25 text renderer reads character and attribute data from separate VGA
planes through aperture bank 6:

- Select aperture bank 6 at port 3DFh.
- The visible 80×25 page starts at aperture address C000h.
- Character bytes are stored in VGA plane 0; attribute bytes are stored at
  the same cell offsets in plane 1.
- Select sequencer register 2 (map mask) at ports 3C4h/3C5h: value 01h
  enables character-plane writes; value 02h enables attribute-plane writes.
- Select graphics-controller register 4 (read map select) at ports 3CEh/3CFh:
  value 00h reads characters; value 01h reads attributes.
- Text attributes use the low nibble for foreground and the high nibble for
  background, using the 16-colour EGA palette.

The model implements text-cell storage and rendering, but does not provide
BIOS teletype services, cursor control, or a full VGA CRTC timing model.

## Packed-pixel SVGA modes

The indexed extension at ports 3E0h/3E1h selects these formats. The mode is accepted only when the complete visible framebuffer fits in 1 MiB.

| Resolution | 8 bpp | 15 bpp | 16 bpp RGB565 | 24 bpp | 32 bpp |
|---|---:|---:|---:|---:|---:|
| 320×200 | Yes | Yes | Yes | Yes | Yes |
| 640×480 | Yes | Yes | Yes | Yes | No |
| 800×600 | Yes | Yes | Yes | No | No |
| 1024×768 | Yes | No | No | No | No |

Formats:
- 8 bpp: palette index through the VGA DAC.
- 15 bpp: RGB555, little-endian.
- 16 bpp: RGB565, little-endian.
- 24 bpp: B, G, R byte order.
- 32 bpp: B, G, R, unused byte. The unused byte is ignored.

A rejected mode request leaves the previously selected display mode unchanged. The implementation does not allocate extra VRAM or silently wrap the framebuffer.

## Port programming

### Enable software mode and aperture

```asm
mov dx, 03DEh
mov al, 03h       ; bit 0 software mode, bit 1 aperture enable, bank 0
out dx, al
```

### Select a 16 KiB bank

```asm
mov dx, 03DFh
mov al, 25        ; bank 25, range 0..63
out dx, al
```

The selected bank appears at C000h-FFFFh. The linear VRAM byte offset is:

```
offset = bank * 16384 + (address - 0xC000)
```

### Select a packed-pixel mode

The indexed extension uses 3E0h (register index) and 3E1h (8-bit register data).

| Index | Register | Meaning |
|---:|---|---|
| 0 | XRES low | Horizontal resolution low byte |
| 1 | XRES high | Horizontal resolution high byte |
| 2 | YRES low | Vertical resolution low byte |
| 3 | YRES high | Vertical resolution high byte |
| 4 | BPP | 8, 15, 16, 24 or 32 |
| 5 | ENABLE | bit 0 = apply mode; 0 = disable packed-pixel mode |
| 6 | BANK | 16 KiB aperture bank, 0..63 |

Example: 640×480 RGB565.

```asm
; XRES = 640 = 0280h
mov dx, 03E0h
mov al, 0
out dx, al
mov dx, 03E1h
mov al, 080h
out dx, al
mov dx, 03E0h
mov al, 1
out dx, al
mov dx, 03E1h
mov al, 2
out dx, al

; YRES = 480 = 01E0h
mov dx, 03E0h
mov al, 2
out dx, al
mov dx, 03E1h
mov al, 0E0h
out dx, al
mov dx, 03E0h
mov al, 3
out dx, al
mov dx, 03E1h
mov al, 1
out dx, al

; 16 bpp, then enable
mov dx, 03E0h
mov al, 4
out dx, al
mov dx, 03E1h
mov al, 16
out dx, al
mov dx, 03E0h
mov al, 5
out dx, al
mov dx, 03E1h
mov al, 1
out dx, al
```

After enabling, write the framebuffer linearly. For 640×480 RGB565, pitch is 1280 bytes and frame size is 614400 bytes. The first pixel is at VRAM offset 0; the final byte is at offset 614399. Select bank N in register 6 (or port 3DFh), then write the corresponding 16 KiB window at C000h-FFFFh.

## Memory limits imposed by 1 MiB

The following formats do not fit as a complete single framebuffer and are therefore rejected: 640×480×32, 800×600×24/32, 1024×768×15/16/24/32. Multiple full-screen pages are not available for the larger modes. Increasing VRAM to 2 or 4 MiB is required for those formats and for page flipping.

## Compatibility boundary

This is a custom emulator-side VGA/SVGA implementation, not a complete Trident TVGA8900D or VESA VBE 2.0 implementation. It currently does not provide a VGA/VBE option ROM, INT 10h mode services, EDID, hardware scan timing, standard VBE bank granularity, or full compatibility with arbitrary DOS VGA drivers. The 3E0h/3E1h interface is a project-specific indexed extension, not a standard VBE port interface.

The Trident implementation is being ported against PCem's `vid_tvga.c`, `vid_svga.c`, `vid_svga_render.c`, and TKD8001 RAMDAC implementation. The current port includes Trident ID/old-mode sequencing, extended Sequencer and Graphics Controller registers, CRTC register masks/write protection, 3D8h/3D9h banking, 3DBh clock-select register state, TKD8001 DAC unlock/control behavior, and packed-pixel 8/15/16/24-bpp rendering paths. This is an incremental port, not yet a complete PCem-equivalent SVGA core: scan timing/retrace, full CRTC-derived geometry, interlace, complete VGA latch/planar edge cases, and all PCem rendering modes remain outstanding.

The implementation supports the framebuffer formats and mode sizes in the tables above. It must not be described as supporting every mode of a physical VGA/SVGA card.


## 2D accelerator extension

The synchronous 2D engine is exposed at indexed ports 3E2h (register index)
and 3E3h (register data). It operates on the active packed-pixel framebuffer.

| Register | Meaning |
|---:|---|
| 00h | Command: 1 solid rectangle, 2 BitBLT, 3 Bresenham line, 4 horizontal gradient fill |
| 01h | Status: 1 complete; 80h generic error; 81h VBE disabled; 82h invalid rectangle; 83h invalid VRAM address; 84h unsupported command |
| 02h | ROP: 0 COPY, 1 XOR, 2 OR, 3 AND, 4 NOT source, 5 SET, 6 CLEAR |
| 04h..07h | Source base address |
| 08h..0Bh | Destination base address |
| 0Ch..0Dh | Source pitch in bytes |
| 0Eh..0Fh | Destination pitch in bytes |
| 10h..13h | Source X/Y |
| 14h..17h | Destination X/Y |
| 18h..1Bh | Rectangle width/height |
| 1Ch..1Fh | Pixel colour |
| 20h..23h | Line end X/Y |
| 24h..25h | Cursor X; 26h..27h cursor Y |
| 28h..2Bh | Cursor bitmap base (16x16, 1bpp) |
| 2Ch..2Fh | Cursor RGB colour (00RRGGBB) |
| 34h | Cursor control, bit 0 visible |

Multi-byte registers are little-endian. Coordinates are clipped to the active
mode. The cursor bitmap uses 32 bytes; zero bits are transparent. Commands
complete immediately in the emulator and update status synchronously.

### Horizontal gradient fill (command 4)

Command 4 fills the destination rectangle with a horizontal linear gradient.
The start pixel is read from registers 1Ch..1Fh; the end pixel is read from
20h..23h. The first column receives the exact start value and the final
column receives the exact end value. Intermediate columns are linearly
interpolated with integer rounding. The same horizontal gradient is repeated
on every row. The command uses the selected ROP and validates the complete
rectangle against the active framebuffer and VRAM bounds before writing.

For 8-bpp indexed mode, the values are palette indices, so a programmed DAC
palette turns the interpolated indices into a colour gradient. For packed
multi-byte modes, interpolation is performed independently for each stored
pixel byte; use 8-bpp for indexed-palette gradients. A one-pixel-wide
rectangle is filled with the start value. Diagnostic status values distinguish disabled VBE (81h), an invalid rectangle (82h), an invalid VRAM address (83h), and an unsupported command (84h).
