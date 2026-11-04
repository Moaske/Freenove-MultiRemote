#include "ui.h"
#include "config.h"
#include "theme.h"
#include "icons.h"
#include "ir_codes.h"
#include "battery.h"
#include "settings.h"
#include "ble_link.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace UI {

Screen  screen = Screen::Main;
uint8_t device = DEV_AMP;
String  irStatus;

namespace {

const GFXfont* FONT      = &FreeSans9pt7b;
const GFXfont* FONT_BOLD = &FreeSansBold9pt7b;

// ---- Settings screen geometry ----
constexpr int16_t SET_HDR_Y = 4,   SET_HDR_H = 28;
constexpr int16_t SET_BT_Y  = 36,  SET_IR_Y = 84, SET_THEME_Y = 132, SET_ROW_H = 44;
constexpr int16_t SET_PWR_LABEL_Y = 180;
constexpr int16_t SET_PWR_Y = 199, SET_PWR_H = 34;

const char* THEME_NAMES[3] = { "Dark", "Light", "Amber" };
const char* PWR_LABELS[3]  = { "Brightness", "Dim after", "Sleep after" };
const char* PWR_OPTS[3][3] = {
    { "Low", "Med", "High" },
    { "5 s", "15 s", "30 s" },
    { "1 min", "2 min", "5 min" },
};

// Text that fits: cut with "..." when wider than maxW.
void textFit(String s, int16_t x, int16_t y, int16_t maxW) {
    if (tft.textWidth(s) > maxW) {
        while (s.length() > 1 && tft.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
        s += "...";
    }
    tft.drawString(s, x, y);
}

void button(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t border) {
    tft.fillRoundRect(x, y, w, h, 7, fill);
    if (border != fill) tft.drawRoundRect(x, y, w, h, 7, border);
}

// ---- Remote screen ----

constexpr int16_t ART_X = 6, ART_Y = NOW_Y + 4;

void drawArt() {
    const Palette& p = palette();
    if (Ble::connected() && Ble::artValid()) {
        tft.setSwapBytes(true);   // the pixels are RGB565 values, low byte first
        tft.pushImage(ART_X, ART_Y, Ble::ART_W, Ble::ART_H, Ble::art());
        tft.setSwapBytes(false);
    } else {
        tft.fillRect(ART_X, ART_Y, Ble::ART_W, Ble::ART_H, p.bg);
        tft.fillRoundRect(ART_X, ART_Y, Ble::ART_W, Ble::ART_H, 6, p.raised);
        Icons::note(ART_X + 30, ART_Y + 30, p.textFaint);
    }
}

// Three lines next to the cover: bold, dim, faint.
void nowLines(const String& a, const String& b, const String& c) {
    const Palette& p = palette();
    tft.fillRect(72, NOW_Y, SCREEN_W - 72, NOW_H, p.bg);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextColor(p.text, p.bg);
    textFit(a, 74, NOW_Y + 10, 160);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    textFit(b, 74, NOW_Y + 32, 160);
    tft.setTextColor(p.textFaint, p.bg);
    textFit(c, 74, NOW_Y + 48, 160);
}

void drawNowText() {
    const Ble::NowPlaying& np = Ble::now();
    if (!Ble::paired() && !Ble::connected()) {
        nowLines("No PC paired", "Pair in Settings >", "Bluetooth pairing");
    } else if (!Ble::connected()) {
        nowLines("PC not connected", "Waiting for the PC...", "Is its Bluetooth on?");
    } else if (!Ble::appSeen()) {
        nowLines("Connected to PC", "Media keys work. Start", "the tray app for tracks");
    } else if (np.state == Ble::Play::Stopped || np.title.isEmpty()) {
        nowLines("Nothing playing", "", "");
    } else {
        nowLines(np.title, np.artist, np.album);
    }
}

void drawNowPlaying() {
    drawArt();
    drawNowText();
}

void drawMedia(uint8_t i, bool pressed) {
    const Palette& p = palette();
    const int16_t x = colX(i);
    const bool on = Ble::connected();
    const uint16_t fill = pressed ? p.border : (i == 1 && on ? p.btBg : p.raised);
    button(x, MEDIA_Y, COL_W, ROW_H, fill, fill);
    Icon ic = i == 0 ? Icon::Prev : Icon::Next;
    if (i == 1) {
        // Follow the PC when the tray app tells us what it is doing.
        const Ble::Play st = Ble::now().state;
        ic = st == Ble::Play::Playing ? Icon::Pause
           : (st == Ble::Play::Paused || st == Ble::Play::Stopped) ? Icon::Play
           : Icon::PlayPause;
    }
    const uint16_t fg = !on ? p.textFaint : i == 1 ? p.btIcon : p.bt;
    Icons::draw(ic, x + COL_W / 2, MEDIA_Y + ROW_H / 2, fg, fill, p.rec);
}

// ---- Bluetooth screen ----

constexpr int16_t BT_STATUS_Y = 36, BT_FORGET_Y = 268, BT_FORGET_H = 44;

void drawPairing() {
    const Palette& p = palette();
    tft.fillScreen(p.bg);
    tft.fillRoundRect(PAD, SET_HDR_Y, 40, SET_HDR_H, 6, p.raised);
    Icons::back(PAD + 20, SET_HDR_Y + SET_HDR_H / 2, p.text);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.text, p.bg);
    tft.drawString("Bluetooth", 54, SET_HDR_Y + SET_HDR_H / 2);

    // Status
    const bool c = Ble::connected(), b = Ble::paired();
    tft.fillRoundRect(PAD, BT_STATUS_Y, SCREEN_W - 2 * PAD, 44, 7, p.surface);
    Icons::bluetooth(18, BT_STATUS_Y + 22, c ? p.bt : p.textFaint);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(p.text, p.surface);
    tft.drawString(c ? "Connected" : b ? "Paired, not connected" : "Not paired", 32, BT_STATUS_Y + 6);
    tft.setTextFont(2);
    tft.setTextColor(p.textFaint, p.surface);
    tft.drawString(c ? (Ble::appSeen() ? "Tray app running" : "Tray app not running")
                     : "Visible as \"MultiRemote\"", 32, BT_STATUS_Y + 25);

    // How to pair
    const char* lines[] = {
        "To pair with your Windows PC:",
        "1. Settings > Bluetooth & devices",
        "2. Add device > Bluetooth",
        "3. Choose \"MultiRemote\"",
        "",
        "Media keys work straight away.",
        "Run the tray app on the PC",
        "for track info and covers.",
    };
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    int16_t y = 92;
    for (const char* l : lines) {
        tft.setTextColor(l == lines[0] ? p.text : p.textDim, p.bg);
        tft.drawString(l, 10, y);
        y += 20;
    }

    // Forget
    tft.fillRoundRect(PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H, 7, b ? p.surface : p.bg);
    tft.drawRoundRect(PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H, 7, p.border);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(b ? p.rec : p.border, b ? p.surface : p.bg);
    tft.drawString("Forget paired PC", SCREEN_W / 2, BT_FORGET_Y + BT_FORGET_H / 2);
}

void drawVol(uint8_t i, bool pressed) {
    const Palette& p = palette();
    const int16_t x = colX(i);
    const int8_t sig = i == 0 ? IrCodes::volDown : i == 1 ? IrCodes::volUp : IrCodes::mute;
    const bool ok = sig >= 0;
    const uint16_t fill = !ok ? p.raised : pressed ? p.tabOnBg : p.ir;
    const uint16_t fg   = !ok ? p.textFaint : pressed ? p.ir : p.irText;
    button(x, VOL_Y, COL_W, ROW_H, fill, fill);
    const Icon ic = i == 0 ? Icon::VolDown : i == 1 ? Icon::VolUp : Icon::Mute;
    Icons::draw(ic, x + COL_W / 2, VOL_Y + ROW_H / 2, fg, fill, p.rec);
}

void drawTab(uint8_t i) {
    const Palette& p = palette();
    const IrDevice& d = IrCodes::devices[i];
    const int16_t x = colX(i);
    const bool on = i == device;
    const uint16_t fill = on ? p.tabOnBg : p.tabOffBg;
    tft.fillRoundRect(x, TABS_Y, COL_W, TABS_H, 5, fill);
    if (on) tft.fillRect(x + 4, TABS_Y + TABS_H - 2, COL_W - 8, 2, p.ir);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? p.tabOnText : (d.loaded ? p.tabOffText : p.border), fill);
    tft.drawString(d.title, x + COL_W / 2, TABS_Y + TABS_H / 2);
}

void slotRect(uint8_t s, int16_t& x, int16_t& y) {
    x = colX(s % 3);
    y = GRID_Y + (s / 3) * (ROW_H + GAP);
}

// ---- Settings screen ----

void segments(int16_t x, int16_t y, int16_t w, int16_t h, const char* const* opts, uint8_t sel) {
    const Palette& p = palette();
    const int16_t g = 3, sw = (w - 2 * g) / 3;
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < 3; i++) {
        const int16_t sx = x + i * (sw + g);
        const bool on = i == sel;
        const uint16_t fill = on ? p.segOnBg : p.raised;
        tft.fillRoundRect(sx, y, sw, h, 5, fill);
        if (on) tft.drawRoundRect(sx, y, sw, h, 5, p.ir);
        tft.setTextColor(on ? p.segOnText : p.textDim, fill);
        tft.drawString(opts[i], sx + sw / 2, y + h / 2);
    }
}

void settingsRow(int16_t y, bool bt, const char* title, const String& sub) {
    const Palette& p = palette();
    tft.fillRoundRect(PAD, y, SCREEN_W - 2 * PAD, SET_ROW_H, 7, p.surface);
    if (bt) Icons::bluetooth(18, y + SET_ROW_H / 2, Ble::connected() ? p.bt : p.textFaint);
    else    Icons::file(18, y + SET_ROW_H / 2, p.ir, p.surface);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextColor(p.text, p.surface);
    tft.drawString(title, 32, y + 6);
    tft.setTextFont(2);
    tft.setTextColor(p.textFaint, p.surface);
    textFit(sub, 32, y + 25, 180);
    Icons::chevron(SCREEN_W - 16, y + SET_ROW_H / 2, p.textFaint);
}

void drawThemeRow() {
    const Palette& p = palette();
    tft.fillRoundRect(PAD, SET_THEME_Y, SCREEN_W - 2 * PAD, SET_ROW_H, 7, p.surface);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.text, p.surface);
    tft.drawString("Theme", 12, SET_THEME_Y + SET_ROW_H / 2);
    segments(70, SET_THEME_Y + 7, 162, 30, THEME_NAMES, Store::s.theme);
}

void drawPowerRow(uint8_t r) {
    const Palette& p = palette();
    const int16_t y = SET_PWR_Y + r * (SET_PWR_H + GAP);
    tft.fillRect(0, y, SCREEN_W, SET_PWR_H, p.bg);
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.text, p.bg);
    tft.drawString(PWR_LABELS[r], 8, y + SET_PWR_H / 2);
    const uint8_t sel = r == 0 ? Store::s.brightness : r == 1 ? Store::s.dimAfter : Store::s.sleepAfter;
    segments(78, y + 2, 158, 30, PWR_OPTS[r], sel);
}

void drawSettings() {
    const Palette& p = palette();
    tft.fillScreen(p.bg);
    // Header
    tft.fillRoundRect(PAD, SET_HDR_Y, 40, SET_HDR_H, 6, p.raised);
    Icons::back(PAD + 20, SET_HDR_Y + SET_HDR_H / 2, p.text);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.text, p.bg);
    tft.drawString("Settings", 54, SET_HDR_Y + SET_HDR_H / 2);
    // Which build this is: the commit it was built from.
    tft.setTextFont(1);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(p.textFaint, p.bg);
    tft.drawString("build " FW_VERSION, SCREEN_W - 8, SET_HDR_Y + SET_HDR_H / 2);

    settingsRow(SET_BT_Y, true, "Bluetooth pairing",
                Ble::connected() ? "Connected to PC" : Ble::paired() ? "Paired, PC not connected" : "Not paired yet");
    settingsRow(SET_IR_Y, false, "Load .ir files", irStatus);
    drawThemeRow();

    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.textFaint, p.bg);
    tft.drawString("Power saving", 8, SET_PWR_LABEL_Y + 8);
    drawBatteryInfo();
    for (uint8_t r = 0; r < 3; r++) drawPowerRow(r);
}

bool in(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

}  // namespace

// ---- Public ----------------------------------------------------------------

// Right of the "Power saving" label: what BAT+ measures, and whether that
// counts as the charger's input power.
void drawBatteryInfo() {
    if (screen != Screen::Settings) return;
    const Palette& p = palette();
    tft.fillRect(110, SET_PWR_LABEL_Y, SCREEN_W - 110, 18, p.bg);
    String s;
    if (!Battery::present()) {
        s = "No battery";
    } else {
        const uint16_t mv = Battery::millivolts();
        s = String(mv / 1000) + "." + String((mv % 1000) / 100) + String((mv % 100) / 10) + " V";
        s += Battery::external() ? "  USB power" : "  " + String(Battery::percent()) + "%";
    }
    tft.setTextFont(2);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(p.textFaint, p.bg);
    tft.drawString(s, SCREEN_W - 8, SET_PWR_LABEL_Y + 8);
}

void drawStatus() {
    if (screen != Screen::Main) return;
    const Palette& p = palette();
    tft.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, p.bg);
    Icons::bluetooth(9, STATUS_Y + STATUS_H / 2, Ble::connected() ? p.bt : p.textFaint);

    const int16_t bx = 18, by = STATUS_Y + 5, bw = 182, bh = 14;
    tft.fillRoundRect(bx, by, bw, bh, 4, p.battTrack);
    String txt;
    if (!Battery::present()) {
        txt = "No battery";
    } else {
        const uint8_t pct = Battery::percent();
        const uint16_t c = pct <= 15 ? p.battLow : pct <= 35 ? p.battMid : p.battOk;
        const int16_t fw = max<int16_t>(4, (int16_t)(bw * pct / 100));
        tft.fillRoundRect(bx, by, fw, bh, 4, c);
        txt = String(pct) + "%";
        if (Battery::external()) Icons::bolt(bx + bw / 2 - 20, by + bh / 2, p.battText);
    }
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(p.battText);   // transparent: drawn over bar and track
    tft.drawString(txt, bx + bw / 2, by + bh / 2 + 1);

    tft.fillRoundRect(204, STATUS_Y, 32, STATUS_H, 6, p.raised);
    Icons::gear(220, STATUS_Y + STATUS_H / 2, p.textDim, p.raised);
}

void drawSlot(uint8_t s, bool pressed) {
    const Palette& p = palette();
    const IrDevice& d = IrCodes::devices[device];
    int16_t x, y;
    slotRect(s, x, y);
    tft.fillRect(x, y, COL_W, ROW_H, p.bg);
    const Slot& sl = d.slots[s];
    if (!d.loaded || sl.kind == SlotKind::Empty) return;

    const uint16_t fill = pressed ? p.border : p.surface;
    button(x, y, COL_W, ROW_H, fill, p.border);
    const int16_t cx = x + COL_W / 2, cy = y + ROW_H / 2;
    if (sl.kind == SlotKind::PlayToggle) {
        Icons::draw(d.playing ? Icon::Pause : Icon::Play, cx, cy, p.text, fill, p.rec);
    } else if (sl.icon != Icon::None) {
        Icons::draw(sl.icon, cx, cy, p.text, fill, p.rec);
    } else {
        tft.setFreeFont(FONT_BOLD);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(p.text, fill);
        String s2 = sl.label;
        while (s2.length() > 1 && tft.textWidth(s2) > COL_W - 8) s2.remove(s2.length() - 1);
        tft.drawString(s2, cx, cy);
    }
}

void drawTabsAndPanel() {
    const Palette& p = palette();
    for (uint8_t i = 0; i < DEV_COUNT; i++) drawTab(i);
    tft.fillRect(0, GRID_Y, SCREEN_W, 2 * ROW_H + GAP, p.bg);
    const IrDevice& d = IrCodes::devices[device];
    if (!d.loaded) {
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(p.textFaint, p.bg);
        const String f = String(d.file) + " not found";
        tft.drawString(IrCodes::sdPresent() ? f : String("No SD card"), SCREEN_W / 2, GRID_Y + 32);
        tft.drawString("Put it in /remote on the SD card", SCREEN_W / 2, GRID_Y + 54);
        return;
    }
    for (uint8_t s = 0; s < SLOTS; s++) drawSlot(s, false);
}

void drawScreen() {
    if (screen == Screen::Settings) {
        drawSettings();
        return;
    }
    if (screen == Screen::Pairing) {
        drawPairing();
        return;
    }
    tft.fillScreen(palette().bg);
    drawStatus();
    drawNowPlaying();
    for (uint8_t i = 0; i < 3; i++) drawMedia(i, false);
    for (uint8_t i = 0; i < 3; i++) drawVol(i, false);
    drawTabsAndPanel();
}

void drawTarget(const Target& t, bool pressed) {
    switch (t.hit) {
    case Hit::MediaPrev: drawMedia(0, pressed); break;
    case Hit::MediaPlay: drawMedia(1, pressed); break;
    case Hit::MediaNext: drawMedia(2, pressed); break;
    case Hit::VolDown:   drawVol(0, pressed); break;
    case Hit::VolUp:     drawVol(1, pressed); break;
    case Hit::Mute:      drawVol(2, pressed); break;
    case Hit::Slot:      drawSlot(t.arg, pressed); break;
    case Hit::Theme:     drawThemeRow(); break;
    case Hit::Brightness: drawPowerRow(0); break;
    case Hit::DimAfter:   drawPowerRow(1); break;
    case Hit::SleepAfter: drawPowerRow(2); break;
    default: break;
    }
}

void onBle(uint8_t ch) {
    if (screen == Screen::Main) {
        if (ch & Ble::CH_LINK) {
            drawStatus();
            for (uint8_t i = 0; i < 3; i++) drawMedia(i, false);
        }
        if (ch & Ble::CH_ART) drawArt();
        if (ch & (Ble::CH_LINK | Ble::CH_META)) {
            drawNowText();
            drawMedia(1, false);
        }
    } else if (ch & (Ble::CH_LINK | Ble::CH_META)) {
        drawScreen();   // Settings / Bluetooth show the connection state
    }
}

void message(const char* text) {
    const Palette& p = palette();
    const int16_t y = screen == Screen::Main ? GRID_Y + 30 : screen == Screen::Pairing ? 230 : SET_IR_Y + 8;
    tft.fillRoundRect(20, y, SCREEN_W - 40, 30, 7, p.raised);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(p.text, p.raised);
    tft.drawString(text, SCREEN_W / 2, y + 15);
}

Target hitTest(int16_t x, int16_t y) {
    Target t;
    auto set = [&](Hit h, uint8_t arg, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
        t.hit = h; t.arg = arg; t.x = rx; t.y = ry; t.w = rw; t.h = rh;
    };

    if (screen == Screen::Main) {
        if (in(x, y, 196, 0, 44, STATUS_Y + STATUS_H + 2)) { set(Hit::Gear, 0, 204, STATUS_Y, 32, STATUS_H); return t; }
        for (uint8_t i = 0; i < 3; i++) {
            const int16_t cx = colX(i);
            if (in(x, y, cx, MEDIA_Y, COL_W, ROW_H)) { set((Hit)((uint8_t)Hit::MediaPrev + i), i, cx, MEDIA_Y, COL_W, ROW_H); return t; }
            if (in(x, y, cx, VOL_Y, COL_W, ROW_H)) {
                const Hit h = i == 0 ? Hit::VolDown : i == 1 ? Hit::VolUp : Hit::Mute;
                set(h, i, cx, VOL_Y, COL_W, ROW_H);
                return t;
            }
            if (in(x, y, cx, TABS_Y, COL_W, TABS_H)) { set(Hit::Tab, i, cx, TABS_Y, COL_W, TABS_H); return t; }
        }
        for (uint8_t s = 0; s < SLOTS; s++) {
            int16_t sx, sy;
            slotRect(s, sx, sy);
            if (in(x, y, sx, sy, COL_W, ROW_H)) { set(Hit::Slot, s, sx, sy, COL_W, ROW_H); return t; }
        }
        return t;
    }

    if (screen == Screen::Pairing) {
        if (in(x, y, 0, 0, 60, SET_HDR_Y + SET_HDR_H + 2)) { set(Hit::Back, 0, PAD, SET_HDR_Y, 40, SET_HDR_H); return t; }
        if (in(x, y, PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H)) { set(Hit::Forget, 0, PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H); return t; }
        return t;
    }

    // Settings
    if (in(x, y, 0, 0, 60, SET_HDR_Y + SET_HDR_H + 2)) { set(Hit::Back, 0, PAD, SET_HDR_Y, 40, SET_HDR_H); return t; }
    if (in(x, y, PAD, SET_BT_Y, SCREEN_W - 2 * PAD, SET_ROW_H)) { set(Hit::Pairing, 0, PAD, SET_BT_Y, SCREEN_W - 2 * PAD, SET_ROW_H); return t; }
    if (in(x, y, PAD, SET_IR_Y, SCREEN_W - 2 * PAD, SET_ROW_H)) { set(Hit::LoadIr, 0, PAD, SET_IR_Y, SCREEN_W - 2 * PAD, SET_ROW_H); return t; }
    // Segmented controls: which third of the control.
    auto seg = [&](Hit h, int16_t sx, int16_t sy, int16_t sw, int16_t sh) {
        if (!in(x, y, sx, sy, sw, sh)) return false;
        set(h, (uint8_t)min(2, (x - sx) * 3 / sw), sx, sy, sw, sh);
        return true;
    };
    if (seg(Hit::Theme, 70, SET_THEME_Y, 166, SET_ROW_H)) return t;
    for (uint8_t r = 0; r < 3; r++) {
        const int16_t ry = SET_PWR_Y + r * (SET_PWR_H + GAP);
        if (seg((Hit)((uint8_t)Hit::Brightness + r), 78, ry, 158, SET_PWR_H)) return t;
    }
    return t;
}

}  // namespace UI
