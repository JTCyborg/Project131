#include <new>
#include "i486.h"
#include "devices.h"
#include "vga.h"
#include "pcem/adapter/pcem_memory.h"

void i486_pcem_cpu_reset(void);
extern "C" unsigned int i486_pcem_execute_instructions(unsigned int instruction_budget);
extern "C" bool i486_pcem_prepare_first_instruction(void);

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
        i486_pcem_cpu_reset();
        devices::reset();
        vga::init();
        return true;
    }

    void close()
    {
        delete[] ram;
        ram = 0;
        ram_size = 0;
        devices::close();
        vga::close();
        bios_loaded = false;
        memset(bios, 0xFF, sizeof(bios));
        pcem_memory_rebuild();
    }

    void reset()
    {
        // Reset CPU registers and hidden segment state; RAM and BIOS persist.
        i486_pcem_cpu_reset();
        devices::reset();
    }

    bool load_bios(const char *path)
    {
        if (!path || !*path)
            return false;

        FILE *file = fopen(path, "rb");
        if (!file)
            return false;

        if (fseek(file, 0, SEEK_END) != 0)
        {
            fclose(file);
            return false;
        }

        const long file_size = ftell(file);
        if (file_size != 8192 && file_size != (long)BiosSize)
        {
            fclose(file);
            return false;
        }

        if (fseek(file, 0, SEEK_SET) != 0)
        {
            fclose(file);
            return false;
        }

        unsigned char *image = new (std::nothrow) unsigned char[(size_t)file_size];
        if (!image)
        {
            fclose(file);
            return false;
        }

        const size_t count = fread(image, 1, (size_t)file_size, file);
        const bool ok = count == (size_t)file_size && !ferror(file);
        fclose(file);

        if (ok)
        {
            memset(bios, 0xFF, sizeof(bios));

            // An 8-KiB 27C64 EPROM occupies the top 8 KiB of the 64-KiB
            // BIOS aperture, placing its reset vector at image offset 1FF0h.
            const size_t bios_offset = file_size == 8192 ? BiosSize - 8192 : 0;
            memcpy(bios + bios_offset, image, (size_t)file_size);
            bios_loaded = true;

            char message[128];
            if (file_size == 8192)
                sprintf(message, "[I486] BIOS loaded: 8192 bytes at FFFFE000h");
            else
                sprintf(message, "[I486] BIOS loaded: 65536 bytes at FFFF0000h");
            log(message);

            sprintf(message, "[I486] RESET_BYTES=%02X %02X %02X %02X %02X",
                read8(0xFFFFFFF0U), read8(0xFFFFFFF1U),
                read8(0xFFFFFFF2U), read8(0xFFFFFFF3U),
                read8(0xFFFFFFF4U));
            log(message);
        }

        delete[] image;
        return ok;
    }

    bool bios_is_loaded()
    {
        return bios_loaded;
    }

    unsigned int execute_instructions(unsigned int instruction_budget)
    {
        if (!ram || !bios_loaded || instruction_budget == 0)
            return 0;

        /*
         * Do not cache this preparation result: the host frame/reset path may
         * alter the expansion CPU's hidden CS cache between frames. The
         * helper is deliberately conditional on EIP still being the BIOS
         * bootstrap target, so it cannot rewind an already-running BIOS.
         */
        if (!i486_pcem_prepare_first_instruction())
            return 0;

        return i486_pcem_execute_instructions(instruction_budget);
    }

    unsigned char read8(unsigned int address)
    {
        bool handled = false;
        unsigned char value = vga::memory_read(address, &handled);
        return handled ? value : read8_internal(address);
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
        bool handled = false;
        vga::memory_write(address, value, &handled);
        if (handled) return;
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
        bool handled = false;
        unsigned char value = vga::in8(port, &handled);
        return handled ? value : devices::in8(port);
    }

    unsigned short in16(unsigned short port)
    {
        return devices::in16(port);
    }

    unsigned int in32(unsigned short port)
    {
        return (unsigned int)in16(port) |
            ((unsigned int)in16((unsigned short)(port + 2)) << 16);
    }

    void out8(unsigned short port, unsigned char value)
    {
        bool handled = false;
        vga::out8(port, value, &handled);
        if (!handled) devices::out8(port, value);
    }

    void out16(unsigned short port, unsigned short value)
    {
        devices::out16(port, value);
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

    void log(const char *message)
    {
        if (!message || !*message)
            return;

        FILE *file = fopen("C:\\Temp\\spectrum_out.txt", "a");
        if (!file)
            return;

        fputs(message, file);
        const size_t length = strlen(message);
        if (length == 0 || message[length - 1] != '\n')
            fputs("\r\n", file);
        fclose(file);
    }
}
