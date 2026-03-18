#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  Heat Pump Control State Machine
//  Replicates ZL-R200A logic exactly
// ═══════════════════════════════════════════════════════════

enum class HpState {
    OFFLINE,            // System turned off by user
    ANTIFREEZE_ON,      // Anti-freeze: (3 min)
    ANTIFREEZE_WAIT,    // Anti-freeze: waiting next cycle
    STARTUP_FAN,        // Heating start: fan+valve ON, waiting 30s
    COMP_DELAY,         // Waiting for 3-min compressor protection delay
    HEATING,            // Normal heating: all on
    SHUTDOWN_COMP,      // Stop: comp OFF, waiting 30s
    SHUTDOWN_FAN,       // Stop: fan+valve OFF, waiting 30s
    STANDBY,            // Temperature satisfied, waiting
    DEFROST_STOP,       // Defrost init: comp+fan OFF, waiting 55s
    DEFROST_VALVE,      // Defrost: valve acting, waiting 5s
    DEFROSTING,         // Active defrost: comp ON, fan OFF, valve reversed
    DEFROST_END_STOP,   // Defrost end: comp OFF, waiting 55s
    DEFROST_END_VALVE,  // Defrost end: valve restoring, waiting 5s
    EXHAUST_PROTECT,    // Over-temp protection active
};

enum class HpError {
    NONE,
    E01_WATER_SENSOR,
    E02_PIPE_SENSOR,
    E03_EXHAUST_SENSOR,
    E04_OUTDOOR_SENSOR,
    EH_EXHAUST_ALARM,
};

struct HpStatus {
    HpState  state;
    HpError  error;
    bool     compressor;
    bool     fan;
    bool     valve;
    bool     defrosting;
    bool     heating;     // actively raising water temp
    uint32_t compRunMinutes;    // compressor run time since last start
    uint32_t defrostCount;      // defrosts since last power-on
};

void           controlInit();
void           controlLoop();        // call every loop iteration
const HpStatus& controlStatus();

// Commands
void controlSetOnline(bool online);  // turn system on/off
void controlForceDefrost();          // manual forced defrost

// Check if currently in SAVE mode active period
bool controlInSavePeriod();
