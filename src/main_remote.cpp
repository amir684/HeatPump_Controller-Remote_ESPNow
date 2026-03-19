// ── Remote main ─────────────────────────────────────────────
// Only built when ROLE_REMOTE is defined (env:remote).
// Runs: display + buttons + menu + WiFi/MQTT + ESP-NOW.
// No real sensors/relays — those are proxied via ESP-NOW.

#ifdef ROLE_REMOTE

#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "display.h"
#include "buttons.h"
#include "menu.h"
#include "wifi_mqtt.h"
#include "espnow_remote.h"
#include "control.h"    // proxy (control_remote.cpp)
#include "sensors.h"    // proxy (sensors_remote.cpp)
#include "buzzer.h"

static uint32_t g_lastDisplay = 0;
static uint32_t g_lastMqtt    = 0;

// Buttons scanned every 10ms on Core 1
static void buttonTask(void*) {
    for (;;) {
        Buttons.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Heat Pump Remote ===");

    settingsInit();
    // sensorsInit/relaysInit/controlInit are no-ops in proxy implementations

    buzzerInit();
    displayInit();
    Buttons.begin();
    xTaskCreatePinnedToCore(buttonTask, "buttons", 2048, NULL, 5, NULL, 1);
    menuInit();

    // WiFi must come up before ESP-NOW init (ESP-NOW needs WiFi channel)
    wifiMqttInit();

    espnowRemoteInit();

    // Send settings to Controller so it is in sync from the start
    espnowSendSettings();

    displayShowMessage("Heat Pump", "Remote v1.0", "Connecting...");
    delay(1500);

    Serial.println("[REMOTE] Ready.");
}

void loop() {
    uint32_t now = millis();

    // If settings were changed in the menu, push them to the Controller
    if (settingsIsDirty()) {
        settingsClearDirty();
        espnowSendSettings();
    }

    buzzerLoop();
    menuLoop();
    espnowRemoteLoop();
    wifiMqttLoop();

    if (now - g_lastDisplay >= T_DISPLAY_UPDATE_MS) {
        g_lastDisplay = now;
        displayUpdate();
    }

    // Periodic MQTT state publish (uses proxied HpStatus + SensorData)
    if (now - g_lastMqtt >= T_MQTT_PUBLISH_MS) {
        g_lastMqtt = now;
        mqttPublishState();
    }
}

#endif // ROLE_REMOTE
