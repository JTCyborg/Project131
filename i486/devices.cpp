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
} nic;

static unsigned int io_trace_count;
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
        /* TXP is retained in CR; frame transport is performed by backend. */
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
}

void reset()
{
    uart_reset();
    memset(&lpt1, 0, sizeof(lpt1));
    lpt1.control = 0x0C;
    nic_reset();
    io_trace_count = 0;
    trace("[I486] DEVICES RESET COM1=3F8 LPT1=378 NE2000=300 MAC=02:50:43:31:31:00");
}

void close() {}

uint8_t in8(uint16_t port)
{
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
    if (port >= 0x300 && port <= 0x31F)
        return nic_read((uint8_t)(port - 0x300));
    if (io_trace_count++ < 128) {
        char msg[96]; sprintf(msg, "[I486] IO UNMAPPED IN8 PORT=%04X VALUE=FF", port); trace(msg);
    }
    return 0xFF;
}

void out8(uint16_t port, uint8_t value)
{
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
        nic_write((uint8_t)(port - 0x300), value);
        return;
    }
    if (io_trace_count++ < 128) {
        char msg[96]; sprintf(msg, "[I486] IO UNMAPPED OUT8 PORT=%04X VALUE=%02X", port, value); trace(msg);
    }
}
} }
