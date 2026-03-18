#include "relays.h"
#include "config.h"
#include "settings.h"

struct RelayPin { uint8_t pin; bool state; };

static RelayPin g_relays[] = {
    {PIN_RELAY_COMP,  false},  // COMP
    {PIN_RELAY_FAN,   false},  // FAN
    {PIN_RELAY_VALVE, false},  // VALVE
};

void relaysInit() {
    for (auto& r : g_relays) {
        pinMode(r.pin, OUTPUT);
        digitalWrite(r.pin, LOW);   // active HIGH (NPN) → start with relay OFF
        r.state = false;
    }
}

void relaySet(Relay r, bool on) {
    auto& relay = g_relays[static_cast<int>(r)];
    relay.state = on;
    digitalWrite(relay.pin, on ? HIGH : LOW);  // active HIGH (NPN)
}

bool relayGet(Relay r) {
    return g_relays[static_cast<int>(r)].state;
}

void relaysAllOff() {
    for (int i = 0; i < 3; i++) {
        relaySet(static_cast<Relay>(i), false);
    }
}

// U34=0: valve OFF during heating, valve ON during defrost
// U34=1: valve ON during heating, valve OFF during defrost
void valveSetHeating() {
    Settings& s = settingsGet();
    relaySet(Relay::VALVE, s.u34 == 1);
}

void valveSetDefrost() {
    Settings& s = settingsGet();
    relaySet(Relay::VALVE, s.u34 == 0);
}
