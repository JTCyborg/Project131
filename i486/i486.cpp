#include "std.h"
#include <new>
#include "i486.h"

// The i486 ISA I/O bus is wired to the Trident adapter. Keep the adapter
// interface narrow: non-VGA ports remain unclaimed by this device.
extern bool isa_vga_adapter_port_read(unsigned port, unsigned char &value);
extern bool isa_vga_adapter_port_write(unsigned port, unsigned char value);
extern bool isa_vga_adapter_isa_mem_read(unsigned address, unsigned char &value);
extern bool isa_vga_adapter_isa_mem_write(unsigned address, unsigned char value);

namespace i486
{
    static unsigned char *ram = 0;
    static unsigned long ram_size = 0;
    static unsigned char bios[BiosSize];
    static bool bios_loaded = false;
    // Trident TVGA8900D Option ROM. Its header defines the decoded window.
    static const unsigned int VgaBiosBase = 0x000C0000U;
    static const unsigned int VgaBiosMaxSize = 64U * 1024U;
    static unsigned char vga_bios[VgaBiosMaxSize];
    static unsigned int vga_bios_size = 0;

    static unsigned char read8_internal(unsigned int address)
    {
        // 486 reset fetch at FFFFFFF0h maps to BIOS offset FFF0h.
        if (address >= 0xFFFF0000U)
            return bios[address & 0xFFFFU];

        // Trident TVGA8900D VGA Option ROM window.
        if (vga_bios_size && address >= VgaBiosBase &&
            address < VgaBiosBase + vga_bios_size)
            return vga_bios[address - VgaBiosBase];

        // VGA owns the decoded ISA aperture before ordinary RAM.
        unsigned char video_value = 0xFF;
        if (isa_vga_adapter_isa_mem_read(address, video_value))
            return video_value;

        // Legacy PC-compatible system BIOS window.
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
        memset(vga_bios, 0xFF, sizeof(vga_bios));
        vga_bios_size = 0;

        // BIOS.BIN is the checked-in Trident TVGA8900D Option ROM.
        // Try the working directory first, then the directory above the EXE.
        if (!load_vga_bios("BIOS.BIN"))
        {
            char module_path[MAX_PATH];
            DWORD n = GetModuleFileNameA(0, module_path, MAX_PATH);
            if (n && n < MAX_PATH)
            {
                char *slash = strrchr(module_path, '\\');
                char *forward = strrchr(module_path, '/');
                if (!slash || (forward && forward > slash)) slash = forward;
                if (slash)
                {
                    *slash = 0;
                    char *parent = strrchr(module_path, '\\');
                    forward = strrchr(module_path, '/');
                    if (!parent || (forward && forward > parent)) parent = forward;
                    if (parent) *parent = 0;
                    strncat(module_path, "\\BIOS.BIN", MAX_PATH - strlen(module_path) - 1);
                    load_vga_bios(module_path);
                }
            }
        }
        return true;
    }

    void close()
    {
        delete[] ram;
        ram = 0;
        ram_size = 0;
        bios_loaded = false;
        memset(bios, 0xFF, sizeof(bios));
        memset(vga_bios, 0xFF, sizeof(vga_bios));
        vga_bios_size = 0;
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

    bool load_vga_bios(const char *path)
    {
        if (!path || !*path) return false;
        FILE *file = fopen(path, "rb");
        if (!file) return false;

        unsigned char image[VgaBiosMaxSize];
        const size_t count = fread(image, 1, sizeof(image), file);
        const int extra = fgetc(file);
        const bool file_ok = count == sizeof(image) && extra == EOF && !ferror(file);
        fclose(file);
        if (!file_ok || image[0] != 0x55 || image[1] != 0xAA || !image[2])
            return false;

        const unsigned int decoded_size = (unsigned int)image[2] * 512U;
        if (decoded_size > VgaBiosMaxSize || decoded_size < 512U)
            return false;

        memcpy(vga_bios, image, sizeof(vga_bios));
        vga_bios_size = decoded_size;
        return true;
    }

    bool vga_bios_is_loaded()
    {
        return vga_bios_size != 0;
    }

    unsigned int vga_bios_rom_size()
    {
        return vga_bios_size;
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
            (address >= 0x000F0000U && address <= 0x000FFFFFU) ||
            (vga_bios_size && address >= VgaBiosBase &&
             address < VgaBiosBase + vga_bios_size))
            return;

        // Route writes into the active VGA aperture; do not mirror them to RAM.
        if (isa_vga_adapter_isa_mem_write(address, value))
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
        unsigned char value = 0xFF;
        if (isa_vga_adapter_port_read((unsigned)port, value))
            return value;
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
        // VGA/Trident register writes are dispatched to the emulated adapter.
        // Unhandled ISA ports are intentionally left for other devices.
        isa_vga_adapter_port_write((unsigned)port, value);
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
