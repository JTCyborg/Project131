#include "std.h"
#include "pc_platform.h"

namespace i486
{
namespace pc
{
namespace
{
    // SiS 496/497 PCI host/ISA bridge, function 0, device 5.
    // IDs and class code follow PCem's sis496.c implementation.
    static unsigned int pci_address = 0;
    static unsigned char sis496[256];
    static unsigned char pic_master_mask = 0xFF;
    static unsigned char pic_slave_mask = 0xFF;
    static unsigned char pic_master_irr = 0;
    static unsigned char pic_slave_irr = 0;
    static unsigned char pic_master_isr = 0;
    static unsigned char pic_slave_isr = 0;
    static unsigned char cmos[128];
    static unsigned char cmos_index = 0;
    static unsigned char pit[4];
    static unsigned char dma[32];

    static void init_sis496()
    {
        memset(sis496, 0, sizeof(sis496));
        sis496[0x00] = 0x39; // SiS vendor ID
        sis496[0x01] = 0x10;
        sis496[0x02] = 0x96; // SiS 496/497 device ID
        sis496[0x03] = 0x04;
        sis496[0x04] = 0x07; // I/O, memory and bus-master command bits
        sis496[0x06] = 0x80;
        sis496[0x07] = 0x02;
        sis496[0x08] = 0x02; // Revision
        sis496[0x09] = 0x00;
        sis496[0x0A] = 0x00;
        sis496[0x0B] = 0x06; // PCI bridge class
        sis496[0x0E] = 0x00; // Single-function device
        // PCem routes onboard PCI interrupts through the SiS bridge.
        sis496[0x44] = 0x00; // ROM shadow control
        sis496[0x45] = 0x00;
    }

    static unsigned char pci_read_config(unsigned int address)
    {
        const unsigned int bus = (pci_address >> 16) & 0xFF;
        const unsigned int device = (pci_address >> 11) & 0x1F;
        const unsigned int function = (pci_address >> 8) & 7;
        const unsigned int reg = (pci_address & 0xFC) + (address & 3);
        if (!(pci_address & 0x80000000U) || bus != 0 ||
            device != 5 || function != 0 || reg >= sizeof(sis496))
            return 0xFF;
        return sis496[reg];
    }

    static void pci_write_config(unsigned int address, unsigned char value)
    {
        const unsigned int bus = (pci_address >> 16) & 0xFF;
        const unsigned int device = (pci_address >> 11) & 0x1F;
        const unsigned int function = (pci_address >> 8) & 7;
        const unsigned int reg = (pci_address & 0xFC) + (address & 3);
        if (!(pci_address & 0x80000000U) || bus != 0 ||
            device != 5 || function != 0 || reg >= sizeof(sis496))
            return;

        // IDs, class, revision and header type are read-only.
        if (reg < 4 || (reg >= 8 && reg <= 0x0B) || reg == 0x0E)
            return;

        if (reg == 0x04)
            sis496[reg] = (unsigned char)(value & 0x07);
        else if (reg == 0x44 || reg == 0x45 || reg >= 0x40)
            sis496[reg] = value;
    }
}

void init()
{
    reset();
    init_sis496();
}

void reset()
{
    pci_address = 0;
    pic_master_mask = pic_slave_mask = 0xFF;
    pic_master_irr = pic_slave_irr = 0;
    pic_master_isr = pic_slave_isr = 0;
    memset(cmos, 0, sizeof(cmos));
    cmos[0x0A] = 0x26; // RTC oscillator running, divider normal
    cmos[0x0B] = 0x02; // 24-hour BCD mode
    cmos[0x0D] = 0x80; // CMOS battery valid
    cmos_index = 0;
    memset(pit, 0, sizeof(pit));
    memset(dma, 0, sizeof(dma));
}

void close()
{
    reset();
    memset(sis496, 0, sizeof(sis496));
}

unsigned int pci_config_address()
{
    return pci_address;
}

bool port_read(unsigned short port, unsigned char &value)
{
    // PCI Configuration Mechanism #1 (SiS 496/497 host bridge).
    if (port >= 0x0CF8 && port <= 0x0CFB)
    {
        value = (unsigned char)(pci_address >> ((port - 0x0CF8) * 8));
        return true;
    }
    if (port >= 0x0CFC && port <= 0x0CFF)
    {
        value = pci_read_config(port - 0x0CFC);
        return true;
    }

    // Dual 8259-compatible PIC register interface.
    switch (port)
    {
    case 0x20: value = pic_master_irr; return true;
    case 0x21: value = pic_master_mask; return true;
    case 0xA0: value = pic_slave_irr; return true;
    case 0xA1: value = pic_slave_mask; return true;
    case 0x40: case 0x41: case 0x42: case 0x43:
        value = pit[port - 0x40]; return true;
    case 0x70: value = cmos_index; return true;
    case 0x71: value = cmos[cmos_index & 0x7F]; return true;
    default: break;
    }

    // 8237 DMA controller register file. Transfer scheduling and bus-master
    // arbitration are connected when the PCem device/timing layer is ported.
    if (port <= 0x0F)
    {
        value = dma[port];
        return true;
    }
    if (port >= 0xC0 && port <= 0xDE)
    {
        value = dma[16 + ((port - 0xC0) >> 1)];
        return true;
    }
    return false;
}

bool port_write(unsigned short port, unsigned char value)
{
    if (port >= 0x0CF8 && port <= 0x0CFB)
    {
        const unsigned int shift = (port - 0x0CF8) * 8;
        pci_address = (pci_address & ~(0xFFU << shift)) |
                      ((unsigned int)value << shift);
        return true;
    }
    if (port >= 0x0CFC && port <= 0x0CFF)
    {
        pci_write_config(port - 0x0CFC, value);
        return true;
    }

    switch (port)
    {
    case 0x20:
        if (value == 0x20) pic_master_isr = 0; // non-specific EOI
        else if (value & 0x10) pic_master_irr = pic_master_isr = 0;
        return true;
    case 0x21: pic_master_mask = value; return true;
    case 0xA0:
        if (value == 0x20) pic_slave_isr = 0;
        else if (value & 0x10) pic_slave_irr = pic_slave_isr = 0;
        return true;
    case 0xA1: pic_slave_mask = value; return true;
    case 0x40: case 0x41: case 0x42: case 0x43:
        pit[port - 0x40] = value; return true;
    case 0x70: cmos_index = (unsigned char)(value & 0x7F); return true;
    case 0x71:
        if ((cmos_index & 0x7F) != 0x0C && (cmos_index & 0x7F) != 0x0D)
            cmos[cmos_index & 0x7F] = value;
        return true;
    default: break;
    }

    if (port <= 0x0F)
    {
        dma[port] = value;
        return true;
    }
    if (port >= 0xC0 && port <= 0xDE)
    {
        dma[16 + ((port - 0xC0) >> 1)] = value;
        return true;
    }
    return false;
}
}
}
