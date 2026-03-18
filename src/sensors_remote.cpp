// ── Remote-side proxy for sensors.h ────────────────────────
// Implements the same interface as sensors.cpp but returns cached
// sensor data received from the Controller via ESP-NOW.

#include "sensors.h"
#include "settings.h"
#include "shared.h"

static SensorData g_data{};

// Called by control_remote.cpp::remoteUpdateStatus()
// (StatusPacket carries all sensor values)
void remoteUpdateSensors(const StatusPacket& pkt) {
    g_data.water     = pkt.water;
    g_data.pipe      = pkt.pipe;
    g_data.exhaust   = pkt.exhaust;
    g_data.outdoor   = pkt.outdoor;
    g_data.waterOk   = pkt.waterOk;
    g_data.pipeOk    = pkt.pipeOk;
    g_data.exhaustOk = pkt.exhaustOk;
    g_data.outdoorOk = pkt.outdoorOk;
}

// ── sensors.h interface ─────────────────────────────────────
void sensorsInit()   { /* nothing on Remote */ }
void sensorsUpdate() { /* data arrives via ESP-NOW */ }

const SensorData& sensorsGet() {
    return g_data;
}

float sensorCorrectedWater() {
    return g_data.water + settingsGet().u02;
}
