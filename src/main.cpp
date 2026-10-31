// Freenove MultiRemote
//
// A touch remote on the Freenove ESP32 Display 2.8" (FNK0114B_2P8):
//  - IR to the amplifier, CD and MiniDisc, from Flipper Zero .ir files in
//    /remote on the SD card (amp.ir, cd.ir, md.ir)
//  - next step: Bluetooth to the Windows PC (media keys, track info, cover)
//
// Hardware: Adafruit 5639 IR emitter IN on GPIO27, battery through the
// Adafruit bq25185 charger on BAT+. See README.md.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#include "config.h"
#include "settings.h"
#include "theme.h"
#include "battery.h"
#include "touch.h"
#include "ir_codes.h"
#include "ui.h"

TFT_eSPI tft;

namespace {

// ---- Backlight and sleep --------------------------------------------------

// Woke: just back from sleep, screen lit, and the touch that woke it must
// not press a button.
enum class Power : uint8_t { Active, Dimmed, Woke };
Power    s_power = Power::Active;
uint32_t s_lastActivity = 0;
const uint8_t DIM_PWM = 12;

void backlight(uint8_t duty) { ledcWrite(BL_CHANNEL, duty); }

void activity() {
    s_lastActivity = millis();
    if (s_power != Power::Active) {
        s_power = Power::Active;
        backlight(Store::brightnessPwm());
    }
}

// Screen off and the CPU in light sleep until the screen or BOOT is touched.
void sleepNow() {
    Serial.println("[power] sleep");
    Serial.flush();
    backlight(0);
    tft.writecommand(0x10);   // ST7789 SLPIN
    delay(5);

    gpio_wakeup_enable((gpio_num_t)PIN_BOOT, GPIO_INTR_LOW_LEVEL);
    // The XPT2046's pen interrupt goes low on a touch. Only use it when it
    // idles high, or the board would wake straight away.
    if (digitalRead(PIN_TOUCH_IRQ) == HIGH)
        gpio_wakeup_enable((gpio_num_t)PIN_TOUCH_IRQ, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    gpio_wakeup_disable((gpio_num_t)PIN_BOOT);
    gpio_wakeup_disable((gpio_num_t)PIN_TOUCH_IRQ);

    tft.writecommand(0x11);   // SLPOUT
    delay(120);
    UI::drawScreen();
    s_power = Power::Woke;
    s_lastActivity = millis();
    backlight(Store::brightnessPwm());
    Serial.println("[power] awake");
}

void powerTick() {
    const uint32_t idle = millis() - s_lastActivity;
    if (idle >= Store::sleepAfterMs()) {
        sleepNow();
    } else if (s_power != Power::Dimmed && idle >= Store::dimAfterMs()) {
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
uint32_t s_downAt = 0, s_nextRepeat = 0;
bool     s_longDone = false;

void sendVolume(Hit h) {
    const int8_t sig = h == Hit::VolDown ? IrCodes::volDown : h == Hit::VolUp ? IrCodes::volUp : IrCodes::mute;
    IrCodes::send(DEV_AMP, sig);
}

void setScreen(Screen s) {
    UI::screen = s;
    UI::drawScreen();
}

void loadIrFiles() {
    IrCodes::loadAll();
    String st;
    if (!IrCodes::sdPresent()) {
        st = "No SD card found";
    } else {
        for (uint8_t i = 0; i < DEV_COUNT; i++) {
            if (!IrCodes::devices[i].loaded) continue;
            if (st.length()) st += ", ";
            st += IrCodes::devices[i].title;
        }
        st = st.length() ? st + " loaded" : String("Nothing in /remote");
    }
    UI::irStatus = st;
}

// Applied on press, so the IR goes out the moment the finger lands.
void onPress(const Target& t) {
    IrDevice& d = IrCodes::devices[UI::device];
    switch (t.hit) {
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
    case Hit::Gear:  setScreen(Screen::Settings); return;
    case Hit::Back:  setScreen(Screen::Main); return;
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
        loadIrFiles();
        UI::drawScreen();
        return;
    case Hit::Pairing:
        UI::message("Bluetooth: next update");
        delay(1200);
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
        s_target = UI::hitTest(x, y);
        if (s_target.hit == Hit::None) return;
        UI::drawTarget(s_target, true);
        onPress(s_target);
        return;
    }

    if (pressed && s_down) {                        // held
        activity();
        if (s_swallow || s_target.hit == Hit::None) return;
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
        static uint32_t upSince = 0;
        if (!upSince) upSince = now;
        if (now - upSince < 40) return;
        upSince = 0;
        s_down = false;
        if (s_swallow || s_target.hit == Hit::None) return;
        onRelease(s_target, now - s_downAt);
        s_target = Target();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(80);                // plenty for a remote, far less current
    btStop();                               // radio off until Bluetooth arrives

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
    if (!Store::s.calOk || digitalRead(PIN_BOOT) == LOW) Touch::calibrate();

    Battery::begin();
    IrCodes::txBegin();
    IrCodes::sdBegin();
    loadIrFiles();

    UI::drawScreen();
    s_lastActivity = millis();
}

void loop() {
    touchTick();
    if (Battery::update()) UI::drawStatus();
    batteryGuard();
    powerTick();
    delay(10);
}
