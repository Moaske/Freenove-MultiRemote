// Pins and fixed numbers for the Freenove 2.8" multi-remote.
#pragma once
#include <Arduino.h>

// Set by the GitHub Actions build to the commit's short hash.
#ifndef FW_VERSION
#define FW_VERSION "local"
#endif

// ---- Pins -------------------------------------------------------------
// IR emitter (Adafruit 5639) IN, on the SPI expansion header.
constexpr uint8_t PIN_IR_TX     = 27;
// Battery: BAT+ through a 100K/100K divider to GPIO34 (see README).
constexpr uint8_t PIN_BATT      = 34;
constexpr uint16_t BATT_X1000   = 2000;   // divider ratio x1000
// The on-board speaker amplifier's enable. Held low: we have no sound.
constexpr uint8_t PIN_AMP_EN    = 4;
// RGB LED, active low. Kept off.
constexpr uint8_t PIN_LED_R     = 22;
constexpr uint8_t PIN_LED_G     = 16;
constexpr uint8_t PIN_LED_B     = 17;
// BOOT button: held at power-on = touch calibration; also wakes from sleep.
constexpr uint8_t PIN_BOOT      = 0;

// SD card (its own pins on this board).
constexpr uint8_t PIN_SD_CS     = 5;
constexpr uint8_t PIN_SD_SCK    = 18;
constexpr uint8_t PIN_SD_MISO   = 19;
constexpr uint8_t PIN_SD_MOSI   = 23;

// XPT2046 resistive touch, on its own SPI bus.
constexpr uint8_t PIN_TOUCH_CS  = 33;
constexpr uint8_t PIN_TOUCH_IRQ = 36;
constexpr uint8_t PIN_TOUCH_SCK = 25;
constexpr uint8_t PIN_TOUCH_MOSI= 32;
constexpr uint8_t PIN_TOUCH_MISO= 39;

// Backlight PWM (core 2.x LEDC).
constexpr uint8_t BL_CHANNEL    = 0;
constexpr uint32_t BL_FREQ      = 5000;
constexpr uint8_t BL_BITS       = 8;

// ---- Screen layout (portrait 240x320, matches the mockup) -------------
constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;
constexpr int16_t PAD = 4;
constexpr int16_t GAP = 4;

constexpr int16_t STATUS_Y = 4,   STATUS_H = 24;
constexpr int16_t NOW_Y    = 32,  NOW_H    = 68;
constexpr int16_t MEDIA_Y  = 104, ROW_H    = 43;
constexpr int16_t VOL_Y    = 151;
constexpr int16_t TABS_Y   = 198, TABS_H   = 24;
constexpr int16_t GRID_Y   = 226;           // two rows of ROW_H, GAP between

constexpr int16_t COL_W    = 74;            // three columns: 4, 82, 160
inline int16_t colX(uint8_t col) { return PAD + col * (COL_W + GAP); }

// ---- SD card layout ---------------------------------------------------
#define IR_DIR "/remote"

// ---- Timing -----------------------------------------------------------
constexpr uint32_t HOLD_REPEAT_DELAY_MS = 400;   // volume: first repeat
constexpr uint32_t HOLD_REPEAT_MS       = 120;   // volume: then every...
constexpr uint32_t LONG_PRESS_MS        = 700;   // play/pause: resync
