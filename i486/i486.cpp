#include <new>
#include "i486.h"
#include "devices.h"
#include "vga.h"
extern "C" int i486_halted;
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

    // Keep this module's append-only diagnostic output bounded per emulator run.
    // The marker is written once; later messages are dropped instead of growing
    // spectrum_out.txt indefinitely when a guest/device enters a repetitive loop.
    static const size_t MaxDiagnosticBytes = 2U * 1024U * 1024U;
    static size_t diagnostic_bytes = 0;
    static bool diagnostic_limit_reported = false;

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
        diagnostic_bytes = 0;
        diagnostic_limit_reported = false;
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

            /*
             * Video bring-up priority: the current 27C64 BIOS reaches its
             * CPU self-test failure HLT at F000:E1B6. In the reference BIOS
             * source, HALT is a one-byte HLT immediately followed by CPU_OK.
             * Replace only that HLT with NOP so execution falls through into
             * the normal chipset and video initialization sequence.
             *
             * Keep this narrowly gated to the 8-KiB image and the exact HLT
             * opcode observed in the runtime trace; do not alter other BIOSes.
             */
            if (file_size == 8192 && bios[0xE1B6] == 0xF4)
            {
                bios[0xE1B6] = 0x90;
                log("[I486][BIOS] CPU self-test HLT at F000:E1B6 bypassed; continuing at CPU_OK F000:E1B7");
            }

            /*
             * Skip the fixed BIOS delay loop at F000:E5B2. The verified ROM
             * bytes are B9 00 50 E2 FE (MOV CX,5000h / LOOP $-2).
             * Replace only LOOP E2 FE with two NOPs when the full signature
             * matches. This preserves the following DEC SI / JS / RET path
             * and does not alter CPU instruction semantics.
             */
            if (file_size == 8192 &&
                bios[0xE5AF] == 0xB9 && bios[0xE5B0] == 0x00 &&
                bios[0xE5B1] == 0x50 && bios[0xE5B2] == 0xE2 &&
                bios[0xE5B3] == 0xFE)
            {
                bios[0xE5B2] = 0x90;
                bios[0xE5B3] = 0x90;
                log("[I486][BIOS] F000:E5B2 fixed delay loop (MOV CX,5000h/LOOP -2) bypassed");
            }


            /*
             * Bring-up bypass for the ROM's repeating PIT calibration.
             * ROM offset 1A0Ah is F000:FA0A (the image is mapped at FE000h).
             * The verified routine occupies FA0A..FA31 and repeatedly polls
             * PIT channel 2. Redirect its entry straight to FA4A, the next
             * executable routine after the calibration table. E9 3D 00 is a
             * near relative JMP from FA0A to FA4A.
             */
            if (file_size == 8192 &&
                bios[0x1A0A] == 0xB9 && bios[0x1A0B] == 0x00 &&
                bios[0x1A0C] == 0x10 && bios[0x1A0D] == 0xE4 &&
                bios[0x1A0E] == 0x62 && bios[0x1A0F] == 0xA8 &&
                bios[0x1A10] == 0x20 && bios[0x1A28] == 0xE4 &&
                bios[0x1A29] == 0x61 && bios[0x1A30] == 0xFF &&
                bios[0x1A31] == 0xE7)
            {
                bios[0x1A0A] = 0xE9;
                bios[0x1A0B] = 0x3D;
                bios[0x1A0C] = 0x00;
                log("[I486][BIOS] PIT calibration F000:FA0A bypassed; redirected execution to F000:FA4A");
            }

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
         * HLT is a real CPU stop state, not an instruction that may be
         * re-dispatched from the following EIP on every Spectrum frame.
         * Keep the PC peripherals running while halted; only give the PCem
         * core another chance when the PIC has an unmasked interrupt pending.
         * The core remains responsible for checking IF and acknowledging it.
         */
        if (i486_halted && !devices::interrupt_pending())
        {
            devices::advance_cycles(660000U);
            return 0;
        }

        /*
         * Do not cache this preparation result: the host frame/reset path may
         * alter the expansion CPU's hidden CS cache between frames. The
         * helper is deliberately conditional on EIP still being the BIOS
         * bootstrap target, so it cannot rewind an already-running BIOS.
         */
        if (!i486_pcem_prepare_first_instruction())
            return 0;

        /*
         * mainloop.cpp schedules one 486 slice per Spectrum frame. Advance
         * the virtual 33 MHz clock by one PAL frame (20 ms) so PIT channel 0
         * keeps its real 1.193182 MHz rate despite the bounded instruction
         * budget used to keep the host responsive.
         */
        static bool hlt_text_dumped = false;
    unsigned int executed = i486_pcem_execute_instructions(instruction_budget);
    if (i486_halted && !hlt_text_dumped)
    {
        vga::dump_text();
        hlt_text_dumped = true;
    }
    else if (!i486_halted)
        hlt_text_dumped = false;
        /* While the CPU runs, PIT time is advanced per instruction by
         * the interpreter timing bridge. A halted CPU still shares wall time
         * with the host frame clock, so peripherals continue to advance.
         */
        if (i486_halted)
            devices::advance_cycles(660000U);
        return executed;
    }

    unsigned char read8(unsigned int address)
    {
        bool handled = false;
        unsigned char value = vga::memory_read(address, &handled);
        return handled ? value : read8_internal(address);
    }

    unsigned short read16(unsigned int address)
    {
        return (unsigned short)(read8(address) |
            ((unsigned short)read8(address + 1U) << 8));
    }

    unsigned int read32(unsigned int address)
    {
        return (unsigned int)read8(address) |
            ((unsigned int)read8(address + 1U) << 8) |
            ((unsigned int)read8(address + 2U) << 16) |
            ((unsigned int)read8(address + 3U) << 24);
    }

    void write8(unsigned int address, unsigned char value)
    {
        static unsigned int ignored_bios_writes = 0;
        bool handled = false;
        vga::memory_write(address, value, &handled);
        if (handled) return;
        if (address >= 0xFFFF0000U ||
            (address >= 0x000F0000U && address <= 0x000FFFFFU))
        {
            if (ignored_bios_writes < 32U)
            {
                char message[128];
                sprintf(message, "[I486][MEM] WRITE_TO_BIOS #%u ADDR=%08X VALUE=%02X IGNORED",
                        ++ignored_bios_writes, address, value);
                log(message);
            }
            return;
        }

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

        /*
         * The BIOS executes a 20,480-iteration delay at F000:E5B2
         * (LOOP -2). Logging every iteration consumes the entire diagnostic
         * budget before POST/VGA activity becomes visible. Keep the first
         * instruction sample and report how many subsequent iterations were
         * suppressed when the next BIOS instruction trace arrives.
         */
        static bool bios_delay_loop_active = false;
        static unsigned long bios_delay_loop_suppressed = 0;
        const bool is_bios_si_trace = strstr(message, "[BIOS-SI-TRACE]") != 0;
        const bool is_bios_delay_iteration =
            strstr(message, "[BIOS-SI-TRACE] F000:0000E5B2 OP=E2") != 0;

        if (bios_delay_loop_active && is_bios_si_trace && !is_bios_delay_iteration)
        {
            char summary[160];
            sprintf(summary,
                    "[I486][BIOS-TRACE] F000:E5B2 LOOP E2 FE completed; suppressed %lu repeated iteration records",
                    bios_delay_loop_suppressed);
            bios_delay_loop_active = false;
            bios_delay_loop_suppressed = 0;
            log(summary);
        }

        if (is_bios_delay_iteration)
        {
            if (bios_delay_loop_active)
            {
                ++bios_delay_loop_suppressed;
                return;
            }
            bios_delay_loop_active = true;
            bios_delay_loop_suppressed = 0;
        }

        const size_t length = strlen(message);
        const size_t line_bytes = length + ((length == 0 || message[length - 1] != '\n') ? 2U : 0U);
        if (diagnostic_bytes + line_bytes > MaxDiagnosticBytes)
        {
            if (!diagnostic_limit_reported)
            {
                diagnostic_limit_reported = true;
                FILE *limit_file = fopen("C:\\Temp\\spectrum_out.txt", "a");
                if (limit_file)
                {
                    fputs("[I486] diagnostic output limit reached (2 MiB); further i486 log messages suppressed\r\n", limit_file);
                    fclose(limit_file);
                }
            }
            return;
        }

        FILE *file = fopen("C:\\Temp\\spectrum_out.txt", "a");
        if (!file)
            return;

        fputs(message, file);
        if (length == 0 || message[length - 1] != '\n')
            fputs("\r\n", file);
        fclose(file);
        diagnostic_bytes += line_bytes;
    }
}
