// The two screens (remote and settings): drawing and finding what was touched.
#pragma once
#include <Arduino.h>

enum class Screen : uint8_t { Main, Settings };

// Everything on screen that reacts to a touch.
enum class Hit : uint8_t {
    None,
    // Remote screen
    Gear, MediaPrev, MediaPlay, MediaNext, VolDown, VolUp, Mute, Tab, Slot,
    // Settings screen
    Back, Pairing, LoadIr, Theme, Brightness, DimAfter, SleepAfter,
};

struct Target {
    Hit hit = Hit::None;
    uint8_t arg = 0;     // tab / slot / option number
    int16_t x = 0, y = 0, w = 0, h = 0;
};

namespace UI {
    extern Screen screen;
    extern uint8_t device;          // selected tab (DeviceId)
    extern String irStatus;         // the "Load .ir files" row's subtitle

    void drawScreen();              // everything
    void drawStatus();              // battery bar row
    void drawBatteryInfo();         // the measured voltage, on Settings
    void drawTabsAndPanel();
    void drawSlot(uint8_t slot, bool pressed);
    void drawTarget(const Target& t, bool pressed);

    Target hitTest(int16_t x, int16_t y);
    void message(const char* text);  // a short note over the panel area
}
