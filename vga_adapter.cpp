#include "std.h"
#include "emul.h"
#include "vars.h"
#include "draw.h"
#include "vga_adapter.h"

namespace
{
    static const unsigned VRAM_SIZE = 1024 * 1024;
    static const unsigned PLANE_SIZE = 64 * 1024;
    static const unsigned APERTURE_SIZE = 16 * 1024;
    static const unsigned APERTURE_BANKS = VRAM_SIZE / APERTURE_SIZE;
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
        unsigned char rgb565_mode;
        unsigned short vbe_index, vbe_regs[10];
        unsigned char vbe_enabled, vbe_bpp;
        unsigned short vbe_xres, vbe_yres, vbe_pitch;
        unsigned char latch[4];
        unsigned width, height;
        DWORD pixels[MAX_PIXELS];
        HWND window;
        bool initialized;
        bool dirty;
        bool text_mode;
        unsigned char accel_regs[64];
        unsigned char accel_index;
    };

    static VGA_ADAPTER vga = {};
    static bool card_present = true;

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
            // Keep the VGA output window at a stable size. Resolution changes
            // affect only the source framebuffer; WM_PAINT scales it to the
            // existing client area above the status bar.
            if (vga.window)
                InvalidateRect(vga.window, nullptr, FALSE);
        }
    }

    static bool configure_vbe_mode(unsigned w, unsigned h, unsigned bpp)
    {
        if (!w || !h || w > 1024 || h > 768 || (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32)) return false;
        unsigned pitch = w * ((bpp + 7) >> 3);
        if (pitch * h > VRAM_SIZE) return false;
        vga.vbe_xres = (unsigned short)w; vga.vbe_yres = (unsigned short)h;
        vga.vbe_bpp = (unsigned char)bpp; vga.vbe_pitch = (unsigned short)pitch;
        vga.vbe_enabled = 1; vga.rgb565_mode = 0; vga.bank = 0;
        vga.software_mode = 1; vga.aperture = 1;
        vga.vbe_regs[1] = (unsigned short)w; vga.vbe_regs[2] = (unsigned short)h; vga.vbe_regs[3] = (unsigned short)bpp;
        vga.vbe_regs[4] = 1; vga.vbe_regs[6] = (unsigned short)w; vga.vbe_regs[7] = (unsigned short)h;
        set_mode(w, h, false); vga.dirty = true; return true;
    }

    static void decode_mode()
    {
        if (!vga.software_mode)
            return;
        if (vga.rgb565_mode)
            set_mode(640, 480, false);           // 640x480 RGB565 direct colour
        else if (vga.seq[4] & 8)
            set_mode(320, 200, false);             // VGA mode 13h, chain-4
        else if (vga.gc[6] & 1)
            set_mode(640, 480, false);             // planar 16-colour graphics
        else
            set_mode(720, 400, true);              // text mode
    }

    static void render_2d_cursor();

    static void render_pixels()
    {
        // Transparent mode already owns vga.pixels with the final RGB frame.
        if (!vga.software_mode || !vga.width || !vga.height || vga.text_mode)
            return;

        if (vga.vbe_enabled)
        {
            unsigned bytes = (vga.vbe_bpp + 7) >> 3;
            for (unsigned y = 0; y < vga.vbe_yres; ++y)
            {
                DWORD *dst = vga.pixels + y * vga.vbe_xres;
                const unsigned char *src = vga.vram + y * vga.vbe_pitch;
                for (unsigned x = 0; x < vga.vbe_xres; ++x)
                {
                    const unsigned char *p = src + x * bytes;
                    unsigned r = 0, g = 0, b = 0;
                    if (vga.vbe_bpp == 8) { dst[x] = palette_color(p[0]); continue; }
                    if (vga.vbe_bpp == 15) { unsigned q=p[0]|(unsigned(p[1])<<8); r=((q>>10)&31)*255/31; g=((q>>5)&31)*255/31; b=(q&31)*255/31; }
                    else if (vga.vbe_bpp == 16) { unsigned q=p[0]|(unsigned(p[1])<<8); r=((q>>11)&31)*255/31; g=((q>>5)&63)*255/63; b=(q&31)*255/31; }
                    else { b=p[0]; g=p[1]; r=p[2]; }
                    dst[x] = rgb(r,g,b);
                }
            }
        }
        else if (vga.width == 320)
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
        render_2d_cursor();
        vga.dirty = false;
    }


    static unsigned accel16(unsigned i) { return unsigned(vga.accel_regs[i]) | (unsigned(vga.accel_regs[i+1]) << 8); }
    static unsigned accel32(unsigned i) { return accel16(i) | (accel16(i+2) << 16); }
    static unsigned char accel_rop(unsigned char a, unsigned char b, unsigned op)
    {
        switch(op & 7) { case 0:return a; case 1:return a^b; case 2:return a|b; case 3:return a&b; case 4:return (unsigned char)~a; case 5:return 0xFF; case 6:return 0; default:return b; }
    }
    static bool accel_rect_valid(unsigned base, unsigned pitch, unsigned x, unsigned y,
        unsigned w, unsigned h, unsigned bytes)
    {
        if (!w || !h || x >= vga.vbe_xres || y >= vga.vbe_yres ||
            w > vga.vbe_xres - x || h > vga.vbe_yres - y ||
            pitch < (x + w) * bytes)
            return false;
        unsigned long long last = (unsigned long long)base +
            (unsigned long long)(y + h - 1) * pitch +
            (unsigned long long)(x + w) * bytes;
        return last <= VRAM_SIZE;
    }
    static bool accel_addr(unsigned base,unsigned pitch,unsigned x,unsigned y,unsigned bytes,unsigned &out)
    {
        if (x >= vga.vbe_xres || y >= vga.vbe_yres || pitch < (x + 1) * bytes)
            return false;
        unsigned long long a=(unsigned long long)base+(unsigned long long)y*pitch+(unsigned long long)x*bytes;
        if(a+bytes>VRAM_SIZE) return false;
        out=(unsigned)a;
        return true;
    }
    static bool accel_pixel(unsigned x,unsigned y,unsigned color)
    {
        unsigned bytes=(vga.vbe_bpp+7)>>3, a;
        if(!accel_addr(accel32(8),accel16(0x0E),x,y,bytes,a)) return false;
        for(unsigned i=0;i<bytes;++i)
            vga.vram[a+i]=accel_rop((unsigned char)(color>>(i*8)),vga.vram[a+i],vga.accel_regs[2]);
        return true;
    }
    static void execute_2d(unsigned cmd)
    {
        if(!vga.vbe_enabled || !vga.vbe_bpp) { vga.accel_regs[1]=0x80; return; }
        unsigned b=(vga.vbe_bpp+7)>>3, sx=accel16(0x10), sy=accel16(0x12);
        unsigned dx=accel16(0x14), dy=accel16(0x16), w=accel16(0x18), h=accel16(0x1A);
        unsigned sp=accel16(0x0C), dp=accel16(0x0E), sb=accel32(4), db=accel32(8), color=accel32(0x1C);
        if(!w) w=1;
        if(!h) h=1;
        vga.accel_regs[1]=0x80;
        if(cmd==1) {
            if (!accel_rect_valid(db,dp,dx,dy,w,h,b)) return;
            for(unsigned y=0;y<h;++y)
                for(unsigned x=0;x<w;++x)
                    if(!accel_pixel(dx+x,dy+y,color)) return;
        } else if(cmd==2) {
            if (!accel_rect_valid(sb,sp,sx,sy,w,h,b) ||
                !accel_rect_valid(db,dp,dx,dy,w,h,b)) return;
            unsigned long long src0=(unsigned long long)sb+(unsigned long long)sy*sp+(unsigned long long)sx*b;
            unsigned long long dst0=(unsigned long long)db+(unsigned long long)dy*dp+(unsigned long long)dx*b;
            bool rev=dst0>src0;
            for(unsigned ry=0;ry<h;++ry) {
                unsigned y=rev?h-1-ry:ry;
                for(unsigned rx=0;rx<w;++rx) {
                    unsigned x=rev?w-1-rx:rx;
                    unsigned sa=sb+(sy+y)*sp+(sx+x)*b;
                    unsigned da=db+(dy+y)*dp+(dx+x)*b;
                    for(unsigned k=0;k<b;++k)
                        vga.vram[da+k]=accel_rop(vga.vram[sa+k],vga.vram[da+k],vga.accel_regs[2]);
                }
            }
        } else if(cmd==3) {
            int x=(int)dx,y=(int)dy,x2=(int)accel16(0x20),y2=(int)accel16(0x22);
            if (x<0 || y<0 || x2<0 || y2<0 ||
                x>=(int)vga.vbe_xres || x2>=(int)vga.vbe_xres ||
                y>=(int)vga.vbe_yres || y2>=(int)vga.vbe_yres ||
                dp < vga.vbe_xres*b) return;
            int ax=abs(x2-x), sxdir=x<x2?1:-1, ay=-abs(y2-y), sydir=y<y2?1:-1, err=ax+ay;
            for(;;) {
                if(!accel_pixel((unsigned)x,(unsigned)y,color)) return;
                if(x==x2&&y==y2) break;
                int e2=2*err;
                if(e2>=ay){err+=ay;x+=sxdir;}
                if(e2<=ax){err+=ax;y+=sydir;}
            }
        } else return;
        vga.accel_regs[1]=1;
        vga.dirty=true;
        if(vga.window) InvalidateRect(vga.window,nullptr,FALSE);
    }
    static void render_2d_cursor()
    {
        if(!(vga.accel_regs[0x34]&1)||!vga.vbe_enabled) return;
        unsigned x0=accel16(0x24), y0=accel16(0x26), base=accel32(0x28), c=accel32(0x2C);
        if (base > VRAM_SIZE - 32) return;
        DWORD color=rgb((c>>16)&255,(c>>8)&255,c&255);
        for(unsigned y=0;y<16&&y0+y<vga.vbe_yres;++y)
            for(unsigned x=0;x<16&&x0+x<vga.vbe_xres;++x)
                if(vga.vram[base+y*2+(x>>3)]&(0x80>>(x&7)))
                    vga.pixels[(y0+y)*vga.vbe_xres+x0+x]=color;
    }

    static unsigned char clamp_channel(int value)
    {
        if (value < 0) return 0;
        if (value > 255) return 255;
        return (unsigned char)value;
    }

    static void render_transparent(const unsigned char *source, unsigned sw, unsigned sh,
        unsigned pitch, unsigned bpp, bool yuy2)
    {
        if (!source || !sw || !sh || sw > 1024 || sh > 768 || sw * sh > MAX_PIXELS)
            return;
        const unsigned bytes_per_pixel = (bpp + 7) >> 3;
        if (yuy2)
        {
            if (bytes_per_pixel != 2 || (sw & 1)) return;
        }
        else if (bpp != 8 && bpp != 16 && bpp != 32)
            return;
        if (pitch < sw * bytes_per_pixel)
            return;

        // This is the final image produced by the selected renderer. Its source
        // format is determined by the active video driver, not by the emulated clone.
        for (unsigned y = 0; y < sh; ++y)
        {
            const unsigned char *src = source + y * pitch;
            DWORD *dst = vga.pixels + y * sw;
            for (unsigned x = 0; x < sw; ++x)
            {
                unsigned r = 0, g = 0, b = 0;
                if (yuy2)
                {
                    const unsigned char *pair = src + (x & ~1U) * 2;
                    int yy = pair[(x & 1U) ? 2 : 0];
                    int u = int(pair[1]) - 128;
                    int v = int(pair[3]) - 128;
                    r = clamp_channel((298 * (yy - 16) + 409 * v + 128) >> 8);
                    g = clamp_channel((298 * (yy - 16) - 100 * u - 208 * v + 128) >> 8);
                    b = clamp_channel((298 * (yy - 16) + 516 * u + 128) >> 8);
                }
                else if (bpp == 8)
                {
                    const PALETTEENTRY &c = pal0[src[x]];
                    r = c.peRed; g = c.peGreen; b = c.peBlue;
                }
                else if (bpp == 16)
                {
                    unsigned value = ((const unsigned short *)src)[x];
                    r = ((value >> 11) & 31) * 255 / 31;
                    g = ((value >> 5) & 63) * 255 / 63;
                    b = (value & 31) * 255 / 31;
                }
                else
                {
                    unsigned value = ((const unsigned *)src)[x];
                    r = (value >> 16) & 255;
                    g = (value >> 8) & 255;
                    b = value & 255;
                }
                r = (r * 115 + 50) / 100;
                g = (g * 115 + 50) / 100;
                b = (b * 115 + 50) / 100;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                dst[x] = rgb(r, g, b);
            }
        }
        set_mode(sw, sh, false);
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
            const int status_height = 24;
            const int image_height = rc.bottom > status_height ? rc.bottom - status_height : rc.bottom;
            if (vga.text_mode)
            {
                // Preserve the legacy text renderer while reserving a status strip.
                SaveDC(dc);
                IntersectClipRect(dc, 0, 0, rc.right, image_height);
                render_text(dc);
                RestoreDC(dc, -1);
            }
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
                StretchDIBits(dc, 0, 0, rc.right, image_height, 0, 0, vga.width, vga.height,
                    vga.pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
            }

            // Persistent mode information, independent of framebuffer size.
            RECT status = { 0, image_height, rc.right, rc.bottom };
            HBRUSH brush = CreateSolidBrush(RGB(32, 32, 32));
            FillRect(dc, &status, brush);
            DeleteObject(brush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(240, 240, 240));
            char mode_text[96];
            unsigned bpp = vga.vbe_enabled ? vga.vbe_bpp :
                (vga.text_mode ? 4 : ((vga.seq[4] & 8) ? 8 : 4));
            if (vga.text_mode)
                wsprintfA(mode_text, "VGA TEXT  %u x %u  %u BPP", vga.width, vga.height, bpp);
            else
                wsprintfA(mode_text, "VGA  %u x %u  %u BPP", vga.width, vga.height, bpp);
            TextOutA(dc, 8, image_height + 4, mode_text, lstrlenA(mode_text));
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
        unsigned linear = (unsigned(vga.bank) * APERTURE_SIZE + (address & 0x3FFF)) & (VRAM_SIZE - 1);
        if (vga.vbe_enabled) return vga.vram[linear];
        if (vga.rgb565_mode)
            return vga.vram[linear];
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
        unsigned linear = (unsigned(vga.bank) * APERTURE_SIZE + (address & 0x3FFF)) & (VRAM_SIZE - 1);
        if (vga.vbe_enabled) { vga.vram[linear] = value; vga.dirty = true; return; }
        if (vga.rgb565_mode)
        {
            vga.vram[linear] = value;
            vga.dirty = true;
            return;
        }
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

void isa_vga_adapter_set_present(bool present)
{
    card_present = present;
    if (!present && vga.window)
        ShowWindow(vga.window, SW_HIDE);
}

bool isa_vga_adapter_port_read(unsigned port, unsigned char &value)
{
    if (!card_present) return false;
    port = normalize_port(port);
    if (port == 0x3E2) { value=vga.accel_index; return true; }
    if (port == 0x3E3) { value=vga.accel_regs[vga.accel_index&63]; return true; }
    if (port == 0x3E0) { value = (unsigned char)vga.vbe_index; return true; }
    if (port == 0x3E1) { ensure_window(); switch (vga.vbe_index) { case 0:value=(unsigned char)vga.vbe_regs[1];break; case 1:value=(unsigned char)(vga.vbe_regs[1]>>8);break; case 2:value=(unsigned char)vga.vbe_regs[2];break; case 3:value=(unsigned char)(vga.vbe_regs[2]>>8);break; case 4:value=(unsigned char)vga.vbe_regs[3];break; case 5:value=(unsigned char)vga.vbe_regs[4];break; case 6:value=vga.bank;break; default:value=0;break;} return true; }
    if (port < 0x3B0 || port > 0x3DF) return false;
    ensure_window();
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
        value = (vga.software_mode ? 1 : 0) | (vga.aperture ? 2 : 0) |
            ((vga.bank & 15) << 2) | (vga.rgb565_mode ? 0x40 : 0);
        return true;
    case 0x3DF: value = vga.bank; return true;
    default: return false;
    }
}

bool isa_vga_adapter_port_write(unsigned port, unsigned char value)
{
    if (!card_present) return false;
    port = normalize_port(port);
    if (port == 0x3E2) { vga.accel_index=value&63; return true; }
    if (port == 0x3E3) { unsigned i=vga.accel_index&63; vga.accel_regs[i]=value; if(i==0) execute_2d(value); if(i==0x34) { vga.dirty=true; if(vga.window) InvalidateRect(vga.window,nullptr,FALSE); } return true; }
    if (port == 0x3E0) { vga.vbe_index = value; return true; }
    if (port == 0x3E1) { ensure_window(); switch (vga.vbe_index) { case 0:vga.vbe_regs[1]=(vga.vbe_regs[1]&0xFF00)|value;break; case 1:vga.vbe_regs[1]=(vga.vbe_regs[1]&0x00FF)|(unsigned(value)<<8);break; case 2:vga.vbe_regs[2]=(vga.vbe_regs[2]&0xFF00)|value;break; case 3:vga.vbe_regs[2]=(vga.vbe_regs[2]&0x00FF)|(unsigned(value)<<8);break; case 4:vga.vbe_regs[3]=value;break; case 5:vga.vbe_regs[4]=value; if(value&1) configure_vbe_mode(vga.vbe_regs[1],vga.vbe_regs[2],vga.vbe_regs[3]); else {vga.vbe_enabled=0;vga.rgb565_mode=0;decode_mode();} break; case 6:vga.bank=value&(APERTURE_BANKS-1);break; default:break;} vga.dirty=true;return true; }
    if (port < 0x3B0 || port > 0x3DF) return false;
    ensure_window();
    if (port >= 0x3B0 && port <= 0x3DF && port != 0x3DE && port != 0x3DF)
    {
        vga.software_mode = 1;
        vga.vbe_enabled = 0;
        vga.rgb565_mode = 0;
    }

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
        vga.rgb565_mode = (value & 0x40) != 0;
        if (!vga.software_mode) { vga.aperture = 0; vga.vbe_enabled = 0; }
        if (vga.rgb565_mode && vga.software_mode) configure_vbe_mode(640, 480, 16);
        else decode_mode();
        vga.dirty = true;
        return true;
    case 0x3DF: vga.bank = value & (APERTURE_BANKS - 1); vga.dirty = true; return true;
    default: return false;
    }
}

bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value)
{
    if (!card_present || !vga.initialized || !vga.software_mode || !vga.aperture || (address & 0xC000) != 0xC000)
        return false;
    value = vga_mem_read(address);
    return true;
}

bool isa_vga_adapter_mem_write(unsigned address, unsigned char value)
{
    if (!card_present || !vga.initialized || !vga.software_mode || !vga.aperture || (address & 0xC000) != 0xC000)
        return false;
    vga_mem_write(address, value);
    return true;
}

void isa_vga_adapter_capture_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride, unsigned bits_per_pixel, bool yuy2)
{
    if (!card_present) return;
    ensure_window();
    if (!vga.software_mode)
        render_transparent(source, width, height, stride, bits_per_pixel, yuy2);
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
