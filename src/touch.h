// XPT2046 resistive touch on its own SPI bus, mapped to portrait pixels with
// a three-point calibration (stored in NVS, redone by holding BOOT at power-on).
#pragma once
#include <Arduino.h>

namespace Touch {
    void begin();
    // Current state: true while pressed; x/y in screen pixels.
    bool read(int16_t& x, int16_t& y);
    // Full-screen calibration; saves to Store.
    void calibrate();
}
