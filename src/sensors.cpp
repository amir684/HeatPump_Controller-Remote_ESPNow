#include "sensors.h"
#include "config.h"
#include "settings.h"
#include <math.h>

static SensorData g_data;

// Read NTC: 3.3V → 10K series → ADC_pin → NTC → GND
// Returns temperature in °C, or NTC_ERROR_VAL on failure
static float readNTC(uint8_t pin) {
    long sum = 0;
    for (int i = 0; i < NTC_NUM_SAMPLES; i++) {
        sum += analogRead(pin);
        delayMicroseconds(100);
    }
    int raw = sum / NTC_NUM_SAMPLES;

    // Reject rail values (open/short circuit)
    if (raw < 10 || raw > 4085) return NTC_ERROR_VAL;

    // Convert ADC to voltage (3.3V reference, 12-bit = 4095)
    float voltage = raw * 3.3f / 4095.0f;

    // Calculate NTC resistance
    float resistance = NTC_SERIES_R * voltage / (3.3f - voltage);
    if (resistance <= 0.0f) return NTC_ERROR_VAL;

    // Steinhart-Hart (B-parameter equation)
    float steinhart = resistance / NTC_NOMINAL_R;
    steinhart = logf(steinhart);
    steinhart /= NTC_B_COEFF;
    steinhart += 1.0f / (NTC_NOMINAL_T + 273.15f);
    steinhart = 1.0f / steinhart;
    steinhart -= 273.15f;

    return steinhart;
}

void sensorsInit() {
    analogSetAttenuation(ADC_11db);  // full 3.3V range
    analogReadResolution(12);         // 12-bit (0-4095)
    g_data = {0, 0, 0, 0, false, false, false, false};
}

void sensorsUpdate() {
    float v;

    v = readNTC(PIN_NTC_WATER);
    g_data.waterOk = (v != NTC_ERROR_VAL);
    if (g_data.waterOk) g_data.water = v;

    v = readNTC(PIN_NTC_PIPE);
    g_data.pipeOk = (v != NTC_ERROR_VAL);
    if (g_data.pipeOk) g_data.pipe = v;

    v = readNTC(PIN_NTC_EXHAUST);
    g_data.exhaustOk = (v != NTC_ERROR_VAL);
    if (g_data.exhaustOk) g_data.exhaust = v;

    v = readNTC(PIN_NTC_OUTDOOR);
    g_data.outdoorOk = (v != NTC_ERROR_VAL);
    if (g_data.outdoorOk) g_data.outdoor = v;
}

const SensorData& sensorsGet() {
    return g_data;
}

float sensorCorrectedWater() {
    return g_data.water + settingsGet().u02;
}
