#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════
//  Relay output control (Active LOW)
// ═══════════════════════════════════════════════════════════

enum class Relay { COMP, FAN, VALVE };

void relaysInit();

void relaySet(Relay r, bool on);
bool relayGet(Relay r);

void relaysAllOff();

// Valve helpers (direction depends on U34 setting)
void valveSetHeating();   // valve position for normal heating
void valveSetDefrost();   // valve position for defrost
