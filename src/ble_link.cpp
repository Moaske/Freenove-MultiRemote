#include "ble_link.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace Ble {
namespace {

// Our own service, used by the tray app.
const char* SVC_UUID  = "6b3f0001-8c2e-4a5e-9d1b-3c7e2f4a9b10";
const char* META_UUID = "6b3f0002-8c2e-4a5e-9d1b-3c7e2f4a9b10";
const char* ART_UUID  = "6b3f0003-8c2e-4a5e-9d1b-3c7e2f4a9b10";

const char* NAME = "MultiRemote";

// HID report map: one 16-bit consumer-control usage, report ID 1.
uint8_t REPORT_MAP[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0,              // End Collection
};

constexpr size_t ART_BYTES = ART_W * ART_H * 2;

NimBLEServer*         s_server = nullptr;
NimBLEHIDDevice*      s_hid = nullptr;
NimBLECharacteristic* s_input = nullptr;

// Written by the NimBLE task, read by loop().
volatile bool s_conn = false;
volatile bool s_linkChanged = false;
portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
char          s_metaRx[512];
size_t        s_metaLen = 0;
volatile bool s_metaPending = false;
uint16_t      s_artRx[ART_W * ART_H];
uint16_t      s_artShow[ART_W * ART_H];
size_t        s_artGot = 0;
volatile bool s_artPending = false;

// loop()'s view.
NowPlaying s_now;
bool       s_artValid = false;
bool       s_appSeen = false;

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
        s_conn = true;
        s_linkChanged = true;
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        s_conn = false;
        s_linkChanged = true;
        Serial.printf("[ble] disconnected (%d)\n", reason);
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        Serial.printf("[ble] paired: %s\n", info.isEncrypted() ? "encrypted" : "NOT encrypted");
        s_linkChanged = true;
    }
};

class MetaCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        NimBLEAttValue v = c->getValue();
        const size_t n = min((size_t)v.size(), sizeof s_metaRx);
        portENTER_CRITICAL(&s_mux);
        memcpy(s_metaRx, v.data(), n);
        s_metaLen = n;
        s_metaPending = true;
        portEXIT_CRITICAL(&s_mux);
    }
};

class ArtCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        NimBLEAttValue v = c->getValue();
        if (v.size() < 2) return;
        const uint8_t* d = v.data();
        const size_t off = d[0] | (d[1] << 8);
        const size_t n = v.size() - 2;
        if (off == 0) s_artGot = 0;
        if (off != s_artGot || off + n > ART_BYTES) {   // out of order: wait for a new image
            s_artGot = SIZE_MAX;
            return;
        }
        memcpy(reinterpret_cast<uint8_t*>(s_artRx) + off, d + 2, n);
        s_artGot = off + n;
        if (s_artGot == ART_BYTES) {
            memcpy(s_artShow, s_artRx, ART_BYTES);
            s_artPending = true;
        }
    }
};

ServerCb s_serverCb;
MetaCb   s_metaCb;
ArtCb    s_artCb;

// Plain printable ASCII: anything else would show as a gap in the font.
String clean(const char* p, size_t n) {
    String s;
    s.reserve(n);
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = (uint8_t)p[i];
        s += (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    s.trim();
    return s;
}

void parseMeta(const char* buf, size_t n) {
    const char* f[5] = { nullptr };
    size_t len[5] = { 0 };
    uint8_t k = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n && k < 5; i++) {
        if (i == n || buf[i] == 0x1F) {
            f[k] = buf + start;
            len[k] = i - start;
            k++;
            start = i + 1;
        }
    }
    NowPlaying np;
    const char st = (k > 0 && len[0]) ? f[0][0] : '?';
    np.state = st == 'P' ? Play::Playing : st == 'p' ? Play::Paused : st == 'S' ? Play::Stopped : Play::Unknown;
    if (k > 1) np.title  = clean(f[1], len[1]);
    if (k > 2) np.artist = clean(f[2], len[2]);
    if (k > 3) np.album  = clean(f[3], len[3]);
    np.hasArt = k > 4 && len[4] && f[4][0] == '1';
    s_now = np;
    if (!np.hasArt) s_artValid = false;
}

}  // namespace

void begin(uint8_t batteryPct) {
    NimBLEDevice::init(NAME);
    NimBLEDevice::setSecurityAuth(true, false, true);   // bond, no PIN, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setMTU(247);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_serverCb);
    s_server->advertiseOnDisconnect(true);

    s_hid = new NimBLEHIDDevice(s_server);
    s_input = s_hid->getInputReport(1);
    s_hid->setManufacturer("Moaske");
    s_hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
    s_hid->setHidInfo(0x00, 0x01);
    s_hid->setReportMap(REPORT_MAP, sizeof REPORT_MAP);
    s_hid->setBatteryLevel(batteryPct);

    NimBLEService* svc = s_server->createService(SVC_UUID);
    NimBLECharacteristic* meta = svc->createCharacteristic(
        META_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, 512);
    NimBLECharacteristic* art = svc->createCharacteristic(
        ART_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, 512);
    meta->setCallbacks(&s_metaCb);
    art->setCallbacks(&s_artCb);

    s_server->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C0);   // generic HID
    adv->addServiceUUID(s_hid->getHidService()->getUUID());
    adv->setName(NAME);
    adv->start();
    Serial.printf("[ble] advertising as %s, %d bond(s)\n", NAME, NimBLEDevice::getNumBonds());
}

uint8_t poll() {
    uint8_t ch = 0;
    if (s_linkChanged) {
        s_linkChanged = false;
        ch |= CH_LINK;
        if (!s_conn) {
            s_now = NowPlaying();
            s_appSeen = false;
            s_artValid = false;
            ch |= CH_META | CH_ART;
        }
    }
    if (s_metaPending) {
        char buf[sizeof s_metaRx];
        size_t n;
        portENTER_CRITICAL(&s_mux);
        n = s_metaLen;
        memcpy(buf, s_metaRx, n);
        s_metaPending = false;
        portEXIT_CRITICAL(&s_mux);
        const bool hadArt = s_artValid;
        parseMeta(buf, n);
        s_appSeen = true;
        ch |= CH_META;
        if (hadArt != s_artValid) ch |= CH_ART;
    }
    if (s_artPending) {
        s_artPending = false;
        s_artValid = true;
        ch |= CH_ART;
    }
    return ch;
}

bool connected() { return s_conn; }
bool paired()    { return NimBLEDevice::getNumBonds() > 0; }
bool appSeen()   { return s_appSeen; }

void forget() {
    if (s_conn && s_server->getConnectedCount())
        s_server->disconnect(s_server->getPeerInfo(0).getConnHandle());
    NimBLEDevice::deleteAllBonds();
    Serial.println("[ble] bonds deleted");
}

bool sendKey(Key k) {
    if (!s_conn || !s_input) return false;
    const uint16_t u = (uint16_t)k;
    uint8_t press[2] = { (uint8_t)(u & 0xFF), (uint8_t)(u >> 8) };
    uint8_t release[2] = { 0, 0 };
    s_input->setValue(press, 2);
    s_input->notify();
    delay(15);
    s_input->setValue(release, 2);
    s_input->notify();
    return true;
}

void setBattery(uint8_t pct) {
    if (s_hid) s_hid->setBatteryLevel(pct, s_conn);
}

const NowPlaying& now() { return s_now; }
bool artValid()          { return s_artValid && s_now.hasArt; }
const uint16_t* art()    { return s_artShow; }

}  // namespace Ble
