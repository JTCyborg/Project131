/* Project131 isolated IBM VGA adapter, 256 KiB VRAM.
 * Initial display support: 80x25 text and VGA mode 13h.
 * Independent of the ATM/Spectrum video pipeline. SPDX-License-Identifier: GPL-2.0
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i486.h"
#include "vga.h"

namespace i486 { namespace vga {
namespace {
static uint8_t vram[256 * 1024];
static uint8_t seq[5], gc[9], crtc[0x19], attr[32], dac[256][3];
static uint8_t seq_index, gc_index, crtc_index, attr_index, dac_write_index, dac_component;
static uint8_t attr_data_phase, status_toggle, dac_mask = 0xFF;
static HWND window_handle;
static unsigned long a000_write_count, b800_write_count, logged_video_writes;
static const char *ClassName = "PROJECT131_I486_VGA";
static uint8_t bda_mode() {
    uint8_t m=3; uint8_t *ram=i486::detail::ram_data();
    if(ram && i486::detail::ram_size_bytes()>0x449) m=ram[0x449];
    return m;
}
static void render(HDC dc,const RECT &rc) {
    if(bda_mode()==0x13) {
        BITMAPINFO bmi; uint8_t rgb[320*200*3]; unsigned int x,y;
        memset(&bmi,0,sizeof(bmi)); bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=320; bmi.bmiHeader.biHeight=-200;
        bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=24; bmi.bmiHeader.biCompression=BI_RGB;
        for(y=0;y<200;y++) for(x=0;x<320;x++) {
            uint8_t p=vram[y*320+x]&dac_mask;
            rgb[(y*320+x)*3+2]=(uint8_t)(dac[p][0]*255U/63U);
            rgb[(y*320+x)*3+1]=(uint8_t)(dac[p][1]*255U/63U);
            rgb[(y*320+x)*3]=(uint8_t)(dac[p][2]*255U/63U);
        }
        StretchDIBits(dc,0,0,rc.right,rc.bottom,0,0,320,200,rgb,&bmi,DIB_RGB_COLORS,SRCCOPY);
    } else {
        HFONT font=CreateFontA(-16,8,0,0,FW_NORMAL,FALSE,FALSE,FALSE,OEM_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,
            FIXED_PITCH|FF_MODERN,"Terminal");
        HGDIOBJ old=SelectObject(dc,font); SetBkColor(dc,RGB(0,0,0));
        SetTextColor(dc,RGB(192,192,192));
        for(unsigned int y=0;y<25;y++) {
            char line[81];
            for(unsigned int x=0;x<80;x++) {
                uint8_t ch=vram[0x18000+(y*80+x)*2];
                line[x]=(ch>=32 && ch<127)?(char)ch:' ';
            }
            line[80]=0; TextOutA(dc,4,(int)y*18,line,80);
        }
        SelectObject(dc,old); DeleteObject(font);
    }
}
static LRESULT CALLBACK VgaWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps; RECT rc; HDC dc=BeginPaint(hwnd,&ps); GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(0,0,0)); FillRect(dc,&rc,bg); DeleteObject(bg);
        render(dc,rc); EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_CLOSE) { ShowWindow(hwnd,SW_HIDE); return 0; }
    return DefWindowProc(hwnd,msg,wp,lp);
}
static void refresh() { if(window_handle && IsWindow(window_handle)) InvalidateRect(window_handle,0,FALSE); }
}
void init() {
    WNDCLASSEXA wc; memset(&wc,0,sizeof(wc)); wc.cbSize=sizeof(wc);
    wc.lpfnWndProc=VgaWndProc; wc.hInstance=GetModuleHandleA(0);
    wc.hCursor=LoadCursorA(0,IDC_ARROW); wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=ClassName; RegisterClassExA(&wc);
    memset(vram,0,sizeof(vram)); memset(seq,0,sizeof(seq)); memset(gc,0,sizeof(gc));
    memset(crtc,0,sizeof(crtc)); memset(attr,0,sizeof(attr)); status_toggle=0;
    a000_write_count=0; b800_write_count=0; logged_video_writes=0;
    for(unsigned int i=0;i<256;i++) for(unsigned int c=0;c<3;c++) dac[i][c]=(uint8_t)(i&63);
    window_handle=CreateWindowExA(0,ClassName,"PC Screen - i486 VGA",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,656,519,0,0,GetModuleHandleA(0),0);
    if(window_handle) ShowWindow(window_handle,SW_SHOWNORMAL);
    i486::log("[I486][VGA] initialized: 256 KiB VRAM, separate PC display window");
}
void dump_text() {
    unsigned int y, x;
    char summary[160];
    sprintf(summary, "[I486][VGA] STATE BDA_MODE=%02X A000_WRITES=%lu B800_WRITES=%lu WINDOW=%p",
        bda_mode(), a000_write_count, b800_write_count, (void *)window_handle);
    i486::log(summary);
    i486::log("[I486][VGA] TEXT BUFFER BEGIN (80x25, B8000)");
    for (y = 0; y < 25; ++y) {
        char line[128];
        char *out = line;
        out += sprintf(out, "[I486][VGA][%02u] ", y);
        for (x = 0; x < 80; ++x) {
            uint8_t ch = vram[0x18000 + (y * 80 + x) * 2];
            *out++ = (ch >= 32 && ch < 127) ? (char)ch : ' ';
        }
        *out = 0;
        i486::log(line);
    }
    i486::log("[I486][VGA] TEXT BUFFER END");
}
void close() { if(window_handle && IsWindow(window_handle)) DestroyWindow(window_handle); window_handle=0; }
uint8_t memory_read(uint32_t a,bool *handled) {
    *handled=false;
    if(a>=0xA0000 && a<=0xAFFFF) { *handled=true; return vram[a-0xA0000]; }
    if(a>=0xB8000 && a<=0xBFFFF) { *handled=true; return vram[0x18000+(a-0xB8000)]; }
    return 0xFF;
}
void memory_write(uint32_t a,uint8_t v,bool *handled) {
    *handled=false;
    if(a>=0xA0000 && a<=0xAFFFF) {
        vram[a-0xA0000]=v; *handled=true; ++a000_write_count;
    }
    else if(a>=0xB8000 && a<=0xBFFFF) {
        vram[0x18000+(a-0xB8000)]=v; *handled=true; ++b800_write_count;
    }
    if (*handled) {
        if (logged_video_writes < 16) {
            char message[128];
            sprintf(message, "[I486][VGA] MEM_WRITE #%lu ADDR=%08X VALUE=%02X",
                ++logged_video_writes, a, v);
            i486::log(message);
        }
        refresh();
    }
}
uint8_t in8(uint16_t p,bool *handled) {
    *handled=true;
    switch(p) {
    case 0x3C0: return (uint8_t)(attr_index|(attr_data_phase?0x20:0));
    case 0x3C1: return attr[attr_index&0x1F];
    case 0x3C2: return 0x10;
    case 0x3C4: return seq_index;
    case 0x3C5: return seq[seq_index%5];
    case 0x3C6: return dac_mask;
    case 0x3C7: return 0;
    case 0x3C8: return dac_write_index;
    case 0x3C9: { uint8_t v=dac[dac_write_index][dac_component]; if(++dac_component==3){dac_component=0;dac_write_index++;} return v; }
    case 0x3CE: return gc_index;
    case 0x3CF: return gc[gc_index%9];
    case 0x3D4: case 0x3B4: return crtc_index;
    case 0x3D5: case 0x3B5: return crtc[crtc_index%0x19];
    case 0x3DA: case 0x3BA: attr_data_phase=0; status_toggle^=0x08; return status_toggle;
    default: *handled=false; return 0xFF;
    }
}
void out8(uint16_t p,uint8_t v,bool *handled) {
    *handled=true;
    switch(p) {
    case 0x3C0: if(!attr_data_phase) attr_index=v&0x1F; else attr[attr_index&0x1F]=v; attr_data_phase^=1; break;
    case 0x3C2: break;
    case 0x3C4: seq_index=v; break;
    case 0x3C5: seq[seq_index%5]=v; break;
    case 0x3C6: dac_mask=v; break;
    case 0x3C7: dac_write_index=v; dac_component=0; break;
    case 0x3C8: dac_write_index=v; dac_component=0; break;
    case 0x3C9: dac[dac_write_index][dac_component]=v&0x3F; if(++dac_component==3){dac_component=0;dac_write_index++;} refresh(); break;
    case 0x3CE: gc_index=v; break;
    case 0x3CF: gc[gc_index%9]=v; break;
    case 0x3D4: case 0x3B4: crtc_index=v; break;
    case 0x3D5: case 0x3B5: crtc[crtc_index%0x19]=v; break;
    case 0x3DA: case 0x3BA: attr_data_phase=0; break;
    default: *handled=false; break;
    }
}
} }
