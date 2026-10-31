#include "settings.h"
#include <Preferences.h>

namespace Store {

Settings s;

namespace {
Preferences prefs;
const uint8_t  BRIGHT_PWM[3] = { 50, 130, 255 };
const uint32_t DIM_MS[3]     = { 5000, 15000, 30000 };
const uint32_t SLEEP_MS[3]   = { 60000, 120000, 300000 };
}

void load() {
    prefs.begin("remote", true);
    s.theme      = min<uint8_t>(prefs.getUChar("theme", s.theme), 2);
    s.brightness = min<uint8_t>(prefs.getUChar("bright", s.brightness), 2);
    s.dimAfter   = min<uint8_t>(prefs.getUChar("dim", s.dimAfter), 2);
    s.sleepAfter = min<uint8_t>(prefs.getUChar("sleep", s.sleepAfter), 2);
    s.calOk      = prefs.getBytesLength("cal") == sizeof s.cal &&
                   prefs.getBytes("cal", s.cal, sizeof s.cal) == sizeof s.cal;
    prefs.end();
}

void save() {
    prefs.begin("remote", false);
    prefs.putUChar("theme", s.theme);
    prefs.putUChar("bright", s.brightness);
    prefs.putUChar("dim", s.dimAfter);
    prefs.putUChar("sleep", s.sleepAfter);
    if (s.calOk) prefs.putBytes("cal", s.cal, sizeof s.cal);
    prefs.end();
}

uint8_t  brightnessPwm() { return BRIGHT_PWM[s.brightness]; }
uint32_t dimAfterMs()    { return DIM_MS[s.dimAfter]; }
uint32_t sleepAfterMs()  { return SLEEP_MS[s.sleepAfter]; }

}  // namespace Store
