#include <new>
#include "i486.h"
#include "pcem/adapter/pcem_memory.h"

namespace i486
{
    static unsigned char *ram = 0;
    static unsigned long ram_size = 0;
    static unsigned char bios[BiosSize];
    static bool bios_loaded = false;

    namespace detail
    {
        unsigned char *ram_data() { return ram; }
        unsigned char *bios_data() { return bios; }
        unsigned long ram_size_bytes() { return ::i486::ram_size; }
    }

    static unsigned char read8_internal(unsigned int address)
    {
        // 486 reset fetch at FFFFFFF0h maps to BIOS offset FFF0h.
        if (address >= 0xFFFF0000U)
            return bios[address & 0xFFFFU];

        // Legacy PC-compatible BIOS window.
        if (address >= 0x000F0000U && address <= 0x000FFFFFU)
            return bios[address & 0xFFFFU];

        if (ram && (unsigned long)address < ram_size)
            return ram[address];

        return 0xFF;
    }

    bool init(unsigned long requested_ram_size)
    {
        close();
        if (requested_ram_size == 0 || requested_ram_size > 0xFFF00000UL)
            return false;

        ram = new (std::nothrow) unsigned char[requested_ram_size];
        if (!ram)
            return false;

        ram_size = requested_ram_size;
        memset(ram, 0, ram_size);
        memset(bios, 0xFF, sizeof(bios));
        bios_loaded = false;
        pcem_memory_rebuild();
        return true;
    }

    void close()
    {
        delete[] ram;
        ram = 0;
        ram_size = 0;
        bios_loaded = false;
        memset(bios, 0xFF, sizeof(bios));
        pcem_memory_rebuild();
    }

    void reset()
    {
        // CPU architectural reset state is established by the PCem core.
        // Platform reset intentionally preserves RAM and the BIOS image.
    }

    bool load_bios(const char *path)
    {
        if (!path || !*path)
            return false;

        FILE *file = fopen(path, "rb");
        if (!file)
            return false;

        unsigned char *image = new (std::nothrow) unsigned char[BiosSize];
        if (!image)
        {
            fclose(file);
            return false;
        }

        const size_t count = fread(image, 1, BiosSize, file);
        const int extra = fgetc(file);
        const bool ok = count == BiosSize && extra == EOF && !ferror(file);
        fclose(file);

        if (ok)
        {
            memcpy(bios, image, BiosSize);
            bios_loaded = true;
        }

        delete[] image;
        return ok;
    }

    bool bios_is_loaded()
    {
        return bios_loaded;
    }

    unsigned char read8(unsigned int address)
    {
        return read8_internal(address);
    }

    unsigned short read16(unsigned int address)
    {
        return (unsigned short)(read8_internal(address) |
            ((unsigned short)read8_internal(address + 1U) << 8));
    }

    unsigned int read32(unsigned int address)
    {
        return (unsigned int)read8_internal(address) |
            ((unsigned int)read8_internal(address + 1U) << 8) |
            ((unsigned int)read8_internal(address + 2U) << 16) |
            ((unsigned int)read8_internal(address + 3U) << 24);
    }

    void write8(unsigned int address, unsigned char value)
    {
        if (address >= 0xFFFF0000U ||
            (address >= 0x000F0000U && address <= 0x000FFFFFU))
            return;

        if (ram && (unsigned long)address < ram_size)
            ram[address] = value;
    }

    void write16(unsigned int address, unsigned short value)
    {
        write8(address, (unsigned char)value);
        write8(address + 1U, (unsigned char)(value >> 8));
    }

    void write32(unsigned int address, unsigned int value)
    {
        write8(address, (unsigned char)value);
        write8(address + 1U, (unsigned char)(value >> 8));
        write8(address + 2U, (unsigned char)(value >> 16));
        write8(address + 3U, (unsigned char)(value >> 24));
    }

    unsigned char in8(unsigned short port)
    {
        (void)port;
        return 0xFF;
    }

    unsigned short in16(unsigned short port)
    {
        return (unsigned short)(in8(port) |
            ((unsigned short)in8((unsigned short)(port + 1)) << 8));
    }

    unsigned int in32(unsigned short port)
    {
        return (unsigned int)in16(port) |
            ((unsigned int)in16((unsigned short)(port + 2)) << 16);
    }

    void out8(unsigned short port, unsigned char value)
    {
        (void)port;
        (void)value;
    }

    void out16(unsigned short port, unsigned short value)
    {
        out8(port, (unsigned char)value);
        out8((unsigned short)(port + 1), (unsigned char)(value >> 8));
    }

    void out32(unsigned short port, unsigned int value)
    {
        out16(port, (unsigned short)value);
        out16((unsigned short)(port + 2), (unsigned short)(value >> 16));
    }

    bool is_initialized()
    {
        return ram != 0;
    }
}
