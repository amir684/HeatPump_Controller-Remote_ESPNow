#include "wifi_mqtt.h"
#include "config.h"
#include "settings.h"
#include "control.h"
#include "sensors.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

// ── MQTT Topics ────────────────────────────────────────────
#define T_AVAILABLE  MQTT_BASE_TOPIC "/available"
#define T_STATE      MQTT_BASE_TOPIC "/state"
#define T_CMD        MQTT_BASE_TOPIC "/cmd"

// ── Clients ────────────────────────────────────────────────
static WiFiClient   g_wifiClient;
static PubSubClient g_mqtt(g_wifiClient);
static WiFiManager  g_wifiManager;

static uint32_t g_lastPublish    = 0;
static uint32_t g_lastReconnect  = 0;
static bool     g_discoveryDone  = false;
static bool     g_wifiEnabled    = true;  // runtime mirror of settings.wifiEnabled

// ── MQTT command callback ──────────────────────────────────
static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    char msg[256];
    if (len >= sizeof(msg)) return;
    memcpy(msg, payload, len);
    msg[len] = '\0';

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

    Settings& s = settingsGet();

    // {"mode":"heat"} or {"mode":"off"}
    if (doc.containsKey("mode")) {
        const char* mode = doc["mode"];
        bool online = (strcmp(mode, "heat") == 0 || strcmp(mode, "on") == 0);
        controlSetOnline(online);
    }

    // {"setpoint": 45.0}
    if (doc.containsKey("setpoint")) {
        float sp = doc["setpoint"].as<float>();
        if (sp > s.u03) sp = s.u03;
        if (sp >= 10.0f && sp <= s.u03) {
            s.setpoint = sp;
            settingsSave();
        }
    }

    // {"work_mode":"auto"} or {"work_mode":"save"}
    if (doc.containsKey("work_mode")) {
        const char* wm = doc["work_mode"];
        s.workMode = (strcmp(wm, "save") == 0) ? 1 : 0;
        settingsSave();
    }

    // {"force_defrost":true}
    if (doc.containsKey("force_defrost") && doc["force_defrost"].as<bool>()) {
        controlForceDefrost();
    }
}

// ── HA Discovery payloads ──────────────────────────────────
static void publishDiscoveryClimate() {
    StaticJsonDocument<1024> doc;

    doc["name"]            = "Heat Pump";
    doc["unique_id"]       = MQTT_DEVICE_ID;
    doc["modes"][0]        = "off";
    doc["modes"][1]        = "heat";
    doc["mode_state_topic"]   = T_STATE;
    doc["mode_state_template"]= "{{ value_json.mode }}";
    doc["mode_command_topic"] = T_CMD;
    doc["mode_command_template"] = "{\"mode\": \"{{ value }}\"}";
    doc["temperature_state_topic"]   = T_STATE;
    doc["temperature_state_template"]= "{{ value_json.setpoint }}";
    doc["temperature_command_topic"] = T_CMD;
    doc["temperature_command_template"] = "{\"setpoint\": {{ value }}}";
    doc["current_temperature_topic"]    = T_STATE;
    doc["current_temperature_template"] = "{{ value_json.water_temp }}";
    doc["action_topic"]    = T_STATE;
    doc["action_template"] = "{{ value_json.action }}";
    doc["min_temp"]        = 10;
    doc["max_temp"]        = (int)settingsGet().u03;
    doc["temp_step"]       = 1;
    doc["availability_topic"] = T_AVAILABLE;

    JsonObject device = doc.createNestedObject("device");
    device["identifiers"][0] = MQTT_DEVICE_ID;
    device["name"]            = "Heat Pump Controller";
    device["model"]           = "ESP32-HeatPump";
    device["manufacturer"]    = "DIY";

    char buf[1024];
    serializeJson(doc, buf, sizeof(buf));
    String topic = String(HA_DISCOVERY_PREFIX) + "/climate/" + MQTT_DEVICE_ID + "/config";
    g_mqtt.publish(topic.c_str(), buf, true);
}

static void publishDiscoverySensor(const char* id, const char* name,
                                   const char* jsonKey, const char* unit,
                                   const char* devClass = nullptr) {
    StaticJsonDocument<512> doc;
    doc["name"]          = name;
    doc["unique_id"]     = String(MQTT_DEVICE_ID) + "_" + id;
    doc["state_topic"]   = T_STATE;
    doc["value_template"]= String("{{ value_json.") + jsonKey + " }}";
    doc["unit_of_measurement"] = unit;
    if (devClass) doc["device_class"] = devClass;
    doc["availability_topic"] = T_AVAILABLE;

    JsonObject device = doc.createNestedObject("device");
    device["identifiers"][0] = MQTT_DEVICE_ID;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" +
                   MQTT_DEVICE_ID + "_" + id + "/config";
    g_mqtt.publish(topic.c_str(), buf, true);
}

static void publishDiscoveryBinarySensor(const char* id, const char* name,
                                          const char* jsonKey,
                                          const char* devClass = nullptr) {
    StaticJsonDocument<512> doc;
    doc["name"]          = name;
    doc["unique_id"]     = String(MQTT_DEVICE_ID) + "_" + id;
    doc["state_topic"]   = T_STATE;
    doc["value_template"]= String("{% if value_json.") + jsonKey + " %}ON{% else %}OFF{% endif %}";
    doc["payload_on"]    = "ON";
    doc["payload_off"]   = "OFF";
    if (devClass) doc["device_class"] = devClass;
    doc["availability_topic"] = T_AVAILABLE;

    JsonObject device = doc.createNestedObject("device");
    device["identifiers"][0] = MQTT_DEVICE_ID;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    String topic = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" +
                   MQTT_DEVICE_ID + "_" + id + "/config";
    g_mqtt.publish(topic.c_str(), buf, true);
}

void mqttPublishDiscovery() {
    if (!g_mqtt.connected()) return;
    publishDiscoveryClimate();
    publishDiscoverySensor("water_temp",      "HP Water Temp",      "water_temp",      "°C", "temperature");
    publishDiscoverySensor("pipe_temp",       "HP Pipe Temp",       "pipe_temp",       "°C", "temperature");
    publishDiscoverySensor("exhaust_temp",    "HP Exhaust Temp",    "exhaust_temp",    "°C", "temperature");
    publishDiscoverySensor("outdoor_temp",    "HP Outdoor Temp",    "outdoor_temp",    "°C", "temperature");
    publishDiscoverySensor("comp_run_min",    "HP Comp Run Minutes","comp_run_min",    "min");
    publishDiscoverySensor("defrost_count",   "HP Defrost Count",   "defrost_count",   "");
    publishDiscoverySensor("state",           "HP State",           "state",           "");
    publishDiscoverySensor("error",           "HP Error",           "error",           "");
    publishDiscoverySensor("work_mode",       "HP Work Mode",       "work_mode",       "");
    publishDiscoveryBinarySensor("compressor","HP Compressor",      "compressor",      "running");
    publishDiscoveryBinarySensor("fan",       "HP Fan",             "fan");
    publishDiscoveryBinarySensor("valve",     "HP Valve",           "valve");
    publishDiscoveryBinarySensor("defrost",   "HP Defrost",         "defrost");
}

// ── State publish ──────────────────────────────────────────
void mqttPublishState() {
    if (!g_mqtt.connected()) return;

    const HpStatus&  hp  = controlStatus();
    const SensorData& d  = sensorsGet();
    Settings& s          = settingsGet();

    StaticJsonDocument<768> doc;
    doc["mode"]    = (hp.state == HpState::OFFLINE) ? "off" : "heat";
    doc["setpoint"]= s.setpoint;

    if (d.waterOk)   doc["water_temp"]   = serialized(String(d.water + s.u02, 1));
    if (d.pipeOk)    doc["pipe_temp"]    = serialized(String(d.pipe, 1));
    if (d.exhaustOk) doc["exhaust_temp"] = serialized(String(d.exhaust, 1));
    if (d.outdoorOk) doc["outdoor_temp"] = serialized(String(d.outdoor, 1));

    // HA climate action: "heating", "idle", or "off"
    if (hp.state == HpState::OFFLINE) {
        doc["action"] = "off";
    } else if (hp.heating) {
        doc["action"] = "heating";
    } else {
        doc["action"] = "idle";
    }

    doc["compressor"]    = hp.compressor;
    doc["fan"]           = hp.fan;
    doc["valve"]         = hp.valve;
    doc["defrost"]       = hp.defrosting;
    doc["work_mode"]     = (s.workMode == 0) ? "auto" : "save";
    doc["comp_run_min"]  = hp.compRunMinutes;
    doc["defrost_count"] = hp.defrostCount;

    // State as string
    static const char* stateNames[] = {
        "offline","antifreeze_on","antifreeze_wait","startup_fan",
        "comp_delay","heating","shutdown_comp","shutdown_fan",
        "standby","defrost_stop","defrost_valve","defrosting",
        "defrost_end_stop","defrost_end_valve","exhaust_protect"
    };
    doc["state"] = stateNames[(int)hp.state];

    // Error as string
    static const char* errorNames[] = {
        "none","E01_water","E02_pipe","E03_exhaust","E04_outdoor","EH_exhaust_alarm"
    };
    doc["error"] = errorNames[(int)hp.error];

    char buf[768];
    serializeJson(doc, buf, sizeof(buf));
    g_mqtt.publish(T_STATE, buf, false);
}

// ── Connect / reconnect ────────────────────────────────────
static void mqttConnect() {
    Settings& s = settingsGet();
    if (strlen(s.mqttHost) == 0) return;

    g_mqtt.setServer(s.mqttHost, s.mqttPort);
    g_mqtt.setCallback(onMqttMessage);

    const char* user = strlen(s.mqttUser) > 0 ? s.mqttUser : nullptr;
    const char* pass = strlen(s.mqttPass) > 0 ? s.mqttPass : nullptr;

    esp_wifi_set_ps(WIFI_PS_NONE);  // keep radio ON for ESP-NOW
    if (g_mqtt.connect(MQTT_DEVICE_ID, user, pass, T_AVAILABLE, 1, true, "offline")) {
        g_mqtt.publish(T_AVAILABLE, "online", true);
        g_mqtt.subscribe(T_CMD);
        if (!g_discoveryDone) {
            mqttPublishDiscovery();
            g_discoveryDone = true;
        }
        mqttPublishState();
    }
}

// ── Public interface ───────────────────────────────────────
void wifiMqttInit() {
    g_wifiEnabled = settingsGet().wifiEnabled;   // restore persisted state

    g_wifiManager.setConfigPortalTimeout(180);  // 3 min AP timeout
    g_wifiManager.setConnectTimeout(20);

    // ESP-NOW requires WiFi STA mode — always init WiFi stack
    WiFi.mode(WIFI_STA);

    if (g_wifiEnabled) {
        WiFi.setAutoReconnect(true);
        // Non-blocking: if no credentials, skip portal (user can trigger manually)
        g_wifiManager.setEnableConfigPortal(false);
        g_wifiManager.autoConnect(AP_NAME);
        if (WiFi.status() == WL_CONNECTED) {
            // Disable power save — keeps radio ON for reliable ESP-NOW reception
            esp_wifi_set_ps(WIFI_PS_NONE);
        }
    } else {
        WiFi.setAutoReconnect(false);
        // Go to channel 1 so controller (also on ch1) can find us immediately
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime("IST-2IDT,M3.4.4/26,M10.5.0", "pool.ntp.org", "time.nist.gov");
    }
}

void wifiStartConfigPortal() {
    Settings& s = settingsGet();

    // Pre-fill MQTT fields with current saved values
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", s.mqttPort ? s.mqttPort : 1883);

    WiFiManagerParameter p_host("mqtt_host", "MQTT Broker IP",  s.mqttHost, 63);
    WiFiManagerParameter p_port("mqtt_port", "MQTT Port",       portStr,     5);
    WiFiManagerParameter p_user("mqtt_user", "MQTT Username",   s.mqttUser, 31);
    WiFiManagerParameter p_pass("mqtt_pass", "MQTT Password",   s.mqttPass, 31);

    g_wifiManager.addParameter(&p_host);
    g_wifiManager.addParameter(&p_port);
    g_wifiManager.addParameter(&p_user);
    g_wifiManager.addParameter(&p_pass);

    g_wifiManager.setConfigPortalTimeout(300);  // 5 min
    g_wifiManager.startConfigPortal(AP_NAME);

    // Save MQTT settings after portal closes
    strncpy(s.mqttHost, p_host.getValue(), sizeof(s.mqttHost) - 1);
    s.mqttHost[sizeof(s.mqttHost) - 1] = '\0';
    int port = atoi(p_port.getValue());
    s.mqttPort = (port > 0) ? port : 1883;
    strncpy(s.mqttUser, p_user.getValue(), sizeof(s.mqttUser) - 1);
    s.mqttUser[sizeof(s.mqttUser) - 1] = '\0';
    strncpy(s.mqttPass, p_pass.getValue(), sizeof(s.mqttPass) - 1);
    s.mqttPass[sizeof(s.mqttPass) - 1] = '\0';
    settingsSave();

    g_discoveryDone = false;  // re-publish discovery with new broker

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime("IST-2IDT,M3.4.4/26,M10.5.0", "pool.ntp.org", "time.nist.gov");
    }
}

void wifiEnable() {
    g_wifiEnabled = true;
    settingsGet().wifiEnabled = true;
    settingsSave();
    WiFi.setAutoReconnect(true);
    WiFi.reconnect();
}

void wifiDisable() {
    g_wifiEnabled = false;
    settingsGet().wifiEnabled = false;
    settingsSave();
    g_mqtt.disconnect();
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);  // disconnect from AP, keep credentials
    // Return to ch1 so the controller (on ch1) can reach us via ESP-NOW
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
}

bool wifiIsEnabled() { return g_wifiEnabled; }

void wifiMqttLoop() {
    if (!g_wifiEnabled || WiFi.status() != WL_CONNECTED) return;

    if (!g_mqtt.connected()) {
        uint32_t now = millis();
        if (now - g_lastReconnect >= 10000UL) {
            g_lastReconnect = now;
            mqttConnect();
        }
    } else {
        g_mqtt.loop();
        uint32_t now = millis();
        if (now - g_lastPublish >= T_MQTT_PUBLISH_MS) {
            g_lastPublish = now;
            mqttPublishState();
        }
    }
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool mqttConnected() { return g_mqtt.connected(); }
