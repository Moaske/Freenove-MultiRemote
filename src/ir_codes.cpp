#include "ir_codes.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>

namespace IrCodes {

IrDevice devices[DEV_COUNT] = {
    { "amp.ir", "Amplifier" },
    { "cd.ir",  "CD" },
    { "md.ir",  "MiniDisc" },
};
int8_t volUp = -1, volDown = -1, mute = -1;

namespace {

IRsend irsend(PIN_IR_TX);
bool   s_sd = false;

// ---- Names -------------------------------------------------------------

// "Vol_up" -> "volup", "Open_close" -> "openclose": letters and digits only.
String norm(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (isalnum((unsigned char)c)) out += (char)tolower((unsigned char)c);
    }
    return out;
}

bool isVolName(const String& n) {
    Icon i = iconFor(n);
    return i == Icon::VolUp || i == Icon::VolDown || i == Icon::Mute;
}

// ---- Parsing -----------------------------------------------------------

IrProto protoFrom(String p) {
    p.trim();
    if (p == "NEC")       return IrProto::NEC;
    if (p == "NECext")    return IrProto::NECext;
    if (p == "Samsung32") return IrProto::Samsung32;
    if (p == "SIRC")      return IrProto::SIRC;
    if (p == "SIRC15")    return IrProto::SIRC15;
    if (p == "SIRC20")    return IrProto::SIRC20;
    if (p == "RC5" || p == "RC5X") return IrProto::RC5;
    if (p == "RC6")       return IrProto::RC6;
    return IrProto::Unknown;
}

// "B8 00 00 00" -> 0x000000B8 (Flipper stores the bytes little-endian).
uint32_t hexBytesLE(const String& s) {
    uint32_t v = 0;
    uint8_t  n = 0;
    int i = 0;
    const int len = s.length();
    while (i < len && n < 4) {
        while (i < len && s[i] == ' ') i++;
        if (i >= len) break;
        uint8_t b = (uint8_t)strtoul(s.substring(i, i + 2).c_str(), nullptr, 16);
        v |= (uint32_t)b << (8 * n++);
        i += 2;
    }
    return v;
}

void appendRaw(std::vector<uint16_t>& raw, const String& s) {
    int i = 0;
    const int len = s.length();
    while (i < len) {
        while (i < len && s[i] == ' ') i++;
        if (i >= len) break;
        int j = i;
        while (j < len && s[j] != ' ') j++;
        long v = s.substring(i, j).toInt();
        if (v > 0) raw.push_back((uint16_t)min(v, 65535L));
        i = j;
    }
}

bool loadDevice(DeviceId id) {
    IrDevice& d = devices[id];
    d.signals.clear();
    d.loaded = false;
    for (auto& sl : d.slots) sl = Slot();
    if (id == DEV_AMP) volUp = volDown = mute = -1;
    if (!s_sd) return false;

    String path = String(IR_DIR) + "/" + d.file;
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[ir] %s: not found\n", path.c_str());
        return false;
    }

    std::vector<uint8_t> pos;     // each kept signal's place in the file
    IrSignal cur;
    bool inBlock = false;
    bool isRaw = false;
    int  index = -1;

    auto commit = [&]() {
        if (!inBlock) return;
        inBlock = false;
        if (isRaw) cur.proto = cur.raw.empty() ? IrProto::Unknown : IrProto::Raw;
        const bool early = index < READ_FIRST;
        const bool vol   = id == DEV_AMP && isVolName(cur.name);
        if (early || vol) {
            d.signals.push_back(cur);
            pos.push_back((uint8_t)min(index, 255));
        }
    };

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line[0] == '#') continue;
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        String key = line.substring(0, colon);
        String val = line.substring(colon + 1);
        key.trim();
        val.trim();

        if (key == "name") {
            commit();
            cur = IrSignal();
            cur.name = val;
            inBlock = true;
            isRaw = false;
            index++;
        } else if (!inBlock) {
            continue;   // Filetype / Version header
        } else if (key == "type") {
            isRaw = (val == "raw");
        } else if (key == "protocol") {
            cur.proto = protoFrom(val);
        } else if (key == "address") {
            cur.address = hexBytesLE(val);
        } else if (key == "command") {
            cur.command = hexBytesLE(val);
        } else if (key == "frequency") {
            cur.freq = (uint32_t)val.toInt();
        } else if (key == "data") {
            appendRaw(cur.raw, val);   // long raw signals span several lines
        }
    }
    commit();
    f.close();

    // ---- The volume row (amp only) ----
    if (id == DEV_AMP) {
        for (size_t i = 0; i < d.signals.size(); i++) {
            Icon ic = iconFor(d.signals[i].name);
            if (ic == Icon::VolUp   && volUp   < 0) volUp   = i;
            if (ic == Icon::VolDown && volDown < 0) volDown = i;
            if (ic == Icon::Mute    && mute    < 0) mute    = i;
        }
    }

    // ---- The panel: first 8 in file order, volume taken out, Play+Pause
    //      merged into one toggle where Play is, then the first 6. ----
    std::vector<int8_t> cand;
    for (size_t i = 0; i < d.signals.size(); i++) {
        if (pos[i] >= READ_FIRST) continue;
        if (id == DEV_AMP && isVolName(d.signals[i].name)) continue;
        cand.push_back((int8_t)i);
    }
    int8_t play = -1, pause = -1;
    for (int8_t i : cand) {
        Icon ic = iconFor(d.signals[i].name);
        if (ic == Icon::Play  && play  < 0) play  = i;
        if (ic == Icon::Pause && pause < 0) pause = i;
    }
    const bool merge = play >= 0 && pause >= 0;

    uint8_t n = 0;
    for (int8_t i : cand) {
        if (n >= SLOTS) break;
        if (merge && i == pause) continue;
        Slot& sl = d.slots[n++];
        sl.sig = i;
        if (merge && i == play) {
            sl.kind = SlotKind::PlayToggle;
            sl.sig2 = pause;
            sl.icon = Icon::Play;
        } else {
            sl.kind  = SlotKind::Signal;
            sl.icon  = iconFor(d.signals[i].name);
            if (sl.icon == Icon::None) sl.label = prettyLabel(d.signals[i].name);
        }
    }

    d.loaded = true;
    Serial.printf("[ir] %s: %u signals kept, %u buttons\n",
                  path.c_str(), (unsigned)d.signals.size(), (unsigned)n);
    return true;
}

// NEC on air is four bytes, each LSB first; sendNEC() wants them MSB first.
uint32_t necFrame(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    return ((uint32_t)reverseBits(b0, 8) << 24) | ((uint32_t)reverseBits(b1, 8) << 16) |
           ((uint32_t)reverseBits(b2, 8) << 8)  |  (uint32_t)reverseBits(b3, 8);
}

}  // namespace

// ---- Public ---------------------------------------------------------------

Icon iconFor(const String& name) {
    const String n = norm(name);
    if (n == "power" || n == "onoff" || n == "standby" || n == "pwr") return Icon::Power;
    if (n == "playpause")                                    return Icon::PlayPause;
    if (n == "play")                                         return Icon::Play;
    if (n == "pause")                                        return Icon::Pause;
    if (n == "stop")                                         return Icon::Stop;
    if (n == "prev" || n == "previous" || n == "skipback")   return Icon::Prev;
    if (n == "next" || n == "skipfwd" || n == "skip")        return Icon::Next;
    if (n == "rew" || n == "rewind" || n == "rw")            return Icon::Rew;
    if (n == "ff" || n == "fwd" || n == "fastforward")       return Icon::FF;
    if (n == "eject" || n == "open" || n == "openclose")     return Icon::Eject;
    if (n == "rec" || n == "record")                         return Icon::Rec;
    if (n == "volup" || n == "volumeup")                     return Icon::VolUp;
    if (n == "voldown" || n == "voldn" || n == "volumedown") return Icon::VolDown;
    if (n == "mute")                                         return Icon::Mute;
    return Icon::None;
}

// "video" -> "Video", "cd" -> "CD", "Tape_mon" -> "Tape mon".
String prettyLabel(const String& name) {
    String s = name;
    s.replace('_', ' ');
    s.trim();
    if (s.length() <= 3) {
        s.toUpperCase();
    } else if (s.length()) {
        s.setCharAt(0, (char)toupper((unsigned char)s[0]));
    }
    return s;
}

bool sdBegin() {
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    s_sd = SD.begin(PIN_SD_CS, SPI, 4000000);
    Serial.printf("[sd] %s\n", s_sd ? "card mounted" : "no card");
    return s_sd;
}

bool sdPresent() { return s_sd; }

void loadAll() {
    if (!s_sd) sdBegin();
    for (uint8_t i = 0; i < DEV_COUNT; i++) loadDevice((DeviceId)i);
}

void txBegin() {
    irsend.begin();
}

bool send(const IrSignal& s) {
    const uint32_t a = s.address, c = s.command;
    switch (s.proto) {
    case IrProto::NEC:
        irsend.sendNEC(necFrame(a & 0xFF, ~a & 0xFF, c & 0xFF, ~c & 0xFF), 32, 0);
        return true;
    case IrProto::NECext:
        // Address and command as captured, byte for byte: encodeNEC() would
        // turn a one-byte address like 0xB8 into B8 47 instead of B8 00.
        irsend.sendNEC(necFrame(a & 0xFF, (a >> 8) & 0xFF, c & 0xFF, (c >> 8) & 0xFF), 32, 0);
        return true;
    case IrProto::Samsung32:
        irsend.sendSAMSUNG(irsend.encodeSAMSUNG(a & 0xFF, c & 0xFF), kSamsungBits, 0);
        return true;
    // Sony devices only respond to a code sent at least three times:
    // kSonyMinRepeat (2) repeats after the first frame.
    case IrProto::SIRC:
        irsend.sendSony(irsend.encodeSony(12, c, a), 12, kSonyMinRepeat);
        return true;
    case IrProto::SIRC15:
        irsend.sendSony(irsend.encodeSony(15, c, a), 15, kSonyMinRepeat);
        return true;
    case IrProto::SIRC20:
        // Flipper's 13-bit address: low 5 bits device, high 8 bits extended.
        irsend.sendSony(irsend.encodeSony(20, c, a & 0x1F, (a >> 5) & 0xFF), 20, kSonyMinRepeat);
        return true;
    case IrProto::RC5:
        if (c < 64) irsend.sendRC5(irsend.encodeRC5(a, c), kRC5Bits);
        else        irsend.sendRC5(irsend.encodeRC5X(a, c), kRC5XBits);
        return true;
    case IrProto::RC6:
        irsend.sendRC6(irsend.encodeRC6(a, c, kRC6Mode0Bits), kRC6Mode0Bits);
        return true;
    case IrProto::Raw:
        irsend.sendRaw(s.raw.data(), s.raw.size(), s.freq ? s.freq : 38000);
        return true;
    default:
        Serial.printf("[ir] %s: protocol not supported\n", s.name.c_str());
        return false;
    }
}

bool send(DeviceId dev, int8_t sig) {
    const IrDevice& d = devices[dev];
    if (!d.loaded || sig < 0 || sig >= (int8_t)d.signals.size()) return false;
    Serial.printf("[ir] %s: %s\n", d.title, d.signals[sig].name.c_str());
    return send(d.signals[sig]);
}

}  // namespace IrCodes
