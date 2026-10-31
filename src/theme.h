// Colour themes (RGB565). Dark is the mockup's palette.
#pragma once
#include <Arduino.h>

constexpr uint16_t rgb(uint32_t c) {
    return (uint16_t)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xFF) >> 3));
}

struct Palette {
    uint16_t bg, surface, raised, border;
    uint16_t text, textDim, textFaint;
    uint16_t bt, btBg, btIcon;          // Bluetooth / PC controls
    uint16_t ir, irText;                // the IR volume row
    uint16_t tabOnBg, tabOnText, tabOffBg, tabOffText;
    uint16_t segOnBg, segOnText;
    uint16_t battTrack, battOk, battMid, battLow, battText;
    uint16_t rec;
};

const Palette& palette();   // the one in use (Store::s.theme)
