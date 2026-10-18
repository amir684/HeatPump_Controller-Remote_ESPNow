// ── Controller main ─────────────────────────────────────────
// Only built when ROLE_CONTROLLER is defined (env:controller).
// Runs: sensors → relays → control state machine → ESP-NOW.
// No display, no buttons, no WiFi/MQTT.

#ifdef ROLE_CONTROLLER

#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "sensors.h"
#include "relays.h"
#include "control.h"
#include "espnow_ctrl.h"
#include "ota_ctrl.h"
#include "fw_info.h"

static uint32_t g_lastSensor = 0;

void setup() {
    Serial.begin(115200);
    Serial.printf("\n=== Heat Pump Controller (CTRL) v%s ===\n", g_fwInfo.version);

    settingsInit();
    sensorsInit();
    relaysInit();
    otaCtrlInit();
    controlInit();
    espnowCtrlInit();

    Serial.println("[CTRL] Ready.");
}

void loop() {
    uint32_t now = millis();

    if (now - g_lastSensor >= T_SENSOR_READ_MS) {
        g_lastSensor = now;
        sensorsUpdate();
    }

    otaCtrlLoop();
    if (!otaCtrlActive()) controlLoop();   // relays stay off during an update
    espnowCtrlLoop();
}

#endif // ROLE_CONTROLLER
