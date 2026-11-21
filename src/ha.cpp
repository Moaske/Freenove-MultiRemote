#include "ha.h"
#include "config.h"
#include "settings.h"
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

namespace Ha {
namespace {

// ---- Config (written by loadConfig() on the main task, under s_mux) ----
bool   s_configured = false;
String s_error = "Not loaded yet";
String s_ssid, s_pass, s_url, s_token, s_title = "Home";
uint8_t s_nSwitch = 0;
bool   s_blinds = false;
Item   s_items[MAX_SWITCHES + 1];

// ---- Live state (written by the task, under s_mux) ----
SemaphoreHandle_t s_mux = nullptr;
Live    s_live[MAX_SWITCHES + 1];
Net     s_net = Net::NoConfig;
String  s_version;
volatile bool    s_testing = false;
volatile int32_t s_testMs = -1;
volatile uint8_t s_changed = 0;

// ---- Requests from the UI ----
volatile bool     s_want = false;
volatile uint32_t s_leftAt = 0;        // when the Home page was closed
volatile bool     s_forceOff = false;
volatile uint32_t s_configGen = 0;     // bumped by loadConfig()

enum class Cmd : uint8_t { Toggle, Cover, Test };
struct Request { Cmd cmd; uint8_t arg; };
QueueHandle_t s_queue = nullptr;

constexpr uint32_t POLL_MS = 3000;
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

struct Lock {
    Lock()  { xSemaphoreTake(s_mux, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_mux); }
};

void flag(uint8_t f) { __atomic_fetch_or(&s_changed, f, __ATOMIC_SEQ_CST); }

void setNet(Net n) {
    bool changed;
    { Lock l; changed = s_net != n; s_net = n; }
    if (changed) flag(CH_NET);
}

void setLive(uint8_t i, const char* state, int8_t pos, Found f) {
    bool changed;
    {
        Lock l;
        Live& v = s_live[i];
        changed = strcmp(v.state, state) != 0 || v.position != pos || v.found != f;
        strlcpy(v.state, state, sizeof v.state);
        v.position = pos;
        v.found = f;
    }
    if (changed) flag(CH_STATE);
}

// ---------------------------------------------------------------------------
// The network task
// ---------------------------------------------------------------------------

WiFiClient        s_plain;
WiFiClientSecure  s_tls;
HTTPClient        s_http;

struct Snapshot {          // the config, copied once per pass
    uint32_t gen = 0;
    bool   ok = false;
    String ssid, pass, base, auth;   // base: ha_url as written
    String url;                       // what we use: .local names resolved
    uint8_t n = 0;         // items in use: switches, then blinds at BLINDS
    String ids[MAX_SWITCHES + 1];
    bool   blinds = false;
} s_cfg;

void snapshot() {
    Lock l;
    s_cfg.gen = s_configGen;
    s_cfg.ok = s_configured;
    s_cfg.ssid = s_ssid;
    s_cfg.pass = s_pass;
    s_cfg.base = s_url;
    s_cfg.url = s_url;
    s_cfg.auth = "Bearer " + s_token;
    s_cfg.n = s_nSwitch;
    s_cfg.blinds = s_blinds;
    for (uint8_t i = 0; i <= MAX_SWITCHES; i++) s_cfg.ids[i] = s_items[i].id;
}

Client& client() {
    if (s_cfg.url.startsWith("https")) {
        s_tls.setInsecure();   // a home server usually has no public certificate
        return s_tls;
    }
    return s_plain;
}

// One request. Returns the HTTP status (<= 0: no answer).
int request(const char* method, const String& path, const String& body, String* reply) {
    s_http.setReuse(true);
    s_http.setTimeout(4000);
    s_http.setConnectTimeout(3000);
    if (!s_http.begin(client(), s_cfg.url + path)) return -1;
    s_http.addHeader("Authorization", s_cfg.auth);
    int code;
    if (strcmp(method, "POST") == 0) {
        s_http.addHeader("Content-Type", "application/json");
        code = s_http.POST(body);
    } else {
        code = s_http.GET();
    }
    if (reply && code > 0) *reply = s_http.getString();
    s_http.end();
    return code;
}

// Classify an HTTP result for the connection state.
bool netFromCode(int code) {
    if (code == 401 || code == 403) { setNet(Net::BadToken); return false; }
    if (code <= 0)                  { setNet(Net::NoHa);     return false; }
    setNet(Net::Online);
    return true;
}

bool checkApi() {
    const uint32_t t0 = millis();
    const int code = request("GET", "/api/", "", nullptr);
    const uint32_t ms = millis() - t0;
    if (!netFromCode(code)) return false;
    String body;
    if (request("GET", "/api/config", "", &body) == 200) {
        JsonDocument filter, doc;
        filter["version"] = true;
        if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
            Lock l;
            s_version = (const char*)(doc["version"] | "");
        }
    }
    s_testMs = (int32_t)ms;
    flag(CH_TEST);
    return true;
}

void readState(uint8_t i) {
    if (s_cfg.ids[i].isEmpty()) return;
    String body;
    const int code = request("GET", "/api/states/" + s_cfg.ids[i], "", &body);
    if (code == 404) { setLive(i, "", -1, Found::Missing); return; }
    if (!netFromCode(code) || code != 200) return;
    JsonDocument filter, doc;
    filter["state"] = true;
    filter["attributes"]["current_position"] = true;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;
    const char* st = doc["state"] | "unknown";
    const int pos = doc["attributes"]["current_position"] | -1;
    setLive(i, st, (int8_t)constrain(pos, -1, 100), Found::Ok);
}

void readAll() {
    for (uint8_t i = 0; i < s_cfg.n; i++) readState(i);
    if (s_cfg.blinds) readState(BLINDS);
}

void callService(const char* service, const String& entity) {
    JsonDocument doc;
    doc["entity_id"] = entity;
    String body;
    serializeJson(doc, body);
    const int code = request("POST", String("/api/services/") + service, body, nullptr);
    netFromCode(code);
    Serial.printf("[ha] %s %s -> %d\n", service, entity.c_str(), code);
}

void handle(const Request& r) {
    switch (r.cmd) {
    case Cmd::Toggle: {
        if (r.arg >= s_cfg.n) return;
        const Live v = live(r.arg);
        const bool on = strcmp(v.state, "on") == 0;
        // Show the new state at once; the next read confirms it.
        setLive(r.arg, on ? "off" : "on", -1, v.found == Found::Missing ? Found::Missing : Found::Ok);
        callService(on ? "homeassistant/turn_off" : "homeassistant/turn_on", s_cfg.ids[r.arg]);
        delay(300);
        readState(r.arg);
        break;
    }
    case Cmd::Cover: {
        if (!s_cfg.blinds) return;
        const char* svc = r.arg == (uint8_t)Cover::Open ? "cover/open_cover"
                        : r.arg == (uint8_t)Cover::Close ? "cover/close_cover" : "cover/stop_cover";
        callService(svc, s_cfg.ids[BLINDS]);
        delay(300);
        readState(BLINDS);
        break;
    }
    case Cmd::Test:
        s_testing = true;
        flag(CH_TEST);
        if (!checkApi()) s_testMs = -2;
        s_testing = false;
        flag(CH_TEST);
        break;
    }
}

bool wifiOn = false;
bool mdnsOn = false;

// "http://homeassistant.local:8123" -> "http://192.168.1.10:8123". The ESP32's
// normal name lookup can't do .local names; mDNS can.
void resolveLocal() {
    s_cfg.url = s_cfg.base;
    const int scheme = s_cfg.base.indexOf("://");
    const int hs = scheme < 0 ? 0 : scheme + 3;
    int he = hs;
    while (he < (int)s_cfg.base.length() && s_cfg.base[he] != ':' && s_cfg.base[he] != '/') he++;
    const String host = s_cfg.base.substring(hs, he);
    if (!host.endsWith(".local")) return;
    if (!mdnsOn) mdnsOn = MDNS.begin("multiremote");
    const IPAddress addr = MDNS.queryHost(host.substring(0, host.length() - 6), 3000);
    if (addr == IPAddress(0, 0, 0, 0)) {
        Serial.printf("[ha] %s: not found via mDNS\n", host.c_str());
        return;
    }
    s_cfg.url = s_cfg.base.substring(0, hs) + addr.toString() + s_cfg.base.substring(he);
    Serial.printf("[ha] %s is %s\n", host.c_str(), addr.toString().c_str());
}

void wifiStop() {
    if (!wifiOn) return;
    s_http.end();
    if (mdnsOn) { MDNS.end(); mdnsOn = false; }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOn = false;
    Serial.println("[ha] Wi-Fi off");
}

bool wifiJoin() {
    if (!wifiOn) {
        WiFi.persistent(false);       // don't write the network to flash
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(true);          // required alongside Bluetooth
        WiFi.begin(s_cfg.ssid.c_str(), s_cfg.pass.c_str());
        wifiOn = true;
        Serial.printf("[ha] joining %s\n", s_cfg.ssid.c_str());
    }
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (s_forceOff || millis() - t0 > WIFI_TIMEOUT_MS) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    Serial.printf("[ha] Wi-Fi up, %s, %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

bool wanted() {
    if (s_forceOff) return false;
    if (s_want) return true;
    return millis() - s_leftAt < Store::wifiOffAfterMs();
}

void task(void*) {
    uint32_t lastRead = 0;
    bool fresh = true;   // just (re)connected: check HA and read everything
    for (;;) {
        if (s_cfg.gen != s_configGen) { snapshot(); fresh = true; }

        if (!s_cfg.ok || !wanted()) {
            wifiStop();
            setNet(s_cfg.ok ? Net::Off : Net::NoConfig);
            fresh = true;
            // Drop requests made while offline.
            Request r;
            while (xQueueReceive(s_queue, &r, 0) == pdTRUE) {}
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        if (WiFi.status() != WL_CONNECTED) {
            setNet(Net::Connecting);
            if (!wifiJoin()) {
                if (!s_forceOff) setNet(Net::NoWifi);
                wifiStop();
                // Try again in a while if the page stays open.
                for (int i = 0; i < 50 && wanted(); i++) vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            fresh = true;
        }

        if (fresh) {
            fresh = false;
            resolveLocal();
            if (checkApi()) readAll();
            lastRead = millis();
        }

        Request r;
        if (xQueueReceive(s_queue, &r, pdMS_TO_TICKS(50)) == pdTRUE) {
            handle(r);
            lastRead = millis();
        } else if (s_want && millis() - lastRead >= POLL_MS) {
            readAll();   // follow changes made elsewhere (the HA app, a wall switch)
            lastRead = millis();
        }
    }
}

// ---------------------------------------------------------------------------
// ha.cfg
// ---------------------------------------------------------------------------

bool loadIcon(Item& it) {
    it.hasIcon = false;
    if (it.icon.isEmpty()) return false;
    File f = SD.open(String(IR_DIR) + "/icons/" + it.icon + ".a8", FILE_READ);
    if (!f) return false;
    uint8_t hdr[4];
    const bool ok = f.read(hdr, 4) == 4 && hdr[0] == 'A' && hdr[1] == '8' &&
                    hdr[2] == ICON_PX && hdr[3] == ICON_PX &&
                    f.read(it.alpha, sizeof it.alpha) == sizeof it.alpha;
    f.close();
    it.hasIcon = ok;
    return ok;
}

// "light.ceiling, Ceiling light, ceiling-light" -> parts
void splitItem(const String& v, Item& it, const char* defIcon) {
    String part[3];
    int start = 0;
    for (uint8_t k = 0; k < 3; k++) {
        int comma = k < 2 ? v.indexOf(',', start) : -1;
        part[k] = comma < 0 ? v.substring(start) : v.substring(start, comma);
        part[k].trim();
        if (comma < 0) break;
        start = comma + 1;
    }
    it.id = part[0];
    it.label = part[1].length() ? part[1] : part[0];
    it.icon = part[2].length() ? part[2] : String(defIcon);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void begin() {
    if (s_mux) return;
    s_mux = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(8, sizeof(Request));
    // Core 1 with the UI, below it in priority; 8 KB for HTTP + JSON.
    xTaskCreatePinnedToCore(task, "ha", 8192, nullptr, 1, nullptr, 1);
}

void loadConfig() {
    String ssid, pass, url, token, title = "Home", error;
    static Item items[MAX_SWITCHES + 1];   // static: ~5.5 KB of icons, too big for the stack
    for (auto& it : items) it = Item();
    uint8_t n = 0;
    bool blinds = false;
    bool ok = false;

    File f = SD.open(String(IR_DIR) + "/ha.cfg", FILE_READ);
    if (!f) {
        error = "No /remote/ha.cfg on the SD card";
    } else {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.isEmpty() || line[0] == '#') continue;
            const int eq = line.indexOf('=');
            if (eq < 0) continue;
            String key = line.substring(0, eq), val = line.substring(eq + 1);
            key.trim(); key.toLowerCase();
            val.trim();
            if (key == "wifi_ssid") ssid = val;
            else if (key == "wifi_password") pass = val;
            else if (key == "ha_url") url = val;
            else if (key == "ha_token") token = val;
            else if (key == "title") title = val;
            else if (key == "switch" && n < MAX_SWITCHES) splitItem(val, items[n++], "lightbulb");
            else if (key == "blinds") { splitItem(val, items[BLINDS], "window-shutter"); blinds = true; }
        }
        f.close();
        while (url.endsWith("/")) url.remove(url.length() - 1);
        if (ssid.isEmpty())       error = "ha.cfg: wifi_ssid missing";
        else if (url.isEmpty())   error = "ha.cfg: ha_url missing";
        else if (token.isEmpty()) error = "ha.cfg: ha_token missing";
        else if (!n && !blinds)   error = "ha.cfg: no switch or blinds";
        else ok = true;
    }
    for (uint8_t i = 0; i < n; i++) loadIcon(items[i]);

    {
        Lock l;
        s_configured = ok;
        s_error = error;
        s_ssid = ssid; s_pass = pass; s_url = url; s_token = token;
        s_title = title.length() ? title : String("Home");
        s_nSwitch = n;
        s_blinds = blinds;
        for (uint8_t i = 0; i <= MAX_SWITCHES; i++) {
            s_items[i] = items[i];
            s_live[i] = Live();
        }
        s_configGen++;
    }
    flag(CH_NET | CH_STATE);
    Serial.printf("[ha] config: %s, %u switches%s\n", ok ? "ok" : error.c_str(), n, blinds ? ", blinds" : "");
}

bool configured()            { return s_configured; }
const String& configError()  { return s_error; }
const String& title()        { return s_title; }
const String& ssid()         { return s_ssid; }
const String& url()          { return s_url; }
uint8_t switchCount()        { return s_nSwitch; }
bool hasBlinds()             { return s_blinds; }
const Item& item(uint8_t i)  { return s_items[min<uint8_t>(i, MAX_SWITCHES)]; }

Live live(uint8_t i) {
    Lock l;
    return s_live[min<uint8_t>(i, MAX_SWITCHES)];
}

Net net() {
    Lock l;
    return s_net;
}

const char* netText() {
    switch (net()) {
    case Net::Off:        return "Wi-Fi off";
    case Net::NoConfig:   return "Not set up";
    case Net::Connecting: return "Connecting...";
    case Net::NoWifi:     return "Wi-Fi not found";
    case Net::Online:     return "Connected";
    case Net::NoHa:       return "HA not reachable";
    case Net::BadToken:   return "Token refused";
    }
    return "";
}

String version() {
    Lock l;
    return s_version;
}

int8_t rssi()          { return net() == Net::Off || WiFi.status() != WL_CONNECTED ? 0 : WiFi.RSSI(); }
String ip()            { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-"); }
bool testing()         { return s_testing; }
int32_t testResultMs() { return s_testMs; }

void setWanted(bool on) {
    if (s_want && !on) s_leftAt = millis();
    if (on) s_forceOff = false;
    s_want = on;
}

void forceOff() {
    s_want = false;
    s_forceOff = true;
}

void toggle(uint8_t i) {
    Request r{ Cmd::Toggle, i };
    xQueueSend(s_queue, &r, 0);
}

void cover(Cover c) {
    Request r{ Cmd::Cover, (uint8_t)c };
    xQueueSend(s_queue, &r, 0);
}

void test() {
    s_testMs = -1;
    Request r{ Cmd::Test, 0 };
    xQueueSend(s_queue, &r, 0);
}

uint8_t poll() {
    return __atomic_exchange_n(&s_changed, (uint8_t)0, __ATOMIC_SEQ_CST);
}

}  // namespace Ha
