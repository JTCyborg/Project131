#include "std.h"
#include "emul.h"
#include "vars.h"
#include "draw.h"
#include "vga_adapter.h"

namespace
{
    static const unsigned VRAM_SIZE = 1024 * 1024;
    static const unsigned APERTURE_SIZE = 16 * 1024;
    static const unsigned APERTURE_BANKS = VRAM_SIZE / APERTURE_SIZE;
    static const unsigned MAX_PIXELS = 1024 * 768;

    struct VGA_ADAPTER
    {
        unsigned char vram[VRAM_SIZE];
        unsigned char seq[16], gc[16], crtc[0x40], attr[0x20];
        // Trident TVGA8900D extension state, following PCem's vid_tvga.c.
        unsigned char tvga_3d8, tvga_3d9;
        unsigned char oldmode, oldctrl1, oldctrl2, newctrl2;
        unsigned char ramdac_state, ramdac_ctrl, bpp;
        unsigned read_bank, write_bank, vram_mask, banked_mask, display_mask;
        unsigned vram_size, plane_size;
        unsigned rowoffset, ma_latch, clock_hz;
        DWORD frame_epoch;
        bool interlace;
        unsigned char seq_index, gc_index, crtc_index, attr_index;
        unsigned char attr_data, attr_flip, attr_palette_enable;
        unsigned char misc_out, status, dac_status;
        unsigned char dac[256][3], dac_mask;
        unsigned char dac_read, dac_write, dac_component;
        unsigned char bank;
        unsigned char software_mode, aperture;
        unsigned char rgb565_mode;
        unsigned short vbe_index, vbe_regs[10];
        unsigned char vbe_enabled, vbe_bpp;
        unsigned short vbe_xres, vbe_yres, vbe_pitch;
        unsigned char latch[4];
        bool write_buffer_pending;
        unsigned write_buffer_address;
        unsigned char write_buffer_low;
        unsigned width, height;
        DWORD pixels[MAX_PIXELS];
        HWND window;
        bool initialized;
        bool dirty;
        bool text_mode;
        bool text_font_written;
        unsigned char accel_regs[64];
        unsigned char accel_index;
    };

    static VGA_ADAPTER vga = {};
    static bool card_present = true;

    static void trident_recalc_banking();
    static void trident_recalc_timings();

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

    static DWORD palette_color(unsigned index, bool attribute_map = false)
    {
        if (attribute_map)
        {
            unsigned mapped = vga.attr[index & 0x0F] & 0x3F;
            unsigned color_select = vga.attr[0x14] & 0x0F;
            if (vga.attr[0x10] & 0x80)
                mapped = (mapped & 0x0F) | (color_select << 4);
            else
                mapped = (mapped & 0x3F) | ((color_select & 0x0C) << 4);
            index = mapped;
        }
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
            // A previous graphics frame may leave arbitrary bytes in plane 2.
            // Only font data written after entering text mode is a valid text font.
            if (text)
                vga.text_font_written = false;
            // Drop the old frame on every graphics/text transition. WM_PAINT
            // will then redraw only the newly selected mode.
            if (text)
                ZeroMemory(vga.pixels, sizeof(vga.pixels));
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
        if (pitch * h > vga.vram_size) return false;
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

        if (vga.vbe_enabled)
        {
            set_mode(vga.vbe_xres, vga.vbe_yres, false);
            return;
        }

        if (vga.rgb565_mode)
        {
            set_mode(640, 480, false);
            return;
        }

        // Match PCem's SVGA mode selection: text requires both the
        // Graphics Controller alphanumeric mode and Attribute Controller
        // graphics-enable bit to be clear. A disabled attribute palette
        // blanks the output rather than changing the underlying mode.
        const bool text = !(vga.gc[6] & 1) && !(vga.attr[0x10] & 1);
        if (text)
        {
            set_mode(720, 400, true);
            return;
        }

        const unsigned graphics_mode = vga.gc[5] & 0x60;
        const bool packed = graphics_mode == 0x40 || graphics_mode == 0x60;
        if (packed && (vga.bpp == 8 || vga.bpp == 15 || vga.bpp == 16 || vga.bpp == 24 || vga.bpp == 32))
        {
            set_mode(640, 480, false);
            return;
        }

        if ((vga.seq[4] & 8) || (vga.seq[1] & 8))
            set_mode(320, 200, false);
        else
            set_mode(640, 480, false);
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
        else if (vga.bpp > 8 || (vga.oldctrl2 & 0x10) || ((vga.gc[5] & 0x60) >= 0x40 && vga.bpp == 8))
        {
            unsigned bytes = (vga.bpp + 7) >> 3;
            unsigned pitch = vga.rowoffset ? vga.rowoffset : vga.width * bytes;
            unsigned start = vga.ma_latch;
            for (unsigned y = 0; y < vga.height; ++y)
            {
                DWORD *dst = vga.pixels + y * vga.width;
                for (unsigned x = 0; x < vga.width; ++x)
                {
                    const unsigned char *p = vga.vram + ((start + y * pitch + x * bytes) & vga.display_mask);
                    unsigned r = 0, g = 0, b = 0;
                    if (vga.bpp == 8) { dst[x] = palette_color(p[0]); continue; }
                    if (vga.bpp == 15) { unsigned q=p[0] | (unsigned(p[1]) << 8); r=((q>>10)&31)*255/31; g=((q>>5)&31)*255/31; b=(q&31)*255/31; }
                    else if (vga.bpp == 16) { unsigned q=p[0] | (unsigned(p[1]) << 8); r=((q>>11)&31)*255/31; g=((q>>5)&63)*255/63; b=(q&31)*255/31; }
                    else if (vga.bpp == 24) { b=p[0]; g=p[1]; r=p[2]; }
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
                    dst[x] = palette_color(vga.vram[plane * vga.plane_size + offset]);
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
                        if (vga.vram[p * vga.plane_size + offset] & mask)
                            colour |= 1U << p;
                    dst[x] = palette_color(colour, true);
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
        return last <= vga.vram_size;
    }
    static bool accel_addr(unsigned base,unsigned pitch,unsigned x,unsigned y,unsigned bytes,unsigned &out)
    {
        if (x >= vga.vbe_xres || y >= vga.vbe_yres || pitch < (x + 1) * bytes)
            return false;
        unsigned long long a=(unsigned long long)base+(unsigned long long)y*pitch+(unsigned long long)x*bytes;
        if(a+bytes>vga.vram_size) return false;
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
        if(!vga.vbe_enabled || !vga.vbe_bpp) { vga.accel_regs[1]=0x81; return; }
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
        } else if(cmd==4) {
            // Horizontal Gradient Fill. Register 1Ch..1Fh is the start
            // pixel and 20h..23h is the end pixel. Interpolate each
            // stored pixel byte across the rectangle, then repeat per row.
            unsigned end_color=accel32(0x20);
            if (!accel_rect_valid(db,dp,dx,dy,w,h,b)) { vga.accel_regs[1]=0x82; return; }
            for(unsigned y=0;y<h;++y) {
                for(unsigned x=0;x<w;++x) {
                    unsigned a;
                    if(!accel_addr(db,dp,dx+x,dy+y,b,a)) { vga.accel_regs[1]=0x83; return; }
                    for(unsigned k=0;k<b;++k) {
                        unsigned c0=(color>>(k*8))&255;
                        unsigned c1=(end_color>>(k*8))&255;
                        unsigned value = w == 1 ? c0 :
                            (c0*(w-1-x)+c1*x+(w-1)/2)/(w-1);
                        vga.vram[a+k]=accel_rop((unsigned char)value,vga.vram[a+k],vga.accel_regs[2]);
                    }
                }
            }
        } else { vga.accel_regs[1]=0x84; return; }
        vga.accel_regs[1]=1;
        vga.dirty=true;
        if(vga.window) InvalidateRect(vga.window,nullptr,FALSE);
    }
    static void render_2d_cursor()
    {
        if(!(vga.accel_regs[0x34]&1)||!vga.vbe_enabled) return;
        unsigned x0=accel16(0x24), y0=accel16(0x26), base=accel32(0x28), c=accel32(0x2C);
        if (base > vga.vram_size - 32) return;
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
        // A mode switch must never leave the previous graphics framebuffer
        // visible underneath an empty or partially populated text screen.
        RECT client;
        GetClipBox(dc, &client);
        HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
        if (background)
        {
            FillRect(dc, &client, background);
            DeleteObject(background);
        }

        // VGA text rendering uses character/attribute planes plus the font
        // bitmap stored in plane 2. This deliberately avoids host fonts:
        // glyph shape, 8/9-dot width and cursor now come from VGA state.
        const unsigned start = (unsigned(vga.crtc[0x0C]) << 8) | vga.crtc[0x0D];
        unsigned pitch = unsigned(vga.crtc[0x13]) * 2;
        const unsigned text_base = 0x8000;
        const unsigned char_width = (vga.seq[1] & 1) ? 8 : 9;
        const unsigned char_height = (vga.crtc[9] & 0x1F) + 1;
        unsigned columns = unsigned(vga.crtc[1]) + 1;
        unsigned vertical = unsigned(vga.crtc[0x12]) |
            (unsigned(vga.crtc[7] & 0x02) << 7) |
            (unsigned(vga.crtc[7] & 0x40) << 3);
        unsigned rows = (vertical + 1) / char_height;
        if (!pitch) pitch = columns;
        if (!columns || columns > 160) columns = 80;
        if (!rows || rows > 100) rows = 25;
        if (pitch < columns) pitch = columns;

        // VGA Sequencer Character Map Select: two independently selectable
        // 8-KiB font maps. The selected map is addressed in plane 2.
        const unsigned map_a = ((vga.seq[3] & 0x03) |
            ((vga.seq[3] & 0x10) >> 2)) * 0x2000;
        const unsigned map_b = (((vga.seq[3] >> 2) & 0x03) |
            ((vga.seq[3] & 0x20) >> 3)) * 0x2000;
        // Never infer font validity from the contents of VRAM: a previous
        // packed-pixel frame can leave non-zero garbage in plane 2. A font is
        // considered present only after the guest writes plane 2 in text mode.
        const bool font_loaded = vga.text_font_written;
        HFONT fallback_font = nullptr;
        HGDIOBJ old_font = nullptr;
        if (!font_loaded)
        {
            fallback_font = CreateFontA(-int(char_height), int(char_width), 0, 0,
                FW_NORMAL, FALSE, FALSE, FALSE, OEM_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                FIXED_PITCH | FF_MODERN, "Terminal");
            if (fallback_font)
                old_font = SelectObject(dc, fallback_font);
            else
            {
                fallback_font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
                if (fallback_font)
                    old_font = SelectObject(dc, fallback_font);
            }
        }

        const bool blink_enabled = (vga.attr[0x10] & 0x08) != 0;
        const bool blink_phase = ((GetTickCount() / 500) & 1) != 0;
        const unsigned cursor_address = (unsigned(vga.crtc[0x0E]) << 8) | vga.crtc[0x0F];
        const unsigned cursor_start = vga.crtc[0x0A] & 0x1F;
        const unsigned cursor_end = vga.crtc[0x0B] & 0x1F;
        const bool cursor_enabled = !(vga.crtc[0x0A] & 0x20) &&
            (((GetTickCount() / 500) & 1) == 0);

        for (unsigned y = 0; y < rows; ++y)
        {
            for (unsigned x = 0; x < columns; ++x)
            {
                const unsigned cell = (text_base + start + y * pitch + x) & (vga.plane_size - 1);
                const unsigned char ch = vga.vram[cell];
                const unsigned char at = vga.vram[vga.plane_size + cell];
                const unsigned fg = at & 0x0F;
                const unsigned bg = blink_enabled ? ((at >> 4) & 7) : ((at >> 4) & 0x0F);
                const bool hidden = blink_enabled && (at & 0x80) && blink_phase;
                const unsigned map_base = ((at & 0x08) && (map_a != map_b)) ? map_b : map_a;
                const unsigned glyph_base = map_base + unsigned(ch) * 32;
                const bool cursor_here = cursor_enabled &&
                    (((start + y * pitch + x) & 0xFFFF) == cursor_address);
                const DWORD fg_color = palette_color(fg, true);
                const DWORD bg_color = palette_color(bg, true);
                if (!font_loaded && fallback_font)
                {
                    char text_char = hidden ? ' ' : (ch ? char(ch) : ' ');
                    SetTextColor(dc, fg_color);
                    SetBkColor(dc, bg_color);
                    TextOutA(dc, int(x * char_width), int(y * char_height),
                        &text_char, 1);
                    continue;
                }
                for (unsigned sy = 0; sy < char_height; ++sy)
                {
                    const unsigned py = y * char_height + sy;
                    const unsigned font_row = sy & 31;
                    unsigned char bits = hidden ? 0 :
                        vga.vram[2 * vga.plane_size + ((glyph_base + font_row) & (vga.plane_size - 1))];
                    const bool cursor_line = cursor_here &&
                        (cursor_start <= cursor_end ?
                            (sy >= cursor_start && sy <= cursor_end) :
                            (sy >= cursor_start || sy <= cursor_end));
                    for (unsigned sx = 0; sx < char_width; ++sx)
                    {
                        bool on = sx < 8 && ((bits >> (7 - sx)) & 1);
                        // VGA's ninth dot repeats bit 0 for line-drawing
                        // characters C0h-DFh when line-graphics is enabled.
                        if (sx == 8 && (vga.attr[0x10] & 0x04) &&
                            ch >= 0xC0 && ch <= 0xDF)
                            on = (bits & 1) != 0;
                        if (cursor_line) on = !on;
                        SetPixelV(dc, int(x * char_width + sx), int(py),
                            on ? fg_color : bg_color);
                    }
                }
            }
        }
        if (old_font)
            SelectObject(dc, old_font);
        if (fallback_font)
            DeleteObject(fallback_font);
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
                (vga.bpp ? vga.bpp : ((vga.seq[4] & 8) ? 8 : 4));
            if (vga.text_mode)
                wsprintfA(mode_text, "VGA TEXT  %u x %u  CRTC", vga.width, vga.height);
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
        unsigned memory_kb = GetPrivateProfileInt("ISA_VGA", "MemoryKB", 1024, ininame);
        if (memory_kb != 256 && memory_kb != 512 && memory_kb != 1024)
            memory_kb = 1024;
        vga.vram_size = memory_kb * 1024;
        vga.plane_size = vga.vram_size / 4;
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
        {
            memcpy(vga.dac[i], ega_palette[i], 3);
            vga.attr[i] = (unsigned char)i;
        }

        // Power-on VGA register state used by the transparent bridge.
        vga.seq[2] = 0x0F;
        vga.seq[4] = 0x0E;
        vga.gc[6] = 0x05;
        vga.misc_out = 1;
        vga.bpp = 8;
        vga.vram_mask = vga.vram_size - 1;
        vga.banked_mask = 0x1FFFF;
        vga.display_mask = 0x3FFFF;
        vga.rowoffset = 0x100;
        vga.ma_latch = 0;
        vga.clock_hz = 36000000;
        vga.ramdac_ctrl = 0;
        trident_recalc_timings();
        trident_recalc_banking();
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

    static void trident_recalc_banking()
    {
        vga.write_bank = unsigned(vga.tvga_3d8 & 0x1F) * 65536U;
        vga.read_bank = (vga.gc[0x0F] & 1)
            ? unsigned(vga.tvga_3d9 & 0x1F) * 65536U
            : vga.write_bank;
    }

    static unsigned char trident_crtc_mask(unsigned index)
    {
        static const unsigned char masks[0x40] = {
            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f,0xff,0x3f,0x7f,0xff,0xff,0xff,0xff,
            0xff,0xff,0xff,0xff,0x7f,0xff,0xff,0xef,0xff,0x00,0x00,0x00,0x00,0x00,0xff,0xff,
            0x7f,0x00,0x00,0x2f,0x00,0x00,0x00,0x03,0x00,0x13,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
        };
        return masks[index & 0x3F];
    }

    static void trident_recalc_timings()
    {
        unsigned rowoffset = unsigned(vga.crtc[0x13]) << 3;
        if (!rowoffset) rowoffset = 0x100;
        if (vga.crtc[0x29] & 0x10) rowoffset += 0x100;

        unsigned ma = (unsigned(vga.crtc[0x0C]) << 8) | vga.crtc[0x0D];
        if ((vga.crtc[0x1E] & 0xA0) == 0xA0) ma |= 0x10000;
        if (vga.crtc[0x27] & 0x01) ma |= 0x20000;
        if (vga.crtc[0x27] & 0x02) ma |= 0x40000;

        if (vga.oldctrl2 & 0x10) { rowoffset <<= 1; ma <<= 1; }
        vga.interlace = (vga.crtc[0x1E] & 0x04) != 0;
        if (vga.interlace) rowoffset >>= 1;
        vga.rowoffset = rowoffset;
        vga.ma_latch = ma;

        unsigned clksel = ((vga.misc_out >> 2) & 3) |
            ((vga.newctrl2 & 1) << 2) | ((vga.oldctrl1 & 0x10) >> 1);
        static const unsigned clocks[16] = {
            0,0,44900000,36000000,57272000,65000000,50350000,40000000,
            88000000,98000000,118800000,108000000,72000000,77000000,80000000,75000000
        };
        vga.clock_hz = clocks[clksel & 0x0F];
        vga.frame_epoch = GetTickCount();
    }

    static unsigned char trident_status1()
    {
        if (!vga.clock_hz) return vga.status & ~8;
        unsigned htotal = (unsigned(vga.crtc[0]) + 5) * 8;
        unsigned vtotal = unsigned(vga.crtc[6]) |
            ((unsigned(vga.crtc[7] & 0x01)) << 8) |
            ((unsigned(vga.crtc[7] & 0x20)) << 4);
        vtotal += 2;
        if (!htotal || vtotal < 16) return vga.status & ~8;

        unsigned long long frame_pixels = (unsigned long long)htotal * vtotal;
        unsigned frame_ms = (unsigned)(frame_pixels * 1000ULL / vga.clock_hz);
        if (!frame_ms) frame_ms = 1;
        unsigned vsync_start = unsigned(vga.crtc[0x10]) |
            ((unsigned(vga.crtc[7] & 0x04)) << 6);
        unsigned retrace_lines = (unsigned(vga.crtc[0x11] & 0x0F) -
            unsigned(vga.crtc[0x10] & 0x0F)) & 0x0F;
        if (!retrace_lines) retrace_lines = 16;

        unsigned phase_ms = (GetTickCount() - vga.frame_epoch) % frame_ms;
        unsigned scanline = (unsigned)((unsigned long long)phase_ms * vtotal / frame_ms);
        if (scanline >= vsync_start && scanline < vsync_start + retrace_lines)
            vga.status |= 8;
        else
            vga.status &= ~8;
        return vga.status;
    }

    static unsigned char vga_mem_read(unsigned address)
    {
        unsigned bridge_linear = unsigned(vga.bank) * APERTURE_SIZE + (address & 0x3FFF);
        unsigned linear = (vga.read_bank + bridge_linear) & vga.vram_mask;
        if (vga.vbe_enabled || vga.bpp > 8 || (vga.oldctrl2 & 0x10) || vga.rgb565_mode)
            return vga.vram[linear];
        if (vga.seq[4] & 8)
        {
            unsigned plane = linear & 3;
            unsigned offset = (linear >> 2) & (vga.plane_size - 1);
            return vga.vram[plane * vga.plane_size + offset];
        }

        // In planar modes the Trident 64-KiB bank selector advances the
        // per-plane address by 16 KiB; the Z80 bridge bank remains a planar
        // byte offset to preserve the emulator's 16-KiB window semantics.
        unsigned offset = ((vga.read_bank >> 2) + (bridge_linear & 0xFFFF)) & (vga.plane_size - 1);
        for (unsigned p = 0; p < 4; ++p)
            vga.latch[p] = vga.vram[p * vga.plane_size + offset];
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
        unsigned bridge_linear = unsigned(vga.bank) * APERTURE_SIZE + (address & 0x3FFF);
        unsigned linear = (vga.write_bank + bridge_linear) & vga.vram_mask;
        if (vga.vbe_enabled || vga.bpp > 8 || (vga.oldctrl2 & 0x10) || vga.rgb565_mode) { vga.vram[linear] = value; vga.dirty = true; return; }
        if (vga.seq[4] & 8)
        {
            unsigned plane = linear & 3;
            unsigned offset = (linear >> 2) & 0xFFFF;
            if (vga.seq[2] & (1U << plane))
                vga.vram[plane * vga.plane_size + offset] = value;
            vga.dirty = true;
            return;
        }

        unsigned offset = ((vga.write_bank >> 2) + (bridge_linear & 0xFFFF)) & (vga.plane_size - 1);
        unsigned rotated = rotate_right(value, vga.gc[3] & 7);
        unsigned mode = vga.gc[5] & 3;
        unsigned logical = (vga.gc[3] >> 3) & 3;
        unsigned bitmask = vga.gc[8];
        for (unsigned p = 0; p < 4; ++p)
        {
            if (!(vga.seq[2] & (1U << p)))
                continue;
            if (vga.text_mode && p == 2)
                vga.text_font_written = true;
            // VGA write mode 1 is a direct latch transfer. It bypasses
            // rotate, set/reset, logical-function and bit-mask processing.
            if (mode == 1)
            {
                vga.vram[p * vga.plane_size + offset] = vga.latch[p];
                continue;
            }

            unsigned char src =
                (vga.gc[1] & (1U << p))
                    ? ((vga.gc[0] & (1U << p)) ? 0xFF : 0)
                    : (unsigned char)rotated;
            unsigned char mask = (unsigned char)bitmask;
            if (mode == 2)
                src = (value & (1U << p)) ? 0xFF : 0;
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
            vga.vram[p * vga.plane_size + offset] = (unsigned char)((data & mask) | (old & ~mask));
        }
        vga.dirty = true;
    }

    // Z80 writes are 8-bit, while packed 15/16-bpp pixels are 16-bit.
    // Hold an aligned low byte and combine the immediately following high
    // byte into one adapter-side 16-bit VRAM commit.
    static void flush_write_buffer()
    {
        if (!vga.write_buffer_pending)
            return;
        vga.vram[vga.write_buffer_address] = vga.write_buffer_low;
        vga.write_buffer_pending = false;
        vga.dirty = true;
    }
}

void isa_vga_adapter_set_present(bool present)
{
    if (!present) flush_write_buffer();
    card_present = present;
    if (!present && vga.window)
        ShowWindow(vga.window, SW_HIDE);
}

bool isa_vga_adapter_port_read(unsigned port, unsigned char &value)
{
    if (!card_present) return false;
    flush_write_buffer();
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
    case 0x3C0: value = (vga.attr_index & 0x1F) | (vga.attr_palette_enable ? 0x20 : 0); return true;
    case 0x3C1: value = vga.attr[vga.attr_index & 0x1F]; return true;
    case 0x3C2:
        value = ((unsigned(vga.dac[0][0]) + vga.dac[0][1] + vga.dac[0][2]) >= 0x50) ? 0x00 : 0x10;
        return true;
    case 0x3C4: value = vga.seq_index; return true;
    case 0x3C5:
        if ((vga.seq_index & 0x0F) == 0x0B) { vga.oldmode = 0; value = 0x33; return true; }
        if ((vga.seq_index & 0x0F) == 0x0C) { value = vga.seq[0x0C]; return true; }
        if ((vga.seq_index & 0x0F) == 0x0D) { value = vga.oldmode ? vga.oldctrl2 : vga.newctrl2; return true; }
        if ((vga.seq_index & 0x0F) == 0x0E && vga.oldmode) { value = vga.oldctrl1; return true; }
        value = vga.seq[vga.seq_index & 0x0F]; return true;
    case 0x3CC: value = vga.misc_out; return true;
    case 0x3CE: value = vga.gc_index; return true;
    case 0x3CF:
        if ((vga.gc_index & 0x0F) == 0x0E) { value = vga.gc[0x0E]; return true; }
        if ((vga.gc_index & 0x0F) == 0x0F) { value = vga.gc[0x0F]; return true; }
        value = vga.gc[vga.gc_index & 0x0F]; return true;
    case 0x3D4: value = vga.crtc_index; return true;
    case 0x3D5: value = (vga.crtc_index > 0x18 && vga.crtc_index < 0x1E) ? 0xFF : vga.crtc[vga.crtc_index & 0x3F]; return true;
    case 0x3C6:
        if (vga.ramdac_state == 4) { value = vga.ramdac_ctrl; return true; }
        ++vga.ramdac_state; value = vga.dac_mask; return true;
    case 0x3C7: vga.ramdac_state = 0; value = vga.dac_status; return true;
    case 0x3C8: vga.ramdac_state = 0; value = vga.dac_write; return true;
    case 0x3C9:
        vga.ramdac_state = 0;
        vga.dac_status = 3;
        value = vga.dac[vga.dac_read][vga.dac_component++] & 63;
        if (vga.dac_component == 3) { vga.dac_component = 0; ++vga.dac_read; }
        return true;
    case 0x3DA:
        vga.attr_flip = 0;
        value = trident_status1();
        return true;
    case 0x3DE:
        value = (vga.software_mode ? 1 : 0) | (vga.aperture ? 2 : 0) |
            ((vga.bank & 15) << 2) | (vga.rgb565_mode ? 0x40 : 0);
        return true;
    case 0x3DF: value = vga.bank; return true;
    case 0x3D8: value = vga.tvga_3d8; return true;
    case 0x3D9: value = vga.tvga_3d9; return true;
    default: return false;
    }
}

bool isa_vga_adapter_port_write(unsigned port, unsigned char value)
{
    if (!card_present) return false;
    flush_write_buffer();
    port = normalize_port(port);
    if (port == 0x3E2) { ensure_window(); vga.accel_index=value&63; return true; }
    if (port == 0x3E3) { ensure_window(); unsigned i=vga.accel_index&63; vga.accel_regs[i]=value; if(i==0) execute_2d(value); if(i==0x34) { vga.dirty=true; if(vga.window) InvalidateRect(vga.window,nullptr,FALSE); } return true; }
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
        if (!vga.attr_flip) {
            vga.attr_index = value & 0x1F;
            vga.attr_palette_enable = (value & 0x20) != 0;
        }
        else vga.attr[vga.attr_index & 0x1F] = value;
        vga.attr_flip ^= 1;
        if (vga.attr_flip && (vga.attr_index == 0x10 || vga.attr_index == 0x12 || vga.attr_index == 0x14))
            trident_recalc_timings();
        decode_mode();
        vga.dirty = true;
        return true;
    case 0x3C2: vga.misc_out = value; trident_recalc_timings(); decode_mode(); return true;
    case 0x3C4: vga.seq_index = value & 0x0F; return true;
    case 0x3C5:
        switch (vga.seq_index & 0x0F)
        {
        case 0x0B: vga.oldmode = 1; return true;
        case 0x0C: if (vga.seq[0x0C] & 0x80) vga.seq[0x0C] = value; return true;
        case 0x0D:
            if (vga.oldmode) vga.oldctrl2 = value;
            else vga.newctrl2 = value;
            trident_recalc_timings();
            break;
        case 0x0E:
            if (vga.oldmode) vga.oldctrl1 = value;
            else { vga.seq[0x0E] = value ^ 2; vga.tvga_3d8 = vga.seq[0x0E] & 0x0F; trident_recalc_banking(); }
            trident_recalc_timings();
            break;
        case 0x01: vga.seq[1] = value; break;
        case 0x02: vga.seq[2] = value & 0x0F; break;
        case 0x03: vga.seq[3] = value; break;
        case 0x04: vga.seq[4] = value & 0x0F; break;
        default: vga.seq[vga.seq_index & 0x0F] = value; break;
        }
        decode_mode(); vga.dirty = true; return true;
    case 0x3CE: vga.gc_index = value & 15; return true;
    case 0x3CF:
        switch (vga.gc_index & 0x0F)
        {
        case 0x06:
            { unsigned char old = vga.gc[6]; vga.gc[6] = value;
              if ((old & 0x0C) && !(value & 0x0C)) vga.banked_mask = 0x1FFFF; }
            break;
        case 0x0E: vga.gc[0x0E] = value ^ 2; vga.tvga_3d9 = vga.gc[0x0E] & 0x0F; trident_recalc_banking(); break;
        case 0x0F: vga.gc[0x0F] = value; trident_recalc_banking(); trident_recalc_timings(); break;
        case 0x02: vga.gc[2] = value; break;
        case 0x04: vga.gc[4] = value & 3; break;
        case 0x05: vga.gc[5] = value; break;
        case 0x07: vga.gc[7] = value; break;
        case 0x08: vga.gc[8] = value; break;
        default: vga.gc[vga.gc_index & 0x0F] = value; break;
        }
        decode_mode(); vga.dirty = true; return true;
    case 0x3D4: vga.crtc_index = value & 0x3F; return true;
    case 0x3D5:
        if ((vga.crtc_index < 7) && (vga.crtc[0x11] & 0x80)) return true;
        if ((vga.crtc_index == 7) && (vga.crtc[0x11] & 0x80))
            value = (vga.crtc[7] & ~0x10) | (value & 0x10);
        vga.crtc[vga.crtc_index & 0x3F] = value & trident_crtc_mask(vga.crtc_index);
        if (vga.crtc_index == 0x1E)
            vga.display_mask = (value & 0x80) ? vga.vram_mask : 0x3FFFF;
        trident_recalc_timings(); decode_mode(); vga.dirty = true; return true;
    case 0x3C6:
        if (vga.ramdac_state == 4) {
            vga.ramdac_state = 0; vga.ramdac_ctrl = value;
            switch (value >> 5) { case 0: case 1: case 2: case 3: vga.bpp=8; break; case 5:vga.bpp=15;break; case 6:vga.bpp=24;break; case 7:vga.bpp=16;break; }
            vga.dirty = true; trident_recalc_timings(); decode_mode(); return true;
        }
        vga.dac_mask = value; return true;
    case 0x3C7: vga.ramdac_state=0; vga.dac_read = value; vga.dac_component = 0; vga.dac_status = 3; return true;
    case 0x3C8: vga.ramdac_state=0; vga.dac_write = value; vga.dac_read = (value - 1) & 255; vga.dac_component = 0; vga.dac_status = 0; return true;
    case 0x3C9:
        vga.ramdac_state=0;
        vga.dac_status = 0;
        vga.dac[vga.dac_write][vga.dac_component++] = value & 63;
        if (vga.dac_component == 3) { vga.dac_component = 0; vga.dac_write = (vga.dac_write + 1) & 255; }
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
    case 0x3D8: if (vga.gc[0x0F] & 4) { vga.tvga_3d8=value; trident_recalc_banking(); } return true;
    case 0x3D9: if (vga.gc[0x0F] & 4) { vga.tvga_3d9=value; trident_recalc_banking(); } return true;
    case 0x3DB:
        vga.misc_out = (vga.misc_out & ~0x0C) | ((value & 3) << 2);
        vga.newctrl2 = (vga.newctrl2 & ~1) | ((value & 4) >> 2);
        vga.oldctrl1 = (vga.oldctrl1 & ~0x10) | ((value & 8) << 1);
        trident_recalc_timings(); decode_mode(); vga.dirty = true; return true;
    default: return false;
    }
}

bool isa_vga_adapter_mem_read(unsigned address, unsigned char &value)
{
    // Ordinary RAM reads cannot observe the adapter's pending VRAM byte.
    // Avoid flushing it on every source read (notably every LDIR iteration).
    if (!card_present || !vga.initialized || !vga.software_mode || !vga.aperture || (address & 0xC000) != 0xC000)
        return false;
    flush_write_buffer();
    value = vga_mem_read(address);
    return true;
}

bool isa_vga_adapter_mem_write(unsigned address, unsigned char value)
{
    // Writes outside the VGA aperture are unrelated to VRAM ordering.
    // Keep the pending low byte so sequential packed-pixel writes can pair.
    if (!card_present || !vga.initialized || !vga.software_mode || !vga.aperture || (address & 0xC000) != 0xC000)
        return false;

    const bool packed16 = (vga.vbe_enabled && (vga.vbe_bpp == 15 || vga.vbe_bpp == 16)) ||
        (!vga.vbe_enabled && (vga.bpp == 15 || vga.bpp == 16));
    if (packed16)
    {
        unsigned linear = (vga.write_bank + unsigned(vga.bank) * APERTURE_SIZE +
            (address & 0x3FFF)) & vga.vram_mask;
        if (vga.write_buffer_pending &&
            linear == vga.write_buffer_address + 1)
        {
            // Complete the little-endian pixel as a single 16-bit commit.
            vga.vram[vga.write_buffer_address] = vga.write_buffer_low;
            vga.vram[linear] = value;
            vga.write_buffer_pending = false;
            vga.dirty = true;
            return true;
        }

        flush_write_buffer();
        // Only buffer an aligned low byte. Do not pair across the 16-KiB
        // aperture boundary, where the Z80 address wraps to another bank.
        if (!(address & 1) && (address & 0x3FFF) != 0x3FFF)
        {
            vga.write_buffer_address = linear;
            vga.write_buffer_low = value;
            vga.write_buffer_pending = true;
            return true;
        }
    }
    else
        flush_write_buffer();

    vga_mem_write(address, value);
    return true;
}

void isa_vga_adapter_capture_frame(const unsigned char *source, unsigned width, unsigned height, unsigned stride, unsigned bits_per_pixel, bool yuy2)
{
    if (!card_present) return;
    flush_write_buffer();
    ensure_window();
    if (!vga.software_mode)
        render_transparent(source, width, height, stride, bits_per_pixel, yuy2);
    if (vga.window && vga.dirty)
        InvalidateRect(vga.window, nullptr, FALSE);
}

void isa_vga_adapter_done()
{
    flush_write_buffer();
    if (vga.window)
    {
        DestroyWindow(vga.window);
        vga.window = nullptr;
    }
    vga.initialized = false;
}
