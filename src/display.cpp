#include "display.h"
#include "config.h"
#include "control.h"
#include "sensors.h"
#include "settings.h"
#include "wifi_mqtt.h"
#include "espnow_remote.h"
#include <U8g2lib.h>
#include <SPI.h>

// ST7567 128x64 Hardware SPI.
// SPI.begin() is called manually with MISO=-1 so GPIO19 (UP button) is NOT claimed.
static U8G2_ST7567_JLX12864_F_4W_HW_SPI u8g2(U8G2_R2, LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN);

static bool     g_inMenu       = false;
static char     g_toastLine1[24] = {};
static char     g_toastLine2[24] = {};
static uint32_t g_toastUntil   = 0;

// ── State name string ──────────────────────────────────────
static const char* stateStr(HpState s) {
    switch (s) {
    case HpState::OFFLINE:          return "OFFLINE";
    case HpState::STANDBY:          return "STANDBY";
    case HpState::STARTUP_FAN:      return "FAN... ";
    case HpState::COMP_DELAY:       return "DELAY  ";
    case HpState::HEATING:          return "HEATING";
    case HpState::SHUTDOWN_COMP:    return "STOP...";
    case HpState::SHUTDOWN_FAN:     return "STOP...";
    case HpState::DEFROST_STOP:     return "DEFROST";
    case HpState::DEFROST_VALVE:    return "DEFROST";
    case HpState::DEFROSTING:       return "DEFROST";
    case HpState::DEFROST_END_STOP: return "DEF-END";
    case HpState::DEFROST_END_VALVE:return "DEF-END";
    case HpState::ANTIFREEZE_ON:    return "ANTIFR.";
    case HpState::ANTIFREEZE_WAIT:  return "ANTIFR.";
    case HpState::EXHAUST_PROTECT:  return "PROTECT";
    default:                        return "UNKNOWN";
    }
}

static const char* errorStr(HpError e) {
    switch (e) {
    case HpError::E01_WATER_SENSOR:   return "E01";
    case HpError::E02_PIPE_SENSOR:    return "E02";
    case HpError::E03_EXHAUST_SENSOR: return "E03";
    case HpError::E04_OUTDOOR_SENSOR: return "E04";
    case HpError::EH_EXHAUST_ALARM:   return " EH";
    default:                          return "";
    }
}

// ── Draw main screen ───────────────────────────────────────
static void drawMainScreen() {
    const HpStatus&  hp = controlStatus();
    const SensorData& d = sensorsGet();
    Settings& s         = settingsGet();
    char buf[32];

    u8g2.clearBuffer();

    // ── TOP BAR (y=9): state | error | W M | time ──────────
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 9, stateStr(hp.state));
    if (hp.error != HpError::NONE)
        u8g2.drawStr(48, 9, errorStr(hp.error));
    if (espnowCtrlSignalLost()) {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(123, 9, "!");
        u8g2.setFont(u8g2_font_6x10_tr);
    }
    if (wifiConnected()) u8g2.drawStr(69, 9, "W");
    if (mqttConnected()) u8g2.drawStr(77, 9, "MQ");
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        u8g2.drawStr(93, 9, buf);
    }
    u8g2.drawHLine(0, 11, 128);

    // ── VERTICAL DIVIDER at x=77 ────────────────────────────
    u8g2.drawVLine(77, 12, 35);

    // ── LEFT ZONE: water temperature (large) ────────────────
    if (d.waterOk) {
        // Split integer and decimal to manually tighten the dot spacing
        float wt = sensorCorrectedWater();
        int ival = (int)floorf(wt);
        int fval = (int)roundf((wt - floorf(wt)) * 10.0f);
        if (fval == 10) { fval = 0; ival++; }
        char ibuf[8], fbuf[4];
        snprintf(ibuf, sizeof(ibuf), "%d", ival);
        snprintf(fbuf, sizeof(fbuf), ".%d", fval);

        u8g2.setFont(u8g2_font_inb27_mn);
        int iw = u8g2.getStrWidth(ibuf);
        u8g2.setFont(u8g2_font_10x20_tr);
        int fw = u8g2.getStrWidth(fbuf);

        int totalW = iw + fw;
        int x = (66 - totalW) / 2;
        if (x < 0) x = 0;

        u8g2.setFont(u8g2_font_inb27_mn);
        u8g2.drawStr(x, 43, ibuf);
        u8g2.setFont(u8g2_font_10x20_tr);
        u8g2.drawStr(x + iw, 43, fbuf);

        u8g2.setFont(u8g2_font_8x13_tr);
        int cxPos = x + totalW + 2;
        if (cxPos > 66) cxPos = 66;
        u8g2.drawStr(cxPos, 26, "C\xb0");
    } else {
        u8g2.setFont(u8g2_font_inb27_mn);
        u8g2.drawStr(4, 43, "---");
    }

    // ── RIGHT ZONE: setpoint / outdoor / pipe ───────────────
    // Each line: value in font_6x10, °C suffix in font_5x7
    auto drawRightLine = [&](int y, const char* valStr) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(80, y, valStr);
        int w = u8g2.getStrWidth(valStr);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(80 + w + 2, y, "\xb0""C");
    };

    snprintf(buf, sizeof(buf), "Sp:%d", (int)roundf(s.setpoint));
    drawRightLine(21, buf);

    if (d.outdoorOk) {
        snprintf(buf, sizeof(buf), "Ot:%.1f", d.outdoor);
        drawRightLine(29, buf);
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(80, 29, "Ot:E04");
    }

    if (d.pipeOk) {
        snprintf(buf, sizeof(buf), "Pp:%.1f", d.pipe);
        drawRightLine(37, buf);
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(80, 37, "Pp:E02");
    }

    if (d.exhaustOk) {
        snprintf(buf, sizeof(buf), "Ex:%.1f", d.exhaust);
        drawRightLine(45, buf);
    } else {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(80, 45, "Ex:E03");
    }

    // ── BOTTOM BAR: relay status + mode + defrost ───────────
    u8g2.drawHLine(0, 46, 128);
    u8g2.setFont(u8g2_font_6x10_tr);

    // Draw each relay label — filled box + white text when active
    auto drawRelay = [&](int x, const char* label, bool active) {
        int w = u8g2.getStrWidth(label) + 4;
        if (active) {
            u8g2.drawBox(x, 48, w, 13);
            u8g2.setDrawColor(0);
        }
        u8g2.drawStr(x + 2, 59, label);
        u8g2.setDrawColor(1);
        return w;
    };

    bool compBlink = (hp.state == HpState::COMP_DELAY) && (millis() / 500 % 2);

    int rx = 0;
    rx += drawRelay(rx, "COMP", hp.compressor || compBlink) + 2;
    rx += drawRelay(rx, "FAN",  hp.fan)        + 2;
    rx += drawRelay(rx, "VLV",  hp.valve)      + 2;

    // Mode + defrost (small, right side)
    u8g2.setFont(u8g2_font_6x10_tr);
    snprintf(buf, sizeof(buf), "%s%s",
        s.workMode == 0 ? "AUTO" : "SAVE",
        hp.defrosting ? " DEF" : "");
    u8g2.drawStr(91, 58, buf);

    // ── TOAST overlay ───────────────────────────────────────
    if (millis() < g_toastUntil) {
        const int W = 100, H = g_toastLine2[0] ? 26 : 16;
        const int X = (128 - W) / 2, Y = (64 - H) / 2;
        u8g2.setDrawColor(0);
        u8g2.drawBox(X, Y, W, H);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(X, Y, W, H);
        u8g2.setFont(u8g2_font_6x10_tr);
        if (g_toastLine1[0])
            u8g2.drawStr(X + 4, Y + 11, g_toastLine1);
        if (g_toastLine2[0])
            u8g2.drawStr(X + 4, Y + 23, g_toastLine2);
        u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();
}

// ── Public interface ───────────────────────────────────────
void displayInit() {
    // Backlight PWM
    ledcSetup(LCD_BL_CHANNEL, LCD_BL_FREQ, LCD_BL_RES);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DEFAULT);

    // Init SPI with MISO=-1 so GPIO19 (UP button) is NOT configured as MISO
    SPI.begin(LCD_SCK_PIN, -1, LCD_SDI_PIN, LCD_CS_PIN);
    u8g2.begin();
    u8g2.setContrast(LCD_CONTRAST);
    u8g2.clearDisplay();
}

void displaySetBrightness(uint8_t level) {
    ledcWrite(LCD_BL_CHANNEL, level);
}

void displayDim()    { ledcWrite(LCD_BL_CHANNEL, LCD_BL_DIM); }
void displayBright() { ledcWrite(LCD_BL_CHANNEL, LCD_BL_DEFAULT); }

void displayUpdate() {
    if (g_inMenu) return;
    drawMainScreen();
}

void displayShowMenu(const char* title,
                     const char* lines[],
                     int         numLines,
                     int         selected,
                     int         scroll) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    // Title bar
    u8g2.drawBox(0, 0, 128, 11);
    u8g2.setDrawColor(0);
    u8g2.drawStr(2, 9, title);
    u8g2.setDrawColor(1);

    // List items (5 visible lines, each 10px high, starting at y=12)
    const int ITEM_H  = 10;
    const int VISIBLE = 5;
    for (int i = 0; i < VISIBLE; i++) {
        int idx = scroll + i;
        if (idx >= numLines) break;
        int y = 12 + i * ITEM_H + 9;
        if (idx == selected) {
            u8g2.drawBox(0, 12 + i * ITEM_H, 128, ITEM_H);
            u8g2.setDrawColor(0);
        }
        u8g2.drawStr(2, y, lines[idx]);
        u8g2.setDrawColor(1);
    }

    // Scroll indicator
    if (numLines > VISIBLE) {
        int barH = 50 * VISIBLE / numLines;
        int barY = 12 + 50 * scroll / numLines;
        u8g2.drawBox(125, barY, 3, barH);
    }

    u8g2.sendBuffer();
}

void displayShowEdit(const char* paramName,
                     const char* valueStr,
                     const char* hint) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);

    u8g2.drawBox(0, 0, 128, 11);
    u8g2.setDrawColor(0);
    u8g2.drawStr(2, 9, "EDIT");
    u8g2.setDrawColor(1);

    u8g2.drawStr(0, 25, paramName);
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(10, 48, valueStr);
    u8g2.setFont(u8g2_font_5x7_tr);

    if (hint) u8g2.drawStr(0, 63, hint);
    else      u8g2.drawStr(0, 63, "UP/DN:chg ENTER:ok BACK:cancel");

    u8g2.sendBuffer();
}

void displayShowMessage(const char* line1, const char* line2, const char* line3) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    if (line1) u8g2.drawStr(0, 20, line1);
    if (line2) u8g2.drawStr(0, 35, line2);
    if (line3) u8g2.drawStr(0, 50, line3);
    u8g2.sendBuffer();
}

bool displayInMenu()         { return g_inMenu; }
void displaySetMenu(bool v)  { g_inMenu = v; }

void displayToast(const char* line1, const char* line2, uint32_t durationMs) {
    strncpy(g_toastLine1, line1 ? line1 : "", sizeof(g_toastLine1) - 1);
    strncpy(g_toastLine2, line2 ? line2 : "", sizeof(g_toastLine2) - 1);
    g_toastUntil = millis() + durationMs;
}
