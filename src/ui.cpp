#include "ui.h"
#include "config.h"
#include "theme.h"
#include "icons.h"
#include "ir_codes.h"
#include "battery.h"
#include "settings.h"
#include "ble_link.h"
#include "ha.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace UI {

Screen  screen = Screen::Main;
uint8_t device = DEV_AMP;
String  irStatus;
int16_t scrollY = 0;

namespace {

const GFXfont* FONT_BOLD = &FreeSansBold9pt7b;

// ---- Shared geometry ----
constexpr int16_t HDR_Y = 4, HDR_H = 28;               // list screens' header
constexpr int16_t LST_TOP = 36, LST_HEIGHT = SCREEN_H - LST_TOP;
constexpr int16_t LST_LEFT = 4, LST_WIDTH = 226;             // scrollbar at 235
constexpr int16_t ROW_TALL = 44, ROW_SEG = 34, ROW_LABEL = 18;

const char* THEME_NAMES[3] = { "Dark", "Light", "Amber" };
const char* PWR_LABELS[4]  = { "Brightness", "Dim after", "Sleep after", "Wi-Fi off" };
const char* PWR_OPTS[4][3] = {
    { "Low", "Med", "High" },
    { "5 s", "15 s", "30 s" },
    { "1 min", "2 min", "5 min" },
    { "At once", "30 s", "2 min" },
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

bool in(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void header(const char* title, int16_t h) {
    const Palette& p = palette();
    tft.fillRect(0, 0, SCREEN_W, HDR_Y + h + 2, p.bg);
    tft.fillRoundRect(PAD, HDR_Y, 40, h, 6, p.raised);
    Icons::back(PAD + 20, HDR_Y + h / 2, p.text);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.text, p.bg);
    textFit(title, 52, HDR_Y + h / 2, 140);
}

// ===========================================================================
// Remote screen
// ===========================================================================

constexpr int16_t ART_X = 6, ART_Y = NOW_Y + 4;
constexpr int16_t TEXT_X = 66, TEXT_W = 168;
constexpr int16_t HOME_X = 156, GEAR_X = 198, TOP_BTN_W = 38;

void drawArt() {
    const Palette& p = palette();
    if (Ble::connected() && Ble::artValid()) {
        tft.setSwapBytes(true);   // the pixels are RGB565 values, low byte first
        tft.pushImage(ART_X, ART_Y, Ble::ART_W, Ble::ART_H, Ble::art());
        tft.setSwapBytes(false);
    } else {
        tft.fillRect(ART_X, ART_Y, Ble::ART_W, Ble::ART_H, p.bg);
        tft.fillRoundRect(ART_X, ART_Y, Ble::ART_W, Ble::ART_H, 6, p.raised);
        Icons::note(ART_X + Ble::ART_W / 2, ART_Y + Ble::ART_H / 2, p.textFaint);
    }
}

// Three lines next to the cover: bold, dim, faint.
void nowLines(const String& a, const String& b, const String& c) {
    const Palette& p = palette();
    tft.fillRect(TEXT_X - 2, NOW_Y, SCREEN_W - TEXT_X + 2, NOW_H, p.bg);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextColor(p.text, p.bg);
    textFit(a, TEXT_X, NOW_Y + 6, TEXT_W);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    textFit(b, TEXT_X, NOW_Y + 27, TEXT_W);
    tft.setTextColor(p.textFaint, p.bg);
    textFit(c, TEXT_X, NOW_Y + 43, TEXT_W);
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

void drawTopButtons(bool homePressed, bool gearPressed) {
    const Palette& p = palette();
    const uint16_t hf = homePressed ? p.border : p.raised;
    const uint16_t gf = gearPressed ? p.border : p.raised;
    tft.fillRoundRect(HOME_X, STATUS_Y, TOP_BTN_W, STATUS_H, 7, hf);
    Icons::alpha(ICON_HA_LOGO, HOME_X + TOP_BTN_W / 2, STATUS_Y + STATUS_H / 2, p.ha, hf);
    tft.fillRoundRect(GEAR_X, STATUS_Y, TOP_BTN_W, STATUS_H, 7, gf);
    Icons::gear(GEAR_X + TOP_BTN_W / 2, STATUS_Y + STATUS_H / 2, p.textDim, gf);
}

// ===========================================================================
// Bluetooth screen
// ===========================================================================

constexpr int16_t BT_STATUS_Y = 36, BT_FORGET_Y = 268, BT_FORGET_H = 44;

void drawPairing() {
    const Palette& p = palette();
    tft.fillScreen(p.bg);
    header("Bluetooth", HDR_H);

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

    tft.fillRoundRect(PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H, 7, b ? p.surface : p.bg);
    tft.drawRoundRect(PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H, 7, p.border);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(b ? p.rec : p.border, b ? p.surface : p.bg);
    tft.drawString("Forget paired PC", SCREEN_W / 2, BT_FORGET_Y + BT_FORGET_H / 2);
}

// ===========================================================================
// Home page (Home Assistant)
// ===========================================================================

constexpr int16_t HOME_HDR_H = 30;
constexpr int16_t TILE_Y = 38, TILE_W = 114, TILE_H = 68;
constexpr int16_t BLIND_LABEL_Y = 254, BLIND_Y = 272, BLIND_H = 44;

// Inside a tile: icon 28 px at (8, 8); label from where the icon ends,
// starting under the state text.
constexpr int16_t LABEL_X = 40, LABEL_Y = 25, LABEL_W = TILE_W - LABEL_X - 6;

// Split text over two lines of at most maxW pixels in the current font:
// whole words on the first line where possible; the rest goes on the second.
void wrap2(const String& text, int16_t maxW, String& l1, String& l2) {
    l1 = text;
    l2 = "";
    if (tft.textWidth(text) <= maxW) return;
    int cut = -1;
    for (int i = 0; i < (int)text.length(); i++)
        if (text[i] == ' ' && tft.textWidth(text.substring(0, i)) <= maxW) cut = i;
    if (cut < 0) {   // one long word: break it where it no longer fits
        cut = text.length();
        while (cut > 1 && tft.textWidth(text.substring(0, cut)) > maxW) cut--;
        l1 = text.substring(0, cut);
        l2 = text.substring(cut);
    } else {
        l1 = text.substring(0, cut);
        l2 = text.substring(cut + 1);
    }
    l2.trim();
}

int16_t tileX(uint8_t i) { return PAD + (i % 2) * (TILE_W + GAP); }
int16_t tileY(uint8_t i) { return TILE_Y + (i / 2) * (TILE_H + GAP); }

uint16_t netColor(const Palette& p) {
    switch (Ha::net()) {
    case Ha::Net::Online:     return p.okFg;
    case Ha::Net::Off:
    case Ha::Net::Connecting: return p.textFaint;
    default:                  return p.errFg;
    }
}

void drawHomeHeader() {
    const Palette& p = palette();
    header(Ha::title().c_str(), HOME_HDR_H);
    const bool online = Ha::net() == Ha::Net::Online;
    const uint16_t c = netColor(p);
    tft.fillRect(186, HDR_Y, SCREEN_W - 186, HOME_HDR_H, p.bg);
    Icons::alpha(online ? ICON_WIFI : ICON_WIFI_OFF, 202, HDR_Y + HOME_HDR_H / 2, c, p.bg);
    tft.setTextFont(2);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(p.textDim, p.bg);
    tft.drawString("HA", SCREEN_W - 6, HDR_Y + HOME_HDR_H / 2);
}

// "on" -> true; "..." while we don't know.
const char* tileState(const Ha::Live& v, bool& on, bool& known) {
    on = false;
    known = false;
    if (Ha::net() != Ha::Net::Online || v.found == Ha::Found::Unknown) return "...";
    if (v.found == Ha::Found::Missing) return "N/A";
    if (!strcmp(v.state, "unavailable") || !strcmp(v.state, "unknown")) return "N/A";
    known = true;
    on = !strcmp(v.state, "on");
    return on ? "On" : "Off";
}

void drawTile(uint8_t i, bool pressed) {
    const Palette& p = palette();
    const int16_t x = tileX(i), y = tileY(i);
    tft.fillRect(x, y, TILE_W, TILE_H, p.bg);
    if (i >= Ha::switchCount()) return;
    const Ha::Item& it = Ha::item(i);
    const Ha::Live v = Ha::live(i);
    bool on, known;
    const char* st = tileState(v, on, known);

    const uint16_t fill = pressed ? p.border : on ? p.onBg : p.surface;
    button(x, y, TILE_W, TILE_H, fill, on ? p.ir : p.border);
    const uint16_t iconFg = on ? p.onFg : p.textFaint;
    if (it.hasIcon) Icons::alpha(it.alpha, Ha::ICON_PX, Ha::ICON_PX, x + 8, y + 8, iconFg, fill);
    else            Icons::alpha(ICON_FALLBACK.a, ICON_FALLBACK.w, ICON_FALLBACK.h, x + 8, y + 8, iconFg, fill);

    tft.setTextFont(2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(on ? p.onFg : p.textFaint, fill);
    tft.drawString(st, x + TILE_W - 8, y + 7);

    // The label: right of the icon, under the state, in the small font,
    // wrapped over two lines.
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(on ? p.text : (known ? p.textDim : p.textFaint), fill);
    String l1, l2;
    wrap2(it.label, LABEL_W, l1, l2);
    tft.drawString(l1, x + LABEL_X, y + LABEL_Y);
    if (l2.length()) textFit(l2, x + LABEL_X, y + LABEL_Y + 16, LABEL_W);
}

String blindText() {
    const Ha::Live v = Ha::live(Ha::BLINDS);
    if (Ha::net() != Ha::Net::Online || v.found == Ha::Found::Unknown) return "";
    if (v.found == Ha::Found::Missing) return "Not found";
    String s = v.state;
    if (s == "unavailable" || s == "unknown") return "N/A";
    if (s.length()) s.setCharAt(0, (char)toupper((unsigned char)s[0]));
    if (v.position >= 0 && s != "Closed") s += " " + String(v.position) + "%";
    return s;
}

void drawBlindLabel() {
    const Palette& p = palette();
    tft.fillRect(0, BLIND_LABEL_Y, SCREEN_W, BLIND_Y - BLIND_LABEL_Y, p.bg);
    if (!Ha::hasBlinds()) return;
    const Ha::Item& bl = Ha::item(Ha::BLINDS);
    if (bl.hasIcon) {
        // The icon from ha.cfg, 28 px averaged down to 14 to fit the label row.
        constexpr uint8_t H = Ha::ICON_PX / 2;
        uint8_t half[H * H];
        for (uint8_t y = 0; y < H; y++)
            for (uint8_t x = 0; x < H; x++) {
                const uint8_t* a = bl.alpha + (2 * y) * Ha::ICON_PX + 2 * x;
                half[y * H + x] = (a[0] + a[1] + a[Ha::ICON_PX] + a[Ha::ICON_PX + 1]) / 4;
            }
        Icons::alpha(half, H, H, PAD + 2, BLIND_LABEL_Y + 1, p.textDim, p.bg);
    } else {
        Icons::alpha(ICON_SHUTTER, PAD + 9, BLIND_LABEL_Y + 8, p.textDim, p.bg);
    }
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.textDim, p.bg);
    textFit(Ha::item(Ha::BLINDS).label, PAD + 20, BLIND_LABEL_Y + 8, 120);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(p.text, p.bg);
    tft.drawString(blindText(), SCREEN_W - 6, BLIND_LABEL_Y + 8);
}

void drawBlindButton(uint8_t i, bool pressed) {
    const Palette& p = palette();
    const int16_t x = colX(i);
    tft.fillRect(x, BLIND_Y, COL_W, BLIND_H, p.bg);
    if (!Ha::hasBlinds()) return;
    const uint16_t fill = pressed ? p.border : p.surface;
    button(x, BLIND_Y, COL_W, BLIND_H, fill, p.border);
    const AlphaIcon& ic = i == 0 ? ICON_ARROW_DOWN : i == 1 ? ICON_STOP : ICON_ARROW_UP;
    Icons::alpha(ic, x + COL_W / 2, BLIND_Y + BLIND_H / 2, p.text, fill);
}

void drawHomeBody() {
    const Palette& p = palette();
    if (!Ha::configured()) {
        tft.fillRect(0, TILE_Y, SCREEN_W, SCREEN_H - TILE_Y, p.bg);
        tft.setTextDatum(MC_DATUM);
        tft.setFreeFont(FONT_BOLD);
        tft.setTextColor(p.text, p.bg);
        tft.drawString("Not set up yet", SCREEN_W / 2, 120);
        tft.setTextFont(2);
        tft.setTextColor(p.errFg, p.bg);
        tft.drawString(Ha::configError(), SCREEN_W / 2, 150);
        tft.setTextColor(p.textDim, p.bg);
        tft.drawString("Copy ha.cfg.example to", SCREEN_W / 2, 182);
        tft.drawString("/remote/ha.cfg and fill it in", SCREEN_W / 2, 200);
        return;
    }
    for (uint8_t i = 0; i < Ha::MAX_SWITCHES; i++) drawTile(i, false);
    drawBlindLabel();
    for (uint8_t i = 0; i < 3; i++) drawBlindButton(i, false);
}

void drawHome() {
    tft.fillScreen(palette().bg);
    drawHomeHeader();
    drawHomeBody();
}

// ===========================================================================
// Scrolling lists: Settings and Home Assistant status
// ===========================================================================

enum Row : uint8_t {
    R_BT, R_HA, R_SD, R_THEME, R_PWRLABEL, R_PWR,                   // Settings
    R_WIFI, R_HAINFO, R_TEST, R_CFG, R_ENTLABEL, R_ENT,            // HA status
};
struct RowDef { Row kind; uint8_t arg; int16_t h; };

uint8_t buildRows(RowDef* r) {
    uint8_t n = 0;
    if (screen == Screen::Settings) {
        r[n++] = { R_BT, 0, ROW_TALL };
        r[n++] = { R_HA, 0, ROW_TALL };
        r[n++] = { R_SD, 0, ROW_TALL };
        r[n++] = { R_THEME, 0, ROW_TALL };
        r[n++] = { R_PWRLABEL, 0, ROW_LABEL };
        for (uint8_t i = 0; i < 4; i++) r[n++] = { R_PWR, i, ROW_SEG };
    } else if (screen == Screen::HaStatus) {
        r[n++] = { R_WIFI, 0, ROW_TALL };
        r[n++] = { R_HAINFO, 0, ROW_TALL };
        r[n++] = { R_TEST, 0, 40 };
        r[n++] = { R_CFG, 0, ROW_LABEL };
        if (Ha::configured()) {
            r[n++] = { R_ENTLABEL, 0, ROW_LABEL };
            for (uint8_t i = 0; i < Ha::switchCount(); i++) r[n++] = { R_ENT, i, ROW_SEG };
            if (Ha::hasBlinds()) r[n++] = { R_ENT, Ha::BLINDS, ROW_SEG };
        }
    }
    return n;
}

int16_t contentHeight() {
    RowDef rows[16];
    const uint8_t n = buildRows(rows);
    int16_t h = 4;
    for (uint8_t i = 0; i < n; i++) h += rows[i].h + (i ? GAP : 0);
    return h;
}

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

enum class RowIcon : uint8_t { Bt, Ha, File, Wifi };

void card(int16_t y, int16_t h, RowIcon icon, uint16_t iconColor, const String& title,
          const String& sub, uint16_t subColor, bool chevron) {
    const Palette& p = palette();
    tft.fillRoundRect(LST_LEFT, y, LST_WIDTH, h, 7, p.surface);
    const int16_t cy = y + h / 2;
    switch (icon) {
    case RowIcon::Bt:   Icons::bluetooth(LST_LEFT + 14, cy, iconColor); break;
    case RowIcon::Ha:   Icons::alpha(ICON_HA_LOGO, LST_LEFT + 14, cy, iconColor, p.surface); break;
    case RowIcon::File: Icons::file(LST_LEFT + 14, cy, iconColor, p.surface); break;
    case RowIcon::Wifi: Icons::alpha(iconColor == p.okFg ? ICON_WIFI : ICON_WIFI_OFF, LST_LEFT + 14, cy, iconColor, p.surface); break;
    }
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FONT_BOLD);
    tft.setTextColor(p.text, p.surface);
    textFit(title, LST_LEFT + 30, y + 6, chevron ? 166 : 186);
    tft.setTextFont(2);
    tft.setTextColor(subColor, p.surface);
    textFit(sub, LST_LEFT + 30, y + 25, chevron ? 166 : 186);
    if (chevron) Icons::chevron(LST_LEFT + LST_WIDTH - 12, cy, p.textFaint);
}

String batteryText() {
    if (!Battery::present()) return "No battery";
    const uint16_t mv = Battery::millivolts();
    String s = String(mv / 1000) + "." + String((mv % 1000) / 100) + String((mv % 100) / 10) + " V";
    s += Battery::external() ? "  USB power" : "  " + String(Battery::percent()) + "%";
    return s;
}

void labelRow(int16_t y, const String& left, const String& right, uint16_t rightColor) {
    const Palette& p = palette();
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(p.textFaint, p.bg);
    tft.drawString(left, LST_LEFT + 4, y + ROW_LABEL / 2 + 1);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(rightColor, p.bg);
    tft.drawString(right, LST_LEFT + LST_WIDTH - 2, y + ROW_LABEL / 2 + 1);
}

String haRowSub() {
    if (!Ha::configured()) return Ha::configError();
    String s = Ha::ssid() + ", " + String(Ha::switchCount()) + " switch";
    if (Ha::switchCount() != 1) s += "es";
    if (Ha::hasBlinds()) s += ", 1 blind";
    return s;
}

String entityBadge(uint8_t i, uint16_t& fg, uint16_t& bg) {
    const Palette& p = palette();
    const Ha::Live v = Ha::live(i);
    if (Ha::net() != Ha::Net::Online || v.found == Ha::Found::Unknown) {
        fg = p.textFaint; bg = p.raised;
        return "...";
    }
    if (v.found == Ha::Found::Missing) {
        fg = p.errFg; bg = p.errBg;
        return "Not found";
    }
    fg = p.okFg; bg = p.okBg;
    String s = v.state;
    if (s.length()) s.setCharAt(0, (char)toupper((unsigned char)s[0]));
    if (i == Ha::BLINDS && v.position >= 0) s += " " + String(v.position) + "%";
    return s;
}

void drawRow(const RowDef& r, int16_t y) {
    const Palette& p = palette();
    tft.fillRect(0, y, SCREEN_W - 6, r.h, p.bg);
    switch (r.kind) {
    case R_BT:
        card(y, r.h, RowIcon::Bt, Ble::connected() ? p.bt : p.textFaint, "Bluetooth pairing",
             Ble::connected() ? "Connected to PC" : Ble::paired() ? "Paired, PC not connected" : "Not paired yet",
             p.textFaint, true);
        break;
    case R_HA:
        card(y, r.h, RowIcon::Ha, Ha::configured() ? p.ha : p.textFaint, "Home Assistant", haRowSub(),
             Ha::configured() ? p.textFaint : p.errFg, true);
        break;
    case R_SD:
        card(y, r.h, RowIcon::File, p.ir, "Reload SD card", irStatus, p.textFaint, true);
        break;
    case R_THEME:
        tft.fillRoundRect(LST_LEFT, y, LST_WIDTH, r.h, 7, p.surface);
        tft.setFreeFont(FONT_BOLD);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(p.text, p.surface);
        tft.drawString("Theme", LST_LEFT + 8, y + r.h / 2);
        segments(70, y + 7, 156, 30, THEME_NAMES, Store::s.theme);
        break;
    case R_PWRLABEL:
        labelRow(y, "Power saving", batteryText(), p.textFaint);
        break;
    case R_PWR: {
        tft.setTextFont(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(p.text, p.bg);
        tft.drawString(PWR_LABELS[r.arg], LST_LEFT + 4, y + r.h / 2);
        const uint8_t sel = r.arg == 0 ? Store::s.brightness : r.arg == 1 ? Store::s.dimAfter
                          : r.arg == 2 ? Store::s.sleepAfter : Store::s.wifiOff;
        segments(80, y + 2, 148, 30, PWR_OPTS[r.arg], sel);
        break;
    }
    case R_WIFI: {
        const Ha::Net n = Ha::net();
        const bool up = n == Ha::Net::Online || n == Ha::Net::NoHa || n == Ha::Net::BadToken;
        String sub;
        if (up) {
            const int8_t dbm = Ha::rssi();
            sub = String(dbm > -60 ? "Signal good" : dbm > -70 ? "Signal fair" : "Signal weak") +
                  " (" + String(dbm) + " dBm), " + Ha::ip();
        } else {
            sub = n == Ha::Net::Connecting ? "Connecting..." : n == Ha::Net::NoWifi ? "Not found or wrong password" : "Off";
        }
        card(y, r.h, RowIcon::Wifi, up ? p.okFg : n == Ha::Net::NoWifi ? p.errFg : p.textFaint,
             Ha::ssid().length() ? Ha::ssid() : String("Wi-Fi"), sub, up ? p.textFaint : n == Ha::Net::NoWifi ? p.errFg : p.textFaint, false);
        break;
    }
    case R_HAINFO: {
        const Ha::Net n = Ha::net();
        String sub = Ha::netText();
        if (n == Ha::Net::Online && Ha::version().length()) sub += ", HA " + Ha::version();
        card(y, r.h, RowIcon::Ha, p.ha, Ha::url().length() ? Ha::url() : String("No address"), sub, netColor(p), false);
        break;
    }
    case R_TEST: {
        const int32_t ms = Ha::testResultMs();
        String label = Ha::testing() ? "Testing..." : ms >= 0 ? "Answered in " + String(ms) + " ms"
                     : ms == -2 ? "No answer - tap to retry" : "Test connection";
        button(LST_LEFT, y, LST_WIDTH, r.h, p.raised, p.border);
        tft.setFreeFont(FONT_BOLD);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(ms == -2 ? p.errFg : p.text, p.raised);
        tft.drawString(label, LST_LEFT + LST_WIDTH / 2, y + r.h / 2);
        break;
    }
    case R_CFG:
        labelRow(y, "Config file", Ha::configured() ? "/remote/ha.cfg, loaded" : Ha::configError(),
                 Ha::configured() ? p.textFaint : p.errFg);
        break;
    case R_ENTLABEL: {
        uint8_t ok = 0, bad = 0, total = Ha::switchCount() + (Ha::hasBlinds() ? 1 : 0);
        for (uint8_t i = 0; i < Ha::switchCount(); i++) {
            const Ha::Found f = Ha::live(i).found;
            ok += f == Ha::Found::Ok; bad += f == Ha::Found::Missing;
        }
        if (Ha::hasBlinds()) {
            const Ha::Found f = Ha::live(Ha::BLINDS).found;
            ok += f == Ha::Found::Ok; bad += f == Ha::Found::Missing;
        }
        String right = bad ? String(ok) + " OK, " + String(bad) + " not found"
                     : ok == total ? "all " + String(total) + " OK" : String("");
        labelRow(y, "Entities", right, bad ? p.errFg : p.textFaint);
        break;
    }
    case R_ENT: {
        const Ha::Item& it = Ha::item(r.arg);
        tft.fillRoundRect(LST_LEFT, y, LST_WIDTH, r.h, 6, p.surface);
        uint16_t bfg, bbg;
        const String badge = entityBadge(r.arg, bfg, bbg);
        tft.setTextFont(2);
        const int16_t bw = tft.textWidth(badge) + 12;
        tft.fillRoundRect(LST_LEFT + LST_WIDTH - bw - 6, y + 8, bw, 18, 4, bbg);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(bfg, bbg);
        tft.drawString(badge, LST_LEFT + LST_WIDTH - 6 - bw / 2, y + 17);
        const int16_t tw = LST_WIDTH - bw - 22;
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(p.text, p.surface);
        textFit(it.label, LST_LEFT + 8, y + 3, tw);
        tft.setTextFont(1);
        tft.setTextColor(p.textFaint, p.surface);
        textFit(it.id, LST_LEFT + 8, y + 22, tw);
        break;
    }
    }
}

void drawScrollbar() {
    const Palette& p = palette();
    tft.fillRect(SCREEN_W - 6, LST_TOP, 6, LST_HEIGHT, p.bg);
    const int16_t total = contentHeight();
    if (total <= LST_HEIGHT) return;
    const int16_t th = max<int16_t>(20, (int32_t)LST_HEIGHT * LST_HEIGHT / total);
    const int16_t ty = LST_TOP + (int32_t)scrollY * (LST_HEIGHT - th) / (total - LST_HEIGHT);
    tft.fillRoundRect(SCREEN_W - 5, ty, 3, th, 1, p.border);
}

void drawList() {
    const Palette& p = palette();
    RowDef rows[16];
    const uint8_t n = buildRows(rows);
    tft.setViewport(0, LST_TOP, SCREEN_W, LST_HEIGHT, false);   // clip to the list
    int16_t y = LST_TOP - scrollY, prevBottom = LST_TOP;
    for (uint8_t i = 0; i < n; i++) {
        if (y > prevBottom) tft.fillRect(0, prevBottom, SCREEN_W - 6, y - prevBottom, p.bg);
        if (y + rows[i].h > LST_TOP && y < SCREEN_H) drawRow(rows[i], y);
        prevBottom = y + rows[i].h;
        y += rows[i].h + GAP;
    }
    if (prevBottom < SCREEN_H) tft.fillRect(0, max(prevBottom, LST_TOP), SCREEN_W - 6, SCREEN_H - max(prevBottom, LST_TOP), p.bg);
    drawScrollbar();
    tft.resetViewport();
}

// Redraw one kind of row in place (e.g. the voltage) if it is on screen.
void drawRowKind(Row kind) {
    RowDef rows[16];
    const uint8_t n = buildRows(rows);
    int16_t y = LST_TOP - scrollY;
    tft.setViewport(0, LST_TOP, SCREEN_W, LST_HEIGHT, false);
    for (uint8_t i = 0; i < n; i++) {
        if (rows[i].kind == kind && y + rows[i].h > LST_TOP && y < SCREEN_H) drawRow(rows[i], y);
        y += rows[i].h + GAP;
    }
    tft.resetViewport();
}

void drawListScreen() {
    tft.fillScreen(palette().bg);
    if (screen == Screen::Settings) {
        header("Settings", HDR_H);
        const Palette& p = palette();
        tft.setTextFont(1);
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(p.textFaint, p.bg);
        tft.drawString("build " FW_VERSION, SCREEN_W - 8, HDR_Y + HDR_H / 2);
    } else {
        header("Home Assistant", HDR_H);
    }
    scrollY = constrain(scrollY, 0, scrollMax());
    drawList();
}

Target hitList(int16_t x, int16_t y) {
    Target t;
    if (y < LST_TOP) return t;
    RowDef rows[16];
    const uint8_t n = buildRows(rows);
    const int16_t cy = y - LST_TOP + scrollY;
    int16_t top = 0;
    for (uint8_t i = 0; i < n; i++) {
        const RowDef& r = rows[i];
        if (cy >= top && cy < top + r.h) {
            const int16_t sy = LST_TOP + top - scrollY;
            auto set = [&](Hit h, uint8_t arg) { t.hit = h; t.arg = arg; t.x = LST_LEFT; t.y = sy; t.w = LST_WIDTH; t.h = r.h; };
            auto seg = [&](Hit h, int16_t sx, int16_t sw) {
                if (x >= sx && x < sx + sw) set(h, (uint8_t)min(2, (x - sx) * 3 / sw));
            };
            switch (r.kind) {
            case R_BT:    set(Hit::Pairing, 0); break;
            case R_HA:    set(Hit::HaRow, 0); break;
            case R_SD:    set(Hit::LoadIr, 0); break;
            case R_THEME: seg(Hit::Theme, 70, 156); break;
            case R_PWR:   seg((Hit)((uint8_t)Hit::Brightness + r.arg), 80, 148); break;
            case R_TEST:  set(Hit::HaTest, 0); break;
            default: break;
            }
            return t;
        }
        top += r.h + GAP;
    }
    return t;
}

}  // namespace

// ===========================================================================
// Public
// ===========================================================================

bool scrollable() { return screen == Screen::Settings || screen == Screen::HaStatus; }

int16_t scrollMax() { return max<int16_t>(0, contentHeight() - LST_HEIGHT); }

void scrollTo(int16_t y) {
    y = constrain(y, 0, scrollMax());
    if (y == scrollY) return;
    scrollY = y;
    drawList();
}

void drawBatteryInfo() {
    if (screen == Screen::Settings) drawRowKind(R_PWRLABEL);
}

void drawStatus() {
    if (screen != Screen::Main) return;
    const Palette& p = palette();
    tft.fillRect(0, STATUS_Y, HOME_X, STATUS_H, p.bg);
    Icons::bluetooth(9, STATUS_Y + STATUS_H / 2, Ble::connected() ? p.bt : p.textFaint);

    const int16_t bx = 18, by = STATUS_Y + 7, bw = HOME_X - bx - 6, bh = 16;
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
    switch (screen) {
    case Screen::Settings:
    case Screen::HaStatus: drawListScreen(); return;
    case Screen::Pairing:  drawPairing(); return;
    case Screen::Home:     drawHome(); return;
    case Screen::Main:     break;
    }
    tft.fillScreen(palette().bg);
    drawStatus();
    drawTopButtons(false, false);
    drawNowPlaying();
    for (uint8_t i = 0; i < 3; i++) drawMedia(i, false);
    for (uint8_t i = 0; i < 3; i++) drawVol(i, false);
    drawTabsAndPanel();
}

void drawTarget(const Target& t, bool pressed) {
    switch (t.hit) {
    case Hit::HomeBtn:   drawTopButtons(pressed, false); break;
    case Hit::Gear:      drawTopButtons(false, pressed); break;
    case Hit::MediaPrev: drawMedia(0, pressed); break;
    case Hit::MediaPlay: drawMedia(1, pressed); break;
    case Hit::MediaNext: drawMedia(2, pressed); break;
    case Hit::VolDown:   drawVol(0, pressed); break;
    case Hit::VolUp:     drawVol(1, pressed); break;
    case Hit::Mute:      drawVol(2, pressed); break;
    case Hit::Slot:      drawSlot(t.arg, pressed); break;
    case Hit::HomeTile:  drawTile(t.arg, pressed); break;
    case Hit::Blind:     drawBlindButton(t.arg, pressed); break;
    case Hit::Theme:      if (!pressed) drawRowKind(R_THEME); break;
    case Hit::Brightness:
    case Hit::DimAfter:
    case Hit::SleepAfter:
    case Hit::WifiOff:    if (!pressed) drawRowKind(R_PWR); break;
    case Hit::HaTest:     if (!pressed) drawRowKind(R_TEST); break;
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
    } else if (screen == Screen::Pairing && (ch & (Ble::CH_LINK | Ble::CH_META))) {
        drawPairing();
    } else if (screen == Screen::Settings && (ch & Ble::CH_LINK)) {
        drawRowKind(R_BT);
    }
}

void onHa(uint8_t ch) {
    switch (screen) {
    case Screen::Home:
        if (ch & Ha::CH_NET) drawHomeHeader();
        if (ch & (Ha::CH_NET | Ha::CH_STATE)) {
            if (!Ha::configured()) { drawHomeBody(); break; }
            for (uint8_t i = 0; i < Ha::switchCount(); i++) drawTile(i, false);
            drawBlindLabel();
        }
        break;
    case Screen::HaStatus:
        drawList();
        break;
    case Screen::Settings:
        if (ch & Ha::CH_NET) drawRowKind(R_HA);
        break;
    default:
        break;
    }
}

void message(const char* text) {
    const Palette& p = palette();
    const int16_t y = screen == Screen::Main ? GRID_Y + 30 : screen == Screen::Pairing ? 230 : 140;
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
    const bool inHeader = y < HDR_Y + HOME_HDR_H + 2;

    switch (screen) {
    case Screen::Main: {
        if (in(x, y, GEAR_X - 4, 0, SCREEN_W - GEAR_X + 4, STATUS_Y + STATUS_H + 2)) { set(Hit::Gear, 0, GEAR_X, STATUS_Y, TOP_BTN_W, STATUS_H); return t; }
        if (in(x, y, HOME_X - 4, 0, TOP_BTN_W + 6, STATUS_Y + STATUS_H + 2)) { set(Hit::HomeBtn, 0, HOME_X, STATUS_Y, TOP_BTN_W, STATUS_H); return t; }
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
    case Screen::Pairing:
        if (inHeader && x < 60) { set(Hit::Back, 0, PAD, HDR_Y, 40, HDR_H); return t; }
        if (in(x, y, PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H)) { set(Hit::Forget, 0, PAD, BT_FORGET_Y, SCREEN_W - 2 * PAD, BT_FORGET_H); return t; }
        return t;
    case Screen::Home:
        if (inHeader && x < 60) { set(Hit::Back, 0, PAD, HDR_Y, 40, HOME_HDR_H); return t; }
        if (!Ha::configured()) return t;
        for (uint8_t i = 0; i < Ha::switchCount(); i++)
            if (in(x, y, tileX(i), tileY(i), TILE_W, TILE_H)) { set(Hit::HomeTile, i, tileX(i), tileY(i), TILE_W, TILE_H); return t; }
        if (Ha::hasBlinds())
            for (uint8_t i = 0; i < 3; i++)
                if (in(x, y, colX(i), BLIND_Y, COL_W, BLIND_H)) { set(Hit::Blind, i, colX(i), BLIND_Y, COL_W, BLIND_H); return t; }
        return t;
    case Screen::Settings:
    case Screen::HaStatus:
        if (y < LST_TOP) {
            if (x < 60) set(Hit::Back, 0, PAD, HDR_Y, 40, HDR_H);
            return t;
        }
        return hitList(x, y);
    }
    return t;
}

}  // namespace UI
