#pragma once
#include <stdint.h>

// ── Controller-side firmware update over ESP-NOW ───────────
// The Remote streams a new image; this module writes it to the
// inactive OTA slot and reboots into it. A new image stays
// "pending verify" until it hears from the Remote — otherwise the
// bootloader rolls back to the previous firmware.

void otaCtrlInit();     // call before controlInit()
void otaCtrlLoop();     // call every loop iteration

// Called from the ESP-NOW receive callback. Returns true if the
// packet was an OTA packet (queued for otaCtrlLoop).
bool otaCtrlHandlePacket(const uint8_t* data, int len);

// True while an update is being received — control must stay suspended
bool otaCtrlActive();

// True if this boot follows an OTA reboot (restore the previous on/off state)
bool otaCtrlResumed();
