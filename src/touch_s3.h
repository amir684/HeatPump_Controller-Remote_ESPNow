#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  4-wire resistive touch of the S3 Remote
//  Calibration lives in NVS "hpui"/"tcal" (same as env:s3_bringup).
// ═══════════════════════════════════════════════════════════

struct TouchCal {
    uint32_t magic;
    bool     swap;
    float    ax, bx, ay, by;   // x = ax*raw + bx, y = ay*raw + by
};

void touchBegin();                      // loads the calibration (or the bench default)
bool touchPressed();
bool touchReadRaw(int& a, int& b);      // false if released during the read
bool touchRead(int& x, int& y);         // screen coordinates, false if not pressed
void touchSaveCal(const TouchCal& cal);
