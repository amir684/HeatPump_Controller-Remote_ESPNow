#include "buzzer.h"
#include "config.h"

// Uses LEDC directly (channel 2, timer 1) — fully independent from
// the backlight (channel 0, timer 0) to avoid PWM interference.
#define BUZ_CHANNEL  2
#define BUZ_RES      8     // 8-bit resolution
#define BUZ_DUTY   128     // 50% duty cycle

// ── Alarm melody (non-blocking) ────────────────────────────
struct Note { uint16_t freq; uint16_t ms; };

static const Note k_alarm[] = {
    {880, 200}, {0,  50},
    {660, 200}, {0,  50},
    {440, 300}, {0,  50},
    {880, 150}, {0,  50},
    {660, 500},
};  // total ≈ 1550 ms
static const int k_alarmLen = sizeof(k_alarm) / sizeof(k_alarm[0]);

static int      g_noteIdx = -1;   // -1 = idle
static uint32_t g_noteEnd = 0;

// ── Helpers ────────────────────────────────────────────────
static void playFreq(uint16_t freq) {
    if (freq == 0) {
        ledcWrite(BUZ_CHANNEL, 0);
    } else {
        ledcSetup(BUZ_CHANNEL, freq, BUZ_RES);
        ledcWrite(BUZ_CHANNEL, BUZ_DUTY);
    }
}

static void silence() {
    ledcWrite(BUZ_CHANNEL, 0);
}

// ── Public ─────────────────────────────────────────────────
void buzzerInit() {
    ledcSetup(BUZ_CHANNEL, 1000, BUZ_RES);
    ledcAttachPin(PIN_BUZZER, BUZ_CHANNEL);
    silence();
}

void buzzerLoop() {
    if (g_noteIdx < 0) return;
    uint32_t now = millis();
    if (now < g_noteEnd) return;

    g_noteIdx++;
    if (g_noteIdx >= k_alarmLen) {
        g_noteIdx = -1;
        silence();
        return;
    }

    const Note& n = k_alarm[g_noteIdx];
    playFreq(n.freq);
    g_noteEnd = now + n.ms;
}

void buzzerClick() {
    playFreq(1200);
    g_noteEnd  = millis() + 40;
    g_noteIdx  = k_alarmLen;  // will call silence() after 40ms via buzzerLoop
}

void buzzerAlarm() {
    g_noteIdx = 0;
    playFreq(k_alarm[0].freq);
    g_noteEnd = millis() + k_alarm[0].ms;
}
