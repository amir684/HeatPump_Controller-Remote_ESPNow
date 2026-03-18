#pragma once

// ── Controller-side ESP-NOW ────────────────────────────────
// Sends StatusPacket to Remote every 500 ms.
// Receives CommandPacket and SettingsPacket from Remote.

void espnowCtrlInit();
void espnowCtrlLoop();

// True if a packet was received from Remote within last 5 seconds
bool espnowRemoteConnected();
