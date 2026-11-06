#pragma once
#include <U8g2lib.h>

// ═══════════════════════════════════════════════════════════
//  ERC240160 (ST7586S) panel + backlight of the S3 touch Remote
// ═══════════════════════════════════════════════════════════

#define LCD_W 240
#define LCD_H 160
#define LCD_ROW_BYTES (LCD_W / 8)              // frame buffer: 1 bit/pixel, MSB = left
#define LCD_BUF_BYTES (LCD_ROW_BYTES * LCD_H)

extern U8G2_ST7586S_ERC240160_F_4W_HW_SPI lcd;

void lcdBegin();                 // datasheet power-on flow, then backlight on
void lcdSetContrast(int v);      // 0..255, sets Vop = 0x100 + v (50 suits this panel)
void lcdSetBacklight(int pct);   // 0..100
bool lcdSend();                  // sends the buffer only if it changed; true if sent
