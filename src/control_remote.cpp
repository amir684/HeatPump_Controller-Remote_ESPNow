// ── Remote-side proxy for control.h ────────────────────────
// Implements the same interface as control.cpp but returns cached
// data received from the Controller via ESP-NOW.
// Commands (SET_ONLINE, FORCE_DEFROST) are forwarded to the Controller.

#include "control.h"
#include "shared.h"
#include "settings.h"
#include "espnow_remote.h"
#include "buzzer.h"
#include <Arduino.h>

static HpStatus g_status{};

// Defined in sensors_remote.cpp — updates the cached SensorData
extern void remoteUpdateSensors(const StatusPacket& pkt);

// Called by espnow_remote.cpp when a StatusPacket arrives
void remoteUpdateStatus(const StatusPacket& pkt) {
    HpState prevState = g_status.state;
    HpError prevError = g_status.error;

    remoteUpdateSensors(pkt);
    g_status.state          = static_cast<HpState>(pkt.state);
    g_status.error          = static_cast<HpError>(pkt.error);
    g_status.compressor     = pkt.compressor;
    g_status.fan            = pkt.fan;
    g_status.valve          = pkt.valve;
    g_status.defrosting     = pkt.defrosting;
    g_status.heating        = pkt.heating;
    g_status.compRunMinutes = pkt.compRunMinutes;
    g_status.defrostCount   = pkt.defrostCount;

    // Trigger alarm on new fault
    bool newError   = (g_status.error != HpError::NONE && prevError == HpError::NONE);
    bool newProtect = (g_status.state == HpState::EXHAUST_PROTECT && prevState != HpState::EXHAUST_PROTECT);
    if (newError || newProtect) buzzerAlarm();
}

// ── control.h interface ─────────────────────────────────────
void controlInit() { /* nothing on Remote */ }

void controlLoop() { /* nothing on Remote */ }

const HpStatus& controlStatus() {
    return g_status;
}

void controlSetOnline(bool online) {
    // Update local settings so menu reflects the change immediately
    settingsGet().systemOnline = online;

    // Forward command to Controller
    CommandPacket pkt{};
    pkt.type = PktType::COMMAND;
    pkt.cmd  = CmdType::SET_ONLINE;
    pkt.bVal = online;
    espnowSendCommand(pkt);
}

void controlForceDefrost() {
    CommandPacket pkt{};
    pkt.type = PktType::COMMAND;
    pkt.cmd  = CmdType::FORCE_DEFROST;
    espnowSendCommand(pkt);
    espnowSetDefrostPending(true);
}

const char* hpStateName(HpState s) {
    static const char* names[] = {
        "offline","antifreeze_on","antifreeze_wait","startup_fan",
        "comp_delay","heating","shutdown_comp","shutdown_fan",
        "standby","defrost_stop","defrost_valve","defrosting",
        "defrost_end_stop","defrost_end_valve","exhaust_protect"
    };
    unsigned i = static_cast<unsigned>(s);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "unknown";
}

const char* hpErrorName(HpError e) {
    static const char* names[] = {
        "none","E01_water","E02_pipe","E03_exhaust","E04_outdoor","EH_exhaust_alarm"
    };
    unsigned i = static_cast<unsigned>(e);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "unknown";
}

bool controlInSavePeriod() {
    // Evaluate locally — Remote has the settings + system time
    const Settings& s = settingsGet();
    if (s.workMode != 1) return false;

    struct tm ti;
    if (!getLocalTime(&ti, 0)) return false;
    int nowMin = ti.tm_hour * 60 + ti.tm_min;

    for (int i = 0; i < 3; i++) {
        if (!s.period[i].enabled) continue;
        int onMin  = s.period[i].onHour  * 60 + s.period[i].onMin;
        int offMin = s.period[i].offHour * 60 + s.period[i].offMin;
        if (onMin < offMin) {
            if (nowMin >= onMin && nowMin < offMin) return true;
        } else {
            // crosses midnight
            if (nowMin >= onMin || nowMin < offMin) return true;
        }
    }
    return false;
}
