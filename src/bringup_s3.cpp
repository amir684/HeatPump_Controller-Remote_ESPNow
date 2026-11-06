// ── ESP32-S3 hardware bring-up (env:s3_bringup) ─────────────
// Standalone test for the new Remote hardware, before the real UI:
//   - ERC240160 image + live contrast tuning (saved to NVS)
//   - backlight PWM slider (check the LED current here)
//   - resistive touch with 3-point calibration (saved to NVS)
//   - orientation check: the arrow must point to the top of the enclosure
//   - shows the S3 MAC (needed for the Controller's peer list), flash, PSRAM
// Calibration + contrast use the "hpui" NVS namespace, which the real
// touch Remote will reuse — so they carry over.
// Based on ERC240160_ESP32_Touch.ino.

#ifdef S3_BRINGUP

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <WiFi.h>
#include "pins_s3.h"

static const int W = 240, H = 160;

U8G2_ST7586S_ERC240160_F_4W_HW_SPI u8g2(S3_LCD_ROTATION, S3_LCD_CS, S3_LCD_DC, S3_LCD_RST);
static Preferences prefs;

static char g_mac[18] = "";

// ── Backlight ───────────────────────────────────────────────
#define BL_CHANNEL 0
static int g_blPercent = 80;

static void setBacklight(int pct) {
    g_blPercent = constrain(pct, 0, 100);
    uint32_t duty = map(g_blPercent, 0, 100, 0, 255);
    ledcWrite(BL_CHANNEL, S3_LCD_BL_ACTIVE_LOW ? 255 - duty : duty);
}

// ── LCD init ────────────────────────────────────────────────
// U8g2's init skips the OTP load and the DDRAM clear and sends a second
// sleep-out; on this panel that gave a smeared, unreadable image. This is the
// ST7586S datasheet power-on flow, with the U8g2 ERC240160 register values.
static void lcdCmd(uint8_t cmd, const uint8_t* args = nullptr, int n = 0, uint32_t waitMs = 0) {
    u8x8_t* u = u8g2.getU8x8();
    u8x8_cad_StartTransfer(u);
    u8x8_cad_SendCmd(u, cmd);
    for (int i = 0; i < n; i++) u8x8_cad_SendArg(u, args[i]);
    u8x8_cad_EndTransfer(u);
    if (waitMs) delay(waitMs);
}

static void lcdInit() {
    digitalWrite(S3_LCD_RST, LOW);  delay(10);
    digitalWrite(S3_LCD_RST, HIGH); delay(120);
    { const uint8_t a[] = {0x9F}; lcdCmd(0xD7, a, 1); }       // Disable auto read
    { const uint8_t a[] = {0x00}; lcdCmd(0xE0, a, 1, 10); }   // Enable OTP read
    lcdCmd(0xE3, nullptr, 0, 20);                             // OTP up-load
    lcdCmd(0xE1);                                             // OTP control out
    lcdCmd(0x11);                                             // Sleep out
    lcdCmd(0x28, nullptr, 0, 50);                             // Display off
    { const uint8_t a[] = {0x36, 0x01}; lcdCmd(0xC0, a, 2); } // Vop 0x136
    { const uint8_t a[] = {0x00}; lcdCmd(0xC3, a, 1); }       // Bias 1/14
    { const uint8_t a[] = {0x07}; lcdCmd(0xC4, a, 1); }       // Booster x8
    { const uint8_t a[] = {0x1D}; lcdCmd(0xD0, a, 1); }       // Analog on
    { const uint8_t a[] = {0x00}; lcdCmd(0xB5, a, 1); }       // N-line 0
    lcdCmd(0x39);                                             // Monochrome
    { const uint8_t a[] = {0x02}; lcdCmd(0x3A, a, 1); }       // DDRAM interface
    { const uint8_t a[] = {0xC8}; lcdCmd(0x36, a, 1); }       // Scan direction (R0)
    { const uint8_t a[] = {0x9F}; lcdCmd(0xB0, a, 1); }       // Duty 1/160
    { const uint8_t a[] = {0x00}; lcdCmd(0xB1, a, 1); }       // First COM
    lcdCmd(0x20);                                             // Inversion off

    // Clear the whole DDRAM, 128 bytes x 160 rows, before display on
    const uint8_t col[] = {0x00, 0x00, 0x00, 0x7F};
    const uint8_t row[] = {0x00, 0x00, 0x00, 0x9F};
    static const uint8_t zeros[128] = {};
    lcdCmd(0x2A, col, 4);
    lcdCmd(0x2B, row, 4);
    u8x8_t* u = u8g2.getU8x8();
    u8x8_cad_StartTransfer(u);
    u8x8_cad_SendCmd(u, 0x2C);
    for (int r = 0; r < 160; r++) u8x8_cad_SendData(u, sizeof(zeros), (uint8_t*)zeros);
    u8x8_cad_EndTransfer(u);

    { const uint8_t a[] = {0x00, 0x08, 0x00, 0x7F}; lcdCmd(0x2A, a, 4); }   // Visible columns
    lcdCmd(0x2B, row, 4);
    lcdCmd(0x29, nullptr, 0, 50);                             // Display on
}

// ── Buzzer + comm LED test: every press clicks and toggles the LED ──
#define BUZ_CHANNEL 2   // own LEDC channel and timer, away from the backlight
static uint32_t g_beepEnd = 0;

static void beep(uint16_t freq, uint16_t ms) {
    ledcSetup(BUZ_CHANNEL, freq, 8);
    ledcAttachPin(S3_BUZZER, BUZ_CHANNEL);
    ledcWrite(BUZ_CHANNEL, 128);
    g_beepEnd = millis() + ms;
}

static void beepLoop() {
    if (g_beepEnd && (int32_t)(millis() - g_beepEnd) >= 0) {
        ledcWrite(BUZ_CHANNEL, 0);
        g_beepEnd = 0;
    }
}

// ── Contrast ────────────────────────────────────────────────
static int g_contrast = 54;   // driver init value (Vop 0x136); tune ~40..70

static void setContrast(int v) {
    g_contrast = constrain(v, 0, 255);
    u8g2.setContrast(g_contrast);
}

// ── Touch: low level ────────────────────────────────────────
static void tpRelease() {
    pinMode(S3_TP_XP, INPUT); pinMode(S3_TP_XM, INPUT);
    pinMode(S3_TP_YP, INPUT); pinMode(S3_TP_YM, INPUT);
}

// Layer 1 to GND, layer 2 pulled up: pressed → YP pulled LOW through the panel
static bool tpPressed() {
    tpRelease();
    pinMode(S3_TP_XM, OUTPUT); digitalWrite(S3_TP_XM, LOW);
    pinMode(S3_TP_YP, INPUT_PULLUP);
    delayMicroseconds(200);
    bool p = (digitalRead(S3_TP_YP) == LOW);
    tpRelease();
    return p;
}

// 9 samples, sort, average the middle 3 (rejects spikes)
static int readFiltered(uint8_t pin) {
    const int N = 9;
    int s[N];
    for (int i = 0; i < N; i++) s[i] = analogRead(pin);
    for (int i = 1; i < N; i++) {
        int k = s[i], j = i - 1;
        while (j >= 0 && s[j] > k) { s[j + 1] = s[j]; j--; }
        s[j + 1] = k;
    }
    return (s[3] + s[4] + s[5]) / 3;
}

static int tpReadA() {   // layer 1 = divider, probe on YP
    tpRelease();
    pinMode(S3_TP_XP, OUTPUT); pinMode(S3_TP_XM, OUTPUT);
    digitalWrite(S3_TP_XP, HIGH); digitalWrite(S3_TP_XM, LOW);
    delayMicroseconds(100);
    int v = readFiltered(S3_TP_YP);
    tpRelease();
    return v;
}

static int tpReadB() {   // layer 2 = divider, probe on XP
    tpRelease();
    pinMode(S3_TP_YP, OUTPUT); pinMode(S3_TP_YM, OUTPUT);
    digitalWrite(S3_TP_YP, HIGH); digitalWrite(S3_TP_YM, LOW);
    delayMicroseconds(100);
    int v = readFiltered(S3_TP_XP);
    tpRelease();
    return v;
}

// Valid only if still pressed after reading (discards samples taken during release)
static bool tpReadRaw(int& a, int& b) {
    if (!tpPressed()) return false;
    a = tpReadA();
    b = tpReadB();
    return tpPressed();
}

// ── Touch: calibration ──────────────────────────────────────
struct TouchCal {
    uint32_t magic;
    bool     swap;
    float    ax, bx, ay, by;   // x = ax*raw + bx, y = ay*raw + by
};
static const uint32_t CAL_MAGIC = 0xC0FFEE01;
static TouchCal g_cal;

static int g_rawA = 0, g_rawB = 0;   // last raw sample, for the Serial log

static bool tpGetPoint(int& x, int& y) {
    int a, b;
    if (!tpReadRaw(a, b)) return false;
    g_rawA = a;
    g_rawB = b;
    int h = g_cal.swap ? b : a;
    int v = g_cal.swap ? a : b;
    x = constrain((int)lroundf(g_cal.ax * h + g_cal.bx), 0, W - 1);
    y = constrain((int)lroundf(g_cal.ay * v + g_cal.by), 0, H - 1);
    return true;
}

static void drawTarget(int x, int y, const char* msg) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr((W - u8g2.getStrWidth(msg)) / 2, 85, msg);
    u8g2.drawCircle(x, y, 6);
    u8g2.drawHLine(x - 10, y, 21);
    u8g2.drawVLine(x, y - 10, 21);
    u8g2.sendBuffer();
}

static void waitRawSample(int& ra, int& rb) {
    for (;;) {
        while (tpPressed()) delay(10);      // wait for release
        delay(150);
        while (!tpPressed()) delay(10);     // wait for press
        delay(60);                          // let the press stabilise
        long sa = 0, sb = 0; int n = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 300) {
            int a, b;
            if (tpReadRaw(a, b)) { sa += a; sb += b; n++; }
            delay(5);
        }
        while (tpPressed()) delay(10);
        if (n >= 10) { ra = sa / n; rb = sb / n; return; }
    }
}

static void runCalibration() {
    const int px[3] = {20, W - 21, 20};
    const int py[3] = {20, 20, H - 21};
    int ra[3], rb[3];

    for (;;) {
        for (int i = 0; i < 3; i++) {
            drawTarget(px[i], py[i], "Touch the target");
            waitRawSample(ra[i], rb[i]);
            Serial.printf("Cal point %d: rawA=%d rawB=%d\n", i, ra[i], rb[i]);
        }
        // Moving horizontally (point 0 → 1) changes mostly the raw channel that is screen X
        g_cal.swap = abs(rb[1] - rb[0]) > abs(ra[1] - ra[0]);
        int h0 = g_cal.swap ? rb[0] : ra[0], h1 = g_cal.swap ? rb[1] : ra[1];
        int v0 = g_cal.swap ? ra[0] : rb[0], v2 = g_cal.swap ? ra[2] : rb[2];
        int dh = h1 - h0, dv = v2 - v0;

        if (abs(dh) < 200 || abs(dv) < 200) {
            drawTarget(-20, -20, "Calibration failed - retry");
            Serial.println("Calibration failed (check touch wiring)");
            delay(1500);
            continue;
        }
        g_cal.ax = float(px[1] - px[0]) / dh;  g_cal.bx = px[0] - g_cal.ax * h0;
        g_cal.ay = float(py[2] - py[0]) / dv;  g_cal.by = py[0] - g_cal.ay * v0;
        g_cal.magic = CAL_MAGIC;
        prefs.putBytes("tcal", &g_cal, sizeof(g_cal));
        Serial.printf("Saved: swap=%d ax=%.5f bx=%.2f ay=%.5f by=%.2f\n",
                      g_cal.swap, g_cal.ax, g_cal.bx, g_cal.ay, g_cal.by);
        return;
    }
}

// ── UI ──────────────────────────────────────────────────────
struct Btn { int x, y, w, h; const char* label; };
static const Btn BTN_CMINUS = {4,   26, 44, 30, "C-"};
static const Btn BTN_CPLUS  = {104, 26, 44, 30, "C+"};
static const Btn BTN_RECAL  = {156, 26, 80, 30, "RECAL"};
static const int SL_X = 4, SL_Y = 60, SL_W = 232, SL_H = 16;    // backlight slider
static const int CV_X = 4, CV_Y = 80, CV_W = 232, CV_H = 78;    // drawing canvas

static const Btn* g_pressed = nullptr;   // drawn inverted while held

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
static bool inBtn(int x, int y, const Btn& b) { return inRect(x, y, b.x, b.y, b.w, b.h); }

static void drawButton(const Btn& b) {
    bool inv = (g_pressed == &b);
    u8g2.setDrawColor(0); u8g2.drawBox(b.x, b.y, b.w, b.h);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_helvB10_tr);
    int tx = b.x + (b.w - u8g2.getStrWidth(b.label)) / 2;
    int ty = b.y + b.h / 2 + 5;
    if (inv) {
        u8g2.drawRBox(b.x, b.y, b.w, b.h, 4);
        u8g2.setDrawColor(0); u8g2.drawStr(tx, ty, b.label); u8g2.setDrawColor(1);
    } else {
        u8g2.drawRFrame(b.x, b.y, b.w, b.h, 4);
        u8g2.drawStr(tx, ty, b.label);
    }
}

static void drawContrastValue() {
    u8g2.setDrawColor(0); u8g2.drawBox(50, 26, 52, 30);
    u8g2.setDrawColor(1);
    char s[8]; snprintf(s, sizeof(s), "%d", g_contrast);
    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.drawStr(76 - u8g2.getStrWidth(s) / 2, 48, s);
}

static void drawSlider() {
    u8g2.setDrawColor(0); u8g2.drawBox(SL_X, SL_Y, SL_W, SL_H);
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(SL_X, SL_Y, SL_W, SL_H, 3);
    u8g2.drawBox(SL_X + 2, SL_Y + 2, (SL_W - 4) * g_blPercent / 100, SL_H - 4);
}

static void drawStatus(int x, int y, bool down) {
    u8g2.setDrawColor(0); u8g2.drawBox(0, 0, W, 24);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_6x10_tf);
    char s[48];
    if (down) snprintf(s, sizeof(s), "X:%3d Y:%3d", x, y);
    else      snprintf(s, sizeof(s), "X:--- Y:---");
    u8g2.drawStr(2, 10, s);
    snprintf(s, sizeof(s), "BL:%3d%%", g_blPercent);
    u8g2.drawStr(W - 2 - u8g2.getStrWidth(s), 10, s);
    snprintf(s, sizeof(s), "MAC %s  PSRAM %uMB", g_mac, (unsigned)(ESP.getPsramSize() >> 20));
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(2, 21, s);
}

// Canvas with an orientation arrow: it must point to the top of the enclosure
static void clearCanvas() {
    u8g2.setDrawColor(0); u8g2.drawBox(CV_X, CV_Y, CV_W, CV_H);
    u8g2.setDrawColor(1); u8g2.drawFrame(CV_X, CV_Y, CV_W, CV_H);
    const int ax = CV_X + CV_W - 24, ay = CV_Y + 8;
    u8g2.drawTriangle(ax, ay, ax - 8, ay + 12, ax + 8, ay + 12);
    u8g2.drawBox(ax - 3, ay + 12, 7, 14);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(ax - 7, ay + 36, "TOP");
}

static void drawUI() {
    u8g2.clearBuffer();
    drawStatus(0, 0, false);
    drawButton(BTN_CMINUS);
    drawButton(BTN_CPLUS);
    drawButton(BTN_RECAL);
    drawContrastValue();
    drawSlider();
    clearCanvas();
    u8g2.sendBuffer();
}

// ── Setup / loop ────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    analogReadResolution(12);
    tpRelease();

    ledcSetup(BL_CHANNEL, 5000, 8);
    ledcAttachPin(S3_LCD_BL, BL_CHANNEL);
    setBacklight(g_blPercent);
    pinMode(S3_COMM_LED, OUTPUT);
    digitalWrite(S3_COMM_LED, LOW);
    beep(2000, 120);   // boot beep

    WiFi.mode(WIFI_STA);   // STA MAC = the address ESP-NOW uses
    strlcpy(g_mac, WiFi.macAddress().c_str(), sizeof(g_mac));

    Serial.println("\n=== S3 Remote bring-up ===");
    Serial.printf("MAC (STA): %s\n", g_mac);
    Serial.printf("Flash: %u MB   PSRAM: %u MB (%s)\n",
                  (unsigned)(ESP.getFlashChipSize() >> 20),
                  (unsigned)(ESP.getPsramSize() >> 20), psramFound() ? "ok" : "NOT FOUND");

    SPI.begin(S3_LCD_SCK, -1, S3_LCD_MOSI, S3_LCD_CS);   // no MISO
    u8g2.begin();
    u8g2.setBusClock(1000000);   // the speed the image was verified at
    lcdInit();
    u8g2.setFontMode(1);

    prefs.begin("hpui", false);
    setContrast(prefs.getInt("contrast", 54));
    bool haveCal = prefs.getBytes("tcal", &g_cal, sizeof(g_cal)) == sizeof(g_cal) &&
                   g_cal.magic == CAL_MAGIC;
    if (haveCal) {
        Serial.printf("Stored cal: swap=%d ax=%.5f bx=%.2f ay=%.5f by=%.2f\n",
                      g_cal.swap, g_cal.ax, g_cal.bx, g_cal.ay, g_cal.by);
    }
    if (!haveCal || tpPressed()) runCalibration();

    drawUI();
    Serial.println("Ready");
}

void loop() {
    static bool     wasDown = false, lastInCanvas = false, pending = false;
    static uint8_t  missCount = 0;
    static int      lx = 0, ly = 0;
    static uint32_t lastSend = 0, downMs = 0, lastRepeat = 0;

    int x, y;
    bool raw = tpGetPoint(x, y);
    if (raw) missCount = 0; else if (missCount < 255) missCount++;
    uint32_t now = millis();

    static uint32_t lastLog = 0;
    if (raw && now - lastLog >= 200) {
        lastLog = now;
        Serial.printf("touch rawA=%d rawB=%d -> x=%d y=%d\n", g_rawA, g_rawB, x, y);
    }

    if (raw) {
        bool edge = !wasDown;
        if (edge) {
            beep(2500, 25);
            digitalWrite(S3_COMM_LED, !digitalRead(S3_COMM_LED));
            downMs = now;
            lastRepeat = now;
            g_pressed = inBtn(x, y, BTN_CMINUS) ? &BTN_CMINUS :
                        inBtn(x, y, BTN_CPLUS)  ? &BTN_CPLUS  :
                        inBtn(x, y, BTN_RECAL)  ? &BTN_RECAL  : nullptr;
            if (g_pressed == &BTN_CMINUS) setContrast(g_contrast - 1);
            if (g_pressed == &BTN_CPLUS)  setContrast(g_contrast + 1);
            if (g_pressed) { drawButton(*g_pressed); drawContrastValue(); }
        }
        // Contrast auto-repeat while held
        if ((g_pressed == &BTN_CMINUS || g_pressed == &BTN_CPLUS) &&
            now - downMs >= 400 && now - lastRepeat >= 120) {
            lastRepeat = now;
            setContrast(g_contrast + (g_pressed == &BTN_CPLUS ? 1 : -1));
            drawContrastValue();
        }

        if (inRect(x, y, SL_X, SL_Y, SL_W, SL_H)) {   // slider follows the finger
            setBacklight(map(x, SL_X + 2, SL_X + SL_W - 3, 0, 100));
            drawSlider();
        }

        bool inCv = inRect(x, y, CV_X + 1, CV_Y + 1, CV_W - 2, CV_H - 2);
        if (inCv) {
            u8g2.setClipWindow(CV_X + 1, CV_Y + 1, CV_X + CV_W - 1, CV_Y + CV_H - 1);
            if (!edge && lastInCanvas) u8g2.drawLine(lx, ly, x, y);
            u8g2.drawDisc(x, y, 1);
            u8g2.setMaxClipWindow();
        }
        lastInCanvas = inCv;
        lx = x; ly = y;
        drawStatus(x, y, true);
        pending = true;
        wasDown = true;
    } else if (wasDown && missCount >= 3) {   // released (3 misses = debounce)
        wasDown = false;
        lastInCanvas = false;
        const Btn* released = g_pressed;
        g_pressed = nullptr;
        if (released) drawButton(*released);
        if (released == &BTN_CMINUS || released == &BTN_CPLUS) {
            prefs.putInt("contrast", g_contrast);
            Serial.printf("Contrast saved: %d\n", g_contrast);
        }
        if (released == &BTN_RECAL) {   // also clears the drawing
            runCalibration();
            drawUI();
        }
        drawStatus(0, 0, false);
        pending = true;
    }

    beepLoop();

    // Full frame ~19 KB on the bus; FSTN is slow anyway — cap at ~20 fps
    if (pending && now - lastSend >= 50) {
        u8g2.sendBuffer();
        lastSend = now;
        pending = false;
    }
    delay(2);
}

#endif // S3_BRINGUP
