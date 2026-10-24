#pragma once
#include <cstdint>

namespace RadioLimits {
    inline constexpr uint16_t MAX_PULSES = 512;
    inline constexpr uint8_t MAX_SLOTS = 16;
    inline constexpr uint16_t MAX_BITS = 256;
}

namespace LearningLimits {
    inline constexpr uint8_t CAPACITY = 64;
    inline constexpr uint8_t KNOW_CAP = 16;
}
