#pragma once
#include <stdint.h>
namespace i486 { namespace vga {
    void init();
    void close();
    uint8_t memory_read(uint32_t address, bool *handled);
    void memory_write(uint32_t address, uint8_t value, bool *handled);
    uint8_t in8(uint16_t port, bool *handled);
    void out8(uint16_t port, uint8_t value, bool *handled);
} }
