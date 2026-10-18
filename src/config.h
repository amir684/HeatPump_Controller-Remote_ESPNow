#pragma once

// ═══════════════════════════════════════════════════════════
//  Heat Pump Controller - Pin & Constant Configuration
// ═══════════════════════════════════════════════════════════

// ── LCD ST7567 128x64 (Software SPI, 4-wire) ───────────────
// Using SW SPI to avoid conflict: HW SPI claims GPIO19 as MISO
// which conflicts with the UP button on GPIO19.
//
//  LCD Pin  │  ESP32 GPIO  │  הערה
//  ─────────┼──────────────┼──────────────────────────────
//  CS       │  GPIO  0     │  Chip Select
//  RST      │  GPIO 16     │  Reset
//  DC       │  GPIO  2     │  Data / Command
//  SCK      │  GPIO 18     │  SPI Clock
//  SDI      │  GPIO 23     │  SPI Data
//  LED      │  GPIO 32     │  Backlight PWM
//  VCC      │  3.3V        │
//  GND      │  GND         │
//
#define LCD_CS_PIN   0
#define LCD_DC_PIN   2
#define LCD_RST_PIN 16
#define LCD_SCK_PIN 18
#define LCD_SDI_PIN 23
#define LCD_BL_PIN  15    // Backlight (PWM)

// Backlight PWM settings
#define LCD_BL_CHANNEL   0        // LEDC channel — timer 0
#define LCD_BL_FREQ      5000     // PWM frequency Hz
#define LCD_BL_RES       8        // 8-bit resolution (0-255)
#define LCD_BL_DEFAULT    10      // brightness on startup (active-LOW: low=bright)
#define LCD_BL_DIM       150      // brightness when dimmed (active-LOW: high=dim)
#define LCD_CONTRAST      130      // display contrast (0-255, ST7567 typical: 120-140)

// ── NTC Sensors 10K/3470 (ADC1 - safe with WiFi) ───────────
// Wiring: 3.3V → 10K fixed → ADC_pin → NTC → GND
#define PIN_NTC_WATER    36   // AN1 - water tank temperature
#define PIN_NTC_PIPE     39   // AN2 - pipe coil temperature
#define PIN_NTC_EXHAUST  34   // AN3 - compressor exhaust temperature
#define PIN_NTC_OUTDOOR  35   // AN4 - outdoor temperature

// ── Relay Outputs (Active LOW - common relay module) ────────
#define PIN_RELAY_COMP   26   // Compressor (30A)
#define PIN_RELAY_FAN    27   // Outdoor fan (3A)
#define PIN_RELAY_VALVE  14   // 4-way valve (3A)
#define PIN_RELAY_PUMP   33   // Water circulation pump (3A)

// ── Buttons (INPUT_PULLUP, active LOW) ─────────────────────
#define PIN_BTN_POWER   27   // System on/off (long press)
#define PIN_BTN_UP      32   // Navigate up / increment
#define PIN_BTN_DOWN    33   // Navigate down / decrement
#define PIN_BTN_ENTER   25   // Confirm / enter submenu
#define PIN_BTN_BACK    26   // Back / cancel

// ── Buzzer ──────────────────────────────────────────────────
#define PIN_BUZZER      14   // Passive speaker via NPN transistor

// ── Comm LED ─────────────────────────────────────────────────
#define PIN_COMM_LED    17   // LED: ON when ESP-NOW signal lost

// ── NTC Parameters ─────────────────────────────────────────
#define NTC_SERIES_R    10000.0f  // Series resistor value (Ohms)
#define NTC_NOMINAL_R   10000.0f  // NTC nominal resistance at 25°C
#define NTC_NOMINAL_T   25.0f     // Reference temperature (°C)
#define NTC_B_COEFF     3470.0f   // B coefficient
#define NTC_NUM_SAMPLES 16        // ADC samples for averaging
#define NTC_ERROR_VAL   -99.0f    // Value returned on sensor error

// ── Sequence Timings ───────────────────────────────────────
#define T_STARTUP_PUMP_TO_FAN_MS   30000UL  // pump → 30s → fan+valve
#define T_STARTUP_FAN_TO_COMP_MS   30000UL  // fan+valve → 30s → compressor
#define T_SHUTDOWN_COMP_TO_FAN_MS  30000UL  // comp off → 30s → fan+valve off
#define T_SHUTDOWN_FAN_TO_PUMP_MS  30000UL  // fan off → 30s → pump off
#define T_COMP_MIN_OFF_MS         180000UL  // 3 min minimum compressor off
#define T_COMP_POWER_ON_DELAY_MS  180000UL  // 3 min wait after power on
#define T_DEFROST_VALVE_WAIT_MS    55000UL  // 55s before valve acts in defrost
#define T_DEFROST_COMP_WAIT_MS      5000UL  // 5s after valve before compressor
#define T_ANTIFREEZE_ON_MS        180000UL  // pump runs 3 min (antifreeze)
#define T_ANTIFREEZE_CYCLE_MS    1800000UL  // antifreeze cycle = 30 min

// ── Exhaust protection ─────────────────────────────────────
#define EXHAUST_RESUME_HYST_C      10.0f    // resume only below U51 minus this
#define EXHAUST_FAIL_READINGS          3    // consecutive bad reads (x5 s) before fail-safe stop

// ── Application Intervals ──────────────────────────────────
#define T_SENSOR_READ_MS      5000UL   // sensor read interval
#define T_MQTT_PUBLISH_MS    10000UL   // MQTT state publish interval
#define T_DISPLAY_UPDATE_MS    300UL   // display refresh rate

// ── Button Timings ─────────────────────────────────────────
#define BTN_DEBOUNCE_MS       50     // debounce time
#define BTN_LONG_PRESS_MS    800     // long press threshold
#define BTN_REPEAT_DELAY_MS  400     // delay before auto-repeat
#define BTN_REPEAT_RATE_MS   150     // auto-repeat rate

// ── MQTT / HA ──────────────────────────────────────────────
#define MQTT_BASE_TOPIC      "heatpump"
#define MQTT_DEVICE_ID       "heatpump_01"
#define HA_DISCOVERY_PREFIX  "homeassistant"
#define AP_NAME              "HeatPump-Setup"

// ── Web page ───────────────────────────────────────────────
#define WEB_HOSTNAME         "heatpump"     // http://heatpump.local
#define WEB_USER             "admin"
#define WEB_DEFAULT_PASS     "heatpump"     // change it from the web page

// ── Parameter Defaults ─────────────────────────────────────
#define DEF_SETPOINT   40.0f
#define DEF_U03        55.0f   // max setpoint limit
#define DEF_U01         5.0f   // hysteresis
#define DEF_U02         0.0f   // water sensor calibration
#define DEF_U30            45  // defrost start time (min)
#define DEF_U31        -5.0f   // defrost start temperature
#define DEF_U32        10.0f   // defrost stop temperature
#define DEF_U33             8  // max defrost time (min)
#define DEF_U34             0  // valve mode: 0=heat-off/defrost-on
#define DEF_U50             1  // exhaust protection mode
#define DEF_U51       110.0f   // exhaust protection temperature
#define DEF_U52             1  // antifreeze enable
#define DEF_U80             0  // auto restart
#define DEF_MQTT_PORT    1883
