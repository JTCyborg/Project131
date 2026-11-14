#pragma once
#include <stdint.h>

namespace i486 { namespace devices {
    void reset();
    uint8_t in8(uint16_t port);
    uint16_t in16(uint16_t port);
    void out8(uint16_t port, uint8_t value);
    void out16(uint16_t port, uint16_t value);
    void close();
} }
