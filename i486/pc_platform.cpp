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
    // 8259A state is kept per module. IRQ inputs are edge-latched; the
    // CPU-facing acknowledge path is exposed through pic_acknowledge().
    struct Pic8259
    {
        unsigned char irr, isr, imr, vector_base;
        unsigned char init_step, read_isr, auto_eoi, rotate_auto;
        unsigned char priority_base, expect_icw4, single;
        unsigned char level;
    };
    static Pic8259 pic_master;
    static Pic8259 pic_slave;
    static unsigned char cmos[128];
    static unsigned char cmos_index = 0;
    struct PitChannel
    {
        unsigned int reload, counter, latch;
        unsigned char access, mode, bcd;
        unsigned char write_low, read_low, latch_valid;
        unsigned char write_phase, read_phase;
        unsigned char output, running;
    };
    static PitChannel pit[3];
    static unsigned char dma[32];

    static unsigned int pit_decode_count(unsigned int value)
    {
        value &= 0xFFFFU;
        return value ? value : 0x10000U;
    }

    static void pit_load(unsigned int channel, unsigned int value)
    {
        if (channel >= 3) return;
        PitChannel &ch = pit[channel];
        ch.reload = pit_decode_count(value);
        ch.counter = ch.reload;
        ch.running = 1;
        if (ch.mode == 0) ch.output = 0;
        else ch.output = 1;
    }

    static unsigned int pit_current_count(const PitChannel &ch)
    {
        return ch.counter & 0xFFFFU;
    }

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
    memset(&pic_master, 0, sizeof(pic_master));
    memset(&pic_slave, 0, sizeof(pic_slave));
    pic_master.imr = pic_slave.imr = 0xFF;
    pic_master.vector_base = 0x08;
    pic_slave.vector_base = 0x70;
    pic_master.priority_base = pic_slave.priority_base = 7;
    memset(cmos, 0, sizeof(cmos));
    cmos[0x0A] = 0x26; // RTC oscillator running, divider normal
    cmos[0x0B] = 0x02; // 24-hour BCD mode
    cmos[0x0D] = 0x80; // CMOS battery valid
    cmos_index = 0;
    memset(pit, 0, sizeof(pit));
    for (unsigned int i = 0; i < 3; ++i)
    {
        pit[i].access = 3;
        pit[i].mode = 3;
        pit[i].output = 1;
    }
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

static int pic_highest_pending(const Pic8259 &pic)
{
    const unsigned char pending = (unsigned char)(pic.irr & ~pic.imr);
    for (unsigned int n = 1; n <= 8; ++n)
    {
        const unsigned char irq = (pic.priority_base + n) & 7;
        // An in-service request blocks requests at the same or lower
        // priority, but does not block a higher-priority interrupt.
        if (pic.isr & (1U << irq))
            break;
        if (pending & (1U << irq))
            return irq;
    }
    return -1;
}

static void pic_refresh_cascade()
{
    if (pic_highest_pending(pic_slave) >= 0)
        pic_master.irr |= 0x04;
    else
        pic_master.irr &= (unsigned char)~0x04;
}

void pic_set_irq(unsigned int irq, bool asserted)
{
    if (irq >= 16) return;
    Pic8259 &pic = (irq < 8) ? pic_master : pic_slave;
    const unsigned int line = irq & 7;
    const unsigned char bit = (unsigned char)(1U << line);
    const bool old_level = (pic.level & bit) != 0;
    if (asserted) pic.level |= bit;
    else pic.level &= (unsigned char)~bit;
    // Edge-triggered mode: latch only a low-to-high transition.
    if (asserted && !old_level) pic.irr |= bit;
    // In edge-triggered mode a falling edge does not erase a latched request.
    if (irq >= 8) pic_refresh_cascade();
}

bool pic_pending()
{
    const int master_irq = pic_highest_pending(pic_master);
    if (master_irq < 0) return false;
    if (master_irq == 2)
        return pic_highest_pending(pic_slave) >= 0;
    return true;
}

void pit_advance(unsigned int input_clocks)
{
    PitChannel &ch = pit[0];
    if (!ch.running || !input_clocks) return;

    // Channel 0 is the PC system timer. Modes 2 and 3 generate periodic
    // IRQ0 requests; mode 0 generates one terminal-count interrupt.
    if (ch.mode == 0)
    {
        if (input_clocks >= ch.counter)
        {
            ch.counter = 0;
            ch.running = 0;
            if (!ch.output)
            {
                ch.output = 1;
                pic_set_irq(0, true);
                pic_set_irq(0, false);
            }
        }
        else ch.counter -= input_clocks;
        return;
    }

    if (ch.mode == 2 || ch.mode == 3)
    {
        const unsigned int period = ch.reload ? ch.reload : 0x10000U;
        if (input_clocks >= ch.counter)
        {
            unsigned int remaining = input_clocks - ch.counter;
            const unsigned int expiries = 1U + remaining / period;
            const unsigned int tail = remaining % period;
            ch.counter = tail ? period - tail : period;
            // Each terminal count is a rising edge of OUT in mode 2/3.
            if (expiries)
            {
                pic_set_irq(0, true);
                pic_set_irq(0, false);
            }
        }
        else ch.counter -= input_clocks;
        return;
    }

    // Modes 1, 4 and 5 retain a useful down-counter approximation until
    // their gate/retrigger semantics are connected to the PC timer layer.
    if (input_clocks >= ch.counter)
    {
        ch.counter = 0;
        ch.running = 0;
        if (!ch.output)
        {
            ch.output = 1;
            pic_set_irq(0, true);
            pic_set_irq(0, false);
        }
    }
    else ch.counter -= input_clocks;
}

bool pic_acknowledge(unsigned char &vector)
{
    const int master_irq = pic_highest_pending(pic_master);
    if (master_irq < 0) return false;
    int irq = master_irq;
    if (master_irq == 2)
    {
        const int slave_irq = pic_highest_pending(pic_slave);
        if (slave_irq < 0) return false;
        pic_slave.irr &= (unsigned char)~(1U << slave_irq);
        if (!pic_slave.auto_eoi) pic_slave.isr |= (unsigned char)(1U << slave_irq);
        else if (pic_slave.rotate_auto) pic_slave.priority_base = (unsigned char)slave_irq;
        pic_refresh_cascade();
        irq = slave_irq + 8;
    }
    pic_master.irr &= (unsigned char)~(1U << master_irq);
    if (!pic_master.auto_eoi) pic_master.isr |= (unsigned char)(1U << master_irq);
    else if (pic_master.rotate_auto) pic_master.priority_base = (unsigned char)master_irq;
    vector = (unsigned char)((irq < 8 ? pic_master.vector_base : pic_slave.vector_base) +
                             (irq & 7));
    return true;
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

    // Dual 8259A command/data ports. OCW3 selects IRR or ISR reads.
    switch (port)
    {
    case 0x20: value = pic_master.read_isr ? pic_master.isr : pic_master.irr; return true;
    case 0x21: value = pic_master.imr; return true;
    case 0xA0: value = pic_slave.read_isr ? pic_slave.isr : pic_slave.irr; return true;
    case 0xA1: value = pic_slave.imr; return true;
    case 0x40: case 0x41: case 0x42:
    {
        PitChannel &ch = pit[port - 0x40];
        unsigned int count = ch.latch_valid ? ch.latch : pit_current_count(ch);
        if (ch.access == 1) value = (unsigned char)count;
        else if (ch.access == 2) value = (unsigned char)(count >> 8);
        else
        {
            value = ch.read_phase ? (unsigned char)(count >> 8) : (unsigned char)count;
            ch.read_phase ^= 1;
            if (!ch.read_phase) ch.latch_valid = 0;
        }
        return true;
    }
    case 0x43: value = 0xFF; return true;
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
    case 0x20: case 0xA0:
    {
        Pic8259 &pic = (port == 0x20) ? pic_master : pic_slave;
        if (value & 0x10) // ICW1: begin initialization
        {
            pic.irr = pic.isr = 0;
            pic.imr = 0;
            pic.init_step = 1;
            pic.expect_icw4 = value & 1;
            pic.single = (value >> 1) & 1;
            pic.auto_eoi = 0;
            pic.priority_base = 7;
            pic.read_isr = 0;
        }
        else if ((value & 0x18) == 0x08) // OCW3
        {
            if (value & 0x02) pic.read_isr = (value >> 2) & 1;
        }
        else // OCW2
        {
            const unsigned char command = (value >> 5) & 7;
            const unsigned char level = value & 7;
            if (command == 0) pic.rotate_auto = 0; // clear rotate in auto-EOI
            else if (command == 4) pic.rotate_auto = 1; // set rotate in auto-EOI
            else if (command == 1 || command == 5) // non-specific EOI
            {
                for (unsigned int n = 1; n <= 8; ++n)
                {
                    const unsigned char irq = (pic.priority_base + n) & 7;
                    if (pic.isr & (1U << irq))
                    {
                        pic.isr &= (unsigned char)~(1U << irq);
                        if (command == 5) pic.priority_base = irq;
                        break;
                    }
                }
            }
            else if (command == 6) pic.priority_base = level; // set lowest priority
            else if (command == 3 || command == 7) // specific EOI
            {
                pic.isr &= (unsigned char)~(1U << level);
                if (command == 7) pic.priority_base = level;
            }
        }
        return true;
    }
    case 0x21: case 0xA1:
    {
        Pic8259 &pic = (port == 0x21) ? pic_master : pic_slave;
        if (pic.init_step == 1) // ICW2
        {
            pic.vector_base = value & 0xF8;
            pic.init_step = pic.single ? (pic.expect_icw4 ? 3 : 0) : 2;
        }
        else if (pic.init_step == 2) // ICW3
        {
            pic.init_step = pic.expect_icw4 ? 3 : 0;
        }
        else if (pic.init_step == 3) // ICW4
        {
            pic.auto_eoi = value & 2;
            pic.init_step = 0;
        }
        else pic.imr = value; // OCW1
        return true;
    }
    case 0x40: case 0x41: case 0x42:
    {
        const unsigned int channel = port - 0x40;
        PitChannel &ch = pit[channel];
        if (ch.access == 1) pit_load(channel, value);
        else if (ch.access == 2) pit_load(channel, (unsigned int)value << 8);
        else if (!ch.write_phase)
        {
            ch.write_low = value;
            ch.write_phase = 1;
        }
        else
        {
            pit_load(channel, (unsigned int)ch.write_low | ((unsigned int)value << 8));
            ch.write_phase = 0;
        }
        return true;
    }
    case 0x43:
    {
        const unsigned int channel = (value >> 6) & 3;
        if (channel == 3) return true; // 8253 has no read-back command
        PitChannel &ch = pit[channel];
        const unsigned int access = (value >> 4) & 3;
        if (!access) // counter latch command
        {
            if (!ch.latch_valid)
            {
                ch.latch = pit_current_count(ch);
                ch.latch_valid = 1;
                ch.read_phase = 0;
            }
            return true;
        }
        ch.access = (unsigned char)access;
        ch.mode = (unsigned char)((value >> 1) & 7);
        if (ch.mode >= 6) ch.mode -= 4;
        ch.bcd = value & 1;
        ch.write_phase = ch.read_phase = 0;
        ch.latch_valid = 0;
        ch.running = 0;
        ch.output = (ch.mode == 0) ? 0 : 1;
        return true;
    }
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
