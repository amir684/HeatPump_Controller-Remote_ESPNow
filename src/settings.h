#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  Persistent settings stored in NVS (Preferences)
// ═══════════════════════════════════════════════════════════

struct Period {
    uint8_t onHour;
    uint8_t onMin;
    uint8_t offHour;
    uint8_t offMin;
    bool    enabled;
};

struct Settings {
    // ── Temperature control ──────────────────────────────
    float  setpoint;      // SP: target water temp (10-u03°C)
    float  u03;           // Max setpoint limit (30-70°C)
    float  u01;           // Hysteresis (2-10°C)
    float  u02;           // Water sensor calibration (-9.9..9.9°C)

    // ── Defrost ──────────────────────────────────────────
    uint8_t u30;          // Defrost start time (10-90 min)
    float   u31;          // Defrost start temp (-20..0°C)
    float   u32;          // Defrost stop temp (2-30°C)
    uint8_t u33;          // Max defrost time (2-15 min)
    uint8_t u34;          // Valve mode: 0=heat-off/defrost-on, 1=heat-on/defrost-off

    // ── Exhaust protection ───────────────────────────────
    uint8_t u50;          // 0=off, 1=protect+fan-keep, 2=protect+fan-stop
    float   u51;          // Exhaust protection temperature (50-125°C)

    // ── System ───────────────────────────────────────────
    uint8_t u52;          // Antifreeze: 0=off, 1=on
    uint8_t u80;          // Auto restart: 0=off, 1=on

    // ── Work mode ────────────────────────────────────────
    uint8_t workMode;     // 0=AUTO, 1=SAVE
    bool    systemOnline; // last known on/off state

    // ── SAVE mode periods ────────────────────────────────
    Period period[3];

    // ── Network ──────────────────────────────────────────
    char mqttHost[64];
    uint16_t mqttPort;
    char mqttUser[32];
    char mqttPass[32];
    bool wifiEnabled;     // persist WiFi on/off across reboots
    char webPass[32];     // web page password (user WEB_USER)
};

// ── Public interface ────────────────────────────────────────
void     settingsInit();
void     settingsSave();       // saves to NVS and marks dirty
void     settingsLoad();
void     settingsFactoryReset();
Settings& settingsGet();
bool     settingsHaveSaved();  // false on a blank NVS (only defaults in memory)

// Dirty flag — set by settingsSave(), cleared by main_remote after ESP-NOW sync
bool     settingsIsDirty();
void     settingsClearDirty();
