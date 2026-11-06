// ── S3 Remote touch UI ──────────────────────────────────────
// Immediate-mode: each frame redraws the current screen, and the widgets
// both draw themselves and report taps. Screen changes slide (push) the old
// frame out; FSTN is slow, so every animation stays within a few frames.

#if defined(ROLE_REMOTE) && defined(REMOTE_S3)

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "ui_s3.h"
#include "lcd_s3.h"
#include "touch_s3.h"
#include "config.h"
#include "settings.h"
#include "control.h"
#include "sensors.h"
#include "wifi_mqtt.h"
#include "espnow_remote.h"
#include "buzzer.h"
#include "fw_info.h"
#include "display.h"

static const int SW = LCD_W, SH = LCD_H;

#define F_TINY  u8g2_font_5x7_tr
#define F_SMALL u8g2_font_helvR08_tr
#define F_LABEL u8g2_font_helvB08_tr
#define F_BTN   u8g2_font_helvB10_tr
#define F_MED   u8g2_font_helvB12_tr
#define F_VAL   u8g2_font_helvB14_tr
#define F_NUM   u8g2_font_logisoso22_tn
#define F_EDIT  u8g2_font_logisoso38_tn
#define F_BIG   u8g2_font_logisoso42_tn

#define FRAME_MS        33UL
#define DIM_AFTER_MS    20000UL
#define MENU_TIMEOUT_MS 30000UL
#define SLIDE_MS        240

// ── State ───────────────────────────────────────────────────
enum class Scr : uint8_t { BOOT, MAIN, MENU, PARAMS, EDIT, PERIODS, PERIOD, SENSORS, WIFI, MQTT, SCREEN, CONFIRM };

static Scr      g_scr = Scr::BOOT;
static Scr      g_stack[8];
static int      g_depth = 0;
static bool     g_paired = false;
static uint32_t g_lastFrameMs = 0, g_lastAct = 0;
static bool     g_dimmed = false;
static int      g_contrast = 50, g_backlight = 80, g_dimLevel = 25;
static bool     g_dimPreview = false;   // DIM slider held: show the dim level live
static uint32_t g_uiSaveAt = 0;      // pending "hpui" write (contrast/backlight)
static uint32_t g_spSaveAt = 0;      // pending settings write after setpoint taps
static uint32_t g_spPopUntil = 0, g_spPopStart = 0;
static float    g_waterShown = NAN;

static uint8_t g_oldBuf[LCD_BUF_BYTES], g_newBuf[LCD_BUF_BYTES], g_lastFrame[LCD_BUF_BYTES];
static struct { uint32_t start; uint16_t ms; int8_t dir; bool vertical; } g_tr = {};

// ── Touch input ─────────────────────────────────────────────
static struct {
    bool down, pressed, released, consumed;
    int  x, y, dx, dy;
    uint32_t downMs;
    uint8_t misses;
} T = {};
static bool g_inputOn = true;

static void readTouch(uint32_t now) {
    T.pressed = T.released = false;
    int x, y;
    if (touchRead(x, y)) {
        T.misses = 0;
        if (!T.down) {
            T.down = T.pressed = true;
            T.consumed = false;
            T.dx = x; T.dy = y;
            T.downMs = now;
        }
        T.x = x; T.y = y;
    } else if (T.down && ++T.misses >= 2) {
        T.down = false;
        T.released = true;
    }
}

static bool inRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
static bool held(int x, int y, int w, int h) {
    return g_inputOn && T.down && !T.consumed && inRect(T.dx, T.dy, x, y, w, h) && inRect(T.x, T.y, x, y, w, h);
}
static bool clicked(int x, int y, int w, int h) {
    if (!g_inputOn || !T.released || T.consumed) return false;
    if (!inRect(T.dx, T.dy, x, y, w, h) || !inRect(T.x, T.y, x, y, w, h)) return false;
    T.consumed = true;   // one widget per tap
    buzzerClick();
    return true;
}

// ── Text ────────────────────────────────────────────────────
static int textW(const char* s, const uint8_t* f) { lcd.setFont(f); return lcd.getStrWidth(s); }

// align: 0 left, 1 centre, 2 right
static void text(int x, int y, const char* s, const uint8_t* f, uint8_t align = 0) {
    lcd.setFont(f);
    if (align) x -= (align == 1) ? lcd.getStrWidth(s) / 2 : lcd.getStrWidth(s);
    lcd.drawStr(x, y, s);
}

// Value followed by a degree ring (the _tr/_tn fonts have no ° glyph)
static int textTemp(int x, int y, const char* s, const uint8_t* f, uint8_t align = 0) {
    lcd.setFont(f);
    int sw = lcd.getStrWidth(s), a = lcd.getAscent();
    int r = a >= 20 ? 3 : (a >= 10 ? 2 : 1);
    int w = sw + 2 + 2 * r + 1;
    if (align) x -= (align == 1) ? w / 2 : w;
    lcd.drawStr(x, y, s);
    lcd.drawCircle(x + sw + 2 + r, y - a + r, r);
    return w;
}

static void fmtTemp(char* buf, size_t n, float v, bool ok) {
    if (ok) snprintf(buf, n, "%.1f", v);
    else    strlcpy(buf, "--.-", n);
}

// ── Icons (drawn in colour c, cut-outs in !c) ───────────────
enum Icon : uint8_t {
    IC_NONE, IC_POWER, IC_MINUS, IC_PLUS, IC_UP, IC_DOWN, IC_BACK, IC_MENU, IC_GEAR, IC_CLOCK,
    IC_SNOW, IC_THERMO, IC_WIFI, IC_HOME, IC_SCREEN, IC_RESET, IC_SUN, IC_MOON, IC_FAN, IC_HEAT,
    IC_VALVE, IC_WARN, IC_CHECK,
};

static void thickLine(int x0, int y0, int x1, int y1) {
    lcd.drawLine(x0, y0, x1, y1);
    lcd.drawLine(x0 + 1, y0, x1 + 1, y1);
}

static void drawIcon(Icon ic, int cx, int cy, uint8_t c, int phase = 0) {
    lcd.setDrawColor(c);
    switch (ic) {
    case IC_POWER:
        lcd.drawCircle(cx, cy + 1, 7);
        lcd.drawCircle(cx, cy + 1, 6);
        lcd.setDrawColor(!c); lcd.drawBox(cx - 3, cy - 7, 7, 5);
        lcd.setDrawColor(c);  lcd.drawBox(cx - 1, cy - 8, 3, 9);
        break;
    case IC_MINUS: lcd.drawBox(cx - 8, cy - 1, 17, 3); break;
    case IC_PLUS:  lcd.drawBox(cx - 8, cy - 1, 17, 3); lcd.drawBox(cx - 1, cy - 8, 3, 17); break;
    case IC_UP:    lcd.drawTriangle(cx, cy - 6, cx - 9, cy + 5, cx + 9, cy + 5); break;
    case IC_DOWN:  lcd.drawTriangle(cx, cy + 6, cx - 9, cy - 5, cx + 9, cy - 5); break;
    case IC_BACK:
        thickLine(cx + 3, cy - 6, cx - 3, cy);
        thickLine(cx - 3, cy, cx + 3, cy + 6);
        break;
    case IC_MENU:
        for (int i = -1; i <= 1; i++) lcd.drawRBox(cx - 9, cy - 1 + i * 6, 19, 3, 1);
        break;
    case IC_GEAR:
        lcd.drawDisc(cx, cy, 6);
        for (int k = 0; k < 8; k++) {
            float a = k * PI / 4;
            lcd.drawBox(lroundf(cx + 7.5f * cosf(a)) - 1, lroundf(cy + 7.5f * sinf(a)) - 1, 3, 3);
        }
        lcd.setDrawColor(!c); lcd.drawDisc(cx, cy, 2);
        break;
    case IC_CLOCK:
        lcd.drawCircle(cx, cy, 8); lcd.drawCircle(cx, cy, 7);
        lcd.drawBox(cx - 1, cy - 5, 2, 6); lcd.drawBox(cx - 1, cy - 1, 5, 2);
        break;
    case IC_SNOW:
        for (int k = 0; k < 3; k++) {
            float a = PI / 2 + k * PI / 3;
            int dx = lroundf(8 * cosf(a)), dy = lroundf(8 * sinf(a));
            thickLine(cx - dx, cy - dy, cx + dx, cy + dy);
        }
        lcd.setDrawColor(!c); lcd.drawDisc(cx, cy, 1);
        break;
    case IC_THERMO:
        lcd.drawRFrame(cx - 3, cy - 9, 7, 14, 3);
        lcd.drawDisc(cx, cy + 5, 4);
        lcd.drawBox(cx - 1, cy - 4, 3, 8);
        break;
    case IC_WIFI:
        for (int r = 4; r <= 10; r += 3) {
            lcd.drawCircle(cx, cy + 6, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
            lcd.drawCircle(cx, cy + 6, r - 1, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        }
        lcd.drawDisc(cx, cy + 6, 1);
        break;
    case IC_HOME:
        lcd.drawTriangle(cx, cy - 9, cx - 10, cy, cx + 10, cy);
        lcd.drawBox(cx - 7, cy, 15, 8);
        lcd.setDrawColor(!c); lcd.drawBox(cx - 2, cy + 3, 5, 5);
        break;
    case IC_SCREEN:
        lcd.drawRFrame(cx - 10, cy - 8, 21, 15, 2);
        lcd.drawBox(cx, cy - 6, 9, 11);
        lcd.drawBox(cx - 5, cy + 9, 11, 2);
        break;
    case IC_RESET:
        lcd.drawCircle(cx, cy, 8, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT);
        lcd.drawCircle(cx, cy, 7, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT);
        lcd.drawTriangle(cx - 5, cy - 8, cx + 1, cy - 12, cx + 1, cy - 4);
        break;
    case IC_SUN:
        lcd.drawDisc(cx, cy, 4);
        for (int k = 0; k < 8; k++) {
            float a = k * PI / 4;
            lcd.drawLine(lroundf(cx + 6 * cosf(a)), lroundf(cy + 6 * sinf(a)),
                         lroundf(cx + 9 * cosf(a)), lroundf(cy + 9 * sinf(a)));
        }
        break;
    case IC_MOON:
        lcd.drawDisc(cx, cy, 8);
        lcd.setDrawColor(!c); lcd.drawDisc(cx + 4, cy - 3, 7);
        break;
    case IC_FAN: {
        float base = phase * PI / 6;
        for (int k = 0; k < 3; k++) {
            float a = base + k * 2 * PI / 3;
            lcd.drawDisc(lroundf(cx + 4.5f * cosf(a)), lroundf(cy + 4.5f * sinf(a)), 3);
        }
        lcd.drawCircle(cx, cy, 8);
        lcd.setDrawColor(!c); lcd.drawDisc(cx, cy, 1);
        break;
    }
    case IC_HEAT: {
        static const int8_t wave[8] = {0, 1, 2, 1, 0, -1, -2, -1};
        for (int i = -1; i <= 1; i++) {
            for (int y = -7; y <= 7; y++) {
                int xo = wave[(y + 8 + phase * 2) & 7];
                lcd.drawBox(cx + i * 6 + xo, cy + y, 2, 1);
            }
        }
        break;
    }
    case IC_VALVE:
        lcd.drawTriangle(cx - 9, cy - 5, cx - 9, cy + 6, cx, cy);
        lcd.drawTriangle(cx + 9, cy - 5, cx + 9, cy + 6, cx, cy);
        lcd.drawBox(cx - 1, cy - 8, 2, 8);
        lcd.drawBox(cx - 4, cy - 9, 9, 2);
        break;
    case IC_WARN:
        lcd.drawTriangle(cx, cy - 11, cx - 12, cy + 9, cx + 12, cy + 9);
        lcd.setDrawColor(!c);
        lcd.drawBox(cx - 1, cy - 4, 3, 7);
        lcd.drawBox(cx - 1, cy + 5, 3, 2);
        break;
    case IC_CHECK:
        thickLine(cx - 7, cy, cx - 2, cy + 5);
        thickLine(cx - 2, cy + 5, cx + 7, cy - 5);
        break;
    default: break;
    }
    lcd.setDrawColor(1);
}

// ── Widgets ─────────────────────────────────────────────────
enum : uint8_t { B_OUTLINE, B_FILLED, B_FLAT };

static int radiusFor(int w, int h) { return min(5, min(w, h) / 2 - 1); }

static void drawButtonFace(int x, int y, int w, int h, const char* label, Icon ic,
                           bool fill, uint8_t style, const uint8_t* font) {
    int r = radiusFor(w, h);
    lcd.setDrawColor(1);
    if (fill)                    lcd.drawRBox(x, y, w, h, r);
    else if (style != B_FLAT)    lcd.drawRFrame(x, y, w, h, r);
    uint8_t c = fill ? 0 : 1;
    int cx = x + w / 2;
    if (ic && label) {
        drawIcon(ic, cx, y + h / 2 - 7, c);
        lcd.setDrawColor(c);
        text(cx, y + h - 5, label, font, 1);
    } else if (ic) {
        drawIcon(ic, cx, y + h / 2, c);
    } else if (label) {
        lcd.setDrawColor(c);
        lcd.setFont(font);
        text(cx, y + (h + lcd.getAscent()) / 2, label, font, 1);
    }
    lcd.setDrawColor(1);
}

static bool button(int x, int y, int w, int h, const char* label, Icon ic = IC_NONE,
                   uint8_t style = B_OUTLINE, const uint8_t* font = F_BTN) {
    bool on = held(x, y, w, h);
    drawButtonFace(x, y, w, h, label, ic, (style == B_FILLED) != on, style, font);
    return clicked(x, y, w, h);
}

// Fires on press, then repeats while held, faster after 2 s
static uint32_t g_rptNext = 0;
static bool repeatButton(int x, int y, int w, int h, Icon ic, const char* label = nullptr) {
    bool on = held(x, y, w, h);
    drawButtonFace(x, y, w, h, label, ic, on, B_OUTLINE, F_BTN);
    if (!on) return false;
    uint32_t now = millis();
    if (T.pressed) {
        buzzerClick();
        g_rptNext = now + 400;
        return true;
    }
    if ((int32_t)(now - g_rptNext) >= 0) {
        g_rptNext = now + (now - T.downMs > 2000 ? 45 : 110);
        return true;
    }
    return false;
}

// Press and hold until the bar fills: for actions that are hard to undo
static bool holdButton(int x, int y, int w, int h, const char* label, uint32_t holdMs) {
    bool on = held(x, y, w, h);
    float p = on ? min(1.0f, (millis() - T.downMs) / (float)holdMs) : 0;
    int r = radiusFor(w, h);
    lcd.drawRFrame(x, y, w, h, r);
    lcd.drawRFrame(x + 1, y + 1, w - 2, h - 2, r);
    int pw = (int)(p * (w - 6));
    if (pw > 0) lcd.drawBox(x + 3, y + 3, pw, h - 6);
    lcd.setDrawColor(2);
    lcd.setFont(F_BTN);
    text(x + w / 2, y + (h + lcd.getAscent()) / 2, on ? "KEEP HOLDING" : label, F_BTN, 1);
    lcd.setDrawColor(1);
    if (on && p >= 1) {
        T.consumed = true;
        buzzerClick();
        return true;
    }
    return false;
}

static void drawSwitch(int x, int y, bool on, float p) {   // p: 0..1 knob position
    lcd.drawRFrame(x, y, 36, 20, 9);
    if (on) lcd.drawRBox(x, y, 36, 20, 9);
    int kx = x + 10 + (int)(p * 16);
    lcd.setDrawColor(on ? 0 : 1);
    lcd.drawDisc(kx, y + 10, 6);
    lcd.setDrawColor(1);
}

static void dottedHLine(int x, int y, int w) {
    for (int i = 0; i < w; i += 2) lcd.drawPixel(x + i, y);
}

// ── Navigation + slide transitions ──────────────────────────
static void startSlide(int dir, bool vertical = false) {
    memcpy(g_oldBuf, g_lastFrame, LCD_BUF_BYTES);
    g_tr.start = millis();
    g_tr.ms = SLIDE_MS;
    g_tr.dir = dir;
    g_tr.vertical = vertical;
}

static void goTo(Scr s) {
    if (g_depth < (int)(sizeof(g_stack) / sizeof(g_stack[0]))) g_stack[g_depth++] = g_scr;
    g_scr = s;
    startSlide(+1);
}

static void goBack() {
    g_scr = g_depth ? g_stack[--g_depth] : Scr::MAIN;
    startSlide(-1);
}

static void goHome() {
    if (g_scr == Scr::MAIN) return;
    g_depth = 0;
    g_scr = Scr::MAIN;
    startSlide(-1);
}

static inline bool getBit(const uint8_t* row, int x) { return row[x >> 3] & (0x80 >> (x & 7)); }

// Pushes the old frame out and the freshly drawn one in. Buffer layout:
// 30 bytes per pixel row, MSB = leftmost pixel.
static void composeSlide(float t) {
    float p = 1 - (1 - t) * (1 - t);   // ease-out
    uint8_t* out = lcd.getBufferPtr();
    memcpy(g_newBuf, out, LCD_BUF_BYTES);
    memset(out, 0, LCD_BUF_BYTES);
    if (!g_tr.vertical) {
        int d = (int)(p * SW);
        for (int y = 0; y < SH; y++) {
            const uint8_t* ro = g_oldBuf + y * LCD_ROW_BYTES;
            const uint8_t* rn = g_newBuf + y * LCD_ROW_BYTES;
            uint8_t* rw = out + y * LCD_ROW_BYTES;
            for (int x = 0; x < SW; x++) {
                bool b = g_tr.dir > 0 ? (x < SW - d ? getBit(ro, x + d) : getBit(rn, x - (SW - d)))
                                      : (x < d ? getBit(rn, x + (SW - d)) : getBit(ro, x - d));
                if (b) rw[x >> 3] |= 0x80 >> (x & 7);
            }
        }
        lcd.drawVLine(g_tr.dir > 0 ? SW - d : d, 0, SH);
    } else {
        int d = (int)(p * SH);
        for (int y = 0; y < SH; y++) {
            int src; const uint8_t* buf;
            if (g_tr.dir > 0) { buf = y < SH - d ? g_oldBuf : g_newBuf; src = y < SH - d ? y + d : y - (SH - d); }
            else              { buf = y < d ? g_newBuf : g_oldBuf;      src = y < d ? y + (SH - d) : y - d; }
            memcpy(out + y * LCD_ROW_BYTES, buf + src * LCD_ROW_BYTES, LCD_ROW_BYTES);
        }
    }
}

// Sub-screen header: back button + title; true when back was tapped
static bool header(const char* title, const char* right = nullptr) {
    bool back = button(0, 0, 40, 22, nullptr, IC_BACK, B_FLAT);
    text(46, 16, title, F_BTN);
    if (right) text(SW - 4, 15, right, F_SMALL, 2);
    lcd.drawHLine(0, 22, SW);
    return back;
}

// ── Toast (also used by the ESP-NOW, OTA and web modules) ───
static char     g_toast1[24] = "", g_toast2[24] = "";
static uint32_t g_toastStart = 0, g_toastUntil = 0;

void displayToast(const char* line1, const char* line2, uint32_t durationMs) {
    uint32_t now = millis();
    if (now >= g_toastUntil) g_toastStart = now;   // slide in only when not already showing
    strlcpy(g_toast1, line1 ? line1 : "", sizeof(g_toast1));
    strlcpy(g_toast2, line2 ? line2 : "", sizeof(g_toast2));
    g_toastUntil = now + durationMs;
}

static void drawToast(uint32_t now) {
    if (now >= g_toastUntil) return;
    int h = g_toast2[0] ? 34 : 22, w = 200, x = (SW - w) / 2;
    float t = min(1.0f, (now - g_toastStart) / 160.0f);
    float out = g_toastUntil - now < 160 ? (g_toastUntil - now) / 160.0f : 1;
    int y = -h + (int)((h + 26) * min(t, out));
    lcd.setDrawColor(0); lcd.drawRBox(x - 2, y - 2, w + 4, h + 4, 6);
    lcd.setDrawColor(1); lcd.drawRBox(x, y, w, h, 5);
    lcd.setDrawColor(0);
    text(SW / 2, y + 15, g_toast1, F_BTN, 1);
    if (g_toast2[0]) text(SW / 2, y + 28, g_toast2, F_SMALL, 1);
    lcd.setDrawColor(1);
}

// ── Status helpers ──────────────────────────────────────────
static bool linkLost() { return espnowCtrlSignalLost(); }

static bool systemOn() {
    return linkLost() ? settingsGet().systemOnline : controlStatus().state != HpState::OFFLINE;
}

static const char* stateChip(const HpStatus& hp) {
    if (linkLost()) return g_paired ? "NO LINK" : "NOT PAIRED";
    switch (hp.error) {
    case HpError::E01_WATER_SENSOR:   return "E01";
    case HpError::E02_PIPE_SENSOR:    return "E02";
    case HpError::E03_EXHAUST_SENSOR: return "E03";
    case HpError::E04_OUTDOOR_SENSOR: return "E04";
    case HpError::EH_EXHAUST_ALARM:   return "EH";
    default: break;
    }
    switch (hp.state) {
    case HpState::OFFLINE:           return "OFF";
    case HpState::ANTIFREEZE_ON:
    case HpState::ANTIFREEZE_WAIT:   return "ANTIFREEZE";
    case HpState::STARTUP_FAN:       return "STARTING";
    case HpState::COMP_DELAY:        return "COMP DELAY";
    case HpState::HEATING:           return "HEATING";
    case HpState::SHUTDOWN_COMP:
    case HpState::SHUTDOWN_FAN:      return "STOPPING";
    case HpState::STANDBY:           return "STANDBY";
    case HpState::EXHAUST_PROTECT:   return "PROTECT";
    default:                         return "DEFROST";
    }
}

// One plain sentence under the water temperature
static void stateSentence(const HpStatus& hp, char* buf, size_t n, bool& tempTail) {
    const Settings& s = settingsGet();
    tempTail = false;
    if (linkLost()) {
        strlcpy(buf, g_paired ? "No signal from controller" : "Copy old remote to pair", n);
        return;
    }
    switch (hp.state) {
    case HpState::OFFLINE:         strlcpy(buf, "System is off", n); break;
    case HpState::HEATING:         snprintf(buf, n, "Heating to %d", (int)roundf(s.setpoint)); tempTail = true; break;
    case HpState::STANDBY:         snprintf(buf, n, "At target, restarts at %d", (int)roundf(s.setpoint - s.u01)); tempTail = true; break;
    case HpState::STARTUP_FAN:     strlcpy(buf, "Starting, fan first", n); break;
    case HpState::COMP_DELAY:      strlcpy(buf, "Compressor rest delay", n); break;
    case HpState::SHUTDOWN_COMP:
    case HpState::SHUTDOWN_FAN:    strlcpy(buf, "Stopping", n); break;
    case HpState::ANTIFREEZE_ON:
    case HpState::ANTIFREEZE_WAIT: strlcpy(buf, "Antifreeze circulation", n); break;
    case HpState::EXHAUST_PROTECT: strlcpy(buf, "Exhaust hot, paused", n); break;
    default:                       strlcpy(buf, "Defrosting the coil", n); break;
    }
}

// ── Confirm dialog ──────────────────────────────────────────
static struct {
    const char* title; const char* l1; const char* l2; const char* action;
    Icon icon; uint16_t holdMs; void (*fn)();
} g_cf = {};

static void askConfirm(const char* title, const char* l1, const char* l2, const char* action,
                       Icon icon, uint16_t holdMs, void (*fn)()) {
    g_cf = {title, l1, l2, action, icon, holdMs, fn};
    goTo(Scr::CONFIRM);
}

static void drawConfirm() {
    if (header(g_cf.title)) { goBack(); return; }
    lcd.drawCircle(26, 62, 17);
    drawIcon(g_cf.icon, 26, 62, 1);
    text(54, 52, g_cf.l1, F_MED);
    if (g_cf.l2) text(54, 72, g_cf.l2, F_SMALL);
    if (button(2, 116, 92, 42, "CANCEL")) { goBack(); return; }
    bool go = g_cf.holdMs ? holdButton(98, 116, 140, 42, g_cf.action, g_cf.holdMs)
                          : button(98, 116, 140, 42, g_cf.action, IC_NONE, B_FILLED);
    if (go) {
        Scr before = g_scr;
        if (g_cf.fn) g_cf.fn();
        if (g_scr == before) goHome();
    }
}

// Full-screen notice drawn and sent at once, for calls that block (WiFi portal)
static void showBlockingNotice(const char* l1, const char* l2, const char* l3) {
    lcd.clearBuffer();
    lcd.drawRFrame(4, 4, SW - 8, SH - 8, 6);
    drawIcon(IC_WIFI, SW / 2, 34, 1);
    text(SW / 2, 70, l1, F_MED, 1);
    text(SW / 2, 92, l2, F_SMALL, 1);
    text(SW / 2, 108, l3, F_SMALL, 1);
    text(SW / 2, 140, "The screen is paused meanwhile", F_TINY, 1);
    lcdSend();
}

// ── Number editor (parameters, setpoint, MQTT port) ─────────
static struct {
    char title[24]; const char* unit; bool isTemp;
    float val, min, max, step; uint8_t decimals;
    const char* const* names;      // enum values (nullptr = number)
    int id;
    int8_t animDir; uint32_t animStart;
} g_ed = {};

static const char* const k_onOff[]  = {"OFF", "ON"};
static const char* const k_valve[]  = {"Heat OFF, defrost ON", "Heat ON, defrost OFF"};
static const char* const k_exh[]    = {"OFF", "Protect, fan runs", "Protect, fan stops"};

struct ParamDef {
    const char* code; const char* name; float min, max, step; uint8_t dec; bool isTemp; const char* unit;
    const char* const* names;
};
static const ParamDef k_params[12] = {
    {"U03", "Max setpoint",        30,   70,   1,   0, true,  nullptr, nullptr},
    {"U01", "Hysteresis",          2,    10,   0.5, 1, true,  nullptr, nullptr},
    {"U02", "Water sensor offset", -9.9, 9.9,  0.1, 1, true,  nullptr, nullptr},
    {"U30", "Defrost interval",    10,   90,   1,   0, false, "min",   nullptr},
    {"U31", "Defrost start temp",  -20,  0,    0.5, 1, true,  nullptr, nullptr},
    {"U32", "Defrost stop temp",   2,    30,   0.5, 1, true,  nullptr, nullptr},
    {"U33", "Max defrost time",    2,    15,   1,   0, false, "min",   nullptr},
    {"U34", "4-way valve mode",    0,    1,    1,   0, false, nullptr, k_valve},
    {"U50", "Exhaust protection",  0,    2,    1,   0, false, nullptr, k_exh},
    {"U51", "Exhaust limit",       50,   125,  1,   0, true,  nullptr, nullptr},
    {"U52", "Antifreeze",          0,    1,    1,   0, false, nullptr, k_onOff},
    {"U80", "Auto restart",        0,    1,    1,   0, false, nullptr, k_onOff},
};

static float paramValue(int id) {
    const Settings& s = settingsGet();
    switch (id) {
    case 0: return s.u03;  case 1: return s.u01;  case 2: return s.u02;  case 3: return s.u30;
    case 4: return s.u31;  case 5: return s.u32;  case 6: return s.u33;  case 7: return s.u34;
    case 8: return s.u50;  case 9: return s.u51;  case 10: return s.u52; case 11: return s.u80;
    case 100: return s.setpoint;
    case 201: return s.mqttPort;
    }
    return 0;
}

static void applyEdit() {
    Settings& s = settingsGet();
    float v = g_ed.val;
    switch (g_ed.id) {
    case 0:  s.u03 = v; if (s.setpoint > s.u03) s.setpoint = s.u03; break;
    case 1:  s.u01 = v; break;
    case 2:  s.u02 = v; break;
    case 3:  s.u30 = (uint8_t)v; break;
    case 4:  s.u31 = v; break;
    case 5:  s.u32 = v; break;
    case 6:  s.u33 = (uint8_t)v; break;
    case 7:  s.u34 = (uint8_t)v; break;
    case 8:  s.u50 = (uint8_t)v; break;
    case 9:  s.u51 = v; break;
    case 10: s.u52 = (uint8_t)v; break;
    case 11: s.u80 = (uint8_t)v; break;
    case 100: s.setpoint = v; break;
    }
    settingsSave();
}

static void openEdit(int id) {
    if (id == 100) {
        const Settings& s = settingsGet();
        strlcpy(g_ed.title, "SETPOINT", sizeof(g_ed.title));
        g_ed.min = 10; g_ed.max = s.u03; g_ed.step = 1; g_ed.decimals = 0;
        g_ed.isTemp = true; g_ed.unit = nullptr; g_ed.names = nullptr;
    } else {
        const ParamDef& p = k_params[id];
        snprintf(g_ed.title, sizeof(g_ed.title), "%s  %s", p.code, p.name);
        g_ed.min = p.min; g_ed.max = p.max; g_ed.step = p.step; g_ed.decimals = p.dec;
        g_ed.isTemp = p.isTemp; g_ed.unit = p.unit; g_ed.names = p.names;
    }
    g_ed.id = id;
    g_ed.val = paramValue(id);
    g_ed.animStart = 0;
    goTo(Scr::EDIT);
}

static void stepEdit(int dir) {
    float v = g_ed.val + dir * g_ed.step;
    v = roundf(v / g_ed.step) * g_ed.step;   // no float drift
    v = constrain(v, g_ed.min, g_ed.max);
    if (v != g_ed.val) {
        g_ed.val = v;
        g_ed.animDir = dir;
        g_ed.animStart = millis();
    }
}

static void drawEdit(uint32_t now) {
    if (header(g_ed.title)) { goBack(); return; }
    if (repeatButton(2, 26, 54, 86, IC_MINUS)) stepEdit(-1);
    if (repeatButton(184, 26, 54, 86, IC_PLUS)) stepEdit(+1);

    // Odometer: the new value rolls in from the direction of the change
    float t = g_ed.animStart ? min(1.0f, (now - g_ed.animStart) / 140.0f) : 1;
    int off = (int)((1 - t) * 12) * g_ed.animDir;
    lcd.setClipWindow(58, 24, 182, 110);
    char v[24];
    if (g_ed.names) {
        int idx = constrain((int)g_ed.val, 0, (int)g_ed.max);
        const char* name = g_ed.names[idx];
        bool longName = textW(name, F_MED) > 120;
        text(SW / 2, 74 + off, name, longName ? F_SMALL : F_MED, 1);
    } else {
        snprintf(v, sizeof(v), "%.*f", g_ed.decimals, g_ed.val);
        if (g_ed.isTemp) textTemp(SW / 2, 82 + off, v, F_EDIT, 1);
        else             text(SW / 2, 82 + off, v, F_EDIT, 1);
        if (g_ed.unit) text(SW / 2, 100, g_ed.unit, F_SMALL, 1);
    }
    lcd.setMaxClipWindow();

    char range[40];
    if (g_ed.names) snprintf(range, sizeof(range), "%d options", (int)g_ed.max + 1);
    else snprintf(range, sizeof(range), "%.*f to %.*f, step %.*f", g_ed.decimals, g_ed.min,
                  g_ed.decimals, g_ed.max, g_ed.decimals, g_ed.step);
    text(SW / 2, 110, range, F_TINY, 1);

    if (button(2, 116, 116, 42, "CANCEL")) { goBack(); return; }
    if (button(122, 116, 116, 42, "SAVE", IC_NONE, B_FILLED)) {
        applyEdit();
        displayToast("SAVED", g_ed.title, 1500);
        goBack();
    }
}

// ── Main screen ─────────────────────────────────────────────
static void powerToggle() {
    bool on = !systemOn();
    controlSetOnline(on);
    settingsSave();
    displayToast(on ? "HEATING ON" : "HEATING OFF", on ? "Starting sequence" : "Stopping sequence", 2000);
}

static void changeSetpoint(int dir) {
    Settings& s = settingsGet();
    float v = constrain(roundf(s.setpoint) + dir, 10.0f, s.u03);
    if (v == s.setpoint) return;
    s.setpoint = v;
    uint32_t now = millis();
    if (now >= g_spPopUntil) g_spPopStart = now;
    g_spPopUntil = now + 1500;
    g_spSaveAt = now + 1200;   // one NVS write + one send after the taps stop
}

static void drawStatusBar(const HpStatus& hp, uint32_t now) {
    bool blink = (now / 500) & 1;
    bool alert = linkLost() || hp.error != HpError::NONE || hp.state == HpState::EXHAUST_PROTECT;
    const char* chip = stateChip(hp);
    int cw = textW(chip, F_LABEL) + 10;
    if (!alert || blink) {
        lcd.drawRBox(0, 0, cw, 14, 3);
        lcd.setDrawColor(0); text(5, 11, chip, F_LABEL); lcd.setDrawColor(1);
    } else {
        lcd.drawRFrame(0, 0, cw, 14, 3);
        text(5, 11, chip, F_LABEL);
    }

    int rx = SW - 1;
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        rx -= textW(buf, F_LABEL);
        text(rx, 11, buf, F_LABEL);
        rx -= 8;
    }
    // Link: dot with radio arcs, crossed out when lost
    int lx = rx - 6;
    lcd.drawDisc(lx, 7, 1);
    lcd.drawCircle(lx, 7, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
    lcd.drawCircle(lx, 7, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
    if (!linkLost()) {
        lcd.drawCircle(lx, 7, 6, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
        lcd.drawCircle(lx, 7, 6, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
    } else if (blink) {
        lcd.drawLine(lx - 6, 13, lx + 6, 1);
    }
    rx -= 18;
    if (wifiConnected()) {
        int wx = rx - 6;
        lcd.drawDisc(wx, 12, 1);
        lcd.drawCircle(wx, 12, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        lcd.drawCircle(wx, 12, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        rx -= 18;
    }
    if (mqttConnected()) {
        int hx = rx - 6;
        lcd.drawTriangle(hx, 2, hx - 6, 7, hx + 6, 7);
        lcd.drawBox(hx - 4, 7, 9, 5);
    }
    lcd.drawHLine(0, 16, SW);
}

static void drawRelayTile(int x, Icon ic, bool on, int phase) {
    if (on) lcd.drawRBox(x, 97, 22, 18, 3);
    drawIcon(ic, x + 11, 106, on ? 0 : 1, on ? phase : 0);
}

static void drawMain(uint32_t now) {
    const HpStatus& hp = controlStatus();
    const SensorData& d = sensorsGet();
    Settings& s = settingsGet();
    bool lost = linkLost();
    bool blink = (now / 500) & 1;
    char buf[40];

    drawStatusBar(hp, now);

    // ── Water temperature, eased toward the received value ──
    float water = sensorCorrectedWater();
    bool wOk = d.waterOk && !lost;
    if (wOk) {
        if (isnan(g_waterShown) || fabsf(water - g_waterShown) > 15) g_waterShown = water;
        g_waterShown += (water - g_waterShown) * 0.2f;
        if (fabsf(water - g_waterShown) < 0.05f) g_waterShown = water;
    }
    text(4, 29, "WATER", F_LABEL);
    fmtTemp(buf, sizeof(buf), g_waterShown, wOk);
    char* dot = strchr(buf, '.');
    char dec[6] = "";
    if (dot) { strlcpy(dec, dot, sizeof(dec)); *dot = '\0'; }
    int iw = textW(buf, F_BIG);
    text(4, 76, buf, F_BIG);
    int dw = textW(dec, F_NUM);
    text(6 + iw, 76, dec, F_NUM);
    lcd.drawCircle(13 + iw + dw, 38, 3);
    text(18 + iw + dw, 50, "C", F_MED);

    // ── Setpoint pop-up after +/- taps ──────────────────────
    if (now < g_spPopUntil) {
        float t = min(1.0f, (now - g_spPopStart) / 120.0f);
        int w = 20 + (int)(124 * t), h = 14 + (int)(52 * t);
        int x = 75 - w / 2, y = 52 - h / 2;
        lcd.setDrawColor(0); lcd.drawBox(x - 1, y - 1, w + 2, h + 2);
        lcd.setDrawColor(1); lcd.drawRBox(x, y, w, h, 5);
        if (t >= 1) {
            lcd.setDrawColor(0);
            text(x + 8, y + 14, "SET TO", F_LABEL);
            snprintf(buf, sizeof(buf), "%d", (int)roundf(s.setpoint));
            textTemp(x + w / 2 + 10, y + h - 9, buf, F_BIG, 1);
            lcd.setDrawColor(1);
        }
    }

    // ── State sentence + progress toward the setpoint ───────
    bool tempTail;
    stateSentence(hp, buf, sizeof(buf), tempTail);
    if (tempTail) textTemp(4, 87, buf, F_SMALL);
    else          text(4, 87, buf, F_SMALL);
    // Bar beside the label: full = setpoint reached, notch = restart point (SP - U01)
    lcd.drawRFrame(60, 21, 86, 9, 3);
    if (wOk) {
        float lo = s.setpoint - 25;
        float frac = constrain((g_waterShown - lo) / (s.setpoint - lo), 0.0f, 1.0f);
        int fw = (int)(frac * 82);
        if (fw > 0) lcd.drawBox(62, 23, fw, 5);
        int rt = 62 + (int)(constrain((s.setpoint - s.u01 - lo) / (s.setpoint - lo), 0.0f, 1.0f) * 82);
        lcd.setDrawColor(2); lcd.drawVLine(rt, 20, 11); lcd.setDrawColor(1);
    }

    // ── Relays: inverted tile = running ─────────────────────
    int phase = (now / 140) % 12;
    drawRelayTile(4,  IC_FAN,   !lost && hp.fan,        phase);
    drawRelayTile(30, IC_HEAT,  !lost && hp.compressor, phase);
    drawRelayTile(56, IC_VALVE, !lost && hp.valve,      0);
    if (!lost && hp.defrosting) drawRelayTile(82, IC_SNOW, blink, 0);
    const char* mode = s.workMode == 0 ? "AUTO" : "SAVE";
    bool saveNow = controlInSavePeriod();
    int mw = textW(mode, F_LABEL) + 12;
    if (saveNow) { lcd.drawRBox(146 - mw, 98, mw, 16, 3); lcd.setDrawColor(0); }
    else         lcd.drawRFrame(146 - mw, 98, mw, 16, 3);
    text(146 - mw / 2, 110, mode, F_LABEL, 1);
    lcd.setDrawColor(1);

    // ── Right column: outdoor / coil / exhaust ──────────────
    lcd.drawVLine(150, 17, 100);
    struct Row { const char* label; float v; bool ok; const char* err; };
    const Row rows[3] = {
        {"OUTDOOR", d.outdoor, d.outdoorOk, "E04"},
        {"COIL",    d.pipe,    d.pipeOk,    "E02"},
        {"EXHAUST", d.exhaust, d.exhaustOk, "E03"},
    };
    for (int i = 0; i < 3; i++) {
        int top = 17 + i * 33;
        bool hot = i == 2 && !lost && (hp.state == HpState::EXHAUST_PROTECT ||
                                       hp.error == HpError::EH_EXHAUST_ALARM);
        if (hot && blink) { lcd.drawBox(151, top, SW - 151, 33); lcd.setDrawColor(0); }
        text(156, top + 11, rows[i].label, F_LABEL);
        if (lost)              text(156, top + 29, "--.-", F_VAL);
        else if (!rows[i].ok)  text(156, top + 29, rows[i].err, F_VAL);
        else { fmtTemp(buf, sizeof(buf), rows[i].v, true); textTemp(156, top + 29, buf, F_VAL); }
        lcd.setDrawColor(1);
        if (i < 2) dottedHLine(153, top + 32, SW - 155);
    }

    // ── Bottom controls ─────────────────────────────────────
    lcd.drawHLine(0, 117, SW);
    bool on = systemOn();
    if (button(0, 119, 42, 41, nullptr, IC_POWER, on ? B_FILLED : B_OUTLINE)) {
        if (on) askConfirm("TURN OFF", "Stop heating?", "Runs the stop sequence", "HOLD TO STOP", IC_POWER, 1000, powerToggle);
        else    askConfirm("TURN ON", "Start heating?", "Runs the start sequence", "START", IC_POWER, 0, powerToggle);
        return;
    }
    if (repeatButton(45, 119, 40, 41, IC_MINUS)) changeSetpoint(-1);
    if (button(88, 119, 62, 41, nullptr, IC_NONE)) { openEdit(100); return; }
    {
        bool hl = held(88, 119, 62, 41);
        lcd.setDrawColor(hl ? 0 : 1);
        text(119, 130, "SETPOINT", F_TINY, 1);
        snprintf(buf, sizeof(buf), "%d", (int)roundf(s.setpoint));
        textTemp(119, 153, buf, F_VAL, 1);
        lcd.setDrawColor(1);
    }
    if (repeatButton(153, 119, 40, 41, IC_PLUS)) changeSetpoint(+1);
    if (button(196, 119, 44, 41, nullptr, IC_MENU)) goTo(Scr::MENU);
}

// ── Menu ────────────────────────────────────────────────────
static void defrostNow() {
    controlForceDefrost();
    displayToast("DEFROST", "Request sent", 2000);
}

static void factoryReset() {
    settingsFactoryReset();
    settingsSave();   // keep the "init" key: next boot stays paired and pushes the defaults
    showBlockingNotice("Factory reset", "Settings back to defaults", "Restarting...");
    delay(1500);
    ESP.restart();
}

static void drawMenu() {
    if (header("MENU")) { goBack(); return; }
    Settings& s = settingsGet();
    char modeLabel[16];
    snprintf(modeLabel, sizeof(modeLabel), "MODE %s", s.workMode == 0 ? "AUTO" : "SAVE");
    struct Tile { const char* label; Icon ic; };
    const Tile tiles[9] = {
        {modeLabel, s.workMode == 0 ? IC_SUN : IC_MOON}, {"PARAMETERS", IC_GEAR}, {"SAVE TIMES", IC_CLOCK},
        {"DEFROST", IC_SNOW},  {"SENSORS", IC_THERMO}, {"WIFI", IC_WIFI},
        {"MQTT", IC_HOME},     {"SCREEN", IC_SCREEN},  {"RESET", IC_RESET},
    };
    for (int i = 0; i < 9; i++) {
        int x = (i % 3) * 80, y = 25 + (i / 3) * 45;
        if (!button(x, y, 79, 44, tiles[i].label, tiles[i].ic, B_OUTLINE, F_LABEL)) continue;
        switch (i) {
        case 0:
            s.workMode = s.workMode == 0 ? 1 : 0;
            settingsSave();
            displayToast(s.workMode == 0 ? "MODE: AUTO" : "MODE: SAVE",
                         s.workMode == 0 ? "Heats whenever needed" : "Heats only in SAVE times", 2000);
            break;
        case 1: goTo(Scr::PARAMS); break;
        case 2: goTo(Scr::PERIODS); break;
        case 3: askConfirm("DEFROST", "Force a defrost?", "Starts the defrost cycle now", "DEFROST NOW",
                           IC_SNOW, 0, defrostNow); break;
        case 4: goTo(Scr::SENSORS); break;
        case 5: goTo(Scr::WIFI); break;
        case 6: goTo(Scr::MQTT); break;
        case 7: goTo(Scr::SCREEN); break;
        case 8: askConfirm("RESET", "Factory reset?", "Defaults, heating OFF", "HOLD TO RESET",
                           IC_RESET, 2000, factoryReset); break;
        }
        return;
    }
}

// ── Parameters ──────────────────────────────────────────────
static int g_paramPage = 0;

static void drawParams() {
    char right[8];
    snprintf(right, sizeof(right), "%d / 3", g_paramPage + 1);
    if (header("PARAMETERS", right)) { goBack(); return; }
    for (int r = 0; r < 4; r++) {
        int id = g_paramPage * 4 + r;
        const ParamDef& p = k_params[id];
        int y = 24 + r * 34;
        bool hl = held(0, y, 196, 33);
        if (hl) lcd.drawRBox(0, y, 196, 33, 3);
        lcd.setDrawColor(hl ? 0 : 1);
        text(4, y + 14, p.code, F_BTN);
        text(4, y + 28, p.name, F_SMALL);
        char v[24];
        const uint8_t* f = p.names ? F_LABEL : F_MED;
        if (p.names) {
            // Short forms of the editor's option names, to fit the row
            static const char* const shortValve[] = {"Heat OFF", "Heat ON"};
            static const char* const shortExh[]   = {"OFF", "Fan runs", "Fan stops"};
            int iv = constrain((int)paramValue(id), 0, (int)p.max);
            strlcpy(v, id == 7 ? shortValve[iv] : (id == 8 ? shortExh[iv] : p.names[iv]), sizeof(v));
            text(192, y + 21, v, f, 2);
        } else if (p.isTemp) {
            snprintf(v, sizeof(v), "%.*f", p.dec, paramValue(id));
            textTemp(192, y + 22, v, f, 2);
        } else {
            snprintf(v, sizeof(v), "%.*f", p.dec, paramValue(id));
            int uw = textW(p.unit, F_TINY);
            text(190 - uw - 2, y + 22, v, f, 2);
            text(192 - uw, y + 22, p.unit, F_TINY);
        }
        lcd.setDrawColor(1);
        if (r < 3) dottedHLine(2, y + 33, 192);
        if (clicked(0, y, 196, 33)) { openEdit(id); return; }
    }
    lcd.drawVLine(197, 23, SH - 23);
    if (button(200, 25, 40, 66, nullptr, IC_UP) && g_paramPage > 0)   { g_paramPage--; startSlide(-1, true); }
    if (button(200, 94, 40, 66, nullptr, IC_DOWN) && g_paramPage < 2) { g_paramPage++; startSlide(+1, true); }
}

// ── SAVE periods ────────────────────────────────────────────
static int      g_periodIdx = 0, g_periodField = 0;
static Period   g_pe;
static uint32_t g_swAnim[3] = {};

static void drawPeriods(uint32_t now) {
    Settings& s = settingsGet();
    if (header("SAVE TIMES", s.workMode == 0 ? "mode is AUTO" : "mode is SAVE")) { goBack(); return; }
    char buf[24];
    for (int i = 0; i < 3; i++) {
        Period& p = s.period[i];
        int y = 25 + i * 45;
        float t = min(1.0f, (now - g_swAnim[i]) / 150.0f);
        float pos = p.enabled ? t : 1 - t;
        drawSwitch(6, y + 12, p.enabled, pos);
        if (clicked(0, y, 52, 44)) {
            p.enabled = !p.enabled;
            g_swAnim[i] = now;
            settingsSave();
        }
        bool hl = held(54, y, 186, 44);
        if (hl) lcd.drawRBox(54, y, 186, 44, 4);
        else    lcd.drawRFrame(54, y, 186, 44, 4);
        lcd.setDrawColor(hl ? 0 : 1);
        snprintf(buf, sizeof(buf), "P%d", i + 1);
        text(62, y + 14, buf, F_LABEL);
        text(62, y + 36, p.enabled ? "active" : "off", F_TINY);
        snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d", p.onHour, p.onMin, p.offHour, p.offMin);
        text(226, y + 29, buf, F_VAL, 2);
        lcd.setDrawColor(1);
        if (clicked(54, y, 186, 44)) {
            g_periodIdx = i;
            g_pe = p;
            g_periodField = 0;
            goTo(Scr::PERIOD);
            return;
        }
    }
}

static void stepPeriod(int dir) {
    uint8_t* f[4] = {&g_pe.onHour, &g_pe.onMin, &g_pe.offHour, &g_pe.offMin};
    int mod = (g_periodField % 2 == 0) ? 24 : 60;
    *f[g_periodField] = (uint8_t)((*f[g_periodField] + dir + mod) % mod);
}

static void drawPeriod() {
    char title[16];
    snprintf(title, sizeof(title), "PERIOD %d", g_periodIdx + 1);
    if (header(title)) { goBack(); return; }
    uint8_t vals[4] = {g_pe.onHour, g_pe.onMin, g_pe.offHour, g_pe.offMin};
    for (int row = 0; row < 2; row++) {
        int y = 28 + row * 44;
        text(6, y + 24, row == 0 ? "ON" : "OFF", F_LABEL);
        for (int k = 0; k < 2; k++) {
            int idx = row * 2 + k;
            int x = 36 + k * 64;
            bool sel = g_periodField == idx;
            if (sel) lcd.drawRBox(x, y, 54, 38, 4);
            else     lcd.drawRFrame(x, y, 54, 38, 4);
            char v[4];
            snprintf(v, sizeof(v), "%02d", vals[idx]);
            lcd.setDrawColor(sel ? 0 : 1);
            text(x + 27, y + 31, v, F_NUM, 1);
            lcd.setDrawColor(1);
            if (clicked(x, y, 54, 38)) g_periodField = idx;
        }
        lcd.drawBox(94, y + 13, 3, 3);
        lcd.drawBox(94, y + 23, 3, 3);
    }
    if (repeatButton(168, 26, 70, 42, IC_UP))   stepPeriod(+1);
    if (repeatButton(168, 72, 70, 42, IC_DOWN)) stepPeriod(-1);
    if (button(2, 118, 116, 40, "CANCEL")) { goBack(); return; }
    if (button(122, 118, 116, 40, "SAVE", IC_NONE, B_FILLED)) {
        Settings& s = settingsGet();
        g_pe.enabled = s.period[g_periodIdx].enabled;
        s.period[g_periodIdx] = g_pe;
        settingsSave();
        displayToast("SAVED", title, 1500);
        goBack();
    }
}

// ── Sensors ─────────────────────────────────────────────────
static void drawSensors() {
    bool lost = linkLost();
    if (header("SENSORS", lost ? "no link" : "live")) { goBack(); return; }
    const SensorData& d = sensorsGet();
    const HpStatus& hp = controlStatus();
    struct Card { const char* label; float v; bool ok; const char* err; };
    const Card cards[4] = {
        {"WATER",   sensorCorrectedWater(), d.waterOk,   "E01"},
        {"OUTDOOR", d.outdoor,              d.outdoorOk, "E04"},
        {"COIL",    d.pipe,                 d.pipeOk,    "E02"},
        {"EXHAUST", d.exhaust,              d.exhaustOk, "E03"},
    };
    char buf[48];
    for (int i = 0; i < 4; i++) {
        int x = (i % 2) * 121, y = 26 + (i / 2) * 46;
        lcd.drawRFrame(x, y, 119, 43, 4);
        text(x + 7, y + 13, cards[i].label, F_LABEL);
        if (lost)              text(x + 112, y + 36, "--.-", F_VAL, 2);
        else if (!cards[i].ok) text(x + 112, y + 36, cards[i].err, F_VAL, 2);
        else { fmtTemp(buf, sizeof(buf), cards[i].v, true); textTemp(x + 112, y + 36, buf, F_VAL, 2); }
    }
    snprintf(buf, sizeof(buf), "Compressor %s   Fan %s   Valve %s",
             hp.compressor ? "ON" : "off", hp.fan ? "ON" : "off", hp.valve ? "ON" : "off");
    text(4, 132, buf, F_SMALL);
    snprintf(buf, sizeof(buf), "Compressor run %u min   Defrosts %u",
             (unsigned)hp.compRunMinutes, (unsigned)hp.defrostCount);
    text(4, 148, buf, F_SMALL);
}

// ── WiFi ────────────────────────────────────────────────────
static void startPortal() {
    showBlockingNotice("WiFi setup is open", "Connect a phone to " AP_NAME, "then open 192.168.4.1");
    wifiStartConfigPortal();
    g_lastAct = millis();
}

static void forgetWifi() {
    showBlockingNotice("WiFi forgotten", "Restarting...", "");
    WiFi.disconnect(true, true);
    delay(1000);
    ESP.restart();
}

static void drawWifi() {
    if (header("WIFI")) { goBack(); return; }
    char buf[48];
    bool en = wifiIsEnabled(), con = wifiConnected();
    drawIcon(IC_WIFI, 22, 48, 1);
    if (!con) lcd.drawLine(10, 62, 34, 34);
    text(46, 42, !en ? "WiFi is off" : (con ? "Connected" : "Not connected"), F_MED);
    if (con) {
        snprintf(buf, sizeof(buf), "%s   %d dBm", WiFi.SSID().c_str(), (int)WiFi.RSSI());
        text(46, 60, buf, F_SMALL);
        snprintf(buf, sizeof(buf), "%s   http://%s.local", WiFi.localIP().toString().c_str(), WEB_HOSTNAME);
        text(46, 76, buf, F_SMALL);
    } else {
        text(46, 60, en ? "Use SETUP to choose a network" : "ESP-NOW link still works", F_SMALL);
    }
    text(4, 104, "ESP-NOW to the controller runs either way.", F_TINY);
    if (button(2, 116, 76, 42, en ? "TURN OFF" : "TURN ON", IC_NONE, en ? B_OUTLINE : B_FILLED, F_LABEL)) {
        if (en) wifiDisable(); else wifiEnable();
        displayToast(en ? "WIFI OFF" : "WIFI ON", en ? nullptr : "Connecting...", 1500);
    }
    if (button(82, 116, 76, 42, "SETUP", IC_NONE, B_OUTLINE, F_LABEL)) {
        askConfirm("WIFI SETUP", "Open setup network?", "Up to 5 min, screen paused", "OPEN", IC_WIFI, 0, startPortal);
        return;
    }
    if (button(162, 116, 76, 42, "FORGET", IC_NONE, B_OUTLINE, F_LABEL)) {
        askConfirm("FORGET WIFI", "Forget the network?", "The remote restarts", "HOLD TO FORGET", IC_WIFI, 1500, forgetWifi);
    }
}

// ── MQTT ────────────────────────────────────────────────────
static void drawMqtt() {
    if (header("MQTT", mqttConnected() ? "connected" : "offline")) { goBack(); return; }
    const Settings& s = settingsGet();
    char buf[80];
    drawIcon(IC_HOME, 22, 46, 1);
    if (s.mqttHost[0]) snprintf(buf, sizeof(buf), "%s : %u", s.mqttHost, s.mqttPort);
    else               strlcpy(buf, "No broker set", sizeof(buf));
    text(46, 42, buf, F_BTN);
    snprintf(buf, sizeof(buf), "User: %s", s.mqttUser[0] ? s.mqttUser : "(none)");
    text(46, 58, buf, F_SMALL);
    text(4, 84, "Change the broker on the web page", F_SMALL);
    snprintf(buf, sizeof(buf), "http://%s.local  or in WIFI > SETUP", WEB_HOSTNAME);
    text(4, 98, buf, F_SMALL);
    if (button(2, 116, 236, 42, "SEND HOME ASSISTANT DISCOVERY", IC_NONE, B_OUTLINE, F_LABEL)) {
        if (mqttConnected()) { mqttPublishDiscovery(); displayToast("MQTT", "Discovery sent", 1500); }
        else                 displayToast("MQTT", "Not connected", 1500);
    }
}

// ── Screen settings + touch calibration ─────────────────────
static void calTarget(int x, int y, int n) {
    lcd.clearBuffer();
    text(SW / 2, 78, "Touch the target", F_BTN, 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "point %d of 3", n);
    text(SW / 2, 94, buf, F_SMALL, 1);
    lcd.drawCircle(x, y, 7);
    lcd.drawHLine(x - 11, y, 23);
    lcd.drawVLine(x, y - 11, 23);
    lcdSend();
}

static void calSample(int& ra, int& rb) {
    for (;;) {
        while (touchPressed()) delay(10);
        delay(150);
        while (!touchPressed()) delay(10);
        delay(60);
        long sa = 0, sb = 0; int n = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 300) {
            int a, b;
            if (touchReadRaw(a, b)) { sa += a; sb += b; n++; }
            delay(5);
        }
        while (touchPressed()) delay(10);
        if (n >= 10) { ra = sa / n; rb = sb / n; buzzerClick(); return; }
    }
}

// Blocking 3-point calibration; the main loop pauses meanwhile
static void runCalibration() {
    const int px[3] = {20, SW - 21, 20};
    const int py[3] = {20, 20, SH - 21};
    for (;;) {
        int ra[3], rb[3];
        for (int i = 0; i < 3; i++) {
            calTarget(px[i], py[i], i + 1);
            calSample(ra[i], rb[i]);
        }
        TouchCal c = {};
        c.swap = abs(rb[1] - rb[0]) > abs(ra[1] - ra[0]);
        int h0 = c.swap ? rb[0] : ra[0], h1 = c.swap ? rb[1] : ra[1];
        int v0 = c.swap ? ra[0] : rb[0], v2 = c.swap ? ra[2] : rb[2];
        int dh = h1 - h0, dv = v2 - v0;
        if (abs(dh) < 200 || abs(dv) < 200) {
            lcd.clearBuffer();
            text(SW / 2, 80, "Calibration failed, again", F_BTN, 1);
            lcdSend();
            delay(1500);
            continue;
        }
        c.ax = float(px[1] - px[0]) / dh;  c.bx = px[0] - c.ax * h0;
        c.ay = float(py[2] - py[0]) / dv;  c.by = py[0] - c.ay * v0;
        touchSaveCal(c);
        T = {};
        g_lastAct = millis();
        displayToast("TOUCH", "Calibrated", 1500);
        return;
    }
}

static void saveUiPrefs() {
    Preferences p;
    p.begin("hpui", false);
    p.putInt("contrast", g_contrast);
    p.putInt("bl", g_backlight);
    p.putInt("dim", g_dimLevel);
    p.end();
}

// Backlight slider row; returns true while the finger is on it
static bool lightSlider(const char* label, int y, int& value, int minPct) {
    const int sx = 50, sw = 188, sh = 22;
    text(4, y + 16, label, F_LABEL);
    lcd.drawRFrame(sx, y, sw, sh, 4);
    bool on = held(sx, y - 3, sw, sh + 6);
    if (on) {
        value = constrain(map(T.x, sx + 4, sx + sw - 5, minPct, 100), minPct, 100);
        g_uiSaveAt = millis() + 1000;
    }
    lcd.drawBox(sx + 3, y + 3, (sw - 6) * value / 100, sh - 6);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", value);
    lcd.setDrawColor(2); text(sx + sw / 2, y + 15, buf, F_LABEL, 1); lcd.setDrawColor(1);
    return on;
}

static void drawScreenSettings() {
    if (header("SCREEN")) { goBack(); return; }
    char buf[64];
    text(4, 44, "CONTRAST", F_LABEL);
    if (repeatButton(98, 25, 44, 30, IC_MINUS)) { g_contrast = max(20, g_contrast - 1); lcdSetContrast(g_contrast); g_uiSaveAt = millis() + 1000; }
    snprintf(buf, sizeof(buf), "%d", g_contrast);
    text(170, 47, buf, F_VAL, 1);
    if (repeatButton(196, 25, 44, 30, IC_PLUS))  { g_contrast = min(90, g_contrast + 1); lcdSetContrast(g_contrast); g_uiSaveAt = millis() + 1000; }

    if (lightSlider("LIGHT", 59, g_backlight, 5)) lcdSetBacklight(g_backlight);

    // Idle level after DIM_AFTER_MS; previewed while the finger is on the slider
    bool dimHeld = lightSlider("DIM", 85, g_dimLevel, 1);
    if (dimHeld) {
        lcdSetBacklight(g_dimLevel);
        g_dimPreview = true;
    } else if (g_dimPreview) {
        lcdSetBacklight(g_backlight);
        g_dimPreview = false;
    }

    char ver[16] = "", built[24] = "";
    espnowCtrlFirmware(ver, sizeof(ver), built, sizeof(built));
    snprintf(buf, sizeof(buf), "v%s  %s  ctrl %s%s", g_fwInfo.version, WiFi.macAddress().c_str(),
             ver[0] ? ver : "?", g_paired ? "" : "  NOT PAIRED");
    text(4, 115, buf, F_TINY);
    if (button(2, 119, 236, 40, "CALIBRATE TOUCH", IC_NONE, B_OUTLINE, F_BTN)) runCalibration();
}

// ── Boot splash ─────────────────────────────────────────────
static void drawBoot(uint32_t now) {
    int cx = SW / 2, cy = 44;
    // Water drop with heat waves cut into it
    lcd.drawDisc(cx, cy + 6, 16);
    lcd.drawTriangle(cx, cy - 26, cx - 15, cy, cx + 15, cy);
    lcd.setDrawColor(0);
    for (int i = -1; i <= 1; i++) {
        static const int8_t wave[8] = {0, 1, 2, 1, 0, -1, -2, -1};
        for (int y = -8; y <= 12; y++) lcd.drawBox(cx + i * 7 + wave[(y + 16 + now / 120) & 7], cy + y, 2, 1);
    }
    lcd.setDrawColor(1);
    text(cx, 98, "HEAT PUMP", F_VAL, 1);
    char buf[32];
    snprintf(buf, sizeof(buf), "touch remote  v%s", g_fwInfo.version);
    text(cx, 113, buf, F_SMALL, 1);
    lcd.drawRFrame(60, 124, 120, 8, 3);
    int pos = (now / 12) % 140;
    lcd.setClipWindow(62, 126, 178, 130);
    lcd.drawBox(42 + pos, 126, 30, 4);
    lcd.setMaxClipWindow();
    text(cx, 150, g_paired ? "Connecting to the controller" : "Not paired: own MAC, view only", F_TINY, 1);
}

// ── Alarm banner ────────────────────────────────────────────
static uint32_t g_alarmAck = 0;

static uint32_t alarmKey(const HpStatus& hp) {
    if (linkLost()) return 0;
    uint32_t k = (uint32_t)hp.error << 8;
    if (hp.state == HpState::EXHAUST_PROTECT) k |= 1;
    return k;
}

static bool alarmVisible() {
    uint32_t k = alarmKey(controlStatus());
    if (k == 0) g_alarmAck = 0;
    return k != 0 && k != g_alarmAck;
}

static void drawAlarm(uint32_t now) {
    const HpStatus& hp = controlStatus();
    const SensorData& d = sensorsGet();
    const char* title = "EXHAUST HOT";
    const char* l1 = "Compressor paused";
    switch (hp.error) {
    case HpError::E01_WATER_SENSOR:   title = "WATER SENSOR";   l1 = "E01: sensor fault"; break;
    case HpError::E02_PIPE_SENSOR:    title = "COIL SENSOR";    l1 = "E02: sensor fault"; break;
    case HpError::E03_EXHAUST_SENSOR: title = "EXHAUST SENSOR"; l1 = "E03: sensor fault"; break;
    case HpError::E04_OUTDOOR_SENSOR: title = "OUTDOOR SENSOR"; l1 = "E04: sensor fault"; break;
    case HpError::EH_EXHAUST_ALARM:   title = "EXHAUST ALARM";  l1 = "EH: exhaust too hot"; break;
    default: break;
    }
    const int x = 14, y = 22, w = SW - 28, h = 116;
    lcd.setDrawColor(0); lcd.drawBox(x - 2, y - 2, w + 4, h + 4);
    lcd.setDrawColor(1);
    lcd.drawRFrame(x, y, w, h, 6);
    if ((now / 400) & 1) { lcd.drawRFrame(x + 1, y + 1, w - 2, h - 2, 5); lcd.drawRFrame(x + 2, y + 2, w - 4, h - 4, 4); }
    drawIcon(IC_WARN, x + 26, y + 32, 1);
    text(x + 50, y + 28, title, F_MED);
    text(x + 50, y + 46, l1, F_SMALL);
    char buf[40];
    if (d.exhaustOk) {
        snprintf(buf, sizeof(buf), "Exhaust %.0f, limit %.0f", d.exhaust, settingsGet().u51);
        textTemp(x + 50, y + 62, buf, F_SMALL);
    }
    text(x + w / 2, y + 88, "The controller handles it.", F_SMALL, 1);
    text(x + w / 2, y + 106, "Tap to dismiss", F_LABEL, 1);
}

// ── Serial screenshots (for the README) ─────────────────────
// "shot" prints the last frame as hex, one pixel row per line.
// "go <screen>" opens a screen without touching the panel.
static void serialCommands() {
    if (!Serial.available()) return;
    String c = Serial.readStringUntil('\n');
    c.trim();
    if (c == "shot") {
        Serial.printf("SHOT %d %d\n", SW, SH);
        for (int y = 0; y < SH; y++) {
            for (int i = 0; i < LCD_ROW_BYTES; i++) Serial.printf("%02X", g_lastFrame[y * LCD_ROW_BYTES + i]);
            Serial.println();
        }
        Serial.println("END");
        return;
    }
    if (!c.startsWith("go ")) return;
    String s = c.substring(3);
    g_lastAct = millis();
    g_depth = 0;
    g_scr = Scr::MAIN;
    if      (s == "menu")    goTo(Scr::MENU);
    else if (s == "params")  goTo(Scr::PARAMS);
    else if (s == "edit")    openEdit(1);
    else if (s == "periods") goTo(Scr::PERIODS);
    else if (s == "period")  { g_periodIdx = 0; g_pe = settingsGet().period[0]; g_periodField = 0; goTo(Scr::PERIOD); }
    else if (s == "sensors") goTo(Scr::SENSORS);
    else if (s == "screen")  goTo(Scr::SCREEN);
    else if (s == "confirm") askConfirm("TURN OFF", "Stop heating?", "Runs the stop sequence", "HOLD TO STOP", IC_POWER, 1000, powerToggle);
    else if (s == "boot")    g_scr = Scr::BOOT;
}

// ── Public ──────────────────────────────────────────────────
void uiBegin(bool paired) {
    g_paired = paired;
    Preferences p;
    p.begin("hpui", true);
    g_contrast  = p.getInt("contrast", 50);
    g_backlight = p.getInt("bl", 80);
    g_dimLevel  = p.getInt("dim", 25);
    p.end();

    touchBegin();
    lcdBegin();
    lcdSetContrast(g_contrast);
    lcdSetBacklight(g_backlight);
    if (touchPressed()) runCalibration();   // finger held at power-up = recalibrate

    lcd.clearBuffer();
    drawBoot(0);
    memcpy(g_lastFrame, lcd.getBufferPtr(), LCD_BUF_BYTES);
    lcdSend();
    g_lastAct = millis();
}

void uiLoop() {
    uint32_t now = millis();

    if (g_spSaveAt && (int32_t)(now - g_spSaveAt) >= 0) { g_spSaveAt = 0; settingsSave(); }
    if (g_uiSaveAt && (int32_t)(now - g_uiSaveAt) >= 0) { g_uiSaveAt = 0; saveUiPrefs(); }

    if (now - g_lastFrameMs < FRAME_MS) return;
    g_lastFrameMs = now;
    serialCommands();
    readTouch(now);

    // Any touch wakes the backlight; the waking tap itself does nothing
    if (T.down) {
        g_lastAct = now;
        if (g_dimmed) {
            g_dimmed = false;
            lcdSetBacklight(g_backlight);
            if (T.pressed) T.consumed = true;
        }
    }
    if (!g_dimmed && now - g_lastAct > DIM_AFTER_MS) {
        g_dimmed = true;
        lcdSetBacklight(g_dimLevel);
    }
    if (g_scr != Scr::MAIN && now - g_lastAct > MENU_TIMEOUT_MS) goHome();
    if (g_scr == Scr::BOOT && now > 2500 && now < 10000) { g_depth = 0; g_scr = Scr::MAIN; startSlide(+1); }

    bool sliding = g_tr.ms && now - g_tr.start < g_tr.ms;
    if (!sliding) g_tr.ms = 0;
    bool alarm = g_scr != Scr::BOOT && alarmVisible();
    if (alarm && T.released && !T.consumed) {
        T.consumed = true;
        g_alarmAck = alarmKey(controlStatus());
        alarm = false;
    }
    g_inputOn = !sliding && !alarm;

    lcd.clearBuffer();
    lcd.setDrawColor(1);
    switch (g_scr) {
    case Scr::BOOT:    drawBoot(now);         break;
    case Scr::MAIN:    drawMain(now);         break;
    case Scr::MENU:    drawMenu();            break;
    case Scr::PARAMS:  drawParams();          break;
    case Scr::EDIT:    drawEdit(now);         break;
    case Scr::PERIODS: drawPeriods(now);      break;
    case Scr::PERIOD:  drawPeriod();          break;
    case Scr::SENSORS: drawSensors();         break;
    case Scr::WIFI:    drawWifi();            break;
    case Scr::MQTT:    drawMqtt();            break;
    case Scr::SCREEN:  drawScreenSettings();  break;
    case Scr::CONFIRM: drawConfirm();         break;
    }
    lcd.setMaxClipWindow();
    lcd.setDrawColor(1);

    // A tap inside this frame may have started a new slide: show its first step next frame
    if (g_tr.ms && now - g_tr.start < g_tr.ms) composeSlide((now - g_tr.start) / (float)g_tr.ms);

    if (alarm) drawAlarm(now);
    drawToast(now);
    memcpy(g_lastFrame, lcd.getBufferPtr(), LCD_BUF_BYTES);
    lcdSend();
}

#endif // ROLE_REMOTE && REMOTE_S3
