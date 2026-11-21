// Home Assistant over Wi-Fi: up to 6 on/off switches and one set of blinds.
//
// Wi-Fi is only on while the Home page (or its status screen) is open, plus
// the "Wi-Fi off" delay from Settings. Everything network-related runs in its
// own task, so connecting or a slow answer never blocks the screen.
//
// Configured by /remote/ha.cfg on the SD card (see sd-card/remote/ha.cfg.example);
// button icons come from /remote/icons/<name>.a8 (tools/make_icons.py).
#pragma once
#include <Arduino.h>

namespace Ha {

constexpr uint8_t MAX_SWITCHES = 6;
constexpr uint8_t BLINDS = MAX_SWITCHES;   // item index of the blinds
constexpr uint8_t ICON_PX = 28;

enum class Net : uint8_t {
    Off,          // not wanted right now
    NoConfig,     // no (valid) ha.cfg
    Connecting,   // joining Wi-Fi / first contact with HA
    NoWifi,       // network not found or wrong password
    Online,       // HA answered
    NoHa,         // Wi-Fi fine, HA did not answer
    BadToken,     // HA said 401
};

enum class Found : uint8_t { Unknown, Ok, Missing };

struct Live {                 // what HA last said about one entity
    char   state[20] = "";    // "on", "off", "open", "unavailable", ...
    int8_t position = -1;     // blinds only, 0..100
    Found  found = Found::Unknown;
};

struct Item {
    String id, label, icon;
    bool   hasIcon = false;
    uint8_t alpha[ICON_PX * ICON_PX];
};

enum class Cover : uint8_t { Close = 0, Stop = 1, Open = 2 };

// What poll() reports as changed.
constexpr uint8_t CH_NET = 1, CH_STATE = 2, CH_TEST = 4;

void begin();                 // start the network task (once)
void loadConfig();            // (re)read ha.cfg and the icons from the SD card

// Config, as loaded (read-only for the UI).
bool configured();
const String& configError();
const String& title();
const String& ssid();
const String& url();
uint8_t switchCount();
bool hasBlinds();
const Item& item(uint8_t i);  // 0..switchCount()-1, or BLINDS

Live live(uint8_t i);         // thread-safe copy
Net  net();
const char* netText();        // one line for the screen
String version();             // HA version, once known
int8_t rssi();                // Wi-Fi signal, dBm
String ip();
bool testing();
int32_t testResultMs();       // -1 none yet, -2 failed

// Requests (from the UI; they queue for the task).
void setWanted(bool on);      // Home page open?
void forceOff();              // screen off: Wi-Fi off now, no delay
void toggle(uint8_t i);
void cover(Cover c);
void test();

uint8_t poll();               // CH_* flags since the last call

}  // namespace Ha
