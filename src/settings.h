// Settings kept in flash (NVS) across power cycles.
#pragma once
#include <Arduino.h>

enum Theme : uint8_t { THEME_DARK = 0, THEME_LIGHT = 1, THEME_AMBER = 2 };

struct Settings {
    uint8_t theme      = THEME_DARK;
    uint8_t brightness = 1;   // 0 Low, 1 Med, 2 High
    uint8_t dimAfter   = 1;   // 0 5 s, 1 15 s, 2 30 s
    uint8_t sleepAfter = 1;   // 0 1 min, 1 2 min, 2 5 min

    // Touch calibration: screen = affine(raw). Valid when calOk.
    bool  calOk = false;
    float cal[6] = { 0, 0, 0, 0, 0, 0 };   // x = c0*rx + c1*ry + c2; y = c3*rx + c4*ry + c5
};

namespace Store {
    extern Settings s;
    void load();
    void save();

    uint8_t  brightnessPwm();   // backlight duty for the chosen level
    uint32_t dimAfterMs();
    uint32_t sleepAfterMs();
}
