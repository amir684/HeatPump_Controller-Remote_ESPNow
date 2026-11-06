// ── ESP32-S3 LCD diagnostic (env:s3_diag) ───────────────────
// Boots with the panel off. Driven over Serial, see handleCommand(): one init
// and a static test image, then Vop, bias, N-line and booster change live, with
// no reset, so V0, XV0, VG and VM can be measured against the printed values.

#ifdef S3_DIAG

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "pins_s3.h"

static const int W = 240, H = 160;

U8G2_ST7586S_ERC240160_F_4W_HW_SPI hw(S3_LCD_ROTATION, S3_LCD_CS, S3_LCD_DC, S3_LCD_RST);
U8G2_ST7586S_ERC240160_F_4W_SW_SPI sw(S3_LCD_ROTATION, S3_LCD_SCK, S3_LCD_MOSI, S3_LCD_CS, S3_LCD_DC, S3_LCD_RST);

// ── Electrical test ─────────────────────────────────────────
static const uint8_t k_lines[]     = {S3_LCD_SCK, S3_LCD_MOSI, S3_LCD_CS, S3_LCD_DC, S3_LCD_RST};
static const char*   k_lineNames[] = {"SCK",      "MOSI",      "CS",      "DC",      "RST"};
static const int     k_lineCount   = sizeof(k_lines);

static void electricalTest() {
    Serial.println("\n--- Electrical test (an open wire looks OK here) ---");
    int faults = 0;

    for (int i = 0; i < k_lineCount; i++) {
        pinMode(k_lines[i], INPUT_PULLUP);
        delay(5);
        bool up = digitalRead(k_lines[i]);
        pinMode(k_lines[i], INPUT_PULLDOWN);
        delay(5);
        bool down = digitalRead(k_lines[i]);
        const char* verdict = (up && !down) ? "ok" :
                              (up && down)  ? "TIED HIGH" :
                              (!up && !down) ? "TIED LOW" : "ODD";
        if (!(up && !down)) faults++;
        Serial.printf("IO%-2d %-4s  pullup=%d pulldown=%d  %s\n",
                      k_lines[i], k_lineNames[i], up, down, verdict);
    }

    // Drive one line against weak pulls on the others
    for (int a = 0; a < k_lineCount; a++) {
        for (int level = 0; level <= 1; level++) {
            for (int b = 0; b < k_lineCount; b++) {
                if (b != a) pinMode(k_lines[b], level ? INPUT_PULLDOWN : INPUT_PULLUP);
            }
            pinMode(k_lines[a], OUTPUT);
            digitalWrite(k_lines[a], level);
            delay(2);
            for (int b = a + 1; b < k_lineCount; b++) {
                if (digitalRead(k_lines[b]) == level) {
                    Serial.printf("SHORT: IO%d %s <-> IO%d %s\n", k_lines[a], k_lineNames[a],
                                  k_lines[b], k_lineNames[b]);
                    faults++;
                }
            }
            pinMode(k_lines[a], INPUT);
        }
    }
    for (int i = 0; i < k_lineCount; i++) pinMode(k_lines[i], INPUT);
    Serial.printf("Electrical faults: %d\n", faults);
}

// ── Panel read-back ─────────────────────────────────────────
// Reads status registers over the bidirectional SDA line (FPC 22). Each read
// runs with a pull-up and with a pull-down: a bit that keeps its value under
// both is really driven by the panel; a bit that follows the pull is floating.
static void bbClockOut(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(S3_LCD_SCK, LOW);
        digitalWrite(S3_LCD_MOSI, (b >> i) & 1);
        delayMicroseconds(20);
        digitalWrite(S3_LCD_SCK, HIGH);
        delayMicroseconds(20);
    }
}

static void bbCommand(uint8_t cmd) {
    digitalWrite(S3_LCD_DC, LOW);
    digitalWrite(S3_LCD_CS, LOW);
    bbClockOut(cmd);
    digitalWrite(S3_LCD_CS, HIGH);
}

static void bbRead(uint8_t cmd, bool select, uint8_t pullMode, uint8_t* out, int n) {
    pinMode(S3_LCD_MOSI, OUTPUT);
    digitalWrite(S3_LCD_DC, LOW);
    digitalWrite(S3_LCD_CS, select ? LOW : HIGH);
    bbClockOut(cmd);
    digitalWrite(S3_LCD_DC, HIGH);
    pinMode(S3_LCD_MOSI, pullMode);
    delayMicroseconds(50);
    for (int k = 0; k < n; k++) {
        uint8_t v = 0;
        for (int i = 7; i >= 0; i--) {
            digitalWrite(S3_LCD_SCK, LOW);
            delayMicroseconds(40);
            v |= digitalRead(S3_LCD_MOSI) << i;
            digitalWrite(S3_LCD_SCK, HIGH);
            delayMicroseconds(40);
        }
        out[k] = v;
    }
    digitalWrite(S3_LCD_CS, HIGH);
    pinMode(S3_LCD_MOSI, OUTPUT);
}

static int readAndPrint(const char* name, uint8_t cmd, bool select) {
    const int n = 5;
    uint8_t up[n], down[n];
    bbRead(cmd, select, INPUT_PULLUP, up, n);
    bbRead(cmd, select, INPUT_PULLDOWN, down, n);
    int driven = 0;
    Serial.printf("%-16s up:", name);
    for (int i = 0; i < n; i++) Serial.printf(" %02X", up[i]);
    Serial.print("  down:");
    for (int i = 0; i < n; i++) Serial.printf(" %02X", down[i]);
    for (int i = 0; i < n; i++) driven += 8 - __builtin_popcount(up[i] ^ down[i]);
    Serial.printf("  driven bits: %d/%d\n", driven, n * 8);
    return driven;
}

static void panelReadTest() {
    Serial.println("--- Panel read-back ---");
    pinMode(S3_LCD_CS, OUTPUT);   digitalWrite(S3_LCD_CS, HIGH);
    pinMode(S3_LCD_SCK, OUTPUT);  digitalWrite(S3_LCD_SCK, HIGH);
    pinMode(S3_LCD_DC, OUTPUT);   digitalWrite(S3_LCD_DC, HIGH);
    pinMode(S3_LCD_MOSI, OUTPUT); digitalWrite(S3_LCD_MOSI, HIGH);
    pinMode(S3_LCD_RST, OUTPUT);
    digitalWrite(S3_LCD_RST, LOW);  delay(20);
    digitalWrite(S3_LCD_RST, HIGH); delay(150);

    readAndPrint("CS off (base)",  0x0A, false);
    readAndPrint("power, reset",   0x0A, true);
    readAndPrint("status, reset",  0x09, true);
    readAndPrint("ID",             0x04, true);
    bbCommand(0x11);                 // Sleep out
    delay(150);
    bbCommand(0x29);                 // Display on
    delay(50);
    readAndPrint("power, disp on", 0x0A, true);
    readAndPrint("status, disp on", 0x09, true);
}

// Replays the U8g2 init by hand, printing the power-mode byte after each step
static void bbCmdArgs(uint8_t cmd, const uint8_t* args, int n) {
    pinMode(S3_LCD_MOSI, OUTPUT);
    digitalWrite(S3_LCD_DC, LOW);
    digitalWrite(S3_LCD_CS, LOW);
    bbClockOut(cmd);
    digitalWrite(S3_LCD_DC, HIGH);
    for (int i = 0; i < n; i++) bbClockOut(args[i]);
    digitalWrite(S3_LCD_CS, HIGH);
}

static void stepPower(const char* step) {
    uint8_t up, down;
    bbRead(0x0A, true, INPUT_PULLUP, &up, 1);
    bbRead(0x0A, true, INPUT_PULLDOWN, &down, 1);
    Serial.printf("  %-12s power=%02X/%02X\n", step, up, down);
}

static void initSteps(uint32_t sleepOutWaitMs) {
    Serial.printf("--- Init steps, wait after sleep-out %u ms ---\n", (unsigned)sleepOutWaitMs);
    digitalWrite(S3_LCD_RST, LOW);  delay(20);
    digitalWrite(S3_LCD_RST, HIGH); delay(150);
    stepPower("hw reset");
    bbCommand(0x01); delay(60);              stepPower("soft reset");
    bbCommand(0x11); delay(sleepOutWaitMs);  stepPower("sleep out");
    bbCommand(0x28); delay(25);              stepPower("disp off");
    { const uint8_t a[] = {0x36, 0x01}; bbCmdArgs(0xC0, a, 2); } stepPower("vop");
    { const uint8_t a[] = {0x00}; bbCmdArgs(0xC3, a, 1); }       stepPower("bias");
    { const uint8_t a[] = {0x07}; bbCmdArgs(0xC4, a, 1); }       stepPower("booster x8");
    { const uint8_t a[] = {0x1D}; bbCmdArgs(0xD0, a, 1); }       stepPower("analog on");
    { const uint8_t a[] = {0x00}; bbCmdArgs(0xB3, a, 1); }       stepPower("fosc");
    { const uint8_t a[] = {0x00}; bbCmdArgs(0xB5, a, 1); }       stepPower("n-line");
    bbCommand(0x39);                                             stepPower("mono");
    { const uint8_t a[] = {0x02}; bbCmdArgs(0x3A, a, 1); }       stepPower("ddram if");
    bbCommand(0x29); delay(100);                                 stepPower("disp on");
}

// Fills the panel by hand after initSteps(), bypassing U8g2 completely
static void manualAllOn() {
    Serial.println("--- Manual fill, all ON ---");
    { const uint8_t a[] = {0x00, 0x08, 0x00, 0x7F}; bbCmdArgs(0x2A, a, 4); }
    { const uint8_t a[] = {0x00, 0x00, 0x00, 0x9F}; bbCmdArgs(0x2B, a, 4); }
    pinMode(S3_LCD_MOSI, OUTPUT);
    digitalWrite(S3_LCD_DC, LOW);
    digitalWrite(S3_LCD_CS, LOW);
    bbClockOut(0x2C);
    digitalWrite(S3_LCD_DC, HIGH);
    for (int i = 0; i < 12800; i++) bbClockOut(0xFF);   // 80 bytes x 160 rows
    digitalWrite(S3_LCD_CS, HIGH);
    stepPower("filled");
    delay(5000);
    stepPower("after 5 s");

    // Candidates for what clears bit 7 under U8g2
    bbCommand(0x11); delay(50);  stepPower("2nd sleepout");
    delay(500);                  stepPower("+500ms");
    bbCommand(0x10); delay(50);  stepPower("sleep in");
    bbCommand(0x11); delay(50);  stepPower("sleep out");
    delay(500);                  stepPower("+500ms");
}

// ── Test screens ────────────────────────────────────────────
static int g_screen = 0;

static void label(U8G2& d, const char* setup, const char* what) {
    d.setFont(u8g2_font_helvB14_tr);
    char s[12];
    snprintf(s, sizeof(s), "#%d", g_screen);
    d.drawStr(8, 24, s);
    d.setFont(u8g2_font_6x10_tf);
    d.drawStr(8, 40, setup);
    d.drawStr(8, 52, what);
}

static void showScreen(U8G2& d, const char* setup, int kind, uint32_t ms) {
    static const char* const k_names[] = {"text+frame", "all ON", "all OFF", "checker"};
    g_screen++;
    Serial.printf("Screen #%d: %s, %s\n", g_screen, setup, k_names[kind]);

    d.clearBuffer();
    d.setDrawColor(1);
    if (kind == 0) {
        d.drawFrame(0, 0, W, H);
        label(d, setup, k_names[kind]);
    } else if (kind == 1) {
        d.drawBox(0, 0, W, H);
    } else if (kind == 3) {
        for (int y = 0; y < H; y += 8) {
            for (int x = (y / 8 % 2) * 8; x < W; x += 16) d.drawBox(x, y, 8, 8);
        }
        d.setDrawColor(0); d.drawBox(4, 4, 110, 54); d.setDrawColor(1);
        label(d, setup, k_names[kind]);
    }
    d.sendBuffer();
    delay(ms);
}

static void showSet(U8G2& d, const char* setup, bool full) {
    d.setContrast(54);
    showScreen(d, setup, 0, 3000);
    if (full) {
        showScreen(d, setup, 1, 2500);
        showScreen(d, setup, 2, 2500);
    }
    showScreen(d, setup, 3, 2500);
}

// All pixels ON while Vop sweeps ~14..18 V (the driver default is 54 = ~16.5 V)
static void contrastSweep(U8G2& d) {
    Serial.println("Contrast sweep, all ON: 30..80");
    d.clearBuffer();
    d.drawBox(0, 0, W, H);
    d.sendBuffer();
    for (int v = 30; v <= 80; v += 5) {
        d.setContrast(v);
        delay(1000);
    }
    d.setContrast(54);
}

// begin() sends a second sleep-out, which leaves bit 7 of the power byte clear.
// A clean sleep-in / sleep-out cycle sets it again.
static void wakePanel(U8G2& d) {
    d.setPowerSave(1);
    delay(150);
    d.setPowerSave(0);
    delay(150);
}

// ── Bus setups ──────────────────────────────────────────────
static void runSwSpi(int sck, int mosi, int dc, const char* name, bool full) {
    SPI.end();
    u8x8_SetPin_4Wire_SW_SPI(sw.getU8x8(), sck, mosi, S3_LCD_CS, dc, S3_LCD_RST);
    sw.begin();
    wakePanel(sw);
    if (full) {   // Did the U8g2 init sequence reach the panel?
        readAndPrint("power, u8g2",  0x0A, true);
        readAndPrint("status, u8g2", 0x09, true);
        readAndPrint("scan, u8g2",   0x0B, true);
        delay(200);
        stepPower("u8g2 +200ms");
        delay(2000);
        stepPower("u8g2 +2.2s");
    }
    showSet(sw, name, full);
}

static void runHwSpi(uint32_t hz, const char* name) {
    SPI.end();
    SPI.begin(S3_LCD_SCK, -1, S3_LCD_MOSI, S3_LCD_CS);
    hw.setBusClock(hz);
    hw.begin();
    wakePanel(hw);
    hw.setBusClock(hz);
    showSet(hw, name, true);
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // never block when no terminal is attached
    delay(1500);
    pinMode(S3_LCD_BL, OUTPUT);
    digitalWrite(S3_LCD_BL, S3_LCD_BL_ACTIVE_LOW ? LOW : HIGH);
    Serial.println("\n=== S3 LCD diagnostic ===");
}

// Writes a different pattern into several rows, then reads them back.
// Different rows read back correctly = RAM and addressing are fine.
static void ramRowTest() {
    Serial.println("--- RAM row test ---");
    panelReadTest();
    initSteps(150);
    static const uint8_t rows[] = {0, 1, 40, 80, 120, 159};
    for (uint8_t r : rows) {
        const uint8_t col[]  = {0x00, 0x08, 0x00, 0x7F};
        const uint8_t row[]  = {0x00, r, 0x00, 0x9F};
        uint8_t data[8];
        for (int i = 0; i < 8; i++) data[i] = (uint8_t)(r + i * 3 + 0x11);
        bbCmdArgs(0x2A, col, 4);
        bbCmdArgs(0x2B, row, 4);
        bbCmdArgs(0x2C, data, 8);
    }
    for (uint8_t r : rows) {
        const uint8_t col[]  = {0x00, 0x08, 0x00, 0x7F};
        const uint8_t row[]  = {0x00, r, 0x00, 0x9F};
        uint8_t got[12];
        bbCmdArgs(0x2A, col, 4);
        bbCmdArgs(0x2B, row, 4);
        bbRead(0x2E, true, INPUT_PULLUP, got, 12);
        Serial.printf("row %3d wrote %02X %02X %02X  read:", r,
                      (uint8_t)(r + 0x11), (uint8_t)(r + 0x14), (uint8_t)(r + 0x17));
        for (int i = 0; i < 12; i++) Serial.printf(" %02X", got[i]);
        Serial.println();
    }
}

// Power-on flow from the ST7586S datasheet (OTP load first, one sleep-out only),
// keeping the Vop, bias and scan values of the U8g2 ERC240160 driver.
static void cmdArgs(uint8_t cmd, const uint8_t* args, int n, uint32_t waitMs = 0) {
    u8x8_t* u = hw.getU8x8();
    u8x8_cad_StartTransfer(u);
    u8x8_cad_SendCmd(u, cmd);
    for (int i = 0; i < n; i++) u8x8_cad_SendArg(u, args[i]);
    u8x8_cad_EndTransfer(u);
    if (waitMs) delay(waitMs);
}

static void datasheetInit(uint16_t vop) {
    digitalWrite(S3_LCD_RST, LOW);  delay(10);
    digitalWrite(S3_LCD_RST, HIGH); delay(120);
    { const uint8_t a[] = {0x9F}; cmdArgs(0xD7, a, 1); }      // Disable auto read
    { const uint8_t a[] = {0x00}; cmdArgs(0xE0, a, 1, 10); }  // Enable OTP read
    cmdArgs(0xE3, nullptr, 0, 20);                            // OTP up-load
    cmdArgs(0xE1, nullptr, 0);                                // OTP control out
    cmdArgs(0x11, nullptr, 0);                                // Sleep out
    cmdArgs(0x28, nullptr, 0, 50);                            // Display off
    { const uint8_t a[] = {(uint8_t)(vop & 0xFF), (uint8_t)(vop >> 8)}; cmdArgs(0xC0, a, 2); }
    { const uint8_t a[] = {0x00}; cmdArgs(0xC3, a, 1); }      // Bias 1/14
    { const uint8_t a[] = {0x07}; cmdArgs(0xC4, a, 1); }      // Booster x8
    { const uint8_t a[] = {0x1D}; cmdArgs(0xD0, a, 1); }      // Analog on
    { const uint8_t a[] = {0x00}; cmdArgs(0xB5, a, 1); }      // N-line 0
    cmdArgs(0x39, nullptr, 0);                                // Monochrome
    { const uint8_t a[] = {0x02}; cmdArgs(0x3A, a, 1); }      // DDRAM interface
    { const uint8_t a[] = {0xC8}; cmdArgs(0x36, a, 1); }      // Scan direction
    { const uint8_t a[] = {0x9F}; cmdArgs(0xB0, a, 1); }      // Duty
    { const uint8_t a[] = {0x00}; cmdArgs(0xB1, a, 1); }      // First COM
    cmdArgs(0x20, nullptr, 0);                                // Inversion off

    // Clear the whole DDRAM, 128 bytes x 160 rows, before display on
    {
        const uint8_t col[] = {0x00, 0x00, 0x00, 0x7F};
        const uint8_t row[] = {0x00, 0x00, 0x00, 0x9F};
        static uint8_t zeros[128];
        cmdArgs(0x2A, col, 4);
        cmdArgs(0x2B, row, 4);
        u8x8_t* u = hw.getU8x8();
        u8x8_cad_StartTransfer(u);
        u8x8_cad_SendCmd(u, 0x2C);
        for (int r = 0; r < 160; r++) u8x8_cad_SendData(u, sizeof(zeros), zeros);
        u8x8_cad_EndTransfer(u);
    }

    { const uint8_t a[] = {0x00, 0x08, 0x00, 0x7F}; cmdArgs(0x2A, a, 4); }
    { const uint8_t a[] = {0x00, 0x00, 0x00, 0x9F}; cmdArgs(0x2B, a, 4); }
    cmdArgs(0x29, nullptr, 0, 50);                            // Display on
}

// Big, slow screens: the digit on screen is the step number
static void bigDigit(int step, int contrast) {
    hw.setContrast(contrast);
    hw.clearBuffer();
    hw.setDrawColor(1);
    hw.setFont(u8g2_font_logisoso92_tn);
    char s[4];
    snprintf(s, sizeof(s), "%d", step);
    hw.drawStr((W - hw.getStrWidth(s)) / 2, 126, s);
    hw.sendBuffer();
    Serial.printf("Step %d: big digit, contrast %d\n", step, contrast);
    delay(6000);
}

static void shape(int step, const char* name) {
    hw.setContrast(54);
    hw.clearBuffer();
    hw.setDrawColor(1);
    if (step == 6) {
        hw.drawBox(0, 0, W / 2, H);                 // left half black
    } else if (step == 7) {
        hw.drawBox(0, 0, W, H / 2);                 // top half black
    } else if (step == 8) {
        for (int i = 0; i < 6; i++) hw.drawFrame(i, i, W - 2 * i, H - 2 * i);   // thick frame
    } else {
        hw.drawFrame(0, 0, W, H);                   // thin frame + normal text
        hw.setFont(u8g2_font_helvB14_tr);
        hw.drawStr(40, 60, "HELLO 12345");
        hw.setFont(u8g2_font_6x10_tf);
        hw.drawStr(40, 90, "small text 6x10");
    }
    hw.sendBuffer();
    Serial.printf("Step %d: %s\n", step, name);
    delay(6000);
}

static uint16_t g_vop  = 0x136;
static uint8_t  g_bias = 0;       // 0 = 1/14 ... 5 = 1/9
static bool     g_on   = false;

static void drawTest() {
    hw.clearBuffer();
    hw.setDrawColor(1);
    hw.drawFrame(0, 0, W, H);
    hw.drawTriangle(30, 8, 10, 44, 50, 44);
    hw.drawBox(22, 44, 17, 30);
    hw.setFont(u8g2_font_helvB14_tr);
    hw.drawStr(8, 100, "HELLO 123");
    hw.setFont(u8g2_font_6x10_tf);
    hw.drawStr(8, 125, "small text 6x10");
    hw.sendBuffer();
}

// Datasheet: V0 = 3.6 + Vop x 0.04 is the full V0-XV0 span
static void printExpected() {
    float span = 3.6f + g_vop * 0.04f;
    int n = 14 - g_bias;
    Serial.printf("Vop %03X bias 1/%d: V0-XV0 %.2f V, VG %.2f V, VM %.2f V\n",
                  g_vop, n, span, 2 * span / n, span / n);
}

static void printStatus() {
    SPI.end();
    pinMode(S3_LCD_SCK, OUTPUT);
    digitalWrite(S3_LCD_SCK, HIGH);
    readAndPrint("power",  0x0A, true);
    readAndPrint("status", 0x09, true);
    SPI.begin(S3_LCD_SCK, -1, S3_LCD_MOSI, S3_LCD_CS);
    hw.setBusClock(1000000);
}

static void panelOn() {
    static bool begun = false;
    SPI.begin(S3_LCD_SCK, -1, S3_LCD_MOSI, S3_LCD_CS);
    if (!begun) {
        hw.begin();
        begun = true;
    }
    hw.setBusClock(1000000);
    datasheetInit(g_vop);
    { const uint8_t a[] = {g_bias}; cmdArgs(0xC3, a, 1); }
    drawTest();
    g_on = true;
    printExpected();
    printStatus();
}

// Boots with the panel held in reset, so the charge pump never runs unattended
static void panelOff() {
    if (g_on) {
        cmdArgs(0x28, nullptr, 0);
        cmdArgs(0x10, nullptr, 0, 200);
    }
    pinMode(S3_LCD_RST, OUTPUT);
    digitalWrite(S3_LCD_RST, LOW);
    g_on = false;
    Serial.println("Panel off, held in reset");
}

// Serial commands: g on, x off, s status, v<hex> Vop, b<0-5> bias, n<hex> N-line, c<0-7> booster
static void handleCommand(const String& line) {
    char c = line[0];
    long arg = strtol(line.c_str() + 1, nullptr, 16);
    if (c == 'g') { panelOn(); return; }
    if (c == 'x') { panelOff(); return; }
    if (!g_on) { Serial.println("Panel is off, send g first"); return; }
    if (c == 's') {
        printExpected();
        printStatus();
    } else if (c == 'v') {
        g_vop = (uint16_t)arg;
        const uint8_t a[] = {(uint8_t)(g_vop & 0xFF), (uint8_t)(g_vop >> 8)};
        cmdArgs(0xC0, a, 2);
        printExpected();
    } else if (c == 'b') {
        g_bias = (uint8_t)arg;
        const uint8_t a[] = {g_bias};
        cmdArgs(0xC3, a, 1);
        printExpected();
    } else if (c == 'n') {
        const uint8_t a[] = {(uint8_t)arg};
        cmdArgs(0xB5, a, 1);
        Serial.printf("N-line %02X\n", (unsigned)arg);
    } else if (c == 'c') {
        const uint8_t a[] = {(uint8_t)arg};
        cmdArgs(0xC4, a, 1);
        Serial.printf("Booster x%d\n", (int)arg + 1);
    }
}

void loop() {
    static bool first = true;
    if (first) {
        first = false;
        panelOff();
    }
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("Ready, panel %s\n", g_on ? "ON" : "off");
    }
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length()) handleCommand(line);
    }
}

#endif // S3_DIAG
