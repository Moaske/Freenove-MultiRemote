// Button icons, drawn from simple shapes (about 20x20, centred on cx/cy).
#pragma once
#include <Arduino.h>
#include "ir_codes.h"

namespace Icons {
    void draw(Icon icon, int16_t cx, int16_t cy, uint16_t fg, uint16_t bg, uint16_t recColor);
    void gear(int16_t cx, int16_t cy, uint16_t fg, uint16_t bg);
    void back(int16_t cx, int16_t cy, uint16_t fg);
    void bluetooth(int16_t cx, int16_t cy, uint16_t fg);
    void note(int16_t cx, int16_t cy, uint16_t fg);
    void file(int16_t cx, int16_t cy, uint16_t fg, uint16_t bg);
    void chevron(int16_t cx, int16_t cy, uint16_t fg);
    void bolt(int16_t cx, int16_t cy, uint16_t fg);
}
