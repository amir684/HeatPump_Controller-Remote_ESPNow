#include "ota_ctrl.h"
#include "shared.h"
#include "control.h"
#include "espnow_ctrl.h"
#include <Arduino.h>
#include <esp_now.h>
#include <esp_ota_ops.h>
#include <MD5Builder.h>

// Packets arrive on the WiFi task; flash writes happen here in loop()
// so a sector erase never stalls the radio.

#define OTA_IDLE_TIMEOUT_MS    30000UL   // abort if the Remote goes quiet
#define OTA_VERIFY_WINDOW_MS  120000UL   // new image must reach the Remote within this
#define OTA_RESUME_MAGIC   0x4F544121UL  // "OTA!"

struct QueuedPkt {
    uint8_t len;
    uint8_t data[250];
};

static QueueHandle_t          g_queue         = nullptr;
static bool                   g_active        = false;
static esp_ota_handle_t       g_handle        = 0;
static const esp_partition_t* g_part          = nullptr;
static uint32_t               g_size          = 0;
static uint32_t               g_written       = 0;
static uint8_t                g_md5[16]       = {};
static MD5Builder             g_md5b;
static uint32_t               g_lastPktMs     = 0;
static bool                   g_pendingVerify = false;
static bool                   g_resumed       = false;

// Survives esp_restart() (not power loss): tells the next boot to restore on/off state
static RTC_NOINIT_ATTR uint32_t g_resumeMagic;

// Arduino core hook: keep a freshly flashed image unconfirmed until
// otaCtrlLoop() sees the Remote (otherwise the core marks it valid at boot).
extern "C" bool verifyRollbackLater() { return true; }

// ── Helpers ─────────────────────────────────────────────────
static void sendAck(PktType of, OtaResult r) {
    OtaAckPacket ack{};
    ack.type       = PktType::OTA_ACK;
    ack.ackOf      = of;
    ack.result     = r;
    ack.nextOffset = g_written;
    const uint8_t remoteMac[] = REMOTE_MAC_BYTES;
    esp_now_send(remoteMac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

static void abortUpdate() {
    if (!g_active) return;
    esp_ota_abort(g_handle);
    g_active = false;
    Serial.println("[OTA-CTRL] Update aborted, control resumed");
}

static void rebootIntoNewImage() {
    g_resumeMagic = OTA_RESUME_MAGIC;
    delay(300);   // let the last ACK leave
    esp_restart();
}

// ── Packet handlers ─────────────────────────────────────────
static void handleBegin(const QueuedPkt& q) {
    if (q.len < sizeof(OtaBeginPacket)) return;
    OtaBeginPacket p;
    memcpy(&p, q.data, sizeof(p));

    abortUpdate();   // a new BEGIN restarts any half-finished transfer
    g_written = 0;

    g_part = esp_ota_get_next_update_partition(nullptr);
    if (!g_part || p.size == 0 || p.size > g_part->size) {
        sendAck(PktType::OTA_BEGIN, OtaResult::ERR_SIZE);
        return;
    }

    // Compressor off before any flash work; control stays suspended until done
    controlSuspend();

    if (esp_ota_begin(g_part, OTA_WITH_SEQUENTIAL_WRITES, &g_handle) != ESP_OK) {
        sendAck(PktType::OTA_BEGIN, OtaResult::ERR_BEGIN);
        return;
    }
    g_active = true;
    g_size   = p.size;
    memcpy(g_md5, p.md5, sizeof(g_md5));
    g_md5b.begin();

    Serial.printf("[OTA-CTRL] Begin: %u bytes -> %s\n", (unsigned)g_size, g_part->label);
    sendAck(PktType::OTA_BEGIN, OtaResult::OK);
}

static void handleData(const QueuedPkt& q) {
    if (q.len < offsetof(OtaDataPacket, data)) return;
    OtaDataPacket p;
    memcpy(&p, q.data, q.len < sizeof(p) ? q.len : sizeof(p));

    if (!g_active) {
        sendAck(PktType::OTA_DATA, OtaResult::ERR_STATE);
        return;
    }
    // Duplicate of a chunk already stored (our ACK was lost) — just re-ACK
    if (p.offset < g_written) {
        sendAck(PktType::OTA_DATA, OtaResult::OK);
        return;
    }
    if (p.offset > g_written || p.len == 0 || p.len > OTA_CHUNK_SIZE ||
        q.len < offsetof(OtaDataPacket, data) + p.len ||
        g_written + p.len > g_size) {
        sendAck(PktType::OTA_DATA, OtaResult::ERR_OFFSET);
        return;
    }
    if (esp_ota_write(g_handle, p.data, p.len) != ESP_OK) {
        abortUpdate();
        sendAck(PktType::OTA_DATA, OtaResult::ERR_WRITE);
        return;
    }
    g_md5b.add(p.data, p.len);
    g_written += p.len;
    sendAck(PktType::OTA_DATA, OtaResult::OK);
}

static void handleEnd() {
    if (!g_active || g_written != g_size) {
        sendAck(PktType::OTA_END, OtaResult::ERR_STATE);
        return;
    }
    uint8_t got[16];
    g_md5b.calculate();
    g_md5b.getBytes(got);
    if (memcmp(got, g_md5, sizeof(got)) != 0) {
        abortUpdate();
        sendAck(PktType::OTA_END, OtaResult::ERR_MD5);
        return;
    }
    // esp_ota_end validates the image and releases the handle either way
    g_active = false;
    if (esp_ota_end(g_handle) != ESP_OK || esp_ota_set_boot_partition(g_part) != ESP_OK) {
        Serial.println("[OTA-CTRL] Image rejected, control resumed");
        sendAck(PktType::OTA_END, OtaResult::ERR_IMAGE);
        return;
    }
    Serial.println("[OTA-CTRL] Update OK, rebooting");
    sendAck(PktType::OTA_END, OtaResult::OK);
    rebootIntoNewImage();
}

// ── Public interface ────────────────────────────────────────
void otaCtrlInit() {
    g_queue = xQueueCreate(4, sizeof(QueuedPkt));

    g_resumed     = (g_resumeMagic == OTA_RESUME_MAGIC);
    g_resumeMagic = 0;

    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        g_pendingVerify = true;
        Serial.println("[OTA-CTRL] New image — waiting for Remote to confirm");
    }
}

bool otaCtrlHandlePacket(const uint8_t* data, int len) {
    const PktType type = static_cast<PktType>(data[0]);
    if (type != PktType::OTA_BEGIN && type != PktType::OTA_DATA &&
        type != PktType::OTA_END   && type != PktType::OTA_ABORT) {
        return false;
    }
    if (g_queue && len <= (int)sizeof(QueuedPkt::data)) {
        QueuedPkt q;
        q.len = (uint8_t)len;
        memcpy(q.data, data, len);
        xQueueSend(g_queue, &q, 0);   // full queue → drop; the Remote retries
    }
    return true;
}

void otaCtrlLoop() {
    QueuedPkt q;
    while (g_queue && xQueueReceive(g_queue, &q, 0) == pdTRUE) {
        g_lastPktMs = millis();
        switch (static_cast<PktType>(q.data[0])) {
        case PktType::OTA_BEGIN: handleBegin(q); break;
        case PktType::OTA_DATA:  handleData(q);  break;
        case PktType::OTA_END:   handleEnd();    break;
        case PktType::OTA_ABORT:
            abortUpdate();
            sendAck(PktType::OTA_ABORT, OtaResult::OK);
            break;
        default: break;
        }
    }

    if (g_active && millis() - g_lastPktMs >= OTA_IDLE_TIMEOUT_MS) {
        Serial.println("[OTA-CTRL] Remote went quiet");
        abortUpdate();
    }

    // Confirm or roll back a freshly flashed image
    if (g_pendingVerify) {
        if (espnowRemoteConnected()) {
            esp_ota_mark_app_valid_cancel_rollback();
            g_pendingVerify = false;
            Serial.println("[OTA-CTRL] New image confirmed");
        } else if (millis() >= OTA_VERIFY_WINDOW_MS) {
            Serial.println("[OTA-CTRL] No contact with Remote — rolling back");
            g_resumeMagic = OTA_RESUME_MAGIC;
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
}

bool otaCtrlActive()  { return g_active; }
bool otaCtrlResumed() { return g_resumed; }
