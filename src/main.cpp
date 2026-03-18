#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "sensors.h"
#include "relays.h"
#include "control.h"
#include "display.h"
#include "buttons.h"
#include "menu.h"
#include "wifi_mqtt.h"

static uint32_t g_lastSensor  = 0;
static uint32_t g_lastDisplay = 0;

// Buttons scanned every 10ms on Core 1 (independent of main loop blocking)
static void buttonTask(void*) {
    for (;;) {
        Buttons.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Heat Pump Controller ===");

    // Order matters: settings first, then hardware
    settingsInit();
    sensorsInit();
    relaysInit();
    displayInit();
    Buttons.begin();
    xTaskCreatePinnedToCore(buttonTask, "buttons", 2048, NULL, 5, NULL, 1);
    menuInit();
    controlInit();
    wifiMqttInit();

    displayShowMessage("Heat Pump Ctrl", "Initializing...", "v1.0");
    delay(1500);
}

void loop() {
    uint32_t now = millis();

    if (now - g_lastSensor >= T_SENSOR_READ_MS) {
        g_lastSensor = now;
        sensorsUpdate();
    }

    controlLoop();
    menuLoop();

    if (now - g_lastDisplay >= T_DISPLAY_UPDATE_MS) {
        g_lastDisplay = now;
        displayUpdate();
    }

    wifiMqttLoop();
}
