#pragma once
#include <stdint.h>

// ═══════════════════════════════════════════════════════════
//  ESP-NOW shared definitions — Controller ↔ Remote
// ═══════════════════════════════════════════════════════════

// ── MAC addresses ──────────────────────────────────────────
#define CTRL_MAC_BYTES   { 0x00, 0x70, 0x07, 0x25, 0x08, 0x74 }
#define REMOTE_MAC_BYTES { 0xE8, 0x6B, 0xEA, 0xD4, 0x59, 0xD8 }

// ── WiFi channel ───────────────────────────────────────────
// Must match the router channel the Remote connects to.
// Remote prints its channel on startup via Serial.
// Change this on the Controller if ESP-NOW doesn't connect.
#define ESPNOW_CHANNEL  1

// ── Packet types ───────────────────────────────────────────
enum class PktType : uint8_t {
    STATUS   = 1,   // Controller → Remote
    COMMAND  = 2,   // Remote → Controller
    SETTINGS = 3,   // Remote → Controller (full settings sync)
    OTA_BEGIN = 4,  // Remote → Controller: start firmware update
    OTA_DATA  = 5,  // Remote → Controller: one image chunk
    OTA_END   = 6,  // Remote → Controller: verify + reboot
    OTA_ABORT = 7,  // Remote → Controller: cancel update
    OTA_ACK   = 8,  // Controller → Remote: reply to any OTA_* packet
    INFO      = 9,  // Controller → Remote (~5 s): firmware version
};

// ── Command types (inside CommandPacket) ───────────────────
enum class CmdType : uint8_t {
    SET_ONLINE    = 1,   // bVal = online
    FORCE_DEFROST = 2,   // no data
};

// ── Controller → Remote (~500 ms) ─────────────────────────
struct StatusPacket {
    PktType  type;
    // State machine
    uint8_t  state;           // HpState cast to uint8_t
    uint8_t  error;           // HpError cast to uint8_t
    bool     compressor;
    bool     fan;
    bool     valve;
    bool     defrosting;
    bool     heating;
    uint32_t compRunMinutes;
    uint32_t defrostCount;
    // Sensor data
    float    water;
    float    pipe;
    float    exhaust;
    float    outdoor;
    bool     waterOk;
    bool     pipeOk;
    bool     exhaustOk;
    bool     outdoorOk;
};

// ── Remote → Controller (on user action) ──────────────────
struct CommandPacket {
    PktType  type;
    CmdType  cmd;
    float    fVal;
    int32_t  iVal;
    bool     bVal;
};

// ── Remote → Controller (on connect + after every save) ───
struct SettingsPacket {
    PktType  type;
    float    setpoint;
    float    u01;
    float    u02;
    uint8_t  u30;
    float    u31;
    float    u32;
    uint8_t  u33;
    uint8_t  u34;
    uint8_t  u50;
    float    u51;
    uint8_t  u52;
    uint8_t  u80;
    uint8_t  workMode;
    bool     systemOnline;
    // 3 heating periods
    uint8_t  periodOnHour[3];
    uint8_t  periodOnMin[3];
    uint8_t  periodOffHour[3];
    uint8_t  periodOffMin[3];
    bool     periodEnabled[3];
};

// ── Firmware update over ESP-NOW (stop-and-wait) ──────────
// Packed: these carry raw image bytes and must fit the 250-byte ESP-NOW limit.
#define OTA_CHUNK_SIZE  240

enum class OtaResult : uint8_t {
    OK         = 0,
    ERR_STATE  = 1,   // no update in progress / not all data received
    ERR_SIZE   = 2,   // image does not fit the OTA partition
    ERR_BEGIN  = 3,   // esp_ota_begin failed
    ERR_OFFSET = 4,   // chunk out of sequence — resend from nextOffset
    ERR_WRITE  = 5,   // flash write failed
    ERR_MD5    = 6,   // received image does not match the MD5
    ERR_IMAGE  = 7,   // image failed validation / cannot be made bootable
};

struct __attribute__((packed)) OtaBeginPacket {
    PktType  type;
    uint32_t size;
    uint8_t  md5[16];
};

struct __attribute__((packed)) OtaDataPacket {
    PktType  type;
    uint32_t offset;
    uint16_t len;
    uint8_t  data[OTA_CHUNK_SIZE];
};

struct __attribute__((packed)) OtaCtlPacket {   // OTA_END, OTA_ABORT
    PktType  type;
};

struct __attribute__((packed)) OtaAckPacket {
    PktType   type;        // OTA_ACK
    PktType   ackOf;       // which request this answers
    OtaResult result;
    uint32_t  nextOffset;  // bytes stored so far
};

struct __attribute__((packed)) InfoPacket {
    PktType  type;         // INFO
    char     version[16];
    char     built[24];
    bool     otaActive;
};
