#pragma once

// PC-compatible board devices for the isolated 486 expansion module.
// Register-level behaviour is intentionally kept behind this interface so
// the PCem CPU core can be connected without coupling it to the ATM host.

namespace i486
{
namespace pc
{
    void init();
    void reset();
    void close();
    bool port_read(unsigned short port, unsigned char &value);
    bool port_write(unsigned short port, unsigned char value);
    unsigned int pci_config_address();

    // 8259A interrupt controller interface used by the future x86 core.
    void pic_set_irq(unsigned int irq, bool asserted);
    bool pic_pending();
    bool pic_acknowledge(unsigned char &interrupt_vector);

    // Advance PIT input clock ticks (nominally 1.193182 MHz on a PC/AT).
    void pit_advance(unsigned int input_clocks);
}
}
