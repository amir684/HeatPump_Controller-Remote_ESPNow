# Heat Pump Controller + Remote — Dual ESP32 via ESP-NOW

ESP32-based smart heat pump controller split across **two boards** communicating wirelessly via **ESP-NOW** (~500µs latency, no router needed).

Replaces the **ZL-R200A** heat pump board with full Home Assistant integration, LCD menu, and identical control logic to the original.

---

> [!CAUTION]
> ## ⚡ ELECTRICAL SAFETY — 230V AC MAINS VOLTAGE
>
> This project involves direct connection to **230V AC mains electricity**.
> Improper handling can cause **electric shock, fire, or death**.
>
> - **Only qualified and licensed electricians** should install or service this system.
> - **Disconnect and lock out mains power** before opening the heat pump enclosure or touching any wiring.
> - The relay outputs switch **live 230V loads** — compressor (30A), fan, valve, and pump.
> - Do not operate with the enclosure open or with exposed conductors.
> - Verify local electrical codes and regulations before installation.
>
> **If you are not trained and certified to work with high-voltage systems — do not attempt this project.**

---

## Architecture Overview

```
┌─────────────────────────────┐         ESP-NOW (2.4GHz)        ┌─────────────────────────────┐
│        CONTROLLER           │◄────────────────────────────────►│           REMOTE            │
│      (Outdoor unit)         │                                  │        (Indoor unit)         │
│                             │  StatusPacket every 500ms ──►    │                             │
│  • 4× NTC temperature       │  ◄── CommandPacket (on action)   │  • ST7567 128×64 LCD        │
│    sensors                  │  ◄── SettingsPacket (on change)  │  • 5-button navigation      │
│  • 4× relay outputs         │                                  │  • WiFi + MQTT              │
│  • Control state machine    │                                  │  • Home Assistant           │
│  • No WiFi / no display     │                                  │  • Full menu system         │
└─────────────────────────────┘                                  └─────────────────────────────┘
```

### Key Design Principles

| | Controller | Remote |
|--|--|--|
| **Role** | Real hardware control | UI + cloud connectivity |
| **Works without the other?** | ✅ Yes — keeps running autonomously | ✅ Yes — shows last known state |
| **WiFi** | ✗ Not needed | ✅ Connects to router via WiFiManager |
| **Settings stored** | Receives from Remote, saves to NVS | Master copy in NVS, synced on every save |
| **MQTT / HA** | ✗ | ✅ Full Discovery |

---

## Photos

### Phase 1 — Firmware & UI Development

Early bench testing of the LCD menu system before any hardware was assembled.

<table>
<tr>
<td align="center" width="50%">
<img src="images/20260315_225025.jpg" alt="LCD Main Menu"><br>
<sub>Main Menu on the ST7567 LCD — System ON/OFF, Temperature, Work Mode, Parameters and SAVE Periods all working in the first firmware build.</sub>
</td>
<td align="center" width="50%">
<img src="images/20260315_225051.jpg" alt="LCD Parameters Screen"><br>
<sub>Parameters screen showing the defrost and calibration settings (U01–U32) — the full configuration menu navigable directly from the display.</sub>
</td>
</tr>
</table>

---

### Phase 2 — First Prototype

First time both boards were connected together and communicating over ESP-NOW.

<table>
<tr>
<td align="center" width="50%">
<img src="images/20260316_233026.jpg" alt="First prototype assembled"><br>
<sub>First working prototype — the Controller perfboard (relays + ESP32) wired to the Remote's LCD and a 5-button pad. Both boards talking over ESP-NOW for the first time.</sub>
</td>
<td align="center" width="50%">
<img src="images/20260316_233038.jpg" alt="LCD live status screen"><br>
<sub>LCD showing live status received from the Controller: water temp 35.6°C, setpoint 51°C, relay states (COMP / FAN / VLV) and operating mode — all transmitted wirelessly at 500 ms intervals.</sub>
</td>
</tr>
</table>

---

### Phase 3 — PCB Build & Assembly

Building the final perfboard PCBs for both units.

<table>
<tr>
<td align="center" width="50%">
<img src="images/20260320_095608.jpg" alt="Remote PCB front"><br>
<sub>Remote PCB — front view. The ST7567 LCD module sits centrally with the 5 navigation buttons mounted below it on the perfboard.</sub>
</td>
<td align="center" width="50%">
<img src="images/20260320_095624.jpg" alt="Remote PCB back"><br>
<sub>Remote PCB — back view. The ESP32-WROOM module and a USB-C power board are soldered in, with hand-wired connections to the LCD and buttons.</sub>
</td>
</tr>
<tr>
<td align="center" width="50%">
<img src="images/20260320_100646.jpg" alt="Both boards side by side"><br>
<sub>Both finished boards side by side before installation — Controller (left, with 4 relays and WiFi antenna) and Remote (right, with LCD, buttons and antenna).</sub>
</td>
<td align="center" width="50%">
<img src="images/20260320_100654.jpg" alt="Controller PCB close-up"><br>
<sub>Controller PCB close-up — three SONGLE 250V/10A relays, the ESP32 module, and the NTC sensor terminal blocks labeled water / pipe / exhaust / outdoor.</sub>
</td>
</tr>
<tr>
<td align="center" width="50%">
<img src="images/20260320_111705.jpg" alt="Controller with relay wiring"><br>
<sub>Controller board with the 230V relay output wiring attached — yellow/green and red cables terminated with spade connectors, ready to connect to the heat pump loads.</sub>
</td>
<td align="center" width="50%">
<img src="images/20260320_111709.jpg" alt="Controller on enclosure base"><br>
<sub>Controller board mounted on standoffs above an aluminium enclosure base — provides physical protection and electrical isolation from the heat pump chassis.</sub>
</td>
</tr>
</table>

---

### Phase 4 — Installation in the Heat Pump

Installing the Controller into the outdoor unit and connecting all sensors and loads.

<table>
<tr>
<td align="center" width="50%">
<img src="images/20260320_112327.jpg" alt="NTC sensor mounting on water tank"><br>
<sub>Brass immersion fitting drilled into the hot-water tank for the NTC water-temperature sensor — direct contact with the water for accurate temperature reading.</sub>
</td>
<td align="center" width="50%">
<img src="images/20260320_120202.jpg" alt="Controller installed in outdoor unit"><br>
<sub>Controller board installed inside the heat pump outdoor enclosure with all relay and sensor wiring connected. The ESP32 communicates wirelessly to the indoor Remote — no data cable needed.</sub>
</td>
</tr>
</table>

---

### Phase 5 — Home Assistant Integration

The Remote connects to Home Assistant via MQTT with full auto-discovery — no manual entity configuration needed.

<table>
<tr>
<td align="center" width="33%">
<img src="images/Screenshot_20260316_234447_Home Assistant.jpg" alt="HA climate card idle" width="220"><br>
<sub>Home Assistant climate card — heat pump in Idle mode, setpoint 47°C, current water temp 29.8°C.</sub>
</td>
<td align="center" width="33%">
<img src="images/Screenshot_20260316_234508_Home Assistant.jpg" alt="HA device sensors" width="220"><br>
<sub>HA device page — all auto-discovered entities: climate control, compressor state, defrost status, and all four temperature sensors.</sub>
</td>
<td align="center" width="33%">
<img src="images/Screenshot_20260316_234757_Home Assistant.jpg" alt="HA climate card heating" width="220"><br>
<sub>Heat pump actively Heating — setpoint 48°C, current water temp 32.3°C. Controlled directly from the HA app.</sub>
</td>
</tr>
</table>

---

### Phase 6 — Final Installed Product

The Remote unit installed indoors, fully operational.

<table>
<tr>
<td align="center" width="33%">
<img src="images/20260413_205955.jpg" alt="Remote installed - status screen"><br>
<sub>Remote on the wall — main status screen: STANDBY, water 49.2°C, setpoint 50°C, outdoor 21.1°C, pipe 23.7°C, exhaust 26.0°C. 4-way valve active indicator visible.</sub>
</td>
<td align="center" width="33%">
<img src="images/20260413_210006.jpg" alt="Remote installed - main menu"><br>
<sub>Main Menu — System ON/OFF, Temperature, Work Mode, Parameters, and SAVE Periods — all accessible directly from the wall-mounted Remote.</sub>
</td>
<td align="center" width="33%">
<img src="images/20260413_210026.jpg" alt="Remote installed - extended menu"><br>
<sub>Extended menu — Force Defrost, View Sensors, WiFi Setup, and MQTT Setup. Full configuration without a computer or phone.</sub>
</td>
</tr>
</table>

---

## Features

### Controller
- Identical control logic to the **ZL-R200A** (startup/shutdown sequences, auto defrost, anti-freeze, exhaust protection)
- Reads 4× NTC 10K/3470 temperature sensors
- Drives 4× relays (compressor, fan, 4-way valve, water pump)
- Sends full status to Remote every **500 ms** via ESP-NOW
- Receives and applies settings/commands from Remote instantly
- Works fully **standalone** — no Remote connection required

### Remote
- **128×64 ST7567 LCD** with full menu system
- **5-button navigation** (up, down, enter, back, power)
- **WiFi** via WiFiManager (captive portal on first boot)
- **Home Assistant** integration via MQTT Discovery
- **Backlight PWM** with auto-dim after inactivity
- **All 12 parameters** configurable from on-device menu (U01–U81)
- **SAVE mode** with up to 3 scheduled heating periods
- **Persistent settings** in NVS — survive power loss, synced to Controller on boot
- **NTP time sync** when WiFi connected
- MQTT broker configurable from browser (WiFiManager portal includes MQTT fields)

---

## ESP-NOW Communication

### Packet Types

| Packet | Direction | Interval | Description |
|--------|-----------|----------|-------------|
| `StatusPacket` | Controller → Remote | Every 500ms | Full state: relays, sensors, state machine |
| `CommandPacket` | Remote → Controller | On user action | SET_ONLINE or FORCE_DEFROST |
| `SettingsPacket` | Remote → Controller | On save + every 30s | Full settings sync |

### MAC Addresses (defined in `src/shared.h`)

```cpp
#define CTRL_MAC_BYTES   { 0x00, 0x70, 0x07, 0x25, 0x08, 0x74 }
#define REMOTE_MAC_BYTES { 0x00, 0x70, 0x07, 0x25, 0x70, 0xF0 }
#define ESPNOW_CHANNEL   1   // must match router WiFi channel
```

> **Important:** `ESPNOW_CHANNEL` must match the channel your router uses.
> The Remote prints its WiFi channel on startup via Serial. Update `shared.h` if needed.

---

## Hardware

### Controller (Outdoor)

| Component | Details |
|-----------|---------|
| MCU | ESP32 DOIT DevKit V1 |
| Sensors | 4× NTC 10K/3470 |
| Relays | 4× relay module (Active LOW) |

#### NTC Sensors — wiring: `3.3V → 10KΩ → GPIO → NTC → GND`

| Sensor | GPIO |
|--------|------|
| Water tank | GPIO 36 |
| Pipe coil | GPIO 39 |
| Compressor exhaust | GPIO 34 |
| Outdoor ambient | GPIO 35 |

> GPIO 34, 35, 36, 39 are **input-only** — external 10KΩ pull-up to 3.3V required.

#### Relays (Active LOW)

| Relay | GPIO | Load |
|-------|------|------|
| Compressor | GPIO 26 | 30A |
| Outdoor Fan | GPIO 27 | 3A |
| 4-way Valve | GPIO 14 | 3A |
| Water Pump | GPIO 33 | 3A |

---

### Remote (Indoor)

| Component | Details |
|-----------|---------|
| MCU | ESP32 DOIT DevKit V1 |
| Display | ST7567 128×64 LCD |
| Buttons | 5× momentary push button |

#### LCD (ST7567) — Hardware SPI

| LCD Pin | GPIO | Note |
|---------|------|------|
| CS | GPIO 0 | Chip Select |
| RST | GPIO 16 | Reset |
| DC | GPIO 2 | Data/Command |
| SCK | GPIO 18 | SPI Clock (VSPI) |
| SDI | GPIO 23 | SPI Data (VSPI) |
| LED | GPIO 32 | Backlight PWM |
| VCC | 3.3V | |
| GND | GND | |

#### Buttons (INPUT_PULLUP)

| Button | GPIO |
|--------|------|
| POWER (long press = on/off) | GPIO 25 |
| UP | GPIO 19 |
| DOWN | GPIO 22 |
| ENTER | GPIO 21 |
| BACK | GPIO 13 |

---

## Software

### Project Structure

```
src/
├── shared.h              ESP-NOW packet structs + MAC addresses (shared)
│
├── main_ctrl.cpp         Controller entry point
├── espnow_ctrl.h/cpp     Controller ESP-NOW (send status, receive cmd/settings)
├── control.h/cpp         Heat pump state machine
├── sensors.h/cpp         NTC temperature reading
├── relays.h/cpp          Relay output control
├── settings.h/cpp        NVS persistent storage + dirty flag
│
├── main_remote.cpp       Remote entry point
├── espnow_remote.h/cpp   Remote ESP-NOW (receive status, send cmd/settings)
├── control_remote.cpp    Proxy control.h — returns cached ESP-NOW data
├── sensors_remote.cpp    Proxy sensors.h — returns cached ESP-NOW data
├── display.h/cpp         ST7567 LCD (U8g2)
├── buttons.h/cpp         Debounced button input
├── menu.h/cpp            Full LCD menu system
├── wifi_mqtt.h/cpp       WiFi + MQTT + HA Discovery
└── config.h              Pin definitions & constants
```

### Build Environments (PlatformIO)

```bash
# Flash Controller (outdoor ESP32)
pio run -e controller --target upload

# Flash Remote (indoor ESP32)
pio run -e remote --target upload
```

### Dependencies (Remote only)

```ini
lib_deps =
    olikraus/U8g2@^2.35.19
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.5
    tzapu/WiFiManager@^2.0.17
```

---

## First Boot

### Remote
1. Power on → ESP32 creates WiFi AP: **`HeatPump-Setup`**
2. Connect with phone/laptop → browser opens automatically
3. Enter WiFi credentials + MQTT broker details (host, port, user, pass)
4. ESP32 connects, gets NTP time, starts sending settings to Controller via ESP-NOW

### Controller
1. Power on → connects to Remote via ESP-NOW automatically
2. Uses last saved settings from NVS until Remote syncs
3. Starts control loop immediately — no WiFi needed

---

## Home Assistant

Auto-discovers via MQTT. All entities appear automatically after connecting to the broker.

| Entity | Type | Description |
|--------|------|-------------|
| Heat Pump | `climate` | On/off + setpoint + current temp |
| HP Water Temp | `sensor` | Water tank temperature |
| HP Pipe Temp | `sensor` | Pipe coil temperature |
| HP Exhaust Temp | `sensor` | Compressor exhaust temperature |
| HP Outdoor Temp | `sensor` | Outdoor temperature |
| HP Compressor | `binary_sensor` | Compressor running state |
| HP Defrost | `binary_sensor` | Defrost cycle active |

### MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `heatpump/state` | Remote → HA | Full JSON state |
| `heatpump/cmd` | HA → Remote | Commands |
| `heatpump/available` | Remote → HA | `online` / `offline` (LWT) |

**Command examples:**
```json
{"mode": "heat"}
{"mode": "off"}
{"setpoint": 45.0}
{"work_mode": "save"}
{"force_defrost": true}
```

---

## Control Logic

Replicates ZL-R200A exactly — runs on the **Controller** board.

### Startup Sequence
```
PUMP ON → 30s → FAN ON + VALVE → 30s → COMPRESSOR ON
```

### Shutdown Sequence
```
COMPRESSOR OFF → 30s → FAN OFF + VALVE → 30s → PUMP OFF
```

### Auto Defrost
- Triggers after compressor runs ≥ **U30** min AND pipe temp ≤ **U31°C**
- Sequence: COMP+FAN off → 55s → valve reverses → 5s → COMP on
- Ends when pipe temp ≥ **U32°C** or max **U33** min reached

### Protections
- **Compressor delay:** 3-minute minimum off time before restart
- **Exhaust protection:** stops heating if exhaust > U51+10°C (U50)
- **Anti-freeze:** pump runs 3 min every 30 min when outdoor < 2°C (U52)

---

## Parameters Reference

| Code | Function | Range | Default |
|------|----------|-------|---------|
| U01 | Hysteresis | 2–10°C | 5°C |
| U02 | Water sensor calibration | ±9.9°C | 0.0°C |
| U30 | Defrost start time | 10–90 min | 45 min |
| U31 | Defrost start temperature | -20–0°C | -5°C |
| U32 | Defrost stop temperature | 2–30°C | 10°C |
| U33 | Max defrost time | 2–15 min | 8 min |
| U34 | Valve mode (0=heat-off, 1=heat-on) | 0/1 | 0 |
| U50 | Exhaust protection mode | 0–2 | 1 |
| U51 | Exhaust protection temp | 50–125°C | 110°C |
| U52 | Anti-freeze | 0/1 | 1 |
| U80 | Auto restart after power loss | 0/1 | 0 |
| U81 | Keypad lock | 0/1 | 0 |

---

## License

Private — all rights reserved.
