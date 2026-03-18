#pragma once
#include <stdint.h>

// ═══════════════════════════════════════════════════════════
//  ESP-NOW shared definitions — Controller ↔ Remote
// ═══════════════════════════════════════════════════════════

// ── MAC addresses ──────────────────────────────────────────
#define CTRL_MAC_BYTES   { 0x00, 0x70, 0x07, 0x25, 0x08, 0x74 }
#define REMOTE_MAC_BYTES { 0x00, 0x70, 0x07, 0x25, 0x70, 0xF0 }

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
