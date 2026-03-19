#include "espnow_remote.h"
#include "shared.h"
#include "settings.h"
#include "buzzer.h"
#include <WiFi.h>
#include <esp_now.h>

// ── Timing ──────────────────────────────────────────────────
#define ESPNOW_RESYNC_INTERVAL_MS   30000UL
#define ESPNOW_TIMEOUT_MS            5000UL
#define ESPNOW_DEFROST_RETRY_MS      3000UL
#define ESPNOW_DEFROST_TIMEOUT_MS   15000UL

#include "control.h"
#include "sensors.h"
#include "display.h"
extern void remoteUpdateStatus(const StatusPacket& pkt);

static uint32_t        g_lastReceived    = 0;
static uint32_t        g_lastResync      = 0;
static bool            g_defrostPending  = false;
static uint32_t        g_lastDefrostSend = 0;
static uint32_t        g_defrostSentAt   = 0;
static volatile bool   g_sendFailed      = false;
static volatile bool   g_lastWasCommand  = false;  // true = last send was a command (not settings)

// ── Callbacks ───────────────────────────────────────────────
static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) g_sendFailed = true;
}

static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 1) return;
    const PktType type = *reinterpret_cast<const PktType*>(data);
    if (type == PktType::STATUS && len >= (int)sizeof(StatusPacket)) {
        const StatusPacket& pkt = *reinterpret_cast<const StatusPacket*>(data);
        remoteUpdateStatus(pkt);
        g_lastReceived = millis();
        if (g_defrostPending && pkt.defrosting) {
            g_defrostPending = false;
        }
    }
}

// ── Init ────────────────────────────────────────────────────
void espnowRemoteInit() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW-REMOTE] esp_now_init failed!");
        return;
    }
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceive);

    esp_now_peer_info_t peer{};
    const uint8_t ctrlMac[] = CTRL_MAC_BYTES;
    memcpy(peer.peer_addr, ctrlMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW-REMOTE] Failed to add peer!");
    } else {
        Serial.printf("[ESPNOW-REMOTE] Ready, ch=%d\n", WiFi.channel());
    }
}

// ── Loop ────────────────────────────────────────────────────
void espnowRemoteLoop() {
    uint32_t now = millis();

    if (g_sendFailed) {
        g_sendFailed = false;
        if (g_lastWasCommand) {
            displayToast("Send failed", "Will retry...", 3000);
            buzzerCommFail();
        }
    }

    if (now - g_lastResync >= ESPNOW_RESYNC_INTERVAL_MS) {
        g_lastResync = now;
        espnowSendSettings();
    }
    if (g_defrostPending) {
        if (now - g_defrostSentAt >= ESPNOW_DEFROST_TIMEOUT_MS) {
            g_defrostPending = false;
            displayToast("CMD FAILED", "Defrost timeout", 4000);
        } else if (now - g_lastDefrostSend >= ESPNOW_DEFROST_RETRY_MS) {
            g_lastDefrostSend = now;
            CommandPacket pkt{};
            pkt.type = PktType::COMMAND;
            pkt.cmd  = CmdType::FORCE_DEFROST;
            espnowSendCommand(pkt);
        }
    }
}

void espnowSetDefrostPending(bool pending) {
    g_defrostPending  = pending;
    g_lastDefrostSend = 0;
    g_defrostSentAt   = millis();
}

// ── Send helpers ────────────────────────────────────────────
void espnowSendCommand(CommandPacket& pkt) {
    g_lastWasCommand = true;
    const uint8_t ctrlMac[] = CTRL_MAC_BYTES;
    esp_now_send(ctrlMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void espnowSendSettings() {
    g_lastWasCommand = false;
    const Settings& s = settingsGet();
    SettingsPacket pkt{};
    pkt.type         = PktType::SETTINGS;
    pkt.setpoint     = s.setpoint;
    pkt.u01          = s.u01;
    pkt.u02          = s.u02;
    pkt.u30          = s.u30;
    pkt.u31          = s.u31;
    pkt.u32          = s.u32;
    pkt.u33          = s.u33;
    pkt.u34          = s.u34;
    pkt.u50          = s.u50;
    pkt.u51          = s.u51;
    pkt.u52          = s.u52;
    pkt.u80          = s.u80;
    pkt.workMode     = s.workMode;
    pkt.systemOnline = s.systemOnline;
    for (int i = 0; i < 3; i++) {
        pkt.periodOnHour[i]  = s.period[i].onHour;
        pkt.periodOnMin[i]   = s.period[i].onMin;
        pkt.periodOffHour[i] = s.period[i].offHour;
        pkt.periodOffMin[i]  = s.period[i].offMin;
        pkt.periodEnabled[i] = s.period[i].enabled;
    }
    const uint8_t ctrlMac[] = CTRL_MAC_BYTES;
    esp_now_send(ctrlMac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

// ── Connection status ────────────────────────────────────────
bool espnowCtrlConnected() {
    return (millis() - g_lastReceived) < ESPNOW_TIMEOUT_MS;
}

bool espnowCtrlSignalLost() {
    return (millis() - g_lastReceived) > 1500UL;
}
