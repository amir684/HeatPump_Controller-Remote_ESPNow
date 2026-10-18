#pragma once
#include <stdint.h>
#include <stddef.h>

// ── Remote-side firmware update support ─────────────────────
// Controller image: uploaded via the web page, staged raw in this
// board's inactive OTA slot, then streamed to the Controller over
// ESP-NOW (stop-and-wait). The Remote's own update uses Update.h
// (web.cpp) and shares the same slot, so only one runs at a time.

enum class CtrlOtaState : uint8_t {
    IDLE, STAGING, BEGIN, SENDING, ENDING, DONE, FAILED,
};

struct CtrlOtaStatus {
    CtrlOtaState state;
    uint32_t     size;
    uint32_t     sent;
    char         msg[48];
};

void otaRemoteInit();
void otaRemoteLoop();

// True while staging or sending — blocks other updates and settings resync
bool otaRemoteBusy();
const CtrlOtaStatus& otaRemoteStatus();

// Staging (called from the web upload handler)
bool otaStageBegin(char* err, size_t errLen);
bool otaStageWrite(const uint8_t* buf, size_t len, char* err, size_t errLen);
bool otaStageEnd(char* err, size_t errLen);   // starts sending on success
void otaStageAbort();

// Called from the ESP-NOW receive callback
void otaRemoteOnAck(const uint8_t* data, int len);
