/*
 * Project131 i486DX isolated PC-compatible I/O devices.
 * UART 16550A-compatible COM1, SPP LPT1, and NE2000-compatible NIC register
 * and remote-DMA core. Host packet transport is intentionally a separate
 * backend and is not reported as connected until SLiRP is wired in.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <stdio.h>
#include <string.h>
#include "i486.h"
#include "devices.h"

namespace i486 { namespace devices {
namespace {
struct Uart {
    uint8_t dll, dlm, ier, iir, fcr, lcr, mcr, lsr, msr, scr, rbr;
    uint8_t tx[4096];
    unsigned int tx_count;
} com1;

struct Parallel {
    uint8_t data, control;
} lpt1;

struct Ne2000 {
    uint8_t command, pstart, pstop, boundary, tsr, tpsr, tbcr0, tbcr1;
    uint8_t isr, rsar0, rsar1, rbcr0, rbcr1, rcr, tcr, dcr, imr;
    uint8_t curr, par[6], mar[8], rsr, tally[3];
    uint8_t ram[32768];
    uint8_t mac[6];
    uint8_t tx_no_backend_logged;
} nic;

static unsigned int io_trace_count;
/* XT-compatible system board ports used by the bundled legacy BIOS.
 * Port 62h is the 8255 input/status port; 61h is the system-control latch.
 * The emulated machine has 640 KiB conventional-memory configuration and
 * an installed 486DX x87 unit. No parity/channel-check faults are injected.
 */
static uint8_t system_control_b;
static uint8_t system_refresh_phase;
static uint8_t system_control_a = 0x0E; /* 640 KiB + coprocessor, no POST loop */
static void trace(const char *s) { i486::log(s); }

static void uart_reset()
{
    memset(&com1, 0, sizeof(com1));
    com1.iir = 1;
    com1.lsr = 0x60; /* THRE | TEMT */
    com1.msr = 0xB0; /* CTS, DSR, DCD asserted */
}

static void nic_reset()
{
    memset(&nic, 0, sizeof(nic));
    nic.command = 0x21; /* STOP, no DMA */
    nic.isr = 0x80;     /* reset status */
    nic.dcr = 0x48;
    nic.rcr = 0x20;
    nic.pstart = 0x40;
    nic.pstop = 0x80;
    nic.boundary = 0x40;
    nic.curr = 0x41;
    nic.mac[0]=0x02; nic.mac[1]=0x50; nic.mac[2]=0x43;
    nic.mac[3]=0x31; nic.mac[4]=0x31; nic.mac[5]=0x00;
    memcpy(nic.par, nic.mac, 6);
}

static uint16_t remote_addr() { return (uint16_t)(nic.rsar0 | ((uint16_t)nic.rsar1 << 8)); }
static uint16_t remote_count() { return (uint16_t)(nic.rbcr0 | ((uint16_t)nic.rbcr1 << 8)); }
static void set_remote_addr(uint16_t v) { nic.rsar0=(uint8_t)v; nic.rsar1=(uint8_t)(v>>8); }
static void set_remote_count(uint16_t v) { nic.rbcr0=(uint8_t)v; nic.rbcr1=(uint8_t)(v>>8); }

static uint8_t nic_dma_read()
{
    uint8_t v = 0xFF;
    uint16_t a = remote_addr();
    if (a >= 0x4000 && a < 0xC000) v = nic.ram[a - 0x4000];
    if (remote_count()) {
        set_remote_addr((uint16_t)(a + 1));
        set_remote_count((uint16_t)(remote_count() - 1));
        if (!remote_count()) nic.isr |= 0x40; /* RDC */
    }
    return v;
}
static void nic_dma_write(uint8_t v)
{
    uint16_t a = remote_addr();
    if (a >= 0x4000 && a < 0xC000) nic.ram[a - 0x4000] = v;
    if (remote_count()) {
        set_remote_addr((uint16_t)(a + 1));
        set_remote_count((uint16_t)(remote_count() - 1));
        if (!remote_count()) nic.isr |= 0x40;
    }
}
static uint8_t nic_read(uint8_t reg)
{
    const uint8_t page = (nic.command >> 6) & 3;
    if (reg == 0) return nic.command;
    if (reg == 0x10) return nic_dma_read();
    if (page == 0) {
        switch(reg) {
        case 1: return nic.pstart; case 2: return nic.pstop;
        case 3: return nic.boundary; case 4: return nic.tsr;
        case 5: return 0; case 6: return 0;
        case 7: return nic.isr; case 8: return nic.rsar0;
        case 9: return nic.rsar1; case 0x0A: return nic.rbcr0;
        case 0x0B: return nic.rbcr1; case 0x0C: return nic.rsr;
        case 0x0D: return nic.tally[0]; case 0x0E: return nic.tally[1];
        case 0x0F: return nic.tally[2];
        }
    } else if (page == 1) {
        if (reg >= 1 && reg <= 6) return nic.par[reg-1];
        if (reg == 7) return nic.curr;
        if (reg >= 8 && reg <= 15) return nic.mar[reg-8];
    } else if (page == 2) {
        switch(reg) {
        case 1: return nic.pstart; case 2: return nic.pstop;
        case 3: return nic.boundary; case 4: return nic.tpsr;
        case 5: return nic.tcr; case 6: return nic.dcr;
        case 7: return nic.imr; case 8: return nic.rsar0;
        case 9: return nic.rsar1; case 0x0A: return nic.rbcr0;
        case 0x0B: return nic.rbcr1; case 0x0C: return nic.rcr;
        }
    }
    return 0xFF;
}
static void nic_write(uint8_t reg, uint8_t v)
{
    const uint8_t page = (nic.command >> 6) & 3;
    if (reg == 0) {
        nic.command = v;
        if (v & 1) nic.isr |= 0x80;
        if (v & 4) {
            /* No host transport is attached in this stage: fail explicitly
             * instead of pretending that an Ethernet frame reached a wire. */
            nic.tsr = 0x08; /* CRS: no carrier */
            nic.isr |= 0x08; /* TXE */
            if (!nic.tx_no_backend_logged) {
                trace("[I486][NE2000] TX requested but SLiRP backend is not attached");
                nic.tx_no_backend_logged = 1;
            }
            nic.command &= (uint8_t)~4;
        }
        return;
    }
    if (reg == 0x10) { nic_dma_write(v); return; }
    if (page == 0) {
        switch(reg) {
        case 1: nic.pstart=v; break; case 2: nic.pstop=v; break;
        case 3: nic.boundary=v; break; case 4: nic.tpsr=v; break;
        case 5: nic.tbcr0=v; break; case 6: nic.tbcr1=v; break;
        case 7: nic.isr &= (uint8_t)~v; break;
        case 8: nic.rsar0=v; break; case 9: nic.rsar1=v; break;
        case 0x0A: nic.rbcr0=v; break; case 0x0B: nic.rbcr1=v; break;
        case 0x0C: nic.rcr=v; break; case 0x0D: nic.tcr=v; break;
        case 0x0E: nic.dcr=v; break; case 0x0F: nic.imr=v; break;
        }
    } else if (page == 1) {
        if (reg >= 1 && reg <= 6) nic.par[reg-1]=v;
        else if (reg == 7) nic.curr=v;
        else if (reg >= 8 && reg <= 15) nic.mar[reg-8]=v;
    }
}


struct Keyboard8042 {
    uint8_t command_byte, output_port, output_data, output_data2;
    uint8_t output_full, output_count, expect_command_byte, expect_output_port;
    uint8_t keyboard_enabled;
} kbc;

struct AtaDisk {
    uint8_t feature, error, sector_count, lba0, lba1, lba2, drive, status, command, control;
    uint8_t buffer[512];
    unsigned int pos;
    unsigned int sectors_left;
    unsigned int lba;
    uint8_t writing;
} ata;
static uint8_t ata_storage[16U * 1024U * 1024U];

struct FloppyController {
    uint8_t dor, ccr, command, command_pos, command_length;
    uint8_t result[16], result_pos, result_length;
} fdc;
static uint8_t cmos[128];
static uint8_t cmos_index;

static void kbc_put(uint8_t value)
{
    if (!kbc.output_count) kbc.output_data=value;
    else if (kbc.output_count==1) kbc.output_data2=value;
    if (kbc.output_count<2) ++kbc.output_count;
    kbc.output_full = kbc.output_count ? 1 : 0;
}
static uint8_t kbc_status()
{
    return (uint8_t)(0x04 | (kbc.output_full ? 1 : 0));
}
static void kbc_reset()
{
    memset(&kbc, 0, sizeof(kbc));
    kbc.command_byte = 0x01;
    kbc.output_port = 0x03;
    kbc.keyboard_enabled = 1;
}
static void kbc_command(uint8_t value)
{
    switch(value) {
    case 0x20: kbc_put(kbc.command_byte); break;
    case 0x60: kbc.expect_command_byte = 1; break;
    case 0xAA: kbc_put(0x55); break;
    case 0xAB: kbc_put(0x00); break;
    case 0xAD: kbc.keyboard_enabled = 0; break;
    case 0xAE: kbc.keyboard_enabled = 1; break;
    case 0xD0: kbc_put(kbc.output_port); break;
    case 0xD1: kbc.expect_output_port = 1; break;
    case 0xA7: case 0xA8: case 0xA9: break;
    default: break;
    }
}
static void kbc_data(uint8_t value)
{
    if (kbc.expect_command_byte) {
        kbc.command_byte = value;
        kbc.expect_command_byte = 0;
        return;
    }
    if (kbc.expect_output_port) {
        kbc.output_port = value;
        kbc.expect_output_port = 0;
        return;
    }
    if (!kbc.keyboard_enabled) return;
    /* AT keyboard is present and idle: acknowledge commands but never
       synthesize key make/break codes. */
    if (value == 0xFF) { kbc_put(0xFA); kbc_put(0xAA); }
    else if (value == 0xF4 || value == 0xF5 || value == 0xED ||
             value == 0xF3 || value == 0xF0) kbc_put(0xFA);
    else kbc_put(0xFA);
}

static unsigned int ata_current_lba()
{
    return (unsigned int)ata.lba0 |
        ((unsigned int)ata.lba1 << 8) |
        ((unsigned int)ata.lba2 << 16) |
        ((unsigned int)(ata.drive & 0x0F) << 24);
}
static void ata_set_lba(unsigned int lba)
{
    ata.lba0=(uint8_t)lba; ata.lba1=(uint8_t)(lba>>8);
    ata.lba2=(uint8_t)(lba>>16);
    ata.drive=(uint8_t)((ata.drive&0xF0)|((lba>>24)&0x0F));
}
static void ata_update_taskfile()
{
    ata.sector_count = (uint8_t)ata.sectors_left;
    ata_set_lba(ata.lba);
}
static void ata_finish_sector()
{
    if (ata.sectors_left) --ata.sectors_left;
    ++ata.lba;
    ata_update_taskfile();
    if (!ata.sectors_left) {
        ata.status = 0x50; /* DRDY | DSC */
        ata.writing = 0;
    } else {
        ata.pos = 0;
        if (ata.writing) {
            const unsigned int offset = ata.lba * 512U;
            if (offset + 512U <= sizeof(ata_storage))
                memcpy(ata_storage + offset, ata.buffer, 512);
        } else {
            const unsigned int offset = ata.lba * 512U;
            memset(ata.buffer, 0, sizeof(ata.buffer));
            if (offset + 512U <= sizeof(ata_storage))
                memcpy(ata.buffer, ata_storage + offset, 512);
        }
        ata.status = 0x58;
    }
}
static void ata_reset()
{
    memset(&ata, 0, sizeof(ata));
    ata.drive = 0xE0;
    ata.status = 0x50; /* present, ready, no error */
}
static void ata_command(uint8_t command)
{
    ata.command = command;
    ata.error = 0;
    ata.pos = 0;
    ata.writing = 0;
    switch(command) {
    case 0xEC: /* IDENTIFY DEVICE */
        memset(ata.buffer, 0, sizeof(ata.buffer));
        /* ATA word fields are stored little-endian. */
        ata.buffer[0]=0x40; /* fixed disk */
        ata.buffer[2]=32; ata.buffer[3]=0x00; /* 32 cylinders */
        ata.buffer[6]=16;   /* 16 heads */
        ata.buffer[12]=63;  /* 63 sectors per track */
        memcpy(ata.buffer+20, "P1310000000000000001", 20);
        memcpy(ata.buffer+54, "VIRTUAL 16MB HDD  ", 18);
        memcpy(ata.buffer+94, "PROJECT131       ", 17);
        ata.buffer[2*49]=0x00; ata.buffer[2*49+1]=0x02; /* LBA supported */
        ata.buffer[2*60]=0x00; ata.buffer[2*60+1]=0x80; /* 32768 sectors */
        ata.buffer[2*61]=0x00; ata.buffer[2*61+1]=0x00;
        ata.status=0x58;
        ata.sectors_left=1;
        ata.lba=0xFFFFFFFFU; /* identify transfer sentinel */
        break;
    case 0x20: case 0x21: case 0x30: case 0x31:
        ata.lba=ata_current_lba();
        ata.sectors_left=ata.sector_count ? ata.sector_count : 256;
        if (ata.lba >= 32768U || ata.sectors_left > 32768U-ata.lba) {
            ata.error=0x10; ata.status=0x51; break;
        }
        ata.writing=(uint8_t)(command==0x30 || command==0x31);
        if (ata.writing) memset(ata.buffer,0,sizeof(ata.buffer));
        else {
            memset(ata.buffer,0,sizeof(ata.buffer));
            memcpy(ata.buffer,ata_storage+ata.lba*512U,512);
        }
        ata.status=0x58;
        break;
    case 0xE7: case 0xEA: ata.status=0x50; break; /* FLUSH CACHE */
    case 0x90: ata.status=0x50; break; /* EXECUTE DEVICE DIAGNOSTIC */
    default: ata.error=0x04; ata.status=0x51; break;
    }
}
static uint8_t ata_data_read()
{
    if (!(ata.status & 0x08) || ata.writing) return 0xFF;
    if (ata.lba == 0xFFFFFFFFU) {
        uint8_t v=ata.buffer[ata.pos++];
        if (ata.pos >= 512) { ata.status=0x50; ata.pos=0; ata.lba=0; }
        return v;
    }
    uint8_t v=ata.buffer[ata.pos++];
    if (ata.pos >= 512) ata_finish_sector();
    return v;
}
static void ata_data_write(uint8_t value)
{
    if (!(ata.status & 0x08) || !ata.writing) return;
    ata.buffer[ata.pos++]=value;
    if (ata.pos >= 512) {
        const unsigned int offset=ata.lba*512U;
        if (offset+512U <= sizeof(ata_storage))
            memcpy(ata_storage+offset,ata.buffer,512);
        ata_finish_sector();
    }
}

static void fdc_reset()
{
    memset(&fdc,0,sizeof(fdc));
    fdc.dor=0x0C;
}
static void fdc_result(const uint8_t *data, uint8_t count)
{
    if (count>sizeof(fdc.result)) count=sizeof(fdc.result);
    memcpy(fdc.result,data,count);
    fdc.result_pos=0;
    fdc.result_length=count;
}
static uint8_t fdc_read_fifo()
{
    if (fdc.result_pos<fdc.result_length) {
        uint8_t v=fdc.result[fdc.result_pos++];
        if (fdc.result_pos>=fdc.result_length) {
            fdc.result_pos=fdc.result_length=0;
        }
        return v;
    }
    return 0xFF;
}
static void fdc_write_fifo(uint8_t value)
{
    if (!fdc.command_pos) {
        fdc.command=value;
        fdc.command_pos=1;
        switch(value & 0x1F) {
        case 0x03: fdc.command_length=3; break; /* SPECIFY */
        case 0x04: fdc.command_length=2; break; /* SENSE DRIVE STATUS */
        case 0x07: fdc.command_length=2; break; /* RECALIBRATE */
        case 0x08: {
            const uint8_t r[2]={0x80,0x00}; fdc_result(r,2);
            fdc.command_pos=fdc.command_length=0; return;
        }
        case 0x0A: fdc.command_length=2; break; /* READ ID */
        case 0x0F: fdc.command_length=3; break; /* SEEK */
        case 0x05: case 0x06: case 0x09: case 0x0C:
            fdc.command_length=9; break;
        default: fdc.command_pos=fdc.command_length=0; return;
        }
        return;
    }
    if (++fdc.command_pos < fdc.command_length) return;
    {
        const uint8_t op=fdc.command & 0x1F;
        uint8_t r[7]={0x40,0x04,0x00,0x00,0x00,0x01,0x02};
        if (op==0x04) { r[0]=(uint8_t)(value&3); r[1]=0x00; fdc_result(r,1); }
        else if (op==0x0A) { r[0]=0x40; r[1]=0x04; r[2]=0; r[3]=0; r[4]=0; r[5]=1; r[6]=2; fdc_result(r,7); }
        else if (op==0x05 || op==0x06 || op==0x09 || op==0x0C) fdc_result(r,7);
        else { const uint8_t ok[1]={0x20}; fdc_result(ok,1); }
    }
    fdc.command_pos=fdc.command_length=0;
}
static uint8_t fdc_main_status()
{
    if (fdc.result_length) return 0xD0; /* RQM | DIO | controller busy */
    if (fdc.command_pos) return 0x90;   /* RQM | controller busy */
    return 0x80;                       /* RQM, idle */
}
static void cmos_reset()
{
    memset(cmos,0,sizeof(cmos));
    cmos[0x10]=0x40; /* one 1.44 MB drive A */
    cmos[0x14]=0x01; /* floppy installed */
    cmos_index=0;
}
static uint8_t device_in8(uint16_t port)
{
    if (port==0x61) {
        /* 8255/AT-compatible readback: error latches clear, timer-2 output
         * low in this no-PIT stage, refresh request bit toggles on reads. */
        system_refresh_phase ^= 0x10;
        return (uint8_t)((system_control_b & 0x0F) | system_refresh_phase);
    }
    if (port==0x62) return system_control_a;
    if (port==0x60) {
        if (!kbc.output_count) return 0;
        {
            uint8_t value=kbc.output_data;
            if (kbc.output_count==2) kbc.output_data=kbc.output_data2;
            --kbc.output_count;
            kbc.output_full=kbc.output_count ? 1 : 0;
            return value;
        }
    }
    if (port==0x64) return kbc_status();
    if (port==0x71) return cmos[cmos_index&0x7F];
    if (port==0x3F2) return fdc.dor;
    if (port==0x1F0) return ata_data_read();
    if (port==0x1F1) return ata.error;
    if (port==0x1F2) return ata.sector_count;
    if (port==0x1F3) return ata.lba0;
    if (port==0x1F4) return ata.lba1;
    if (port==0x1F5) return ata.lba2;
    if (port==0x1F6) return ata.drive;
    if (port==0x1F7 || port==0x3F6) return ata.status;
    if (port==0x3F4) return fdc_main_status();
    if (port==0x3F5) return fdc_read_fifo();
    if (port==0x3F7) return 0x80; /* disk-change line: no media */
    return 0xFF;
}
static void device_out8(uint16_t port, uint8_t value)
{
    if (port==0x61) {
        /* Bits 0..3 are the writable system-control latch. Writing bit 7
         * acknowledges the timer-0 output latch; no IRQ/PIT is attached. */
        system_control_b = (uint8_t)(value & 0x0F);
        return;
    }
    if (port==0x62) {
        /* Port C is an input/status port on the XT-compatible board. */
        return;
    }
    if (port==0x60) { kbc_data(value); return; }
    if (port==0x64) { kbc_command(value); return; }
    if (port==0x70) { cmos_index=(uint8_t)(value&0x7F); return; }
    if (port==0x1F0) { ata_data_write(value); return; }
    if (port==0x1F1) { ata.feature=value; return; }
    if (port==0x1F2) { ata.sector_count=value; return; }
    if (port==0x1F3) { ata.lba0=value; return; }
    if (port==0x1F4) { ata.lba1=value; return; }
    if (port==0x1F5) { ata.lba2=value; return; }
    if (port==0x1F6) { ata.drive=value; return; }
    if (port==0x1F7) { ata_command(value); return; }
    if (port==0x3F6) {
        ata.control=value;
        if (value&0x04) { ata_reset(); ata.control=value; }
        return;
    }
    if (port==0x3F2) { fdc.dor=value; return; }
    if (port==0x3F5) { fdc_write_fifo(value); return; }
    if (port==0x3F7) { fdc.ccr=value&3; return; }
}
static uint16_t device_in16(uint16_t port)
{
    if (port==0x1F0) return (uint16_t)(ata_data_read() | ((uint16_t)ata_data_read()<<8));
    return (uint16_t)(in8(port) | ((uint16_t)in8((uint16_t)(port+1))<<8));
}
static void device_out16(uint16_t port, uint16_t value)
{
    if (port==0x1F0) { ata_data_write((uint8_t)value); ata_data_write((uint8_t)(value>>8)); return; }
    out8(port,(uint8_t)value);
    out8((uint16_t)(port+1),(uint8_t)(value>>8));
}

}

void reset()
{
    system_control_b = 0;
    system_refresh_phase = 0;
    system_control_a = 0x0E;
    uart_reset();
    memset(&lpt1, 0, sizeof(lpt1));
    lpt1.control = 0x0C;
    nic_reset();
    kbc_reset();
    ata_reset();
    fdc_reset();
    cmos_reset();
    io_trace_count = 0;
    trace("[I486] DEVICES RESET COM1=3F8 LPT1=378 NE2000=300 KBC=60/64 IDE=1F0 FDD=3F0 DRIVE=A:NO_MEDIA HDD=16MB_BLANK");
}

void close() {}

uint8_t in8(uint16_t port)
{
    if (port==0x60 || port==0x61 || port==0x62 || port==0x64 || port==0x71 ||
        (port>=0x1F0 && port<=0x1F7) || port==0x3F2 || port==0x3F4 || port==0x3F5 || port==0x3F6 || port==0x3F7)
        return device_in8(port);
    if (port >= 0x3F8 && port <= 0x3FF) {
        const uint8_t reg = (uint8_t)(port - 0x3F8);
        switch(reg) {
        case 0: return (com1.lcr & 0x80) ? com1.dll : com1.rbr;
        case 1: return (com1.lcr & 0x80) ? com1.dlm : com1.ier;
        case 2: return com1.iir;
        case 3: return com1.lcr; case 4: return com1.mcr;
        case 5: return com1.lsr; case 6: return com1.msr;
        case 7: return com1.scr;
        }
    }
    if (port >= 0x378 && port <= 0x37A) {
        if (port == 0x378) return lpt1.data;
        if (port == 0x379) return 0xD8; /* no paper error, selected, not busy */
        return lpt1.control;
    }
    if (port >= 0x300 && port <= 0x31F) {
        const uint8_t offset = (uint8_t)(port - 0x300);
        if (offset == 0x1F) { nic_reset(); return 0; }
        if (offset >= 0x10 && offset <= 0x17) return nic_dma_read();
        return nic_read(offset);
    }
    if (io_trace_count++ < 128) {
        char msg[96]; sprintf(msg, "[I486] IO UNMAPPED IN8 PORT=%04X VALUE=FF", port); trace(msg);
    }
    return 0xFF;
}

void out8(uint16_t port, uint8_t value)
{
    if (port==0x60 || port==0x61 || port==0x62 || port==0x64 || port==0x70 ||
        (port>=0x1F0 && port<=0x1F7) || port==0x3F2 || port==0x3F5 || port==0x3F6 || port==0x3F7) {
        device_out8(port,value); return;
    }
    if (port >= 0x3F8 && port <= 0x3FF) {
        const uint8_t reg = (uint8_t)(port - 0x3F8);
        switch(reg) {
        case 0:
            if (com1.lcr & 0x80) com1.dll=value;
            else {
                com1.lsr |= 0x60;
                if (com1.tx_count < sizeof(com1.tx)) com1.tx[com1.tx_count++] = value;
                if (value == '\n' || com1.tx_count >= sizeof(com1.tx)) {
                    char msg[160]; unsigned int n=com1.tx_count;
                    if (n > 120) n=120;
                    memcpy(msg, "[I486][COM1] ", 13);
                    memcpy(msg+13, com1.tx, n); msg[13+n]=0;
                    trace(msg); com1.tx_count=0;
                }
            }
            break;
        case 1: if (com1.lcr & 0x80) com1.dlm=value; else com1.ier=value&0x0F; break;
        case 2: com1.fcr=value; if (value&2) com1.rbr=0; break;
        case 3: com1.lcr=value; break;
        case 4: com1.mcr=value; break;
        case 7: com1.scr=value; break;
        }
        return;
    }
    if (port >= 0x378 && port <= 0x37A) {
        if (port == 0x378) lpt1.data=value;
        else if (port == 0x37A) lpt1.control=value;
        return;
    }
    if (port >= 0x300 && port <= 0x31F) {
        const uint8_t offset = (uint8_t)(port - 0x300);
        if (offset == 0x1F) nic_reset();
        else if (offset >= 0x10 && offset <= 0x17) nic_dma_write(value);
        else nic_write(offset, value);
        return;
    }
    if (io_trace_count++ < 128) {
        char msg[96]; sprintf(msg, "[I486] IO UNMAPPED OUT8 PORT=%04X VALUE=%02X", port, value); trace(msg);
    }
}
uint16_t in16(uint16_t port) { return device_in16(port); }
void out16(uint16_t port, uint16_t value) { device_out16(port,value); }

} }
