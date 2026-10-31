#include "touch.h"
#include "config.h"
#include "settings.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

extern TFT_eSPI tft;

namespace Touch {
namespace {

SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);

const int16_t Z_MIN = 350;   // pressure below this is not a touch

// Average a few readings while pressed; false if released meanwhile.
bool rawAvg(int32_t& rx, int32_t& ry, uint8_t n) {
    int32_t sx = 0, sy = 0;
    for (uint8_t i = 0; i < n; i++) {
        TS_Point p = ts.getPoint();
        if (p.z < Z_MIN) return false;
        sx += p.x;
        sy += p.y;
        delay(4);
    }
    rx = sx / n;
    ry = sy / n;
    return true;
}

void target(int16_t x, int16_t y, uint16_t fg, uint16_t bg) {
    tft.fillScreen(bg);
    tft.drawFastHLine(x - 12, y, 25, fg);
    tft.drawFastVLine(x, y - 12, 25, fg);
    tft.drawCircle(x, y, 6, fg);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.drawString("Touch calibration", SCREEN_W / 2, SCREEN_H / 2 - 12);
    tft.drawString("Tap the cross", SCREEN_W / 2, SCREEN_H / 2 + 8);
}

}  // namespace

void begin() {
    touchSPI.begin(PIN_TOUCH_SCK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
    ts.begin(touchSPI);
    ts.setRotation(0);
}

bool read(int16_t& x, int16_t& y) {
    TS_Point p = ts.getPoint();
    if (p.z < Z_MIN) return false;
    const float* c = Store::s.cal;
    float fx = c[0] * p.x + c[1] * p.y + c[2];
    float fy = c[3] * p.x + c[4] * p.y + c[5];
    x = (int16_t)constrain(fx, 0, SCREEN_W - 1);
    y = (int16_t)constrain(fy, 0, SCREEN_H - 1);
    return true;
}

void calibrate() {
    const int16_t sx[3] = { 24, SCREEN_W - 24, 24 };
    const int16_t sy[3] = { 24, 24, SCREEN_H - 24 };
    int32_t rx[3], ry[3];
    const uint16_t fg = TFT_WHITE, bg = TFT_BLACK;

    for (uint8_t i = 0; i < 3; i++) {
        target(sx[i], sy[i], fg, bg);
        while (ts.getPoint().z >= Z_MIN) delay(10);          // released first
        while (!rawAvg(rx[i], ry[i], 8)) delay(10);           // then a firm tap
        tft.fillCircle(sx[i], sy[i], 6, TFT_GREEN);
        while (ts.getPoint().z >= Z_MIN) delay(10);
        delay(200);
    }

    // Solve x = a*rx + b*ry + c (and the same for y) through the 3 points.
    const double det = (double)rx[0] * (ry[1] - ry[2]) - (double)ry[0] * (rx[1] - rx[2]) +
                       ((double)rx[1] * ry[2] - (double)rx[2] * ry[1]);
    if (fabs(det) < 1.0) {   // taps on a line: try again
        calibrate();
        return;
    }
    auto solve = [&](const int16_t* v, float* out) {
        out[0] = (float)((v[0] * (ry[1] - ry[2]) - ry[0] * (v[1] - v[2]) + (v[1] * ry[2] - v[2] * ry[1])) / det);
        out[1] = (float)((rx[0] * (v[1] - v[2]) - v[0] * (rx[1] - rx[2]) + (rx[1] * v[2] - rx[2] * v[1])) / det);
        out[2] = (float)((rx[0] * ((double)ry[1] * v[2] - (double)ry[2] * v[1]) -
                          ry[0] * ((double)rx[1] * v[2] - (double)rx[2] * v[1]) +
                          v[0] * ((double)rx[1] * ry[2] - (double)rx[2] * ry[1])) / det);
    };
    solve(sx, &Store::s.cal[0]);
    solve(sy, &Store::s.cal[3]);
    Store::s.calOk = true;
    Store::save();
    Serial.printf("[touch] calibrated: %.4f %.4f %.1f / %.4f %.4f %.1f\n",
                  Store::s.cal[0], Store::s.cal[1], Store::s.cal[2],
                  Store::s.cal[3], Store::s.cal[4], Store::s.cal[5]);
}

}  // namespace Touch
