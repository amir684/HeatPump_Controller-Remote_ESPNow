#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  LCD 12864A (ST7920) display management
// ═══════════════════════════════════════════════════════════

void displayInit();
void displayUpdate();   // call every T_DISPLAY_UPDATE_MS

// Backlight control (0 = off, 255 = max brightness)
void displaySetBrightness(uint8_t level);
void displayDim();    // set to LCD_BL_DIM
void displayBright(); // set to LCD_BL_DEFAULT

// Force a specific screen (used by menu)
void displayShowMenu(const char* title,
                     const char* lines[],
                     int         numLines,
                     int         selected,
                     int         scroll);

void displayShowEdit(const char* paramName,
                     const char* valueStr,
                     const char* hint = nullptr);

void displayShowMessage(const char* line1,
                        const char* line2 = nullptr,
                        const char* line3 = nullptr);

bool displayInMenu();     // true when menu has taken over the display
void displaySetMenu(bool inMenu);

// Show a short temporary message overlaid on the main screen
void displayToast(const char* line1, const char* line2 = nullptr, uint32_t durationMs = 3000);
