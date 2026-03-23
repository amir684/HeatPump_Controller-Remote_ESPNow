#include "control.h"
#include "config.h"
#include "settings.h"
#include "sensors.h"
#include "relays.h"
#include <time.h>

// ── State machine ───────────────────────────────────────────
static HpState   g_state       = HpState::OFFLINE;
static HpError   g_error       = HpError::NONE;
static uint32_t  g_stateMs     = 0;   // millis() when entered current state
static uint32_t  g_compStopMs  = 0;   // when compressor last stopped
static uint32_t  g_compStartMs = 0;   // when compressor last started
static bool      g_powerOnDelay = true; // 3-min delay on first boot
static uint32_t  g_powerOnMs   = 0;
static bool      g_forceDefrost = false;
static uint32_t  g_defrostCount = 0;
static uint32_t  g_antifreezeMs = 0;

static HpStatus g_status = {};

// ── Helpers ────────────────────────────────────────────────
static uint32_t stateAge() { return millis() - g_stateMs; }

static void enterState(HpState s) {
    g_state   = s;
    g_stateMs = millis();
}

static bool compProtectionExpired() {
    // 3-min minimum off time
    if (g_compStopMs == 0) return true;
    return (millis() - g_compStopMs) >= T_COMP_MIN_OFF_MS;
}

static bool heatingNeeded() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();
    if (!d.waterOk) return false;
    float tWater = sensorCorrectedWater();

    // SAVE mode: only heat within configured periods
    if (s.workMode == 1 && !controlInSavePeriod()) return false;

    return tWater <= (s.setpoint - s.u01);
}

static bool heatingDone() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();
    if (!d.waterOk) return true; // stop on sensor failure
    float tWater = sensorCorrectedWater();

    if (s.workMode == 1 && !controlInSavePeriod()) return true;
    return tWater >= s.setpoint;
}

static bool shouldStartDefrost() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();

    uint32_t compRunMs = millis() - g_compStartMs;
    if (compRunMs < (uint32_t)s.u30 * 60000UL) return false;

    if (d.pipeOk) {
        return d.pipe <= s.u31;
    }
    // Timer defrost fallback when pipe sensor fails
    if (d.outdoorOk) return d.outdoor <= 3.0f;
    return true;  // outdoor sensor also failed → defrost anyway
}

static bool shouldStopDefrost() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();

    if (d.pipeOk && d.pipe >= s.u32) return true;

    uint32_t defRunMs = millis() - g_stateMs;
    return defRunMs >= (uint32_t)s.u33 * 60000UL;
}

static void checkExhaustProtection() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();
    if (s.u50 == 0 || !d.exhaustOk) return;

    if (d.exhaust >= s.u51) {
        // Stop compressor with 3-min protection
        if (relayGet(Relay::COMP)) g_compStopMs = millis();
        relaySet(Relay::COMP, false);
        // U50=2: stop fan too; U50=1: fan keeps running to cool down
        if (s.u50 == 2) relaySet(Relay::FAN, false);
        valveSetHeating();
        g_error = HpError::EH_EXHAUST_ALARM;
        enterState(HpState::EXHAUST_PROTECT);
    }
}

static void checkSensorErrors() {
    const SensorData& d = sensorsGet();
    if (!d.waterOk)   { g_error = HpError::E01_WATER_SENSOR; }
    else if (!d.pipeOk)   { g_error = HpError::E02_PIPE_SENSOR; }
    else if (!d.exhaustOk){ g_error = HpError::E03_EXHAUST_SENSOR; }
    else if (!d.outdoorOk){ g_error = HpError::E04_OUTDOOR_SENSOR; }
    else                   { g_error = HpError::NONE; }
}

// ── Main state machine ─────────────────────────────────────
void controlInit() {
    g_powerOnMs   = millis();
    g_powerOnDelay= true;

    Settings& s = settingsGet();
    if (s.u80 == 1) {
        // Auto-restart: restore last state
        if (s.systemOnline) {
            enterState(HpState::STANDBY);  // will re-evaluate heating need
        } else {
            enterState(HpState::OFFLINE);
        }
    } else {
        enterState(HpState::OFFLINE);
    }
}

void controlSetOnline(bool online) {
    Settings& s = settingsGet();
    s.systemOnline = online;
    settingsSave();

    if (online) {
        if (g_state == HpState::OFFLINE ||
            g_state == HpState::ANTIFREEZE_ON ||
            g_state == HpState::ANTIFREEZE_WAIT) {
            enterState(HpState::STANDBY);
        }
    } else {
        // Graceful shutdown
        if (relayGet(Relay::COMP)) g_compStopMs = millis();
        relaySet(Relay::COMP, false);
        relaySet(Relay::FAN,  false);
        valveSetHeating();
        enterState(HpState::OFFLINE);
    }
}

void controlForceDefrost() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();

    if (g_state != HpState::HEATING) return;
    if (d.pipeOk && d.pipe > s.u31) return;  // pipe too warm for defrost

    g_forceDefrost = true;
}

bool controlInSavePeriod() {
    struct tm ti;
    if (!getLocalTime(&ti)) return true;  // if no time, allow heating

    Settings& s = settingsGet();
    int nowMin = ti.tm_hour * 60 + ti.tm_min;

    for (int i = 0; i < 3; i++) {
        const Period& p = s.period[i];
        if (!p.enabled) continue;
        int onMin  = p.onHour  * 60 + p.onMin;
        int offMin = p.offHour * 60 + p.offMin;

        if (offMin > onMin) {
            // Same day period
            if (nowMin >= onMin && nowMin < offMin) return true;
        } else {
            // Overnight period (e.g. 22:00 → 06:00 next day)
            if (nowMin >= onMin || nowMin < offMin) return true;
        }
    }
    return false;
}

void controlLoop() {
    Settings& s = settingsGet();
    const SensorData& d = sensorsGet();

    // Power-on delay (3 minutes before first compressor start)
    if (g_powerOnDelay) {
        if ((millis() - g_powerOnMs) >= T_COMP_POWER_ON_DELAY_MS) {
            g_powerOnDelay = false;
        }
    }

    checkSensorErrors();

    switch (g_state) {

    // ── OFFLINE ──────────────────────────────────────────
    case HpState::OFFLINE:
        relaysAllOff();

        // Anti-freeze check (U52)
        if (s.u52 == 1 && d.outdoorOk && d.outdoor <= 2.0f) {
            g_antifreezeMs = millis();
            enterState(HpState::ANTIFREEZE_ON);
        }
        break;

    // ── ANTIFREEZE ───────────────────────────────────────
    case HpState::ANTIFREEZE_ON:
        if (d.outdoorOk && d.outdoor >= 3.0f) {
            enterState(HpState::OFFLINE);
            break;
        }
        if (stateAge() >= T_ANTIFREEZE_ON_MS) {
            enterState(HpState::ANTIFREEZE_WAIT);
        }
        break;

    case HpState::ANTIFREEZE_WAIT: {
        if (d.outdoorOk && d.outdoor >= 3.0f) {
            enterState(HpState::OFFLINE);
            break;
        }
        uint32_t waitTime = T_ANTIFREEZE_CYCLE_MS - T_ANTIFREEZE_ON_MS;
        if (stateAge() >= waitTime) {
            enterState(HpState::ANTIFREEZE_ON);
        }
        break;
    }

    // ── STANDBY ──────────────────────────────────────────
    case HpState::STANDBY:
        // Stop all outputs except pump (pump off already)
        relaySet(Relay::COMP,  false);
        relaySet(Relay::FAN,   false);
        valveSetHeating();

        // Water sensor failure → stay in standby with error
        if (!d.waterOk) break;

        if (heatingNeeded()) {
            relaySet(Relay::FAN, true);
            valveSetHeating();
            enterState(HpState::STARTUP_FAN);
        }
        break;

    // ── STARTUP SEQUENCE ─────────────────────────────────
    case HpState::STARTUP_FAN:
        if (stateAge() >= T_STARTUP_FAN_TO_COMP_MS) {
            if (g_powerOnDelay || !compProtectionExpired()) {
                enterState(HpState::COMP_DELAY);
            } else {
                relaySet(Relay::COMP, true);
                g_compStartMs = millis();
                enterState(HpState::HEATING);
            }
        }
        break;

    case HpState::COMP_DELAY:
        if (!g_powerOnDelay && compProtectionExpired()) {
            relaySet(Relay::COMP, true);
            g_compStartMs = millis();
            enterState(HpState::HEATING);
        }
        // If heating no longer needed while waiting
        if (heatingDone()) {
            relaySet(Relay::FAN, false);
            valveSetHeating();
            enterState(HpState::SHUTDOWN_FAN);
        }
        break;

    // ── HEATING ──────────────────────────────────────────
    case HpState::HEATING:
        checkExhaustProtection();

        // Check if defrost needed
        if (g_forceDefrost || shouldStartDefrost()) {
            g_forceDefrost = false;
            relaySet(Relay::COMP, false);
            g_compStopMs = millis();
            relaySet(Relay::FAN, false);
            enterState(HpState::DEFROST_STOP);
            break;
        }

        // Check if heating done
        if (heatingDone()) {
            relaySet(Relay::COMP, false);
            g_compStopMs = millis();
            enterState(HpState::SHUTDOWN_COMP);
        }
        break;

    // ── SHUTDOWN SEQUENCE ─────────────────────────────────
    case HpState::SHUTDOWN_COMP:
        if (stateAge() >= T_SHUTDOWN_COMP_TO_FAN_MS) {
            relaySet(Relay::FAN, false);
            valveSetHeating();
            enterState(HpState::SHUTDOWN_FAN);
        }
        break;

    case HpState::SHUTDOWN_FAN:
        if (stateAge() >= T_SHUTDOWN_FAN_TO_PUMP_MS) {
            enterState(HpState::STANDBY);
        }
        break;

    // ── DEFROST START SEQUENCE ────────────────────────────
    case HpState::DEFROST_STOP:
        // comp+fan already off at entry
        if (stateAge() >= T_DEFROST_VALVE_WAIT_MS) {
            valveSetDefrost();
            enterState(HpState::DEFROST_VALVE);
        }
        break;

    case HpState::DEFROST_VALVE:
        if (stateAge() >= T_DEFROST_COMP_WAIT_MS) {
            relaySet(Relay::COMP, true);
            g_compStartMs = millis();
            g_defrostCount++;
            enterState(HpState::DEFROSTING);
        }
        break;

    // ── ACTIVE DEFROST ────────────────────────────────────
    case HpState::DEFROSTING:
        // Pump stays on during defrost (heater on too if applicable)
        if (shouldStopDefrost()) {
            relaySet(Relay::COMP, false);
            g_compStopMs = millis();
            enterState(HpState::DEFROST_END_STOP);
        }
        break;

    // ── DEFROST END SEQUENCE ──────────────────────────────
    case HpState::DEFROST_END_STOP:
        if (stateAge() >= T_DEFROST_VALVE_WAIT_MS) {
            valveSetHeating();
            enterState(HpState::DEFROST_END_VALVE);
        }
        break;

    case HpState::DEFROST_END_VALVE:
        if (stateAge() >= T_DEFROST_COMP_WAIT_MS) {
            // Restart heating: fan on, then comp after delay
            relaySet(Relay::FAN, true);
            enterState(HpState::STARTUP_FAN);
        }
        break;

    // ── EXHAUST PROTECTION ────────────────────────────────
    case HpState::EXHAUST_PROTECT:
        // Wait for 3-min compressor min-off, then resume heating normally
        if (compProtectionExpired()) {
            g_error = HpError::NONE;
            enterState(HpState::STANDBY);
        }
        break;

    default:
        enterState(HpState::OFFLINE);
        break;
    }

    // ── Build status ─────────────────────────────────────
    g_status.state       = g_state;
    g_status.error       = g_error;
    g_status.compressor  = relayGet(Relay::COMP);
    g_status.fan         = relayGet(Relay::FAN);
    g_status.valve       = relayGet(Relay::VALVE);
    g_status.defrosting  = (g_state == HpState::DEFROST_STOP   ||
                             g_state == HpState::DEFROST_VALVE  ||
                             g_state == HpState::DEFROSTING     ||
                             g_state == HpState::DEFROST_END_STOP ||
                             g_state == HpState::DEFROST_END_VALVE);
    g_status.heating     = (g_state == HpState::HEATING);
    g_status.compRunMinutes = g_compStartMs ?
        (millis() - g_compStartMs) / 60000UL : 0;
    g_status.defrostCount = g_defrostCount;
}

const HpStatus& controlStatus() {
    return g_status;
}
