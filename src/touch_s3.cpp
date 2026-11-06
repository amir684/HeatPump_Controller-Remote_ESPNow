#ifdef REMOTE_S3

#include "touch_s3.h"
#include "lcd_s3.h"
#include "pins_s3.h"
#include <Preferences.h>

static const uint32_t CAL_MAGIC = 0xC0FFEE01;

// Measured on the first unit (2026-11-05). Used until a calibration is saved,
// e.g. right after the old Remote's NVS is copied in.
static TouchCal g_cal = {CAL_MAGIC, true, -0.07362f, 265.82f, 0.05543f, -27.00f};

static void release() {
    pinMode(S3_TP_XP, INPUT); pinMode(S3_TP_XM, INPUT);
    pinMode(S3_TP_YP, INPUT); pinMode(S3_TP_YM, INPUT);
}

// 9 samples, sort, average the middle 3 (rejects spikes)
static int readFiltered(uint8_t pin) {
    const int N = 9;
    int s[N];
    for (int i = 0; i < N; i++) s[i] = analogRead(pin);
    for (int i = 1; i < N; i++) {
        int k = s[i], j = i - 1;
        while (j >= 0 && s[j] > k) { s[j + 1] = s[j]; j--; }
        s[j + 1] = k;
    }
    return (s[3] + s[4] + s[5]) / 3;
}

void touchBegin() {
    analogReadResolution(12);
    release();
    Preferences p;
    p.begin("hpui", true);
    TouchCal c;
    if (p.getBytes("tcal", &c, sizeof(c)) == sizeof(c) && c.magic == CAL_MAGIC) g_cal = c;
    p.end();
}

// Layer 1 to GND, layer 2 pulled up: pressed → YP pulled LOW through the panel
bool touchPressed() {
    release();
    pinMode(S3_TP_XM, OUTPUT); digitalWrite(S3_TP_XM, LOW);
    pinMode(S3_TP_YP, INPUT_PULLUP);
    delayMicroseconds(200);
    bool p = (digitalRead(S3_TP_YP) == LOW);
    release();
    return p;
}

bool touchReadRaw(int& a, int& b) {
    if (!touchPressed()) return false;
    pinMode(S3_TP_XP, OUTPUT); pinMode(S3_TP_XM, OUTPUT);
    digitalWrite(S3_TP_XP, HIGH); digitalWrite(S3_TP_XM, LOW);
    delayMicroseconds(100);
    a = readFiltered(S3_TP_YP);
    release();
    pinMode(S3_TP_YP, OUTPUT); pinMode(S3_TP_YM, OUTPUT);
    digitalWrite(S3_TP_YP, HIGH); digitalWrite(S3_TP_YM, LOW);
    delayMicroseconds(100);
    b = readFiltered(S3_TP_XP);
    release();
    return touchPressed();   // discard samples taken during release
}

bool touchRead(int& x, int& y) {
    int a, b;
    if (!touchReadRaw(a, b)) return false;
    int h = g_cal.swap ? b : a;
    int v = g_cal.swap ? a : b;
    x = constrain((int)lroundf(g_cal.ax * h + g_cal.bx), 0, LCD_W - 1);
    y = constrain((int)lroundf(g_cal.ay * v + g_cal.by), 0, LCD_H - 1);
    return true;
}

void touchSaveCal(const TouchCal& cal) {
    g_cal = cal;
    g_cal.magic = CAL_MAGIC;
    Preferences p;
    p.begin("hpui", false);
    p.putBytes("tcal", &g_cal, sizeof(g_cal));
    p.end();
}

#endif // REMOTE_S3
