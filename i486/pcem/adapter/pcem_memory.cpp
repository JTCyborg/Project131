/*
 * Project131 PCem memory adapter.
 *
 * Initial stage: direct real-mode identity mappings for the expansion RAM,
 * plus the low and high BIOS windows. Paging/MMU translation is intentionally
 * not enabled until the page-walk and fault paths are ported. All access stays
 * inside the i486 module and never uses Spectrum/ATM memory.
 */

#include "../../../std.h"
#include "../../i486.h"
#include "pcem_memory.h"

#include <stdint.h>
#include <string.h>

namespace
{
    static const unsigned int PageShift = 12;
    static const unsigned int PageSize = 1U << PageShift;
    static const unsigned int PageCount = 1U << 20;
    static const uintptr_t InvalidPage = (uintptr_t)-1;

    static uintptr_t read_lookup[PageCount];
    static uintptr_t write_lookup[PageCount];
    static unsigned char unmapped_page[PageSize];
}

extern "C"
{
    uintptr_t *readlookup2 = read_lookup;
    uintptr_t *writelookup2 = write_lookup;

    void pcem_memory_rebuild(void)
    {
        unsigned int page;
        unsigned char *ram = i486::detail::ram_data();
        unsigned char *bios = i486::detail::bios_data();
        unsigned long ram_size = i486::detail::ram_size_bytes();
        unsigned long ram_pages = ram_size >> PageShift;

        for (page = 0; page < PageCount; ++page)
        {
            read_lookup[page] = InvalidPage;
            write_lookup[page] = InvalidPage;
        }

        memset(unmapped_page, 0xFF, sizeof(unmapped_page));

        if (ram)
        {
            if (ram_pages > PageCount)
                ram_pages = PageCount;

            for (page = 0; page < (unsigned int)ram_pages; ++page)
            {
                const uintptr_t linear_base = (uintptr_t)page << PageShift;
                const uintptr_t offset = (uintptr_t)ram - linear_base;
                read_lookup[page] = offset;
                write_lookup[page] = offset;
            }
        }

        if (bios)
        {
            const unsigned int low_bios_page = 0x000F0000U >> PageShift;
            const unsigned int high_bios_page = 0xFFFF0000U >> PageShift;
            const uintptr_t low_offset = (uintptr_t)bios - (uintptr_t)0x000F0000U;
            const uintptr_t high_offset = (uintptr_t)bios - (uintptr_t)0xFFFF0000U;

            for (page = 0; page < 16; ++page)
            {
                read_lookup[low_bios_page + page] = low_offset;
                write_lookup[low_bios_page + page] = InvalidPage;

                read_lookup[high_bios_page + page] = high_offset;
                write_lookup[high_bios_page + page] = InvalidPage;
            }
        }
    }

    uint8_t readmembl(uint32_t address)
    {
        return i486::read8(address);
    }

    uint16_t readmemwl(uint32_t address)
    {
        return (uint16_t)(i486::read8(address) |
            ((uint16_t)i486::read8(address + 1U) << 8));
    }

    uint32_t readmemll(uint32_t address)
    {
        return (uint32_t)i486::read8(address) |
            ((uint32_t)i486::read8(address + 1U) << 8) |
            ((uint32_t)i486::read8(address + 2U) << 16) |
            ((uint32_t)i486::read8(address + 3U) << 24);
    }

    uint64_t readmemql(uint32_t address)
    {
        return (uint64_t)readmemll(address) |
            ((uint64_t)readmemll(address + 4U) << 32);
    }

    void writemembl(uint32_t address, uint8_t value)
    {
        i486::write8(address, value);
    }

    void writememwl(uint32_t address, uint16_t value)
    {
        i486::write16(address, value);
    }

    void writememll(uint32_t address, uint32_t value)
    {
        i486::write32(address, value);
    }

    void writememql(uint32_t address, uint64_t value)
    {
        writememll(address, (uint32_t)value);
        writememll(address + 4U, (uint32_t)(value >> 32));
    }

    uint8_t *getpccache(uint32_t address)
    {
        const unsigned int page = address >> PageShift;
        if (read_lookup[page] != InvalidPage)
            return (uint8_t *)read_lookup[page];

        return (uint8_t *)((uintptr_t)unmapped_page -
            ((uintptr_t)address & ~((uintptr_t)PageSize - 1U)));
    }
}
