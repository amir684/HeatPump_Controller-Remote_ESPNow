#ifdef REMOTE_S3

#include "lcd_s3.h"
#include "config.h"
#include "pins_s3.h"
#include <SPI.h>

U8G2_ST7586S_ERC240160_F_4W_HW_SPI lcd(S3_LCD_ROTATION, S3_LCD_CS, S3_LCD_DC, S3_LCD_RST);

static uint32_t g_lastHash = 0;

static void lcdCmd(uint8_t cmd, const uint8_t* args = nullptr, int n = 0, uint32_t waitMs = 0) {
    u8x8_t* u = lcd.getU8x8();
    u8x8_cad_StartTransfer(u);
    u8x8_cad_SendCmd(u, cmd);
    for (int i = 0; i < n; i++) u8x8_cad_SendArg(u, args[i]);
    u8x8_cad_EndTransfer(u);
    if (waitMs) delay(waitMs);
}

// U8g2's own init skips the OTP load and the DDRAM clear and sends a second
// sleep-out; on this panel that gave a smeared image. This is the ST7586S
// datasheet power-on flow with the U8g2 ERC240160 register values.
static void panelInit() {
    digitalWrite(S3_LCD_RST, LOW);  delay(10);
    digitalWrite(S3_LCD_RST, HIGH); delay(120);
    { const uint8_t a[] = {0x9F}; lcdCmd(0xD7, a, 1); }       // Disable auto read
    { const uint8_t a[] = {0x00}; lcdCmd(0xE0, a, 1, 10); }   // Enable OTP read
    lcdCmd(0xE3, nullptr, 0, 20);                             // OTP up-load
    lcdCmd(0xE1);                                             // OTP control out
    lcdCmd(0x11);                                             // Sleep out
    lcdCmd(0x28, nullptr, 0, 50);                             // Display off
    { const uint8_t a[] = {0x32, 0x01}; lcdCmd(0xC0, a, 2); } // Vop 0x132
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
    u8x8_t* u = lcd.getU8x8();
    u8x8_cad_StartTransfer(u);
    u8x8_cad_SendCmd(u, 0x2C);
    for (int r = 0; r < 160; r++) u8x8_cad_SendData(u, sizeof(zeros), (uint8_t*)zeros);
    u8x8_cad_EndTransfer(u);

    { const uint8_t a[] = {0x00, 0x08, 0x00, 0x7F}; lcdCmd(0x2A, a, 4); }   // Visible columns
    lcdCmd(0x2B, row, 4);
    lcdCmd(0x29, nullptr, 0, 50);                             // Display on
}

void lcdBegin() {
    ledcSetup(LCD_BL_CHANNEL, LCD_BL_FREQ, LCD_BL_RES);
    ledcAttachPin(S3_LCD_BL, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, 0);

    SPI.begin(S3_LCD_SCK, -1, S3_LCD_MOSI, S3_LCD_CS);   // no MISO
    lcd.begin();
    lcd.setBusClock(4000000);
    panelInit();
    lcd.setFontMode(1);
    lcd.clearBuffer();
    g_lastHash = 0;
}

void lcdSetContrast(int v) {
    lcd.setContrast((uint8_t)constrain(v, 0, 255));
}

void lcdSetBacklight(int pct) {
    uint32_t duty = map(constrain(pct, 0, 100), 0, 100, 0, 255);
    ledcWrite(LCD_BL_CHANNEL, S3_LCD_BL_ACTIVE_LOW ? 255 - duty : duty);
}

// FNV-1a over the frame: an unchanged frame costs no bus time
bool lcdSend() {
    const uint8_t* p = lcd.getBufferPtr();
    uint32_t h = 2166136261u;
    for (int i = 0; i < LCD_BUF_BYTES; i++) h = (h ^ p[i]) * 16777619u;
    if (h == g_lastHash) return false;
    g_lastHash = h;
    lcd.sendBuffer();
    return true;
}

#endif // REMOTE_S3
