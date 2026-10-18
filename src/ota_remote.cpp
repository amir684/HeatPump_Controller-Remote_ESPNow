#include "ota_remote.h"
#include "shared.h"
#include "fw_info.h"
#include "display.h"
#include "espnow_remote.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <MD5Builder.h>
#include <Update.h>

// ── Timing ──────────────────────────────────────────────────
#define OTA_BEGIN_TIMEOUT_MS   3000UL
#define OTA_DATA_TIMEOUT_MS     400UL
#define OTA_END_TIMEOUT_MS    10000UL   // Controller verifies the whole image
#define OTA_MAX_RETRIES           8
#define OTA_SELF_CONFIRM_MS   60000UL   // own new image is confirmed after 1 min uptime
#define FLASH_SECTOR           4096

static CtrlOtaStatus          g_st         = {CtrlOtaState::IDLE, 0, 0, ""};
static const esp_partition_t* g_part       = nullptr;
static uint32_t               g_erasedTo   = 0;
static MD5Builder             g_md5b;
static uint8_t                g_md5[16]    = {};
static bool                   g_infoChecked = false;

// Sender
static bool     g_waiting    = false;
static uint32_t g_lastSendMs = 0;
static uint8_t  g_retries    = 0;
static uint32_t g_lastToast  = 0;

// ACK hand-off from the WiFi task
static portMUX_TYPE  g_ackMux   = portMUX_INITIALIZER_UNLOCKED;
static OtaAckPacket  g_ack      = {};
static volatile bool g_ackReady = false;

static bool g_selfPendingVerify = false;

// Arduino core hook: keep our own new image unconfirmed until it has run a while
extern "C" bool verifyRollbackLater() { return true; }

// ── Helpers ─────────────────────────────────────────────────
static void setMsg(const char* msg) {
    strncpy(g_st.msg, msg, sizeof(g_st.msg) - 1);
    g_st.msg[sizeof(g_st.msg) - 1] = '\0';
}

static void fail(const char* msg) {
    g_st.state = CtrlOtaState::FAILED;
    g_waiting  = false;
    setMsg(msg);
    OtaCtlPacket abortPkt{PktType::OTA_ABORT};   // best effort
    espnowSendRaw(&abortPkt, sizeof(abortPkt));
    displayToast("CTRL UPDATE", "FAILED", 5000);
    Serial.printf("[OTA-REMOTE] Controller update failed: %s\n", msg);
}

static uint32_t timeoutFor(CtrlOtaState s) {
    switch (s) {
    case CtrlOtaState::BEGIN:  return OTA_BEGIN_TIMEOUT_MS;
    case CtrlOtaState::ENDING: return OTA_END_TIMEOUT_MS;
    default:                   return OTA_DATA_TIMEOUT_MS;
    }
}

// Sends the packet for the current state (first try or retry)
static void sendCurrent() {
    switch (g_st.state) {
    case CtrlOtaState::BEGIN: {
        OtaBeginPacket p{};
        p.type = PktType::OTA_BEGIN;
        p.size = g_st.size;
        memcpy(p.md5, g_md5, sizeof(p.md5));
        espnowSendRaw(&p, sizeof(p));
        break;
    }
    case CtrlOtaState::SENDING: {
        OtaDataPacket p{};
        p.type   = PktType::OTA_DATA;
        p.offset = g_st.sent;
        uint32_t left = g_st.size - g_st.sent;
        p.len    = left < OTA_CHUNK_SIZE ? left : OTA_CHUNK_SIZE;
        if (esp_partition_read(g_part, p.offset, p.data, p.len) != ESP_OK) {
            fail("staging read error");
            return;
        }
        espnowSendRaw(&p, offsetof(OtaDataPacket, data) + p.len);
        break;
    }
    case CtrlOtaState::ENDING: {
        OtaCtlPacket p{PktType::OTA_END};
        espnowSendRaw(&p, sizeof(p));
        break;
    }
    default:
        return;
    }
    g_waiting    = true;
    g_lastSendMs = millis();
}

static PktType expectedAck(CtrlOtaState s) {
    switch (s) {
    case CtrlOtaState::BEGIN:  return PktType::OTA_BEGIN;
    case CtrlOtaState::ENDING: return PktType::OTA_END;
    default:                   return PktType::OTA_DATA;
    }
}

static const char* resultStr(OtaResult r) {
    switch (r) {
    case OtaResult::ERR_STATE:  return "controller: bad state";
    case OtaResult::ERR_SIZE:   return "controller: image too large";
    case OtaResult::ERR_BEGIN:  return "controller: cannot start update";
    case OtaResult::ERR_WRITE:  return "controller: flash write error";
    case OtaResult::ERR_MD5:    return "controller: MD5 mismatch";
    case OtaResult::ERR_IMAGE:  return "controller: image rejected";
    default:                    return "controller: error";
    }
}

static void handleAck(const OtaAckPacket& ack) {
    if (!g_waiting || ack.ackOf != expectedAck(g_st.state)) return;
    g_waiting = false;
    g_retries = 0;

    if (ack.result == OtaResult::ERR_OFFSET && g_st.state == CtrlOtaState::SENDING) {
        g_st.sent = ack.nextOffset;   // resync to what the Controller has
        sendCurrent();
        return;
    }
    if (ack.result != OtaResult::OK) {
        fail(resultStr(ack.result));
        return;
    }

    switch (g_st.state) {
    case CtrlOtaState::BEGIN:
        g_st.state = CtrlOtaState::SENDING;
        g_st.sent  = 0;
        setMsg("sending");
        sendCurrent();
        break;
    case CtrlOtaState::SENDING:
        g_st.sent = ack.nextOffset;
        if (g_st.sent >= g_st.size) {
            g_st.state = CtrlOtaState::ENDING;
            setMsg("verifying");
        }
        sendCurrent();
        break;
    case CtrlOtaState::ENDING:
        g_st.state = CtrlOtaState::DONE;
        setMsg("done - controller rebooting");
        displayToast("CTRL UPDATE", "OK - rebooting", 5000);
        Serial.println("[OTA-REMOTE] Controller update OK");
        break;
    default:
        break;
    }
}

// ── Staging ─────────────────────────────────────────────────
bool otaStageBegin(char* err, size_t errLen) {
    // Both updates use this board's inactive OTA slot
    if (otaRemoteBusy() || Update.isRunning()) {
        snprintf(err, errLen, "another update is running");
        return false;
    }
    g_part = esp_ota_get_next_update_partition(nullptr);
    if (!g_part) {
        snprintf(err, errLen, "no OTA partition (flash new partition table by USB)");
        return false;
    }
    g_st.state  = CtrlOtaState::STAGING;
    g_st.size   = 0;
    g_st.sent   = 0;
    setMsg("uploading");
    g_erasedTo  = 0;
    g_infoChecked = false;
    g_md5b.begin();
    return true;
}

bool otaStageWrite(const uint8_t* buf, size_t len, char* err, size_t errLen) {
    if (g_st.state != CtrlOtaState::STAGING) {
        snprintf(err, errLen, "upload not started");
        return false;
    }
    // The first chunk carries the image header and FwInfo
    if (!g_infoChecked) {
        if (!fwInfoCheck(buf, len, FW_ROLE_CONTROLLER, err, errLen)) {
            otaStageAbort();
            return false;
        }
        g_infoChecked = true;
    }
    if (g_st.size + len > g_part->size) {
        snprintf(err, errLen, "file too large");
        otaStageAbort();
        return false;
    }
    while (g_erasedTo < g_st.size + len) {
        if (esp_partition_erase_range(g_part, g_erasedTo, FLASH_SECTOR) != ESP_OK) {
            snprintf(err, errLen, "flash erase error");
            otaStageAbort();
            return false;
        }
        g_erasedTo += FLASH_SECTOR;
    }
    if (esp_partition_write(g_part, g_st.size, buf, len) != ESP_OK) {
        snprintf(err, errLen, "flash write error");
        otaStageAbort();
        return false;
    }
    g_md5b.add(const_cast<uint8_t*>(buf), len);
    g_st.size += len;
    return true;
}

bool otaStageEnd(char* err, size_t errLen) {
    if (g_st.state != CtrlOtaState::STAGING || g_st.size == 0) {
        snprintf(err, errLen, "nothing uploaded");
        otaStageAbort();
        return false;
    }
    if (!espnowCtrlConnected()) {
        snprintf(err, errLen, "no link to controller");
        otaStageAbort();
        return false;
    }
    g_md5b.calculate();
    g_md5b.getBytes(g_md5);

    Serial.printf("[OTA-REMOTE] Staged %u bytes, sending to controller\n", (unsigned)g_st.size);
    g_st.state = CtrlOtaState::BEGIN;
    g_retries  = 0;
    setMsg("starting");
    sendCurrent();
    return true;
}

void otaStageAbort() {
    g_st.state = CtrlOtaState::IDLE;
    g_st.size  = 0;
    g_st.sent  = 0;
    g_waiting  = false;
}

// ── ACK from WiFi task ──────────────────────────────────────
void otaRemoteOnAck(const uint8_t* data, int len) {
    if (len < (int)sizeof(OtaAckPacket)) return;
    portENTER_CRITICAL(&g_ackMux);
    memcpy(&g_ack, data, sizeof(g_ack));
    g_ackReady = true;
    portEXIT_CRITICAL(&g_ackMux);
}

// ── Public interface ────────────────────────────────────────
void otaRemoteInit() {
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        g_selfPendingVerify = true;
    }
}

void otaRemoteLoop() {
    uint32_t now = millis();

    // Our own new image: confirm once it has run for a minute without crashing
    if (g_selfPendingVerify && now >= OTA_SELF_CONFIRM_MS) {
        esp_ota_mark_app_valid_cancel_rollback();
        g_selfPendingVerify = false;
        Serial.println("[OTA-REMOTE] Own new image confirmed");
    }

    if (g_ackReady) {
        OtaAckPacket ack;
        portENTER_CRITICAL(&g_ackMux);
        ack = g_ack;
        g_ackReady = false;
        portEXIT_CRITICAL(&g_ackMux);
        handleAck(ack);
    }

    if (g_waiting && now - g_lastSendMs >= timeoutFor(g_st.state)) {
        if (++g_retries > OTA_MAX_RETRIES) {
            fail("no response from controller");
        } else {
            sendCurrent();
        }
    }

    if (g_st.state == CtrlOtaState::SENDING && now - g_lastToast >= 1000UL) {
        g_lastToast = now;
        char pct[24];
        snprintf(pct, sizeof(pct), "%u%%", (unsigned)(100ULL * g_st.sent / g_st.size));
        displayToast("CTRL UPDATE", pct, 1500);
    }
}

bool otaRemoteBusy() {
    switch (g_st.state) {
    case CtrlOtaState::STAGING:
    case CtrlOtaState::BEGIN:
    case CtrlOtaState::SENDING:
    case CtrlOtaState::ENDING:
        return true;
    default:
        return false;
    }
}

const CtrlOtaStatus& otaRemoteStatus() { return g_st; }
