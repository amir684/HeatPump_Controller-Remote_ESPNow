#include "espnow_ctrl.h"
#include "shared.h"
#include "control.h"
#include "sensors.h"
#include "settings.h"
#include "ota_ctrl.h"
#include "fw_info.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ── Timing ──────────────────────────────────────────────────
#define ESPNOW_SEND_INTERVAL_MS   500UL
#define ESPNOW_TIMEOUT_MS        5000UL
#define CHANNEL_SCAN_MS     600UL    // dwell per channel while scanning
#define SCAN_AFTER_MS      5000UL    // scan if no successful ACK for 5 s
#define INFO_INTERVAL_MS   5000UL    // firmware version broadcast to Remote

// ── Channel scanning ─────────────────────────────────────────
static const uint8_t k_channels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
static const uint8_t k_numChan    = sizeof(k_channels) / sizeof(k_channels[0]);
static uint8_t          g_chIdx          = 0;
static uint32_t         g_lastChanSwitch = 0;
static volatile bool    g_gotSuccess     = false;   // set from WiFi task
static uint32_t         g_lastSuccess    = 0;
static bool             g_scanning       = false;

static uint32_t g_lastSend      = 0;
static uint32_t g_lastInfo      = 0;
static uint32_t g_lastReceived  = 0;

static const uint8_t k_remoteMac[] = REMOTE_MAC_BYTES;

// ── Helpers ─────────────────────────────────────────────────
template<typename T>
static T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool validateAndApplySettings(const SettingsPacket& pkt) {
    // Float range checks — reject packet if NaN or grossly out of range
    if (isnan(pkt.setpoint) || isnan(pkt.u01) || isnan(pkt.u02) ||
        isnan(pkt.u31)      || isnan(pkt.u32) || isnan(pkt.u51)) {
        Serial.println("[ESPNOW-CTRL] Settings rejected: NaN value");
        return false;
    }

    Settings& s = settingsGet();
    s.setpoint = clamp(pkt.setpoint, 10.0f,  70.0f);
    s.u01      = clamp(pkt.u01,       2.0f,  10.0f);
    s.u02      = clamp(pkt.u02,      -9.9f,   9.9f);
    s.u30      = clamp(pkt.u30,      (uint8_t)10,  (uint8_t)90);
    s.u31      = clamp(pkt.u31,     -20.0f,   0.0f);
    s.u32      = clamp(pkt.u32,       2.0f,  30.0f);
    s.u33      = clamp(pkt.u33,      (uint8_t)2,   (uint8_t)15);
    s.u34      = clamp(pkt.u34,      (uint8_t)0,   (uint8_t)1);
    s.u50      = clamp(pkt.u50,      (uint8_t)0,   (uint8_t)2);
    s.u51      = clamp(pkt.u51,      50.0f, 125.0f);
    s.u52      = clamp(pkt.u52,      (uint8_t)0,   (uint8_t)1);
    s.u80      = clamp(pkt.u80,      (uint8_t)0,   (uint8_t)1);
    s.workMode = clamp(pkt.workMode, (uint8_t)0,   (uint8_t)1);
    s.systemOnline = pkt.systemOnline;

    for (int i = 0; i < 3; i++) {
        s.period[i].onHour  = clamp(pkt.periodOnHour[i],  (uint8_t)0, (uint8_t)23);
        s.period[i].onMin   = clamp(pkt.periodOnMin[i],   (uint8_t)0, (uint8_t)59);
        s.period[i].offHour = clamp(pkt.periodOffHour[i], (uint8_t)0, (uint8_t)23);
        s.period[i].offMin  = clamp(pkt.periodOffMin[i],  (uint8_t)0, (uint8_t)59);
        s.period[i].enabled = pkt.periodEnabled[i];
    }
    return true;
}

// ── Callbacks ───────────────────────────────────────────────
static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        g_gotSuccess = true;
    }
}

static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 1) return;
    // Only the paired Remote may command the heat pump or flash firmware
    if (memcmp(mac, k_remoteMac, 6) != 0) return;

    // Hearing the Remote proves the current channel is right
    g_gotSuccess = true;

    if (otaCtrlHandlePacket(data, len)) {
        g_lastReceived = millis();
        return;
    }

    const PktType type = *reinterpret_cast<const PktType*>(data);

    if (type == PktType::COMMAND && len >= (int)sizeof(CommandPacket)) {
        const CommandPacket& pkt = *reinterpret_cast<const CommandPacket*>(data);
        switch (pkt.cmd) {
        case CmdType::SET_ONLINE:    controlSetOnline(pkt.bVal); break;
        case CmdType::FORCE_DEFROST: controlForceDefrost();      break;
        }
    }
    else if (type == PktType::SETTINGS && len >= (int)sizeof(SettingsPacket)) {
        const SettingsPacket& pkt = *reinterpret_cast<const SettingsPacket*>(data);
        if (validateAndApplySettings(pkt)) {
            settingsSave();
        }
    }

    g_lastReceived = millis();
}

// ── Channel switch helper ────────────────────────────────────
static void switchToChannel(uint8_t idx) {
    uint8_t ch = k_channels[idx];
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    esp_now_peer_info_t peer{};
    const uint8_t remoteMac[] = REMOTE_MAC_BYTES;
    memcpy(peer.peer_addr, remoteMac, 6);
    peer.channel = ch;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;
    esp_now_mod_peer(&peer);

    Serial.printf("[ESPNOW-CTRL] Scanning ch=%d\n", ch);
}

// ── Init ────────────────────────────────────────────────────
void espnowCtrlInit() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    g_chIdx       = 0;
    g_lastSuccess = millis();   // give channel 1 a 5-second window before scanning
    esp_wifi_set_channel(k_channels[g_chIdx], WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW-CTRL] esp_now_init failed!");
        return;
    }
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceive);

    esp_now_peer_info_t peer{};
    const uint8_t remoteMac[] = REMOTE_MAC_BYTES;
    memcpy(peer.peer_addr, remoteMac, 6);
    peer.channel = k_channels[g_chIdx];
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW-CTRL] Failed to add peer!");
    } else {
        Serial.printf("[ESPNOW-CTRL] Ready, ch=%d\n", k_channels[g_chIdx]);
    }
}

// ── Loop ────────────────────────────────────────────────────
void espnowCtrlLoop() {
    uint32_t now = millis();

    // Process ACK success flag from WiFi task
    if (g_gotSuccess) {
        g_gotSuccess  = false;
        g_lastSuccess = now;
        g_scanning    = false;
    }

    // Start scanning only if no successful ACK for SCAN_AFTER_MS.
    // Never hop channels mid-update: a stalled transfer times out on its own.
    if (!g_scanning && !otaCtrlActive() && (now - g_lastSuccess >= SCAN_AFTER_MS)) {
        g_scanning       = true;
        g_lastChanSwitch = now;  // give current channel one full dwell first
    }

    // Advance to next channel while scanning
    if (g_scanning && (now - g_lastChanSwitch >= CHANNEL_SCAN_MS)) {
        g_lastChanSwitch = now;
        g_chIdx = (g_chIdx + 1) % k_numChan;
        switchToChannel(g_chIdx);
    }

    if (now - g_lastInfo >= INFO_INTERVAL_MS) {
        g_lastInfo = now;
        InfoPacket info{};
        info.type = PktType::INFO;
        strncpy(info.version, g_fwInfo.version, sizeof(info.version) - 1);
        strncpy(info.built,   g_fwInfo.built,   sizeof(info.built) - 1);
        info.otaActive = otaCtrlActive();
        esp_now_send(k_remoteMac, reinterpret_cast<const uint8_t*>(&info), sizeof(info));
    }

    if (now - g_lastSend < ESPNOW_SEND_INTERVAL_MS) return;
    g_lastSend = now;

    const HpStatus&   hp = controlStatus();
    const SensorData& d  = sensorsGet();

    StatusPacket pkt{};
    pkt.type           = PktType::STATUS;
    pkt.state          = static_cast<uint8_t>(hp.state);
    pkt.error          = static_cast<uint8_t>(hp.error);
    pkt.compressor     = hp.compressor;
    pkt.fan            = hp.fan;
    pkt.valve          = hp.valve;
    pkt.defrosting     = hp.defrosting;
    pkt.heating        = hp.heating;
    pkt.compRunMinutes = hp.compRunMinutes;
    pkt.defrostCount   = hp.defrostCount;
    pkt.water          = d.water;
    pkt.pipe           = d.pipe;
    pkt.exhaust        = d.exhaust;
    pkt.outdoor        = d.outdoor;
    pkt.waterOk        = d.waterOk;
    pkt.pipeOk         = d.pipeOk;
    pkt.exhaustOk      = d.exhaustOk;
    pkt.outdoorOk      = d.outdoorOk;

    const uint8_t remoteMac[] = REMOTE_MAC_BYTES;
    esp_now_send(remoteMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

// ── Connection status ────────────────────────────────────────
bool espnowRemoteConnected() {
    return g_lastReceived != 0 && (millis() - g_lastReceived) < ESPNOW_TIMEOUT_MS;
}
