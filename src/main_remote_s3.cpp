// ── S3 touch Remote main (env:remote_s3) ────────────────────
// Same jobs as main_remote.cpp (WiFi/MQTT, web, ESP-NOW, OTA) with the
// touch UI in place of the display/buttons/menu modules.

#if defined(ROLE_REMOTE) && defined(REMOTE_S3)

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "shared.h"
#include "settings.h"
#include "ui_s3.h"
#include "wifi_mqtt.h"
#include "espnow_remote.h"
#include "control.h"    // proxy (control_remote.cpp)
#include "buzzer.h"
#include "web.h"
#include "ota_remote.h"
#include "fw_info.h"

static uint32_t g_lastMqtt = 0;

void setup() {
    Serial.begin(115200);
    // Short wait when a PC is attached but no terminal reads. Not 0: HWCDC::write()
    // in core 2.0.17 then counts its retries down from 0, wraps, and spins forever.
    Serial.setTxTimeoutMs(10);
    Serial.setTimeout(50);      // serial screenshot commands (ui_s3.cpp)
    Serial.printf("\n=== Heat Pump Touch Remote v%s ===\n", g_fwInfo.version);

    settingsInit();

    // The Controller only accepts packets from the old Remote's MAC. Take it over
    // only once that Remote's settings are here (copied NVS has the "init" key):
    // a blank board then stays on its own MAC and cannot touch the heating.
    bool paired = settingsHaveSaved();
    if (paired) {
        const uint8_t mac[] = REMOTE_MAC_BYTES;
        esp_base_mac_addr_set(mac);   // must run before the WiFi stack starts
    }
    Serial.printf("[REMOTE] %s\n", paired ? "Paired: using the old Remote's MAC"
                                          : "Not paired: own MAC, settings never sent");

    otaRemoteInit();
    buzzerInit();
    pinMode(PIN_COMM_LED, OUTPUT);
    digitalWrite(PIN_COMM_LED, LOW);
    uiBegin(paired);

    // WiFi must come up before ESP-NOW init (ESP-NOW needs the WiFi channel)
    wifiMqttInit();
    espnowRemoteInit();
    webBegin();

    // Send settings to the Controller so it is in sync from the start
    espnowSendSettings();
    Serial.println("[REMOTE] Ready.");
}

void loop() {
    uint32_t now = millis();

    // If settings were changed on the screen or the web page, push them to the Controller
    if (settingsIsDirty()) {
        settingsClearDirty();
        espnowSendSettings();
    }

    digitalWrite(PIN_COMM_LED, espnowCtrlSignalLost() ? HIGH : LOW);
    buzzerLoop();
    uiLoop();
    espnowRemoteLoop();
    otaRemoteLoop();
    wifiMqttLoop();
    webLoop();

    // Periodic MQTT state publish (uses proxied HpStatus + SensorData)
    if (now - g_lastMqtt >= T_MQTT_PUBLISH_MS) {
        g_lastMqtt = now;
        mqttPublishState();
    }
}

#endif // ROLE_REMOTE && REMOTE_S3
