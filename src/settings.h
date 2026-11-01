#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include "config.h"

// ---- Settings ------------------------------------------------------------------------
// Values the Settings page can change, kept in the ESP32's flash (NVS) so they survive
// power-off. The defaults come from config.h; DEFAULTS on the Settings page goes back to
// them. Changes are saved 2 s after the last tap, to spare the flash.
// Temperatures are always stored in F and speeds in MPH; the units settings only change
// what is shown.

struct Settings {
    bool  autoBrightness;
    int   nightBrightnessPct;
    bool  celsius;            // show temperatures in C
    bool  kmh;                // show speeds in KM/H
    bool  alarmsOn;
    float ficmLowV;
    float tftHighF;
    float ectHighF;
    float voltsLowV;
    float voltsHighV;
    int   screenOffMin;       // 0 = never
    int   sleepMin;           // 0 = never
    bool  wifiOn;             // phone hotspot (wifi_portal.h)
};

static Settings settings;
static bool settingsDirty = false;
static bool wifiStartFailed = false;   // not a setting: the hotspot couldn't start (wifi_portal.h)
static bool wifiRestartNeeded = false; // the password changed while the hotspot was on

// Hotspot password: random, made the first time the hotspot starts and kept in flash. It's
// not part of Settings, so DEFAULTS keeps it; the Settings page can make a new one.
static String wifiPassword = "";

// Firmware upload permission (Settings, UPDATE): not saved, and it lapses on its own
static unsigned long fwUploadUnlockedAt = 0;   // 0 = locked

inline bool fwUploadUnlocked() {
    return fwUploadUnlockedAt && millis() - fwUploadUnlockedAt < FW_UPLOAD_UNLOCK_MS;
}

// Seconds left before uploads lock again (0 = locked)
inline unsigned long fwUploadSecondsLeft() {
    return fwUploadUnlocked() ? (FW_UPLOAD_UNLOCK_MS - (millis() - fwUploadUnlockedAt)) / 1000 : 0;
}

// Makes and saves a new 10-character hotspot password. Letters and digits that are easy
// to tell apart and type on a phone (no 0/o, 1/l/i); about 49 bits of randomness.
// esp_random() is a true random source while the radio is on (Bluetooth starts first).
inline void wifiPasswordNew() {
    static const char CHARS[] = "abcdefghjkmnpqrstuvwxyz23456789";
    String pw = "";
    for (int i = 0; i < 10; i++) pw += CHARS[esp_random() % (sizeof(CHARS) - 1)];
    wifiPassword = pw;
    Preferences p;
    if (p.begin("cyd", false)) { p.putString("wifiPass", wifiPassword); p.end(); }
    logDebug("SETTINGS", "New hotspot password made (shown on the Settings page)");
}
static unsigned long settingsChangedAt = 0;

inline void settingsDefaults() {
    settings.autoBrightness     = AUTO_BRIGHTNESS;
    settings.nightBrightnessPct = BRIGHTNESS_MIN_PCT;
    settings.celsius            = false;
    settings.kmh                = false;
    settings.alarmsOn           = true;
    settings.ficmLowV           = ALARM_FICM_LOW_V;
    settings.tftHighF           = ALARM_TFT_HIGH_F;
    settings.ectHighF           = ALARM_ECT_HIGH_F;
    settings.voltsLowV          = ALARM_VOLTS_LOW_V;
    settings.voltsHighV         = ALARM_VOLTS_HIGH_V;
    settings.screenOffMin       = SCREEN_OFF_AFTER_MS / 60000;
    settings.sleepMin           = SLEEP_AFTER_MS / 60000;
    settings.wifiOn             = false;   // off by default: the hotspot draws ~100 mA
}

// Call early in setup(), before anything uses the settings
inline void settingsLoad() {
    settingsDefaults();
    Preferences p;
    if (!p.begin("cyd", true)) return;   // nothing saved yet: keep the defaults
    settings.autoBrightness     = p.getBool("autoBright", settings.autoBrightness);
    settings.nightBrightnessPct = p.getInt("nightPct", settings.nightBrightnessPct);
    settings.celsius            = p.getBool("celsius", settings.celsius);
    settings.kmh                = p.getBool("kmh", settings.kmh);
    settings.alarmsOn           = p.getBool("alarmsOn", settings.alarmsOn);
    settings.ficmLowV           = p.getFloat("ficmLow", settings.ficmLowV);
    settings.tftHighF           = p.getFloat("tftHigh", settings.tftHighF);
    settings.ectHighF           = p.getFloat("ectHigh", settings.ectHighF);
    settings.voltsLowV          = p.getFloat("voltsLow", settings.voltsLowV);
    settings.voltsHighV         = p.getFloat("voltsHigh", settings.voltsHighV);
    settings.screenOffMin       = p.getInt("screenOff", settings.screenOffMin);
    settings.sleepMin           = p.getInt("sleep", settings.sleepMin);
    settings.wifiOn             = p.getBool("wifiOn", settings.wifiOn);
    wifiPassword                = p.getString("wifiPass", "");
    p.end();
    logDebug("SETTINGS", "Loaded from flash: brightness " + String(settings.autoBrightness ? "auto" : "fixed") +
                         ", night " + String(settings.nightBrightnessPct) + "%, " + (settings.celsius ? "C" : "F") +
                         ", " + (settings.kmh ? "KM/H" : "MPH") + ", alarms " + (settings.alarmsOn ? "on" : "off") +
                         ", screen off " + String(settings.screenOffMin) + " min, sleep " + String(settings.sleepMin) + " min, WiFi " + (settings.wifiOn ? "on" : "off"));
}

inline void settingsSave() {
    Preferences p;
    if (!p.begin("cyd", false)) { logDebug("SETTINGS", "Could not open flash storage"); return; }
    p.putBool("autoBright", settings.autoBrightness);
    p.putInt("nightPct", settings.nightBrightnessPct);
    p.putBool("celsius", settings.celsius);
    p.putBool("kmh", settings.kmh);
    p.putBool("alarmsOn", settings.alarmsOn);
    p.putFloat("ficmLow", settings.ficmLowV);
    p.putFloat("tftHigh", settings.tftHighF);
    p.putFloat("ectHigh", settings.ectHighF);
    p.putFloat("voltsLow", settings.voltsLowV);
    p.putFloat("voltsHigh", settings.voltsHighV);
    p.putInt("screenOff", settings.screenOffMin);
    p.putInt("sleep", settings.sleepMin);
    p.putBool("wifiOn", settings.wifiOn);
    p.end();
    settingsDirty = false;
    logDebug("SETTINGS", "Saved to flash");
}

inline void settingsChanged() {
    settingsDirty = true;
    settingsChangedAt = millis();
}

// Call on every loop() pass: saves 2 s after the last change
inline void settingsTick() {
    if (settingsDirty && millis() - settingsChangedAt > 2000) settingsSave();
}

#endif
