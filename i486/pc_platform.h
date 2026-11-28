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
}
}
