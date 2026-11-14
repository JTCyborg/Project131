#include "std.h"
#include "emul.h"
#include "vars.h"
#include "draw.h"
#include "vga_adapter.h"

namespace
{
    static const unsigned VRAM_SIZE = 256 * 1024;
    static const unsigned PLANE_SIZE = 64 * 1024;
    static const unsigned APERTURE_SIZE = 16 * 1024;
    static const unsigned MAX_PIXELS = 1024 * 768;

    struct VGA_ADAPTER
    {
        unsigned char vram[VRAM_SIZE];
        unsigned char seq[5], gc[9], crtc[0x19], attr[0x15];
        unsigned char seq_index, gc_index, crtc_index, attr_index;
        unsigned char attr_data, attr_flip;
        unsigned char misc_out, status;
        unsigned char dac[256][3], dac_mask;
        unsigned char dac_read, dac_write, dac_component;
        unsigned char bank;
        unsigned char software_mode, aperture;
        unsigned char latch[4];
        unsigned width, height;
        DWORD pixels[MAX_PIXELS];
        HWND window;
        bool initialized;
        bool dirty;
        bool text_mode;
    };

    static VGA_ADAPTER vga = {};

    static const unsigned char ega_palette[16][3] =
    {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };

    static DWORD rgb(unsigned r, unsigned g, unsigned b)
    {
        return (r << 16) | (g << 8) | b;
    }

    static DWORD palette_color(unsigned index)
    {
        const unsigned char *c = vga.dac[index & vga.dac_mask];
        return rgb((c[0] & 63) * 4, (c[1] & 63) * 4, (c[2] & 63) * 4);
    }

    static void set_mode(unsigned w, unsigned h, bool text)
    {
        if (w > 1024 || h > 768 || w * h > MAX_PIXELS)
            return;
        if (vga.width != w || vga.height != h || vga.text_mode != text)
        {
            vga.width = w;
            vga.height = h;
            vga.text_mode = text;
            vga.dirty = true;
            if (vga.window)
            {
                RECT r = { 0, 0, LONG(w), LONG(h) };
                AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
                SetWindowPos(vga.window, nullptr, 0, 0, r.right-r.left, r.bottom-r.top,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    static void decode_mode()
    {
        if (!vga.software_mode)
            return;
        if (vga.seq[4] & 8)
            set_mode(320, 200, false);             // VGA mode 13h, chain-4
        else if (vga.gc[6] & 1)
            set_mode(640, 480, false);             // planar 16-colour graphics
        else
            set_mode(720, 400, true);              // text mode
    }

    static void render_pixels()
    {
        if (!vga.width || !vga.height || vga.text_mode)
            return;

        if (vga.width == 320)
        {
            for (unsigned y = 0; y < 200; ++y)
            {
                DWORD *dst = vga.pixels + y * 320;
                for (unsigned x = 0; x < 320; ++x)
                {
                    unsigned a = y * 320 + x;
                    unsigned plane = a & 3;
                    unsigned offset = a >> 2;
                    dst[x] = palette_color(vga.vram[plane * PLANE_SIZE + offset]);
                }
            }
        }
        else
        {
            for (unsigned y = 0; y < 480; ++y)
            {
                DWORD *dst = vga.pixels + y * 640;
                for (unsigned x = 0; x < 640; ++x)
                {
                    unsigned offset = y * 80 + (x >> 3);
                    unsigned mask = 0x80 >> (x & 7);
                    unsigned colour = 0;
                    for (unsigned p = 0; p < 4; ++p)
                        if (vga.vram[p * PLANE_SIZE + offset] & mask)
                            colour |= 1U << p;
                    dst[x] = palette_color(colour);
                }
            }
        }
        vga.dirty = false;
    }

    static void render_transparent(const unsigned char *source, unsigned sw, unsigned sh, unsigned stride)
    {
        if (!source || !sw || !sh || !stride)
            return;

        // Spectrum/ATM renderers publish a normalized stream of 8 pixels + one
        // packed ink/paper attribute. The VGA card receives indexed pixels.
        for (unsigned i = 0; i < 16; ++i)
        {
            vga.dac[i][0] = (unsigned char)(pal0[i].peRed >> 2);
            vga.dac[i][1] = (unsigned char)(pal0[i].peGreen >> 2);
            vga.dac[i][2] = (unsigned char)(pal0[i].peBlue >> 2);
        }

        const unsigned outw = 320, outh = 200;
        const unsigned x0 = sw > outw ? (sw - outw) / 2 : 0;
        const unsigned y0 = sh > outh ? (sh - outh) / 2 : 0;
        for (unsigned y = 0; y < outh; ++y)
        {
            unsigned sy = y0 + (y * (sh - y0 < outh ? sh - y0 : outh)) / outh;
            if (sy >= sh) sy = sh - 1;
            for (unsigned x = 0; x < outw; ++x)
            {
                unsigned sx = x0 + (x * (sw - x0 < outw ? sw - x0 : outw)) / outw;
                if (sx >= sw) sx = sw - 1;
                unsigned group = sx >> 3;
                unsigned bit = 7 - (sx & 7);
                const unsigned char *cell = source + sy * stride + group * 2;
                unsigned char pixels = cell[0];
                unsigned char attr = cell[1];
                unsigned colour = (pixels & (1U << bit)) ? (attr & 15) : (attr >> 4);
                unsigned address = y * outw + x;
                vga.vram[(address & 3) * PLANE_SIZE + (address >> 2)] = (unsigned char)colour;
            }
        }
        set_mode(320, 200, false);
        vga.dirty = true;
    }

    static void render_text(HDC dc)
    {
        static const COLORREF colors[16] =
        {
            RGB(0,0,0),RGB(0,0,170),RGB(0,170,0),RGB(0,170,170),
            RGB(170,0,0),RGB(170,0,170),RGB(170,85,0),RGB(170,170,170),
            RGB(85,85,85),RGB(85,85,255),RGB(85,255,85),RGB(85,255,255),
            RGB(255,85,85),RGB(255,85,255),RGB(255,255,85),RGB(255,255,255)
        };
        HFONT font = CreateFontA(-16, 8, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            OEM_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
            FIXED_PITCH | FF_MODERN, "Terminal");
        HGDIOBJ old = SelectObject(dc, font);
        SetBkMode(dc, OPAQUE);
        for (unsigned y = 0; y < 25; ++y)
        {
            for (unsigned x = 0; x < 80; ++x)
            {
                unsigned a = 0x18000 + (y * 80 + x) * 2;
                unsigned char ch = vga.vram[a & (VRAM_SIZE - 1)];
                unsigned char at = vga.vram[(a + 1) & (VRAM_SIZE - 1)];
                SetTextColor(dc, colors[at & 15]);
                SetBkColor(dc, colors[(at >> 4) & 15]);
                char c = ch ? (char)ch : ' ';
                TextOutA(dc, int(x * 9), int(y * 16), &c, 1);
            }
        }
        SelectObject(dc, old);
        DeleteObject(font);
    }

    static LRESULT CALLBACK vga_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (vga.text_mode)
                render_text(dc);
            else
            {
                render_pixels();
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = LONG(vga.width);
                bmi.bmiHeader.biHeight = -LONG(vga.height);
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0, vga.width, vga.height,
                    vga.pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (msg == WM_CLOSE)
        {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    static void ensure_window()
    {
        if (vga.initialized)
            return;
        vga.initialized = true;
        WNDCLASSA wc = {};
        wc.lpfnWndProc = vga_window_proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "UnrealSpeccyISAVGAScreen";
        RegisterClassA(&wc);
        vga.window = CreateWindowA(wc.lpszClassName, "ISA VGA Adapter - Trident TVGA8900D",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 660, 440,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (vga.window)
            ShowWindow(vga.window, SW_SHOWNORMAL);

        vga.width = 320;
        vga.height = 200;
        vga.dac_mask = 0xFF;
        for (unsigned i = 0; i < 16; ++i)
            memcpy(vga.dac[i], ega_palette[i], 3);

        // Power-on VGA register state used by the transparent bridge.
        vga.seq[2] = 0x0F;
        vga.seq[4] = 0x0E;
        vga.gc[6] = 0x05;
        vga.misc_out = 1;
    }

    static unsigned normalize_port(unsigned port)
    {
        return port & 0x3FF;
    }

    static unsigned normalize_crtc(unsigned port)
    {
        if (((port & 0xFFF0) == 0x3D0 || (port & 0xFFF0) == 0x3B0) && !(vga.misc_out & 1))
            port ^= 0x60;
        return port;
    }

    static unsigned rotate_right(unsigned char value, unsigned count)
    {
        count &= 7;
        return ((unsigned)value >> count) | ((unsigned)value << ((8 - count) & 7));
    }

    static unsigned char vga_mem_read(unsigned address)
    {
        unsigned linear = (unsigned(vga.bank) * APERTURE_SIZE + (address & 0x3FFF)) & 0x3FFFF;
        if (vga.seq[4] & 8)
        {
            unsigned plane = linear & 3;
            unsigned offset = (linear >> 2) & 0xFFFF;
            return vga.vram[plane * PLANE_SIZE + offset];
        }

        unsigned offset = linear & 0xFFFF;
        for (unsigned p = 0; p < 4; ++p)
            vga.latch[p] = vga.vram[p * PLANE_SIZE + offset];
        if (vga.gc[5] & 8)
        {
            unsigned result = 0xFF;
            for (unsigned p = 0; p < 4; ++p)
                if (vga.gc[7] & (1U << p))
                {
                    unsigned mask = (vga.gc[2] & (1U << p)) ? 0xFF : 0;
                    result &= ~(vga.latch[p] ^ mask);
                }
            return (unsigned char)result;
        }
        return vga.latch[vga.gc[4] & 3];
    }

    static void vga_mem_write(unsigned address, unsigned char value)
    {
        unsigned linear = (unsigned(vga.bank) * APERTURE_SIZE + (address & 0x3FFF)) & 0x3FFFF;
        if (vga.seq[4] & 8)
        {
            unsigned plane = linear & 3;
            unsigned offset = (linear >> 2) & 0xFFFF;
            if (vga.seq[2] & (1U << plane))
                vga.vram[plane * PLANE_SIZE + offset] = value;
            vga.dirty = true;
            return;
        }

        unsigned offset = linear & 0xFFFF;
        unsigned rotated = rotate_right(value, vga.gc[3] & 7);
        unsigned mode = vga.gc[5] & 3;
        unsigned logical = (vga.gc[3] >> 3) & 3;
        unsigned bitmask = vga.gc[8];
        for (unsigned p = 0; p < 4; ++p)
        {
            if (!(vga.seq[2] & (1U << p)))
                continue;
            unsigned char src = (mode == 1) ? vga.latch[p] :
                ((vga.gc[1] & (1U << p)) ? ((vga.gc[0] & (1U << p)) ? 0xFF : 0) : (unsigned char)rotated);
            unsigned char mask = (unsigned char)bitmask;
            if (mode == 2)
            {
                src = (value & (1U << p)) ? 0xFF : 0;
                mask = 0xFF;
            }
            if (mode == 3)
            {
                src = (vga.gc[0] & (1U << p)) ? 0xFF : 0;
                mask = (unsigned char)(rotated & bitmask);
            }
            unsigned char old = vga.latch[p];
            unsigned char data = src;
            if (logical == 1) data &= old;
            else if (logical == 2) data |= old;
            else if (logical == 3) data ^= old;
            vga.vram[p * PLANE_SIZE + offset] = (unsigned char)((data & mask) | (old & ~mask));
        }
        vga.dirty = true;
    }
}

bool isa_vga_adapter_port_read(unsigned port, unsigned char &value)
{
    ensure_window();
    port = normalize_port(port);
    port = normalize_crtc(port);
    switch (port)
    {
    case 0x3C1: value = vga.attr[vga.attr_index & 0x1F]; return true;
    case 0x3C2: value = vga.status; return true;
    case 0x3C5: value = vga.seq[vga.seq_index % 5]; return true;
    case 0x3CC: value = vga.misc_out; return true;
    case 0x3CF: value = vga.gc[vga.gc_index % 9]; return true;
    case 0x3D4: value = vga.crtc_index; return true;
    case 0x3D5: value = vga.crtc[vga.crtc_index % 0x19]; return true;
    case 0x3C6: value = vga.dac_mask; return true;
    case 0x3C7: value = 0; return true;
    case 0x3C8: value = vga.dac_write; return true;
    case 0x3C9:
        value = vga.dac[vga.dac_read][vga.dac_component++] & 63;
        if (vga.dac_component == 3) { vga.dac_component = 0; ++vga.dac_read; }
        return true;
    case 0x3DA:
        vga.attr_flip = 0;
        vga.status ^= 8;
        value = vga.status;
        return true;
    case 0x3DE:
        value = (vga.software_mode ? 1 : 0) | (vga.aperture ? 2 : 0) | ((vga.bank & 15) << 2);
        return true;
    case 0x3DF: value = vga.bank; return true;
    default: return false;
    }
}

bool isa_vga_adapter_port_write(unsigned port, unsigned char value)
{
    ensure_window();
    port = normalize_port(port);
    if (port >= 0x3B0 && port <= 0x3DF && port != 0x3DE && port != 0x3DF)
        vga.software_mode = 1;

    port = normalize_crtc(port);
    switch (port)
    {
    case 0x3C0:
        if (!vga.attr_flip) vga.attr_index = value & 0x1F;
        else vga.attr[vga.attr_index & 0x1F] = value;
        vga.attr_flip ^= 1;
        decode_mode();
        vga.dirty = true;
        return true;
    case 0x3C2: vga.misc_out = value; decode_mode(); return true;
    case 0x3C4: vga.seq_index = value & 7; return true;
    case 0x3C5: vga.seq[vga.seq_index % 5] = value; decode_mode(); vga.dirty = true; return true;
    case 0x3CE: vga.gc_index = value & 15; return true;
    case 0x3CF: vga.gc[vga.gc_index % 9] = value; decode_mode(); vga.dirty = true; return true;
    case 0x3D4: vga.crtc_index = value & 0x1F; return true;
    case 0x3D5: vga.crtc[vga.crtc_index % 0x19] = value; vga.dirty = true; return true;
    case 0x3C6: vga.dac_mask = value; return true;
    case 0x3C7: vga.dac_read = value; vga.dac_component = 0; return true;
    case 0x3C8: vga.dac_write = value; vga.dac_component = 0; return true;
    case 0x3C9:
        vga.dac[vga.dac_write][vga.dac_component++] = value & 63;
        if (vga.dac_component == 3) { vga.dac_component = 0; ++vga.dac_write; }
        vga.dirty = true;
        return true;
    case 0x3DE:
        vga.software_mode = (value & 1) != 0;
        vga.aperture = (value & 2) != 0;
        vga.bank = (value >> 2) & 15;
        if (!vga.software_mode) vga.aperture = 0;
        decode_mode();
        vga.dirty = true;
        return true;
    case 0x3DF: vga.bank = value & 15; return true;
    default: return false;
    }
}

bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value)
{
    ensure_window();
    if (!vga.software_mode || !vga.aperture || (address & 0xC000) != 0xC000)
        return false;
    value = vga_mem_read(address);
    return true;
}

bool isa_vga_adapter_mem_write(unsigned address, unsigned char value)
{
    ensure_window();
    if (!vga.software_mode || !vga.aperture || (address & 0xC000) != 0xC000)
        return false;
    vga_mem_write(address, value);
    return true;
}

void isa_vga_adapter_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride)
{
    ensure_window();
    if (!vga.software_mode)
        render_transparent(source, width, height, stride);
    if (vga.window && vga.dirty)
        InvalidateRect(vga.window, nullptr, FALSE);
}

void isa_vga_adapter_done()
{
    if (vga.window)
    {
        DestroyWindow(vga.window);
        vga.window = nullptr;
    }
    vga.initialized = false;
}
