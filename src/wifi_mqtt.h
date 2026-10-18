#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  WiFi (WiFiManager) + MQTT + Home Assistant Discovery
// ═══════════════════════════════════════════════════════════

void wifiMqttInit();
void wifiMqttLoop();    // call every loop iteration

bool wifiConnected();
bool mqttConnected();

void mqttPublishState();           // publish full state JSON
void mqttPublishDiscovery();       // (re)send HA discovery configs
void wifiStartConfigPortal();      // open AP config portal

void wifiEnable();                 // reconnect to saved AP
void wifiDisable();                // disconnect from AP (keeps credentials + ESP-NOW)
bool wifiIsEnabled();

void mqttRestart();                // reconnect with the current broker settings
