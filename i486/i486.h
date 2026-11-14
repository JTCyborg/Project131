#pragma once

// Isolated platform interface for the Intel 486DX expansion module.
// The CPU interpreter is imported from PCem in a later integration step.

namespace i486
{
    static const unsigned long DefaultRamSize = 16UL * 1024UL * 1024UL;
    static const unsigned long BiosSize = 64UL * 1024UL;

    bool init(unsigned long ram_size = DefaultRamSize);
    void close();
    void reset();
    bool load_bios(const char *path);
    bool bios_is_loaded();

    unsigned char read8(unsigned int address);
    unsigned short read16(unsigned int address);
    unsigned int read32(unsigned int address);
    void write8(unsigned int address, unsigned char value);
    void write16(unsigned int address, unsigned short value);
    void write32(unsigned int address, unsigned int value);

    unsigned char in8(unsigned short port);
    unsigned short in16(unsigned short port);
    unsigned int in32(unsigned short port);
    void out8(unsigned short port, unsigned char value);
    void out16(unsigned short port, unsigned short value);
    void out32(unsigned short port, unsigned int value);

    bool is_initialized();

    // Internal bridge for the isolated PCem memory adapter.
    namespace detail
    {
        unsigned char *ram_data();
        unsigned char *bios_data();
        unsigned long ram_size_bytes();
    }
}
