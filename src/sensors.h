#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  NTC 10K/3470 sensor readings
// ═══════════════════════════════════════════════════════════

struct SensorData {
    float water;    // water tank temperature (°C)
    float pipe;     // pipe coil temperature (°C)
    float exhaust;  // compressor exhaust temperature (°C)
    float outdoor;  // outdoor ambient temperature (°C)

    bool waterOk;
    bool pipeOk;
    bool exhaustOk;
    bool outdoorOk;
};

void             sensorsInit();
void             sensorsUpdate();      // call every T_SENSOR_READ_MS
const SensorData& sensorsGet();
float            sensorCorrectedWater(); // water + calibration offset (u02)
