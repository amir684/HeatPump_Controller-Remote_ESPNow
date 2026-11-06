#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static Settings    g_settings;
static bool        g_dirty = false;

static void applyDefaults() {
    g_settings.setpoint    = DEF_SETPOINT;
    g_settings.u03         = DEF_U03;
    g_settings.u01         = DEF_U01;
    g_settings.u02         = DEF_U02;
    g_settings.u30         = DEF_U30;
    g_settings.u31         = DEF_U31;
    g_settings.u32         = DEF_U32;
    g_settings.u33         = DEF_U33;
    g_settings.u34         = DEF_U34;
    g_settings.u50         = DEF_U50;
    g_settings.u51         = DEF_U51;
    g_settings.u52         = DEF_U52;
    g_settings.u80         = DEF_U80;
    g_settings.workMode    = 0;
    g_settings.systemOnline = false;

    for (int i = 0; i < 3; i++) {
        g_settings.period[i] = {0, 0, 0, 0, false};
    }

    memset(g_settings.mqttHost, 0, sizeof(g_settings.mqttHost));
    g_settings.mqttPort = DEF_MQTT_PORT;
    memset(g_settings.mqttUser, 0, sizeof(g_settings.mqttUser));
    memset(g_settings.mqttPass, 0, sizeof(g_settings.mqttPass));
    g_settings.wifiEnabled = true;
    strncpy(g_settings.webPass, WEB_DEFAULT_PASS, sizeof(g_settings.webPass) - 1);
    g_settings.webPass[sizeof(g_settings.webPass) - 1] = '\0';
}

void settingsInit() {
    prefs.begin("heatpump", false);
    settingsLoad();
}

void settingsLoad() {
    applyDefaults();

    if (!prefs.isKey("init")) return;  // nothing saved yet

    g_settings.setpoint    = prefs.getFloat("sp",   DEF_SETPOINT);
    g_settings.u03         = prefs.getFloat("u03",  DEF_U03);
    g_settings.u01         = prefs.getFloat("u01",  DEF_U01);
    g_settings.u02         = prefs.getFloat("u02",  DEF_U02);
    g_settings.u30         = prefs.getUChar("u30",  DEF_U30);
    g_settings.u31         = prefs.getFloat("u31",  DEF_U31);
    g_settings.u32         = prefs.getFloat("u32",  DEF_U32);
    g_settings.u33         = prefs.getUChar("u33",  DEF_U33);
    g_settings.u34         = prefs.getUChar("u34",  DEF_U34);
    g_settings.u50         = prefs.getUChar("u50",  DEF_U50);
    g_settings.u51         = prefs.getFloat("u51",  DEF_U51);
    g_settings.u52         = prefs.getUChar("u52",  DEF_U52);
    g_settings.u80         = prefs.getUChar("u80",  DEF_U80);
    g_settings.workMode    = prefs.getUChar("mode", 0);
    g_settings.systemOnline= prefs.getBool("online", false);

    prefs.getBytes("periods", g_settings.period, sizeof(g_settings.period));

    prefs.getString("mqttHost", g_settings.mqttHost, sizeof(g_settings.mqttHost));
    g_settings.mqttPort = prefs.getUShort("mqttPort", DEF_MQTT_PORT);
    prefs.getString("mqttUser", g_settings.mqttUser, sizeof(g_settings.mqttUser));
    prefs.getString("mqttPass", g_settings.mqttPass, sizeof(g_settings.mqttPass));
    g_settings.wifiEnabled = prefs.getBool("wifiEn", true);

    // Older saves have no web password — keep the default then
    char pass[sizeof(g_settings.webPass)] = {};
    if (prefs.getString("webPass", pass, sizeof(pass)) > 0 && pass[0] != '\0') {
        memcpy(g_settings.webPass, pass, sizeof(pass));
    }
}

void settingsSave() {
    g_dirty = true;
    prefs.putBool("init", true);

    prefs.putFloat("sp",    g_settings.setpoint);
    prefs.putFloat("u03",   g_settings.u03);
    prefs.putFloat("u01",   g_settings.u01);
    prefs.putFloat("u02",   g_settings.u02);
    prefs.putUChar("u30",   g_settings.u30);
    prefs.putFloat("u31",   g_settings.u31);
    prefs.putFloat("u32",   g_settings.u32);
    prefs.putUChar("u33",   g_settings.u33);
    prefs.putUChar("u34",   g_settings.u34);
    prefs.putUChar("u50",   g_settings.u50);
    prefs.putFloat("u51",   g_settings.u51);
    prefs.putUChar("u52",   g_settings.u52);
    prefs.putUChar("u80",   g_settings.u80);
    prefs.putUChar("mode",  g_settings.workMode);
    prefs.putBool("online", g_settings.systemOnline);

    prefs.putBytes("periods", g_settings.period, sizeof(g_settings.period));

    prefs.putString("mqttHost", g_settings.mqttHost);
    prefs.putUShort("mqttPort", g_settings.mqttPort);
    prefs.putString("mqttUser", g_settings.mqttUser);
    prefs.putString("mqttPass", g_settings.mqttPass);
    prefs.putBool("wifiEn", g_settings.wifiEnabled);
    prefs.putString("webPass", g_settings.webPass);
}

void settingsFactoryReset() {
    prefs.clear();
    applyDefaults();
}

Settings& settingsGet() {
    return g_settings;
}

bool settingsHaveSaved() {
    return prefs.isKey("init");
}

bool settingsIsDirty()    { return g_dirty; }
void settingsClearDirty() { g_dirty = false; }
