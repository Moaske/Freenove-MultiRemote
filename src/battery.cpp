#include "battery.h"
#include "config.h"

namespace Battery {
namespace {

// mV at BAT+. With input power the bq25185 holds BAT+ at 4.5 V; a cell tops
// out at 4.2 V. Hysteresis so ADC noise cannot flicker between the two.
const uint16_t EXT_ON_MV  = 4350;
const uint16_t EXT_OFF_MV = 4280;
const uint16_t PRESENT_MV = 2500;
const uint32_t SAMPLE_MS  = 1000;

// Charge estimate under the board's own load. 0% is where the 3.3 V
// regulator starts to drop out, not where the cell is empty.
const uint16_t CURVE_MV[]  = { 3400, 3500, 3600, 3680, 3730, 3770, 3810, 3860, 3930, 4010, 4100, 4150 };
const uint8_t  CURVE_PCT[] = {    0,    3,    8,   17,   27,   38,   48,   59,   70,   82,   95,  100 };

uint16_t s_mv = 0;
uint8_t  s_pct = 0;
bool     s_ext = false, s_present = false;
uint32_t s_lastAt = 0;

uint16_t readMv() {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BATT);
    return (uint16_t)((sum / 16) * BATT_X1000 / 1000);
}

uint8_t curvePct(uint16_t mv) {
    const uint8_t n = sizeof CURVE_MV / sizeof CURVE_MV[0];
    if (mv <= CURVE_MV[0]) return 0;
    for (uint8_t i = 1; i < n; i++)
        if (mv <= CURVE_MV[i])
            return (uint8_t)(CURVE_PCT[i - 1] + (uint32_t)(CURVE_PCT[i] - CURVE_PCT[i - 1]) *
                             (mv - CURVE_MV[i - 1]) / (CURVE_MV[i] - CURVE_MV[i - 1]));
    return 100;
}

void sample(bool first) {
    const uint16_t raw = readMv();
    // A jump of more than 0.25 V is the power source changing: take it now.
    if (first || (raw > s_mv ? raw - s_mv : s_mv - raw) > 250) s_mv = raw;
    else s_mv = (uint16_t)((s_mv * 3u + raw) / 4u);

    s_present = s_mv >= PRESENT_MV;
    const bool wasExt = s_ext;
    if (!s_ext && s_mv >= EXT_ON_MV) s_ext = true;
    else if (s_ext && s_mv < EXT_OFF_MV) s_ext = false;

    // On the cell the shown percentage only goes down, so it does not wander
    // up after a load spike. On external power it stays where it was.
    if (!s_ext) {
        const uint8_t est = curvePct(s_mv);
        if (first || wasExt || est < s_pct || est > s_pct + 10) s_pct = est;
    }
}

}  // namespace

void begin() {
    analogSetPinAttenuation(PIN_BATT, ADC_11db);
    sample(true);
    s_lastAt = millis();
    Serial.printf("[batt] %u mV, %u%%%s\n", s_mv, s_pct, s_ext ? ", external power" : "");
}

bool update() {
    const uint32_t now = millis();
    if (now - s_lastAt < SAMPLE_MS) return false;
    s_lastAt = now;
    const uint8_t pct = s_pct;
    const bool ext = s_ext, pres = s_present;
    sample(false);
    return pct != s_pct || ext != s_ext || pres != s_present;
}

uint8_t percent()      { return s_pct; }
uint16_t millivolts()  { return s_mv; }
bool external()        { return s_ext; }
bool present()         { return s_present; }

}  // namespace Battery
