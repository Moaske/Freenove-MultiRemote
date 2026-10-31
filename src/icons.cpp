#include "icons.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace Icons {
namespace {

void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
    tft.drawLine(x0, y0, x1, y1, c);
    tft.drawLine(x0 + 1, y0, x1 + 1, y1, c);
    tft.drawLine(x0, y0 + 1, x1, y1 + 1, c);
}

void speaker(int16_t x, int16_t y, uint16_t c) {
    tft.fillRect(x - 10, y - 4, 5, 9, c);
    tft.fillTriangle(x - 6, y - 4, x + 1, y - 10, x + 1, y + 10, c);
    tft.fillTriangle(x - 6, y - 4, x - 6, y + 4, x + 1, y + 10, c);
}

}  // namespace

void draw(Icon icon, int16_t x, int16_t y, uint16_t fg, uint16_t bg, uint16_t recColor) {
    switch (icon) {
    case Icon::Power:
        tft.fillCircle(x, y, 9, fg);
        tft.fillCircle(x, y, 7, bg);
        tft.fillRect(x - 3, y - 10, 7, 9, bg);
        tft.fillRect(x - 1, y - 11, 3, 11, fg);
        break;
    case Icon::Play:
        tft.fillTriangle(x - 6, y - 9, x - 6, y + 9, x + 9, y, fg);
        break;
    case Icon::Pause:
        tft.fillRect(x - 7, y - 8, 5, 17, fg);
        tft.fillRect(x + 3, y - 8, 5, 17, fg);
        break;
    case Icon::PlayPause:
        tft.fillTriangle(x - 10, y - 8, x - 10, y + 8, x + 1, y, fg);
        tft.fillRect(x + 4, y - 8, 3, 17, fg);
        tft.fillRect(x + 9, y - 8, 3, 17, fg);
        break;
    case Icon::Stop:
        tft.fillRect(x - 7, y - 7, 15, 15, fg);
        break;
    case Icon::Prev:
        tft.fillRect(x - 9, y - 8, 3, 17, fg);
        tft.fillTriangle(x + 8, y - 8, x + 8, y + 8, x - 5, y, fg);
        break;
    case Icon::Next:
        tft.fillRect(x + 7, y - 8, 3, 17, fg);
        tft.fillTriangle(x - 8, y - 8, x - 8, y + 8, x + 5, y, fg);
        break;
    case Icon::Rew:
        tft.fillTriangle(x, y - 8, x, y + 8, x - 10, y, fg);
        tft.fillTriangle(x + 10, y - 8, x + 10, y + 8, x, y, fg);
        break;
    case Icon::FF:
        tft.fillTriangle(x, y - 8, x, y + 8, x + 10, y, fg);
        tft.fillTriangle(x - 10, y - 8, x - 10, y + 8, x, y, fg);
        break;
    case Icon::Eject:
        tft.fillTriangle(x - 9, y + 2, x + 9, y + 2, x, y - 8, fg);
        tft.fillRect(x - 9, y + 5, 19, 3, fg);
        break;
    case Icon::Rec:
        tft.fillCircle(x, y, 8, recColor);
        break;
    case Icon::VolUp:
        speaker(x - 2, y, fg);
        tft.fillRect(x + 3, y - 1, 10, 3, fg);
        tft.fillRect(x + 7, y - 5, 3, 11, fg);
        break;
    case Icon::VolDown:
        speaker(x - 2, y, fg);
        tft.fillRect(x + 3, y - 1, 10, 3, fg);
        break;
    case Icon::Mute:
        speaker(x - 2, y, fg);
        thickLine(x + 3, y - 5, x + 11, y + 5, fg);
        thickLine(x + 11, y - 5, x + 3, y + 5, fg);
        break;
    default:
        break;
    }
}

void gear(int16_t x, int16_t y, uint16_t fg, uint16_t bg) {
    for (uint8_t i = 0; i < 8; i++) {
        const float a = i * PI / 4;
        tft.fillCircle(x + (int16_t)lroundf(6.5f * cosf(a)), y + (int16_t)lroundf(6.5f * sinf(a)), 2, fg);
    }
    tft.fillCircle(x, y, 6, fg);
    tft.fillCircle(x, y, 2, bg);
}

void back(int16_t x, int16_t y, uint16_t fg) {
    thickLine(x + 3, y - 6, x - 3, y, fg);
    thickLine(x - 3, y, x + 3, y + 6, fg);
}

void chevron(int16_t x, int16_t y, uint16_t fg) {
    tft.drawLine(x - 2, y - 4, x + 2, y, fg);
    tft.drawLine(x + 2, y, x - 2, y + 4, fg);
}

void bluetooth(int16_t x, int16_t y, uint16_t fg) {
    tft.drawLine(x, y - 7, x, y + 7, fg);
    tft.drawLine(x, y - 7, x + 4, y - 3, fg);
    tft.drawLine(x + 4, y - 3, x - 4, y + 4, fg);
    tft.drawLine(x, y + 7, x + 4, y + 3, fg);
    tft.drawLine(x + 4, y + 3, x - 4, y - 4, fg);
}

void note(int16_t x, int16_t y, uint16_t fg) {
    tft.fillCircle(x - 7, y + 8, 4, fg);
    tft.fillCircle(x + 7, y + 6, 4, fg);
    tft.fillRect(x - 4, y - 9, 2, 17, fg);
    tft.fillRect(x + 10, y - 11, 2, 17, fg);
    tft.fillRect(x - 4, y - 11, 16, 3, fg);
}

void file(int16_t x, int16_t y, uint16_t fg, uint16_t bg) {
    tft.drawRect(x - 6, y - 8, 12, 16, fg);
    tft.fillTriangle(x + 2, y - 8, x + 6, y - 8, x + 6, y - 4, bg);
    tft.drawLine(x + 2, y - 8, x + 6, y - 4, fg);
    tft.drawFastHLine(x - 3, y - 1, 6, fg);
    tft.drawFastHLine(x - 3, y + 2, 6, fg);
}

void bolt(int16_t x, int16_t y, uint16_t fg) {
    tft.fillTriangle(x + 1, y - 5, x - 3, y + 1, x + 1, y + 1, fg);
    tft.fillTriangle(x - 1, y - 1, x + 3, y - 1, x - 1, y + 5, fg);
}

}  // namespace Icons
