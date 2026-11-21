// Freenove MultiRemote
//
// A touch remote on the Freenove ESP32 Display 2.8" (FNK0114B_2P8):
//  - IR to the amplifier, CD and MiniDisc, from Flipper Zero .ir files in
//    /remote on the SD card (amp.ir, cd.ir, md.ir)
//  - Bluetooth LE to the Windows PC: media keys, and track info plus cover
//    from the tray app (tray/)
//  - Home Assistant over Wi-Fi (only while its page is open): six switches
//    and the blinds, set up in /remote/ha.cfg
//
// Hardware: Adafruit 5639 IR emitter IN on GPIO27, battery through the
// Adafruit bq25185 charger on BAT+. See README.md.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "settings.h"
#include "theme.h"
#include "battery.h"
#include "touch.h"
#include "ir_codes.h"
#include "ui.h"
#include "ble_link.h"
#include "ha.h"

TFT_eSPI tft;

namespace {

// ---- Backlight and sleep --------------------------------------------------
//
// Active -> Dimmed (backlight low) -> Standby (screen off, Bluetooth still
// connected, so the PC link and track info are there the moment it wakes)
// -> after DEEP_AFTER_MS in standby: deep sleep, which drops Bluetooth;
// a touch or BOOT restarts the remote and the PC reconnects in a few seconds.

enum class Power : uint8_t { Active, Dimmed, Standby };
Power    s_power = Power::Active;
uint32_t s_lastActivity = 0;
uint32_t s_standbySince = 0;
const uint8_t  DIM_PWM = 12;
const uint32_t DEEP_AFTER_MS = 30UL * 60 * 1000;

void backlight(uint8_t duty) { ledcWrite(BL_CHANNEL, duty); }

void wakeScreen() {
    if (s_power == Power::Standby) {
        tft.writecommand(0x11);   // ST7789 SLPOUT
        delay(120);
        UI::drawScreen();         // nothing was drawn while the panel slept
        Serial.println("[power] awake");
        Ha::setWanted(UI::screen == Screen::Home || UI::screen == Screen::HaStatus);
    }
    s_power = Power::Active;
    backlight(Store::brightnessPwm());
}

void activity() {
    s_lastActivity = millis();
    if (s_power != Power::Active) wakeScreen();
}

void standby() {
    Serial.println("[power] standby");
    backlight(0);
    tft.writecommand(0x10);       // ST7789 SLPIN
    Ha::forceOff();               // nobody can see the Home page: Wi-Fi off now
    s_power = Power::Standby;
    s_standbySince = millis();
}

void deepSleep() {
    Serial.println("[power] deep sleep");
    Serial.flush();
    backlight(0);
    tft.writecommand(0x10);
    // The XPT2046's pen interrupt goes low on a touch. Only wake on it when
    // it idles high, or the board would wake straight away; else BOOT.
    if (digitalRead(PIN_TOUCH_IRQ) == HIGH) esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_TOUCH_IRQ, 0);
    else                                    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOOT, 0);
    esp_deep_sleep_start();
}

void powerTick() {
    const uint32_t now = millis();
    if (s_power == Power::Standby) {
        if (digitalRead(PIN_BOOT) == LOW) { activity(); return; }
        if (now - s_standbySince >= DEEP_AFTER_MS) deepSleep();
        return;
    }
    const uint32_t idle = now - s_lastActivity;
    if (idle >= Store::sleepAfterMs()) {
        standby();
    } else if (s_power == Power::Active && idle >= Store::dimAfterMs()) {
        s_power = Power::Dimmed;
        backlight(DIM_PWM);
    }
}

// Below 3.38 V for 15 s on the cell: the regulator is about to drop out.
// Say so and power down cleanly; BOOT (or charging + reset) wakes it.
void batteryGuard() {
    static uint32_t lowSince = 0;
    if (!Battery::present() || Battery::external() || Battery::millivolts() >= 3380) {
        lowSince = 0;
        return;
    }
    if (!lowSince) lowSince = millis();
    if (millis() - lowSince < 15000) return;
    if (s_power == Power::Standby) wakeScreen();
    backlight(Store::brightnessPwm());
    UI::message("Battery empty - charge me");
    delay(3000);
    backlight(0);
    tft.writecommand(0x10);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOOT, 0);
    esp_deep_sleep_start();
}

// ---- Touch handling -------------------------------------------------------

Target   s_target;            // what the finger is on
bool     s_down = false;
bool     s_swallow = false;   // the touch that woke the screen does nothing
uint32_t s_downAt = 0, s_nextRepeat = 0, s_upSince = 0;
bool     s_longDone = false;
// Scrolling lists: a finger that moves more than SCROLL_SLOP drags the list.
constexpr int16_t SCROLL_SLOP = 10;
int16_t  s_downY = 0, s_scroll0 = 0;
bool     s_dragging = false;
uint32_t s_lastScrollDraw = 0;

void sendVolume(Hit h) {
    const int8_t sig = h == Hit::VolDown ? IrCodes::volDown : h == Hit::VolUp ? IrCodes::volUp : IrCodes::mute;
    IrCodes::send(DEV_AMP, sig);
}

void mediaKey(Ble::Key k) {
    if (Ble::sendKey(k)) return;
    UI::message(Ble::paired() ? "PC not connected" : "Pair a PC in Settings first");
    delay(1200);
    UI::drawTabsAndPanel();
}

void setScreen(Screen s) {
    if (UI::screen != s) UI::scrollY = 0;
    UI::screen = s;
    Ha::setWanted(s == Screen::Home || s == Screen::HaStatus);
    UI::drawScreen();
}

// Everything from the SD card: .ir files, ha.cfg and its icons.
void loadSdFiles() {
    IrCodes::loadAll();
    Ha::loadConfig();
    String st;
    if (!IrCodes::sdPresent()) {
        st = "No SD card found";
    } else {
        for (uint8_t i = 0; i < DEV_COUNT; i++) {
            if (!IrCodes::devices[i].loaded) continue;
            if (st.length()) st += ", ";
            st += IrCodes::devices[i].title;
        }
        st = st.length() ? st : String("No .ir files");
        if (Ha::configured()) st += ", Home";
    }
    UI::irStatus = st;
}

// Applied on press, so the IR goes out the moment the finger lands.
void onPress(const Target& t) {
    IrDevice& d = IrCodes::devices[UI::device];
    switch (t.hit) {
    case Hit::MediaPrev: mediaKey(Ble::Key::Prev); break;
    case Hit::MediaPlay: mediaKey(Ble::Key::PlayPause); break;
    case Hit::MediaNext: mediaKey(Ble::Key::Next); break;
    case Hit::VolDown: case Hit::VolUp: case Hit::Mute:
        sendVolume(t.hit);
        s_nextRepeat = millis() + HOLD_REPEAT_DELAY_MS;
        break;
    case Hit::Slot: {
        const Slot& sl = d.slots[t.arg];
        if (sl.kind == SlotKind::Signal) {
            IrCodes::send((DeviceId)UI::device, sl.sig);
            const Icon ic = sl.icon;
            if (ic == Icon::Stop || ic == Icon::Power || ic == Icon::Eject) d.playing = false;
            if (ic == Icon::Play) d.playing = true;
            if (ic == Icon::Pause) d.playing = false;
            // Keep a Play/Pause toggle elsewhere in the panel in step.
            for (uint8_t s = 0; s < SLOTS; s++)
                if (d.slots[s].kind == SlotKind::PlayToggle) UI::drawSlot(s, false);
        }
        // PlayToggle acts on release (short press) or on the long press.
        break;
    }
    default:
        break;
    }
}

void onRelease(const Target& t, uint32_t heldMs) {
    IrDevice& d = IrCodes::devices[UI::device];
    switch (t.hit) {
    case Hit::Gear:    setScreen(Screen::Settings); return;
    case Hit::HomeBtn: setScreen(Screen::Home); return;
    case Hit::Back:
        setScreen(UI::screen == Screen::Pairing || UI::screen == Screen::HaStatus ? Screen::Settings : Screen::Main);
        return;
    case Hit::HaRow:    setScreen(Screen::HaStatus); return;
    case Hit::HomeTile: Ha::toggle(t.arg); break;
    case Hit::Blind:    Ha::cover((Ha::Cover)t.arg); break;
    case Hit::HaTest:   Ha::test(); break;
    case Hit::WifiOff:  Store::s.wifiOff = t.arg; Store::save(); break;
    case Hit::Tab:
        if (t.arg != UI::device) {
            UI::device = t.arg;
            UI::drawTabsAndPanel();
        }
        return;
    case Hit::Slot: {
        Slot& sl = d.slots[t.arg];
        if (sl.kind == SlotKind::PlayToggle && !s_longDone && heldMs < LONG_PRESS_MS) {
            IrCodes::send((DeviceId)UI::device, d.playing ? sl.sig2 : sl.sig);
            d.playing = !d.playing;
        }
        break;
    }
    case Hit::LoadIr:
        UI::message("Reading SD card...");
        loadSdFiles();
        UI::drawScreen();
        return;
    case Hit::Pairing: setScreen(Screen::Pairing); return;
    case Hit::Forget:
        if (!Ble::paired()) return;
        Ble::forget();
        UI::message("Forgotten. Remove it in Windows too");
        delay(2500);
        UI::drawScreen();
        return;
    case Hit::Theme:
        if (Store::s.theme != t.arg) {
            Store::s.theme = t.arg;
            Store::save();
            UI::drawScreen();
        }
        return;
    case Hit::Brightness:
        Store::s.brightness = t.arg;
        Store::save();
        backlight(Store::brightnessPwm());
        break;
    case Hit::DimAfter:   Store::s.dimAfter = t.arg;   Store::save(); break;
    case Hit::SleepAfter: Store::s.sleepAfter = t.arg; Store::save(); break;
    default: break;
    }
    UI::drawTarget(t, false);
}

void touchTick() {
    int16_t x, y;
    const bool pressed = Touch::read(x, y);
    const uint32_t now = millis();

    if (pressed && !s_down) {                       // finger down
        s_down = true;
        s_downAt = now;
        s_longDone = false;
        if (s_power != Power::Active) {             // first touch only wakes
            s_swallow = true;
            activity();
            return;
        }
        s_swallow = false;
        activity();
        s_downY = y;
        s_scroll0 = UI::scrollY;
        s_dragging = false;
        s_target = UI::hitTest(x, y);
        if (s_target.hit == Hit::None) return;
        UI::drawTarget(s_target, true);
        onPress(s_target);
        return;
    }

    if (pressed) s_upSince = 0;

    if (pressed && s_down) {                        // held
        activity();
        if (s_swallow) return;
        // Lists: drag to scroll; a drag cancels the button under the finger.
        if (UI::scrollable() && !s_dragging && abs(y - s_downY) > SCROLL_SLOP && UI::scrollMax() > 0) {
            s_dragging = true;
            if (s_target.hit != Hit::None) UI::drawTarget(s_target, false);
            s_target = Target();
        }
        if (s_dragging) {
            if (now - s_lastScrollDraw >= 40) {
                s_lastScrollDraw = now;
                UI::scrollTo(s_scroll0 - (y - s_downY));
            }
            return;
        }
        if (s_target.hit == Hit::None) return;
        const bool vol = s_target.hit == Hit::VolDown || s_target.hit == Hit::VolUp;
        if (vol && now >= s_nextRepeat) {           // hold to keep turning
            sendVolume(s_target.hit);
            s_nextRepeat = now + HOLD_REPEAT_MS;
        }
        if (s_target.hit == Hit::Slot && !s_longDone && now - s_downAt >= LONG_PRESS_MS) {
            IrDevice& d = IrCodes::devices[UI::device];
            if (d.slots[s_target.arg].kind == SlotKind::PlayToggle) {
                d.playing = !d.playing;             // resync without sending
                s_longDone = true;
                UI::drawSlot(s_target.arg, true);
            }
        }
        return;
    }

    if (!pressed && s_down) {                       // released
        // A resistive panel can blink "up" for a moment while pressed.
        if (!s_upSince) s_upSince = now;
        if (now - s_upSince < 40) return;
        s_upSince = 0;
        s_down = false;
        if (s_dragging) { s_dragging = false; return; }
        if (s_swallow || s_target.hit == Hit::None) return;
        onRelease(s_target, now - s_downAt);
        s_target = Target();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    // A deep-sleep wake leaves the wake pins in RTC mode, where normal reads
    // don't work: hand them back to the digital side first.
    rtc_gpio_deinit((gpio_num_t)PIN_TOUCH_IRQ);
    rtc_gpio_deinit((gpio_num_t)PIN_BOOT);
    setCpuFrequencyMhz(80);                // plenty for a remote (Bluetooth needs >= 80)

    pinMode(PIN_AMP_EN, OUTPUT);  digitalWrite(PIN_AMP_EN, LOW);
    pinMode(PIN_LED_R, OUTPUT);   digitalWrite(PIN_LED_R, HIGH);
    pinMode(PIN_LED_G, OUTPUT);   digitalWrite(PIN_LED_G, HIGH);
    pinMode(PIN_LED_B, OUTPUT);   digitalWrite(PIN_LED_B, HIGH);
    pinMode(PIN_BOOT, INPUT_PULLUP);
    pinMode(PIN_TOUCH_IRQ, INPUT);

    Store::load();

    tft.init();
    tft.setRotation(0);                     // portrait
    ledcSetup(BL_CHANNEL, BL_FREQ, BL_BITS);
    ledcAttachPin(TFT_BL, BL_CHANNEL);
    tft.fillScreen(TFT_BLACK);
    backlight(Store::brightnessPwm());

    Touch::begin();
    // BOOT held at power-on recalibrates -- but not when BOOT just woke us.
    const bool woke = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
    if (!Store::s.calOk || (!woke && digitalRead(PIN_BOOT) == LOW)) Touch::calibrate();

    Battery::begin();
    Ble::begin(Battery::percent());
    IrCodes::txBegin();
    IrCodes::sdBegin();
    Ha::begin();
    loadSdFiles();

    UI::drawScreen();
    s_lastActivity = millis();
}

void loop() {
    touchTick();

    const uint8_t ch = Ble::poll();
    if (ch && s_power != Power::Standby) UI::onBle(ch);
    const uint8_t hc = Ha::poll();
    if (hc && s_power != Power::Standby) UI::onHa(hc);

    if (Battery::update()) {
        static uint8_t sentPct = 255;
        if (Battery::percent() != sentPct) {
            sentPct = Battery::percent();
            Ble::setBattery(sentPct);       // Windows shows it under Bluetooth
        }
        if (s_power != Power::Standby) {
            UI::drawStatus();
            UI::drawBatteryInfo();
        }
    }
    batteryGuard();
    powerTick();
    delay(s_power == Power::Standby ? 40 : 10);
}
