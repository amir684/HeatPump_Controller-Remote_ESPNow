#include "web.h"
#include "web_page.h"
#include "config.h"
#include "settings.h"
#include "control.h"
#include "sensors.h"
#include "espnow_remote.h"
#include "ota_remote.h"
#include "fw_info.h"
#include "wifi_mqtt.h"
#include "display.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>

static WebServer g_server(80);
static bool      g_running     = false;
static bool      g_mdnsStarted = false;
static uint32_t  g_rebootAt    = 0;      // 0 = no reboot scheduled

// Upload state (one upload at a time)
static char      g_uploadErr[64] = "";
static bool      g_uploadFirst   = false;
static bool      g_updateBegun   = false;

// ── Helpers ─────────────────────────────────────────────────
static bool checkAuth() {
    if (g_server.authenticate(WEB_USER, settingsGet().webPass)) return true;
    g_server.requestAuthentication(BASIC_AUTH, "HeatPump");
    return false;
}

static void sendError(int code, const char* msg) {
    g_server.send(code, "text/plain", msg);
}

static const char* otaStateName(CtrlOtaState s) {
    switch (s) {
    case CtrlOtaState::STAGING: return "uploading";
    case CtrlOtaState::BEGIN:   return "starting";
    case CtrlOtaState::SENDING: return "sending";
    case CtrlOtaState::ENDING:  return "verifying";
    case CtrlOtaState::DONE:    return "done";
    case CtrlOtaState::FAILED:  return "failed";
    default:                    return "idle";
    }
}

// ── /api/status ─────────────────────────────────────────────
static void handleStatus() {
    if (!checkAuth()) return;
    const HpStatus&   hp = controlStatus();
    const SensorData& d  = sensorsGet();
    const Settings&   s  = settingsGet();

    StaticJsonDocument<2048> doc;
    doc["state"]     = hpStateName(hp.state);
    doc["error"]     = hpErrorName(hp.error);
    doc["online"]    = hp.state != HpState::OFFLINE;
    doc["link"]      = espnowCtrlConnected();
    if (d.waterOk)   doc["water"]   = sensorCorrectedWater();
    if (d.pipeOk)    doc["pipe"]    = d.pipe;
    if (d.exhaustOk) doc["exhaust"] = d.exhaust;
    if (d.outdoorOk) doc["outdoor"] = d.outdoor;
    doc["comp"]         = hp.compressor;
    doc["fan"]          = hp.fan;
    doc["valve"]        = hp.valve;
    doc["defrost"]      = hp.defrosting;
    doc["compRunMin"]   = hp.compRunMinutes;
    doc["defrostCount"] = hp.defrostCount;

    JsonObject set = doc.createNestedObject("settings");
    set["sp"]  = s.setpoint;
    set["u03"] = s.u03;  set["u01"] = s.u01;  set["u02"] = s.u02;
    set["u30"] = s.u30;  set["u31"] = s.u31;  set["u32"] = s.u32;
    set["u33"] = s.u33;  set["u34"] = s.u34;
    set["u50"] = s.u50;  set["u51"] = s.u51;
    set["u52"] = s.u52;  set["u80"] = s.u80;
    set["mqttHost"] = s.mqttHost;
    set["mqttPort"] = s.mqttPort;
    set["mqttUser"] = s.mqttUser;

    char ctrlVer[16], ctrlBuilt[24];
    espnowCtrlFirmware(ctrlVer, sizeof(ctrlVer), ctrlBuilt, sizeof(ctrlBuilt));
    JsonObject fw = doc.createNestedObject("fw");
    fw["remote"]      = g_fwInfo.version;
    fw["remoteBuilt"] = g_fwInfo.built;
    fw["ctrl"]        = ctrlVer;
    fw["ctrlBuilt"]   = ctrlBuilt;

    const CtrlOtaStatus& ota = otaRemoteStatus();
    JsonObject o = doc.createNestedObject("ota");
    o["state"] = otaStateName(ota.state);
    o["pct"]   = ota.size ? (int)(100ULL * ota.sent / ota.size) : 0;
    o["msg"]   = ota.msg;

    doc["rssi"]   = WiFi.RSSI();
    doc["mqtt"]   = mqttConnected();
    doc["uptime"] = millis() / 1000;

    String out;
    serializeJson(doc, out);
    g_server.send(200, "application/json", out);
}

// ── /api/settings (POST JSON) ───────────────────────────────
// All-or-nothing: every field present must be in range, or nothing is saved.
static bool readFloat(JsonObject in, const char* key, float lo, float hi, float& out, String& err) {
    if (!in.containsKey(key)) return true;
    float v = in[key].as<float>();
    if (isnan(v) || v < lo || v > hi) {
        err = String(key) + " out of range (" + lo + ".." + hi + ")";
        return false;
    }
    out = v;
    return true;
}

static bool readInt(JsonObject in, const char* key, int lo, int hi, uint8_t& out, String& err) {
    if (!in.containsKey(key)) return true;
    int v = in[key].as<int>();
    if (v < lo || v > hi) {
        err = String(key) + " out of range (" + lo + ".." + hi + ")";
        return false;
    }
    out = (uint8_t)v;
    return true;
}

static bool readStr(JsonObject in, const char* key, char* out, size_t outLen, String& err) {
    if (!in.containsKey(key)) return true;
    const char* v = in[key] | "";
    if (strlen(v) >= outLen) {
        err = String(key) + " too long";
        return false;
    }
    strlcpy(out, v, outLen);
    return true;
}

static void handleSettings() {
    if (!checkAuth()) return;
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, g_server.arg("plain")) != DeserializationError::Ok) {
        sendError(400, "bad JSON");
        return;
    }
    JsonObject in = doc.as<JsonObject>();
    Settings   n  = settingsGet();   // edit a copy, commit only if all valid
    String     err;

    // Same ranges as the on-device menu
    bool ok =
        readFloat(in, "u03", 30.0f,  70.0f, n.u03, err) &&
        readFloat(in, "sp",  10.0f,  70.0f, n.setpoint, err) &&
        readFloat(in, "u01",  2.0f,  10.0f, n.u01, err) &&
        readFloat(in, "u02", -9.9f,   9.9f, n.u02, err) &&
        readInt  (in, "u30", 10, 90, n.u30, err) &&
        readFloat(in, "u31", -20.0f,  0.0f, n.u31, err) &&
        readFloat(in, "u32",  2.0f,  30.0f, n.u32, err) &&
        readInt  (in, "u33",  2, 15, n.u33, err) &&
        readInt  (in, "u34",  0,  1, n.u34, err) &&
        readInt  (in, "u50",  0,  2, n.u50, err) &&
        readFloat(in, "u51", 50.0f, 125.0f, n.u51, err) &&
        readInt  (in, "u52",  0,  1, n.u52, err) &&
        readInt  (in, "u80",  0,  1, n.u80, err) &&
        readStr  (in, "mqttHost", n.mqttHost, sizeof(n.mqttHost), err) &&
        readStr  (in, "mqttUser", n.mqttUser, sizeof(n.mqttUser), err) &&
        readStr  (in, "mqttPass", n.mqttPass, sizeof(n.mqttPass), err);

    if (ok && in.containsKey("mqttPort")) {
        long port = in["mqttPort"].as<long>();
        if (port < 1 || port > 65535) { err = "mqttPort out of range"; ok = false; }
        else n.mqttPort = (uint16_t)port;
    }
    if (ok && in.containsKey("webPass")) {
        const char* p = in["webPass"] | "";
        if (strlen(p) < 4 || strlen(p) >= sizeof(n.webPass)) {
            err = "password must be 4-31 characters";
            ok = false;
        } else {
            strlcpy(n.webPass, p, sizeof(n.webPass));
        }
    }
    if (!ok) {
        sendError(400, err.c_str());
        return;
    }
    if (n.setpoint > n.u03) n.setpoint = n.u03;

    Settings& s = settingsGet();
    bool mqttChanged = strcmp(s.mqttHost, n.mqttHost) != 0 || s.mqttPort != n.mqttPort ||
                       strcmp(s.mqttUser, n.mqttUser) != 0 || strcmp(s.mqttPass, n.mqttPass) != 0;
    s = n;
    settingsSave();   // marks dirty → main loop pushes to the Controller
    if (mqttChanged) mqttRestart();

    g_server.send(200, "text/plain", "saved");
}

// ── /api/cmd (POST JSON) ────────────────────────────────────
static void handleCmd() {
    if (!checkAuth()) return;
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, g_server.arg("plain")) != DeserializationError::Ok) {
        sendError(400, "bad JSON");
        return;
    }
    if (doc.containsKey("power")) {
        controlSetOnline(doc["power"].as<bool>());
    } else if (doc["defrost"] | false) {
        controlForceDefrost();
    } else {
        sendError(400, "unknown command");
        return;
    }
    g_server.send(200, "text/plain", "sent");
}

// ── /update/remote — this board's own firmware ──────────────
static void uploadRemote() {
    HTTPUpload& up = g_server.upload();
    switch (up.status) {
    case UPLOAD_FILE_START:
        g_uploadErr[0] = '\0';
        g_uploadFirst  = true;
        g_updateBegun  = false;
        if (!g_server.authenticate(WEB_USER, settingsGet().webPass)) {
            strlcpy(g_uploadErr, "unauthorized", sizeof(g_uploadErr));
        } else if (otaRemoteBusy()) {
            strlcpy(g_uploadErr, "controller update in progress", sizeof(g_uploadErr));
        }
        break;
    case UPLOAD_FILE_WRITE:
        if (g_uploadErr[0]) break;
        if (g_uploadFirst) {
            g_uploadFirst = false;
            if (!fwInfoCheck(up.buf, up.currentSize, FW_ROLE_REMOTE, g_uploadErr, sizeof(g_uploadErr))) break;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                strlcpy(g_uploadErr, Update.errorString(), sizeof(g_uploadErr));
                break;
            }
            g_updateBegun = true;
            displayToast("REMOTE UPDATE", "receiving...", 60000);
        }
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            strlcpy(g_uploadErr, Update.errorString(), sizeof(g_uploadErr));
        }
        break;
    case UPLOAD_FILE_END:
        if (g_uploadErr[0]) break;
        if (!g_updateBegun) {
            strlcpy(g_uploadErr, "empty file", sizeof(g_uploadErr));
        } else if (!Update.end(true)) {
            strlcpy(g_uploadErr, Update.errorString(), sizeof(g_uploadErr));
        }
        break;
    case UPLOAD_FILE_ABORTED:
        strlcpy(g_uploadErr, "upload aborted", sizeof(g_uploadErr));
        break;
    }
    if (g_uploadErr[0] && g_updateBegun) {
        Update.abort();
        g_updateBegun = false;
        displayToast("REMOTE UPDATE", "FAILED", 4000);
    }
}

static void finishRemote() {
    if (!checkAuth()) return;
    if (g_uploadErr[0]) {
        sendError(400, g_uploadErr);
        return;
    }
    g_server.send(200, "text/plain", "OK - remote rebooting");
    displayToast("REMOTE UPDATE", "OK - rebooting", 3000);
    g_rebootAt = millis() + 1500;
}

// ── /update/ctrl — Controller firmware, relayed over ESP-NOW ─
static void uploadCtrl() {
    HTTPUpload& up = g_server.upload();
    switch (up.status) {
    case UPLOAD_FILE_START:
        g_uploadErr[0] = '\0';
        if (!g_server.authenticate(WEB_USER, settingsGet().webPass)) {
            strlcpy(g_uploadErr, "unauthorized", sizeof(g_uploadErr));
        } else if (!espnowCtrlConnected()) {
            strlcpy(g_uploadErr, "no link to controller", sizeof(g_uploadErr));
        } else {
            otaStageBegin(g_uploadErr, sizeof(g_uploadErr));
        }
        break;
    case UPLOAD_FILE_WRITE:
        if (g_uploadErr[0]) break;
        otaStageWrite(up.buf, up.currentSize, g_uploadErr, sizeof(g_uploadErr));
        break;
    case UPLOAD_FILE_END:
        if (g_uploadErr[0]) break;
        otaStageEnd(g_uploadErr, sizeof(g_uploadErr));
        break;
    case UPLOAD_FILE_ABORTED:
        strlcpy(g_uploadErr, "upload aborted", sizeof(g_uploadErr));
        otaStageAbort();
        break;
    }
}

static void finishCtrl() {
    if (!checkAuth()) return;
    if (g_uploadErr[0]) {
        sendError(400, g_uploadErr);
        return;
    }
    g_server.send(200, "text/plain", "uploaded - sending to controller");
}

// ── Public interface ────────────────────────────────────────
static void addRoutes() {
    g_server.on("/", HTTP_GET, [] {
        if (!checkAuth()) return;
        g_server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
    });
    g_server.on("/api/status",   HTTP_GET,  handleStatus);
    g_server.on("/api/settings", HTTP_POST, handleSettings);
    g_server.on("/api/cmd",      HTTP_POST, handleCmd);
    g_server.on("/update/remote", HTTP_POST, finishRemote, uploadRemote);
    g_server.on("/update/ctrl",   HTTP_POST, finishCtrl,   uploadCtrl);
    g_server.onNotFound([] { sendError(404, "not found"); });
}

void webBegin() {
    if (g_running) return;
    static bool routesAdded = false;   // webBegin() runs again after the WiFi portal
    if (!routesAdded) {
        addRoutes();
        routesAdded = true;
    }
    g_server.begin();
    g_running = true;
}

void webStop() {
    if (!g_running) return;
    g_server.stop();
    g_running = false;
}

void webLoop() {
    if (g_rebootAt && (int32_t)(millis() - g_rebootAt) >= 0) {
        ESP.restart();
    }
    if (!g_running) return;

    if (!g_mdnsStarted && WiFi.status() == WL_CONNECTED) {
        if (MDNS.begin(WEB_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            g_mdnsStarted = true;
            Serial.printf("[WEB] http://%s.local  (%s)\n", WEB_HOSTNAME,
                          WiFi.localIP().toString().c_str());
        }
    }
    g_server.handleClient();
}
