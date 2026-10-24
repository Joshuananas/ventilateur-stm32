#pragma once
#include <cstdint>

namespace RemoteButton {
    inline constexpr uint8_t OFF = 0;
    inline constexpr uint8_t SPEED_FIRST = 1;
    inline constexpr uint8_t SPEED_LAST = 6;
    inline constexpr uint8_t LIGHT = 7;
    inline constexpr uint8_t DIRECTION = 8;
    inline constexpr uint8_t BRIGHTNESS_DOWN = 9;
    inline constexpr uint8_t BRIGHTNESS_UP = 10;
    inline constexpr uint8_t TIMER_1H = 11;
    inline constexpr uint8_t TIMER_3H = 12;
    inline constexpr uint8_t TIMER_6H = 13;
}
