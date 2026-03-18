#pragma once
#include <Arduino.h>
#include "config.h"

// ── Button IDs ─────────────────────────────────────────────
#define BTN_POWER  0
#define BTN_UP     1
#define BTN_DOWN   2
#define BTN_ENTER  3
#define BTN_BACK   4
#define BTN_COUNT  5

class ButtonManager {
public:
    void begin();
    void update();  // call every loop()

    bool pressed(uint8_t id);    // true ONCE on short press (cleared after read)
    bool longPress(uint8_t id);  // true ONCE on long press  (cleared after read)
    bool repeat(uint8_t id);     // true while held for UP/DOWN auto-repeat
    bool isHeld(uint8_t id);     // true while currently held

private:
    static const uint8_t PINS[BTN_COUNT];

    bool     _lastRaw[BTN_COUNT];
    uint32_t _debounceMs[BTN_COUNT];
    bool     _stable[BTN_COUNT];   // true = pressed
    uint32_t _pressMs[BTN_COUNT];
    bool     _shortFired[BTN_COUNT];
    bool     _longFired[BTN_COUNT];
    bool     _longArmed[BTN_COUNT];
    uint32_t _repeatMs[BTN_COUNT];
};

extern ButtonManager Buttons;
