// The Li-Ion cell, read through the board's battery divider on GPIO34.
// Logic and voltage curve from the freenove28 build of SquachWatch-CYD-BATT,
// written for this board with the Adafruit bq25185 charger on BAT+.
#pragma once
#include <Arduino.h>

namespace Battery {
    void begin();
    // Call often; samples once a second. True when what is shown changed.
    bool update();
    uint8_t percent();     // 0..100, estimated under load
    uint16_t millivolts(); // at BAT+, filtered
    bool external();       // charger has input power (BAT+ held at ~4.5 V)
    bool present();
}
