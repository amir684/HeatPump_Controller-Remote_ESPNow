#include "buttons.h"
#include "buzzer.h"

const uint8_t ButtonManager::PINS[BTN_COUNT] = {
    PIN_BTN_POWER, PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_ENTER, PIN_BTN_BACK
};

ButtonManager Buttons;

void ButtonManager::begin() {
    for (int i = 0; i < BTN_COUNT; i++) {
        pinMode(PINS[i], INPUT_PULLUP);
        _lastRaw[i]    = false;
        _debounceMs[i] = 0;
        _stable[i]     = false;
        _pressMs[i]    = 0;
        _shortFired[i] = false;
        _longFired[i]  = false;
        _longArmed[i]  = false;
        _repeatMs[i]   = 0;
    }
}

void ButtonManager::update() {
    uint32_t now = millis();

    for (int i = 0; i < BTN_COUNT; i++) {
        bool raw = (digitalRead(PINS[i]) == LOW);  // active LOW

        // Reset debounce timer on every edge
        if (raw != _lastRaw[i]) {
            _lastRaw[i]    = raw;
            _debounceMs[i] = now;
        }

        // Only act after signal is stable for DEBOUNCE_MS
        if ((now - _debounceMs[i]) < BTN_DEBOUNCE_MS) continue;

        bool stable = raw;  // true = pressed

        if (stable && !_stable[i]) {
            // Freshly pressed
            buzzerClick();
            _pressMs[i]    = now;
            _shortFired[i] = false;
            _longFired[i]  = false;
            _longArmed[i]  = true;
            _repeatMs[i]   = now;
        }

        if (!stable && _stable[i]) {
            // Released
            if (_longArmed[i]) {
                _shortFired[i] = true;  // short press only if long didn't fire
            }
            _longArmed[i] = false;
        }

        if (stable && _stable[i]) {
            // Still held
            if (_longArmed[i] && (now - _pressMs[i]) >= BTN_LONG_PRESS_MS) {
                _longFired[i] = true;
                _longArmed[i] = false;  // disarm: won't re-fire
            }
            // Auto-repeat for UP / DOWN
            if (!_longArmed[i] && (i == BTN_UP || i == BTN_DOWN)) {
                uint32_t interval = ((now - _pressMs[i]) < BTN_LONG_PRESS_MS + BTN_REPEAT_DELAY_MS)
                                    ? BTN_REPEAT_DELAY_MS : BTN_REPEAT_RATE_MS;
                if ((now - _repeatMs[i]) >= interval) {
                    _repeatMs[i] = now;
                }
            }
        }

        _stable[i] = stable;
    }
}

bool ButtonManager::pressed(uint8_t id) {
    if (_shortFired[id]) { _shortFired[id] = false; return true; }
    return false;
}

bool ButtonManager::longPress(uint8_t id) {
    if (_longFired[id]) { _longFired[id] = false; return true; }
    return false;
}

bool ButtonManager::repeat(uint8_t id) {
    if (!_stable[id]) return false;
    uint32_t now = millis();
    uint32_t interval = ((now - _pressMs[id]) < BTN_LONG_PRESS_MS + BTN_REPEAT_DELAY_MS)
                        ? BTN_REPEAT_DELAY_MS : BTN_REPEAT_RATE_MS;
    if (_stable[id] && !_longArmed[id] && (now - _repeatMs[id]) >= interval) {
        _repeatMs[id] = now;
        return true;
    }
    return false;
}

bool ButtonManager::isHeld(uint8_t id) {
    return _stable[id];
}
