#pragma once

// ═══════════════════════════════════════════════════════════
//  ESP32-S3 Remote (ESP32-S3-WROOM-1 N16R2) — pin map
//  ERC240160FS-2 (ST7586S, 240x160) + 4-wire resistive touch
//
//  Avoided: strapping 0/3/45/46, USB 19/20, flash 26-32.
//  33-37 are kept free so the same map also works on an N16R8 (octal PSRAM).
// ═══════════════════════════════════════════════════════════

// ── LCD, 4-wire SPI (HW SPI: these are also the S3 default SPI pins) ──
#define S3_LCD_SCK    12   // FPC 24 "A0"  (SCL in serial mode)
#define S3_LCD_MOSI   11   // FPC 22 "D0"  (SDA)
#define S3_LCD_CS     10   // FPC 9  "CSB"
#define S3_LCD_DC      9   // FPC 21 "D1"  (A0 = data/command)
#define S3_LCD_RST    14   // FPC 13 "RST"

// ── Backlight: N-MOSFET low-side switch → active-HIGH ──
#define S3_LCD_BL     15   // also XTAL_32K_P: fine, no 32 kHz crystal is fitted
#define S3_LCD_BL_ACTIVE_LOW  0

// ── Touch (XP/YP must be ADC1 — ADC2 is unusable with WiFi on) ──
#define S3_TP_XP       4   // ADC1_CH3, layer 1
#define S3_TP_XM       6   //           layer 1
#define S3_TP_YP       5   // ADC1_CH4, layer 2
#define S3_TP_YM       7   //           layer 2

// ── Buzzer + comm LED ──
#define S3_BUZZER     16   // passive speaker via NPN transistor
#define S3_COMM_LED   17   // ON when ESP-NOW signal lost

// ── Display orientation: FPC/wiring at the top of the enclosure ──
// Checked on the panel: R2 drew the image upside down with the FPC at the top.
#define S3_LCD_ROTATION  U8G2_R0
