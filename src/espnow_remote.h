#pragma once

// ── Remote-side ESP-NOW ─────────────────────────────────────
// Receives StatusPacket from Controller every 500 ms.
// Sends CommandPacket and SettingsPacket to Controller.

void espnowRemoteInit();
void espnowRemoteLoop();   // call every loop — handles periodic re-sync

// True if a StatusPacket was received from Controller within last 5 seconds
bool espnowCtrlConnected();

// True if no StatusPacket received for more than 1.5 seconds (3 missed packets)
bool espnowCtrlSignalLost();

// Send a command to the Controller immediately
void espnowSendCommand(struct CommandPacket& pkt);

// Send full settings to the Controller immediately
void espnowSendSettings();

// Mark a forced defrost as pending — remote will retry until Controller confirms defrosting=true
void espnowSetDefrostPending(bool pending);
