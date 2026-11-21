// The screens: drawing them and finding what was touched.
//
//   Main      the remote (now playing, media, volume, IR devices)
//   Home      Home Assistant switches and blinds
//   Settings  scrolling list -> Pairing (Bluetooth), HaStatus (Home Assistant)
#pragma once
#include <Arduino.h>

enum class Screen : uint8_t { Main, Settings, Pairing, Home, HaStatus };

// Everything on screen that reacts to a touch. Keep the runs that are used
// with "+ i" (MediaPrev.., Brightness..) in order.
enum class Hit : uint8_t {
    None,
    // Remote screen
    Gear, HomeBtn, MediaPrev, MediaPlay, MediaNext, VolDown, VolUp, Mute, Tab, Slot,
    // Settings screen
    Back, Pairing, HaRow, LoadIr, Theme, Brightness, DimAfter, SleepAfter, WifiOff,
    // Bluetooth screen
    Forget,
    // Home page
    HomeTile, Blind,
    // Home Assistant status
    HaTest,
};

struct Target {
    Hit hit = Hit::None;
    uint8_t arg = 0;     // tab / slot / option / tile number
    int16_t x = 0, y = 0, w = 0, h = 0;
};

namespace UI {
    extern Screen screen;
    extern uint8_t device;          // selected tab (DeviceId)
    extern String irStatus;         // what the SD card had to offer
    extern int16_t scrollY;         // Settings / HaStatus list offset

    void drawScreen();              // everything
    void drawStatus();              // battery bar row (remote screen)
    void drawBatteryInfo();         // the measured voltage, on Settings
    void drawTabsAndPanel();
    void drawSlot(uint8_t slot, bool pressed);
    void drawTarget(const Target& t, bool pressed);

    Target hitTest(int16_t x, int16_t y);
    void message(const char* text);  // a short note over the screen
    void onBle(uint8_t changed);     // redraw what a Bluetooth change touched
    void onHa(uint8_t changed);      // ... and a Home Assistant change

    // Lists that scroll (Settings, Home Assistant status).
    bool scrollable();
    int16_t scrollMax();
    void scrollTo(int16_t y);        // clamps and redraws
}
