#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  Buzzer — passive speaker via NPN transistor on PIN_BUZZER
// ═══════════════════════════════════════════════════════════

void buzzerInit();
void buzzerLoop();    // call every loop iteration (non-blocking melody playback)

void buzzerClick();   // short beep — call on any button press
void buzzerAlarm();   // 1.5s fault melody — call on new error/protection event
