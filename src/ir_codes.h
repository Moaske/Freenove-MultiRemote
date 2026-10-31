// Flipper Zero .ir files: loading them from the SD card, turning them into
// the buttons the screen shows, and sending a signal on the IR emitter.
#pragma once
#include <Arduino.h>
#include <vector>

enum class IrProto : uint8_t {
    NEC, NECext, Samsung32, SIRC, SIRC15, SIRC20, RC5, RC6, Raw, Unknown
};

struct IrSignal {
    String   name;
    IrProto  proto   = IrProto::Unknown;
    uint32_t address = 0;           // Flipper's 4 bytes, little-endian
    uint32_t command = 0;
    uint32_t freq    = 38000;       // raw signals only
    std::vector<uint16_t> raw;      // raw signals only: mark/space in us
};

// What a button looks like. Anything that is not a known audio function is
// a source select and is shown as its name.
enum class Icon : uint8_t {
    None, Power, Play, Pause, PlayPause, Stop, Prev, Next, Rew, FF, Eject, Rec,
    VolUp, VolDown, Mute
};

enum class SlotKind : uint8_t { Empty, Signal, PlayToggle };

struct Slot {
    SlotKind kind  = SlotKind::Empty;
    int8_t   sig   = -1;        // index into the device's signals
    int8_t   sig2  = -1;        // PlayToggle: the Pause signal (sig = Play)
    Icon     icon  = Icon::None;
    String   label;             // shown when icon == None
};

constexpr uint8_t SLOTS = 6;     // device panel: 3 x 2
constexpr uint8_t READ_FIRST = 8;// only the first 8 signals of a file count

struct IrDevice {
    const char* file;            // e.g. "amp.ir"
    const char* title;           // tab label
    bool loaded = false;
    std::vector<IrSignal> signals;
    Slot slots[SLOTS];
    bool playing = false;        // our guess for the Play/Pause toggle
};

enum DeviceId : uint8_t { DEV_AMP = 0, DEV_CD = 1, DEV_MD = 2, DEV_COUNT = 3 };

namespace IrCodes {
    extern IrDevice devices[DEV_COUNT];
    // The volume row, taken from amp.ir. -1 when the name is not in the file.
    extern int8_t volUp, volDown, mute;

    bool sdBegin();                 // mount the card; false = no card
    void loadAll();                 // (re)read every device file
    bool sdPresent();

    void txBegin();
    // Send one signal; returns false for a protocol we cannot send.
    bool send(const IrSignal& s);
    bool send(DeviceId dev, int8_t sig);

    Icon iconFor(const String& name);
    String prettyLabel(const String& name);
}
