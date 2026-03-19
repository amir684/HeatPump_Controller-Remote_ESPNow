#include "buzzer.h"
#include "config.h"

// Uses LEDC directly (channel 2, timer 1) — fully independent from
// the backlight (channel 0, timer 0) to avoid PWM interference.
#define BUZ_CHANNEL  2
#define BUZ_RES      8     // 8-bit resolution
#define BUZ_DUTY   128     // 50% duty cycle

struct Note { uint16_t freq; uint16_t ms; };

// ── Fault alarm: descending tones ≈ 1550ms ─────────────────
static const Note k_alarm[] = {
    {880, 200}, {0,  50},
    {660, 200}, {0,  50},
    {440, 300}, {0,  50},
    {880, 150}, {0,  50},
    {660, 500},
};
static const int k_alarmLen = sizeof(k_alarm) / sizeof(k_alarm[0]);

// ── Comm fail: ascending tones ≈ 1200ms ────────────────────
static const Note k_comm[] = {
    {330, 150}, {0,  40},
    {440, 150}, {0,  40},
    {550, 150}, {0,  40},
    {660, 150}, {0,  40},
    {880, 400},
};
static const int k_commLen = sizeof(k_comm) / sizeof(k_comm[0]);

// ── Playback state ──────────────────────────────────────────
static const Note* g_melody    = nullptr;
static int         g_melodyLen = 0;
static int         g_noteIdx   = -1;   // -1 = idle
static uint32_t    g_noteEnd   = 0;

// ── Helpers ────────────────────────────────────────────────
static void playFreq(uint16_t freq) {
    if (freq == 0) {
        ledcWrite(BUZ_CHANNEL, 0);
    } else {
        ledcSetup(BUZ_CHANNEL, freq, BUZ_RES);
        ledcWrite(BUZ_CHANNEL, BUZ_DUTY);
    }
}

static void silence() { ledcWrite(BUZ_CHANNEL, 0); }

static void startMelody(const Note* melody, int len) {
    g_melody    = melody;
    g_melodyLen = len;
    g_noteIdx   = 0;
    playFreq(melody[0].freq);
    g_noteEnd = millis() + melody[0].ms;
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
    if (g_noteIdx >= g_melodyLen) {
        g_noteIdx = -1;
        silence();
        return;
    }

    const Note& n = g_melody[g_noteIdx];
    playFreq(n.freq);
    g_noteEnd = now + n.ms;
}

void buzzerClick() {
    playFreq(1200);
    g_melody    = nullptr;
    g_melodyLen = 0;
    g_noteEnd   = millis() + 40;
    g_noteIdx   = 1;  // will trigger idle after 40ms
}

void buzzerAlarm()    { startMelody(k_alarm, k_alarmLen); }
void buzzerCommFail() { startMelody(k_comm,  k_commLen);  }
