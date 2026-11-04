// Bluetooth LE to the Windows PC.
//
//  - A HID "consumer control" device: Play/Pause, Next and Previous arrive
//    in Windows as media keys, for whatever player is active. No app needed.
//  - The battery level, which Windows shows under Bluetooth & devices.
//  - A service of our own that the tray app (tray/) writes to: the track's
//    title, artist and album, playing/paused, and a 60x60 cover.
//
// Protocol (both characteristics write-only, encrypted, so paired PCs only):
//   META  "<state>\x1f<title>\x1f<artist>\x1f<album>\x1f<hasArt>"
//         state: 'P' playing, 'p' paused, 'S' nothing playing; hasArt '1'/'0'
//         Text is plain ASCII (the tray app turns e.g. "é" into "e").
//   ART   [offset lo][offset hi][data...] -- chunks of a 60x60 RGB565 image,
//         little-endian pixels, 7200 bytes in all, sent in order from 0.
#pragma once
#include <Arduino.h>

namespace Ble {

constexpr int16_t ART_W = 60, ART_H = 60;

enum class Play : uint8_t { Unknown, Playing, Paused, Stopped };

struct NowPlaying {
    Play   state = Play::Unknown;
    String title, artist, album;
    bool   hasArt = false;
};

enum class Key : uint16_t { PlayPause = 0xCD, Next = 0xB5, Prev = 0xB6 };

// What poll() reports as changed.
constexpr uint8_t CH_LINK = 1, CH_META = 2, CH_ART = 4;

void begin(uint8_t batteryPct);
uint8_t poll();                 // call from loop(); CH_* flags

bool connected();
bool paired();                  // a PC is bonded
bool appSeen();                 // the tray app has sent something this connection
void forget();                  // drop the bond (and the connection)

bool sendKey(Key k);
void setBattery(uint8_t pct);

const NowPlaying& now();
bool artValid();
const uint16_t* art();          // ART_W x ART_H, RGB565

}  // namespace Ble
