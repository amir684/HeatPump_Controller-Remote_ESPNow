#include "menu.h"
#include "config.h"
#include "buttons.h"
#include "display.h"
#include "control.h"
#include "settings.h"
#include "sensors.h"
#include "wifi_mqtt.h"
#include <Arduino.h>
#include <WiFi.h>

// ── Menu state ─────────────────────────────────────────────
enum class Screen {
    MAIN,
    MAIN_MENU,
    PARAMS,
    PERIODS,
    PERIOD_EDIT,
    SENSORS,
    WIFI_MENU,
    MQTT_MENU,
    EDIT_FLOAT,
    EDIT_INT,
    EDIT_STRING,
    CONFIRM,
};

struct EditCtx {
    const char* name;
    float    fVal, fMin, fMax, fStep;
    int      iVal, iMin, iMax;
    char     sVal[64];
    int      paramId;
};

static Screen   g_screen    = Screen::MAIN;
static Screen   g_prevScr   = Screen::MAIN;
static int      g_cursor    = 0;
static int      g_scroll    = 0;
static uint32_t g_actMs     = 0;
static bool     g_dimmed    = false;
static EditCtx  g_edit      = {};
static int      g_periodIdx = 0;
static int      g_periodFld = 0;
static int      g_confirmAct= 0;

#define MENU_TIMEOUT_MS   30000UL
#define BACKLIGHT_DIM_MS  20000UL

// ── Menu item lists ────────────────────────────────────────
static const char* MAIN_ITEMS[] = {
    "1. System ON/OFF",
    "2. Temperature",
    "3. Work Mode",
    "4. Parameters",
    "5. SAVE Periods",
    "6. Force Defrost",
    "7. View Sensors",
    "8. WiFi Setup",
    "9. MQTT Setup",
    "0. Factory Reset",
};
static const int MAIN_COUNT = 10;

static const char* PARAM_NAMES[] = {
    "U01 Hysteresis",
    "U02 Water Cal.",
    "U30 Dfr.Start Tm",
    "U31 Dfr.Start T",
    "U32 Dfr.Stop T",
    "U33 Dfr.Max Tm",
    "U34 Valve Mode",
    "U50 Exh.Protect",
    "U51 Exh.Temp",
    "U52 Antifreeze",
    "U80 Auto Restart",
    "U81 Keypad Lock",
};
static const int PARAM_COUNT = 12;

// ── Helpers ────────────────────────────────────────────────
static void goTo(Screen s) {
    g_prevScr = g_screen;
    g_screen  = s;
    g_cursor  = 0;
    g_scroll  = 0;
    g_actMs   = millis();
    displaySetMenu(s != Screen::MAIN);
}

static void goBack() { goTo(g_prevScr); }
static void exitMenu() { g_screen = Screen::MAIN; displaySetMenu(false); }

static void clamp(int total, int vis) {
    if (g_cursor < g_scroll)             g_scroll = g_cursor;
    if (g_cursor >= g_scroll + vis)      g_scroll = g_cursor - vis + 1;
}

// String editor character set
static const char CHARS[] =
    " .0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$-_=+:,./";
static const int CHARS_LEN = sizeof(CHARS) - 1;

static int charIdx(char c) {
    for (int i = 0; i < CHARS_LEN; i++) if (CHARS[i] == c) return i;
    return 0;
}

// ── Parameter load/save ────────────────────────────────────
static void loadParam(int id) {
    Settings& s = settingsGet();
    g_edit.name    = PARAM_NAMES[id];
    g_edit.paramId = id;
    switch (id) {
    case 0:  g_edit.fVal=s.u01; g_edit.fMin=2;    g_edit.fMax=10;  g_edit.fStep=0.5f; goTo(Screen::EDIT_FLOAT); break;
    case 1:  g_edit.fVal=s.u02; g_edit.fMin=-9.9f;g_edit.fMax=9.9f;g_edit.fStep=0.1f; goTo(Screen::EDIT_FLOAT); break;
    case 2:  g_edit.iVal=s.u30; g_edit.iMin=10;   g_edit.iMax=90;                      goTo(Screen::EDIT_INT);   break;
    case 3:  g_edit.fVal=s.u31; g_edit.fMin=-20;  g_edit.fMax=0;   g_edit.fStep=0.5f; goTo(Screen::EDIT_FLOAT); break;
    case 4:  g_edit.fVal=s.u32; g_edit.fMin=2;    g_edit.fMax=30;  g_edit.fStep=0.5f; goTo(Screen::EDIT_FLOAT); break;
    case 5:  g_edit.iVal=s.u33; g_edit.iMin=2;    g_edit.iMax=15;                      goTo(Screen::EDIT_INT);   break;
    case 6:  g_edit.iVal=s.u34; g_edit.iMin=0;    g_edit.iMax=1;                       goTo(Screen::EDIT_INT);   break;
    case 7:  g_edit.iVal=s.u50; g_edit.iMin=0;    g_edit.iMax=2;                       goTo(Screen::EDIT_INT);   break;
    case 8:  g_edit.fVal=s.u51; g_edit.fMin=50;   g_edit.fMax=125; g_edit.fStep=1.0f; goTo(Screen::EDIT_FLOAT); break;
    case 9:  g_edit.iVal=s.u52; g_edit.iMin=0;    g_edit.iMax=1;                       goTo(Screen::EDIT_INT);   break;
    case 10: g_edit.iVal=s.u80; g_edit.iMin=0;    g_edit.iMax=1;                       goTo(Screen::EDIT_INT);   break;
    case 11: g_edit.iVal=s.u81; g_edit.iMin=0;    g_edit.iMax=1;                       goTo(Screen::EDIT_INT);   break;
    }
}

static void saveParam() {
    Settings& s = settingsGet();
    switch (g_edit.paramId) {
    case 0:  s.u01=(uint8_t)g_edit.fVal; break;  // actually float
    case 1:  s.u02=g_edit.fVal; break;
    case 2:  s.u30=(uint8_t)g_edit.iVal; break;
    case 3:  s.u31=g_edit.fVal; break;
    case 4:  s.u32=g_edit.fVal; break;
    case 5:  s.u33=(uint8_t)g_edit.iVal; break;
    case 6:  s.u34=(uint8_t)g_edit.iVal; break;
    case 7:  s.u50=(uint8_t)g_edit.iVal; break;
    case 8:  s.u51=g_edit.fVal; break;
    case 9:  s.u52=(uint8_t)g_edit.iVal; break;
    case 10: s.u80=(uint8_t)g_edit.iVal; break;
    case 11: s.u81=(uint8_t)g_edit.iVal; break;
    case 100: s.setpoint=g_edit.fVal; break;
    case 201: s.mqttPort=(uint16_t)g_edit.iVal; break;
    }
    settingsSave();
}

// ── Screen handlers ────────────────────────────────────────

static void handleMainMenu() {
    if (Buttons.pressed(BTN_UP)   || Buttons.repeat(BTN_UP))   { if (g_cursor > 0) g_cursor--; clamp(MAIN_COUNT, 5); }
    if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN))  { if (g_cursor < MAIN_COUNT-1) g_cursor++; clamp(MAIN_COUNT, 5); }

    if (Buttons.pressed(BTN_BACK)) { exitMenu(); return; }

    if (Buttons.pressed(BTN_ENTER)) {
        Settings& s = settingsGet();
        const HpStatus& hp = controlStatus();
        switch (g_cursor) {
        case 0:
            controlSetOnline(hp.state == HpState::OFFLINE);
            displayShowMessage(hp.state==HpState::OFFLINE ? "System: ONLINE":"System: OFFLINE", nullptr, nullptr);
            delay(800); exitMenu(); return;
        case 1:
            g_edit.name="Setpoint"; g_edit.fVal=s.setpoint;
            g_edit.fMin=10; g_edit.fMax=70; g_edit.fStep=1.0f; g_edit.paramId=100;
            goTo(Screen::EDIT_FLOAT); return;
        case 2:
            s.workMode = (s.workMode==0) ? 1 : 0; settingsSave();
            displayShowMessage(s.workMode==0?"Mode: AUTO":"Mode: SAVE", nullptr, nullptr);
            delay(800); exitMenu(); return;
        case 3: goTo(Screen::PARAMS);    return;
        case 4: goTo(Screen::PERIODS);   return;
        case 5:
            controlForceDefrost();
            displayShowMessage("Force Defrost","Sent!",nullptr);
            delay(800); exitMenu(); return;
        case 6: goTo(Screen::SENSORS);   return;
        case 7: goTo(Screen::WIFI_MENU); return;
        case 8: goTo(Screen::MQTT_MENU); return;
        case 9: g_confirmAct=9; goTo(Screen::CONFIRM); return;
        }
    }
    displayShowMenu("MAIN MENU", MAIN_ITEMS, MAIN_COUNT, g_cursor, g_scroll);
}

static void handleParams() {
    if (Buttons.pressed(BTN_UP)   || Buttons.repeat(BTN_UP))   { if (g_cursor > 0) g_cursor--; clamp(PARAM_COUNT, 5); }
    if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN))  { if (g_cursor < PARAM_COUNT-1) g_cursor++; clamp(PARAM_COUNT, 5); }
    if (Buttons.pressed(BTN_BACK))  { goTo(Screen::MAIN_MENU); return; }
    if (Buttons.pressed(BTN_ENTER)) { loadParam(g_cursor); return; }
    displayShowMenu("PARAMETERS", PARAM_NAMES, PARAM_COUNT, g_cursor, g_scroll);
}

static void handlePeriods() {
    char bufs[3][24];
    const char* lines[3];
    Settings& s = settingsGet();
    for (int i = 0; i < 3; i++) {
        const Period& p = s.period[i];
        snprintf(bufs[i], 24, "P%d %s %02d:%02d-%02d:%02d",
            i+1, p.enabled?"ON ":"off", p.onHour,p.onMin,p.offHour,p.offMin);
        lines[i] = bufs[i];
    }
    if (Buttons.pressed(BTN_UP))    { if (g_cursor > 0) g_cursor--; }
    if (Buttons.pressed(BTN_DOWN))  { if (g_cursor < 2) g_cursor++; }
    if (Buttons.pressed(BTN_BACK))  { goTo(Screen::MAIN_MENU); return; }
    if (Buttons.pressed(BTN_ENTER)) { g_periodIdx=g_cursor; g_periodFld=0; goTo(Screen::PERIOD_EDIT); return; }
    displayShowMenu("SAVE PERIODS", lines, 3, g_cursor, 0);
}

static void handlePeriodEdit() {
    Settings& s  = settingsGet();
    Period&   p  = s.period[g_periodIdx];
    const char* fnames[] = {"ON Hour","ON Min","OFF Hour","OFF Min","Enable"};
    int* fields[] = {(int*)&p.onHour,(int*)&p.onMin,(int*)&p.offHour,(int*)&p.offMin};
    int  maxVals[] = {23, 59, 23, 59};

    if (g_periodFld < 4) {
        if (Buttons.pressed(BTN_UP)   || Buttons.repeat(BTN_UP))   { if (*fields[g_periodFld] < maxVals[g_periodFld]) (*fields[g_periodFld])++; }
        if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN))  { if (*fields[g_periodFld] > 0) (*fields[g_periodFld])--; }
    } else {
        if (Buttons.pressed(BTN_UP) || Buttons.pressed(BTN_DOWN))  { p.enabled = !p.enabled; }
    }
    if (Buttons.pressed(BTN_BACK))  { if (g_periodFld > 0) g_periodFld--; else goTo(Screen::PERIODS); return; }
    if (Buttons.pressed(BTN_ENTER)) {
        if (g_periodFld < 4) { g_periodFld++; }
        else { settingsSave(); goTo(Screen::PERIODS); return; }
    }

    char val[8];
    if (g_periodFld < 4) snprintf(val, sizeof(val), "%02d", *fields[g_periodFld]);
    else                  snprintf(val, sizeof(val), "%s", p.enabled?"ON":"off");
    char title[24]; snprintf(title, sizeof(title), "P%d %s", g_periodIdx+1, fnames[g_periodFld]);
    displayShowEdit(title, val, "UP/DN:chg EN:next BK:prev");
}

static void handleSensors() {
    const SensorData& d = sensorsGet();
    char l1[22],l2[22],l3[22],l4[22];
    snprintf(l1,22,"Water  :%s%.1fC", d.waterOk   ?"":"-", d.waterOk   ?sensorCorrectedWater():0);
    snprintf(l2,22,"Pipe   :%s%.1fC", d.pipeOk    ?"":"-", d.pipeOk    ?d.pipe:0);
    snprintf(l3,22,"Exhaust:%s%.1fC", d.exhaustOk ?"":"-", d.exhaustOk ?d.exhaust:0);
    snprintf(l4,22,"Outdoor:%s%.1fC", d.outdoorOk ?"":"-", d.outdoorOk ?d.outdoor:0);
    const char* lines[] = {l1,l2,l3,l4};
    if (Buttons.pressed(BTN_BACK) || Buttons.pressed(BTN_ENTER)) { goTo(Screen::MAIN_MENU); return; }
    displayShowMenu("SENSORS", lines, 4, -1, 0);
}

static void handleWifi() {
    const char* items[] = {
        wifiConnected() ? "WiFi: Connected" : "WiFi: No conn.",
        wifiIsEnabled() ? "Disable WiFi"    : "Enable WiFi",
        "Start WiFi Setup",
        "Forget WiFi",
        "Back",
    };
    if (Buttons.pressed(BTN_UP))    { if (g_cursor > 0) g_cursor--; }
    if (Buttons.pressed(BTN_DOWN))  { if (g_cursor < 4) g_cursor++; }
    if (Buttons.pressed(BTN_BACK))  { goTo(Screen::MAIN_MENU); return; }
    if (Buttons.pressed(BTN_ENTER)) {
        if (g_cursor==1) {
            if (wifiIsEnabled()) { wifiDisable(); displayShowMessage("WiFi Disabled",nullptr,nullptr); }
            else                 { wifiEnable();  displayShowMessage("WiFi Enabled","Connecting...",nullptr); }
            delay(800); return;
        }
        if (g_cursor==2) { displayShowMessage("WiFi AP:","HeatPump-Setup","Connect w/ phone"); wifiStartConfigPortal(); exitMenu(); return; }
        if (g_cursor==3) { WiFi.disconnect(true,true); displayShowMessage("WiFi cleared","Restarting...",nullptr); delay(1000); ESP.restart(); }
        if (g_cursor==4) { goTo(Screen::MAIN_MENU); return; }
    }
    displayShowMenu("WIFI SETUP", items, 5, g_cursor, 0);
}

static void handleMqtt() {
    Settings& s = settingsGet();
    const char* items[] = {"Host (IP)","Port","Username","Password","Send Discovery","Back"};
    if (Buttons.pressed(BTN_UP))    { if (g_cursor > 0) g_cursor--; }
    if (Buttons.pressed(BTN_DOWN))  { if (g_cursor < 5) g_cursor++; }
    if (Buttons.pressed(BTN_BACK))  { goTo(Screen::MAIN_MENU); return; }
    if (Buttons.pressed(BTN_ENTER)) {
        switch (g_cursor) {
        case 0: g_edit.name="MQTT Host"; strncpy(g_edit.sVal,s.mqttHost,sizeof(g_edit.sVal)); g_edit.paramId=200; goTo(Screen::EDIT_STRING); return;
        case 1: g_edit.name="MQTT Port"; g_edit.iVal=s.mqttPort; g_edit.iMin=1; g_edit.iMax=65535; g_edit.paramId=201; goTo(Screen::EDIT_INT); return;
        case 2: g_edit.name="MQTT User"; strncpy(g_edit.sVal,s.mqttUser,sizeof(g_edit.sVal)); g_edit.paramId=202; goTo(Screen::EDIT_STRING); return;
        case 3: g_edit.name="MQTT Pass"; strncpy(g_edit.sVal,s.mqttPass,sizeof(g_edit.sVal)); g_edit.paramId=203; goTo(Screen::EDIT_STRING); return;
        case 4: mqttPublishDiscovery(); displayShowMessage("Discovery sent!",nullptr,nullptr); delay(800); break;
        case 5: goTo(Screen::MAIN_MENU); return;
        }
    }
    displayShowMenu("MQTT SETUP", items, 6, g_cursor, 0);
}

static void handleEditFloat() {
    if (Buttons.pressed(BTN_UP)   || Buttons.repeat(BTN_UP))   { g_edit.fVal+=g_edit.fStep; if(g_edit.fVal>g_edit.fMax) g_edit.fVal=g_edit.fMax; }
    if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN))  { g_edit.fVal-=g_edit.fStep; if(g_edit.fVal<g_edit.fMin) g_edit.fVal=g_edit.fMin; }
    if (Buttons.pressed(BTN_BACK))  { goBack(); return; }
    if (Buttons.pressed(BTN_ENTER)) { saveParam(); goBack(); return; }
    char v[16]; snprintf(v,sizeof(v),"%.1f",g_edit.fVal);
    displayShowEdit(g_edit.name, v, "UP/DN:chg EN:ok BK:cancel");
}

static void handleEditInt() {
    if (Buttons.pressed(BTN_UP)   || Buttons.repeat(BTN_UP))   { if(g_edit.iVal<g_edit.iMax) g_edit.iVal++; }
    if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN))  { if(g_edit.iVal>g_edit.iMin) g_edit.iVal--; }
    if (Buttons.pressed(BTN_BACK))  { goBack(); return; }
    if (Buttons.pressed(BTN_ENTER)) { saveParam(); goBack(); return; }
    char v[16]; snprintf(v,sizeof(v),"%d",g_edit.iVal);
    displayShowEdit(g_edit.name, v, "UP/DN:chg EN:ok BK:cancel");
}

static void handleEditString() {
    Settings& s = settingsGet();
    int len = strlen(g_edit.sVal);

    if (Buttons.pressed(BTN_UP) || Buttons.repeat(BTN_UP)) {
        if (len == 0) { g_edit.sVal[0]=' '; g_edit.sVal[1]='\0'; len=1; }
        g_edit.sVal[len-1] = CHARS[(charIdx(g_edit.sVal[len-1])+1) % CHARS_LEN];
    }
    if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN)) {
        if (len == 0) { g_edit.sVal[0]=' '; g_edit.sVal[1]='\0'; len=1; }
        g_edit.sVal[len-1] = CHARS[(charIdx(g_edit.sVal[len-1])-1+CHARS_LEN) % CHARS_LEN];
    }
    if (Buttons.pressed(BTN_ENTER)) {
        if (len < (int)sizeof(g_edit.sVal)-2) { g_edit.sVal[len]=' '; g_edit.sVal[len+1]='\0'; }
    }
    if (Buttons.longPress(BTN_ENTER)) {
        // trim and save
        int l=strlen(g_edit.sVal); while(l>0&&g_edit.sVal[l-1]==' '){g_edit.sVal[l-1]='\0';l--;}
        switch(g_edit.paramId){
        case 200: strncpy(s.mqttHost,g_edit.sVal,sizeof(s.mqttHost)); break;
        case 202: strncpy(s.mqttUser,g_edit.sVal,sizeof(s.mqttUser)); break;
        case 203: strncpy(s.mqttPass,g_edit.sVal,sizeof(s.mqttPass)); break;
        }
        settingsSave(); goBack(); return;
    }
    if (Buttons.pressed(BTN_BACK)) {
        if (len > 0) g_edit.sVal[len-1]='\0';
    }
    if (Buttons.longPress(BTN_BACK)) { goBack(); return; }

    char buf[66]; snprintf(buf,sizeof(buf),"%s_",g_edit.sVal);
    displayShowEdit(g_edit.name, buf, "UP/DN:chr EN:next LP-EN:save");
}

static void handleConfirm() {
    const char* items[] = {"Cancel","YES - Confirm"};
    if (Buttons.pressed(BTN_UP) || Buttons.pressed(BTN_DOWN)) { g_cursor = (g_cursor==0)?1:0; }
    if (Buttons.pressed(BTN_BACK))  { goTo(Screen::MAIN_MENU); return; }
    if (Buttons.pressed(BTN_ENTER)) {
        if (g_cursor==1) { settingsFactoryReset(); displayShowMessage("Factory Reset!","Restarting...",nullptr); delay(1500); ESP.restart(); }
        goTo(Screen::MAIN_MENU); return;
    }
    displayShowMenu("FACTORY RESET?", items, 2, g_cursor, 0);
}

// ── Public interface ───────────────────────────────────────
void menuInit() {
    g_screen = Screen::MAIN;
    g_actMs  = millis();
    displaySetMenu(false);
}

void menuLoop() {
    Settings& s = settingsGet();
    uint32_t  now = millis();

    // Keypad locked
    if (s.u81 == 1) {
        if (Buttons.longPress(BTN_ENTER)) {
            s.u81 = 0; settingsSave();
            displayShowMessage("Unlocked!", nullptr, nullptr); delay(600);
        }
        return;
    }

    // POWER long press: toggle on/off from anywhere
    if (Buttons.longPress(BTN_POWER)) {
        const HpStatus& hp = controlStatus();
        controlSetOnline(hp.state == HpState::OFFLINE);
        if (g_screen != Screen::MAIN) exitMenu();
        return;
    }

    // ── Backlight activity tracking (all screens) ──────────
    bool anyBtn = Buttons.isHeld(BTN_POWER)||Buttons.isHeld(BTN_UP)||
                  Buttons.isHeld(BTN_DOWN)||Buttons.isHeld(BTN_ENTER)||Buttons.isHeld(BTN_BACK);
    if (anyBtn) {
        g_actMs = now;
        if (g_dimmed) { displayBright(); g_dimmed=false; }
    }
    if (!g_dimmed && (now - g_actMs) >= BACKLIGHT_DIM_MS) { displayDim(); g_dimmed=true; }

    // ── Main screen ────────────────────────────────────────
    if (g_screen == Screen::MAIN) {
        if (Buttons.pressed(BTN_ENTER))                      { goTo(Screen::MAIN_MENU); displayShowMenu("MAIN MENU", MAIN_ITEMS, MAIN_COUNT, g_cursor, g_scroll); return; }
        if (Buttons.pressed(BTN_UP)   || Buttons.repeat(BTN_UP))   { if(s.setpoint<70) { s.setpoint+=1.0f; settingsSave(); } }
        if (Buttons.pressed(BTN_DOWN) || Buttons.repeat(BTN_DOWN))  { if(s.setpoint>10) { s.setpoint-=1.0f; settingsSave(); } }
        return;
    }

    // ── Menu timeout ───────────────────────────────────────
    if ((now - g_actMs) > MENU_TIMEOUT_MS) { exitMenu(); return; }

    // ── Sub-screens ────────────────────────────────────────
    switch (g_screen) {
    case Screen::MAIN_MENU:   handleMainMenu();   break;
    case Screen::PARAMS:      handleParams();     break;
    case Screen::PERIODS:     handlePeriods();    break;
    case Screen::PERIOD_EDIT: handlePeriodEdit(); break;
    case Screen::SENSORS:     handleSensors();    break;
    case Screen::WIFI_MENU:   handleWifi();       break;
    case Screen::MQTT_MENU:   handleMqtt();       break;
    case Screen::EDIT_FLOAT:  handleEditFloat();  break;
    case Screen::EDIT_INT:    handleEditInt();    break;
    case Screen::EDIT_STRING: handleEditString(); break;
    case Screen::CONFIRM:     handleConfirm();    break;
    default: exitMenu(); break;
    }
}

bool menuIsActive() { return g_screen != Screen::MAIN; }
