#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

#include <Arduino.h>
#include "config.h"
#include "settings.h"

// ---- Auto brightness ---------------------------------------------------------------
// The CYD's light sensor (LDR on GPIO34, beside the screen) sets the backlight PWM:
// full brightness in daylight, down to the night level (Settings page) in the dark. The reading is
// smoothed over a few seconds so headlights or a shadow don't make the screen flicker.

const int TFT_BACKLIGHT_PIN  = 21;
const int BACKLIGHT_CHANNEL  = 0;      // LEDC PWM channel
const int BACKLIGHT_FREQ_HZ  = 5000;
const int BACKLIGHT_RES_BITS = 8;      // duty 0-255

static bool  backlightOn    = true;    // false while the screen is off (power.h)
static float lightSmoothed  = -1;      // smoothed LDR reading (-1 = not read yet)
static int   backlightDuty  = 255;

inline int readLightRaw() { return analogRead(LDR_PIN); }

// Map the smoothed reading to a duty cycle. Perceived brightness isn't linear, so the
// 0..1 light level is squared: most of the range is spent on the dim end.
inline int dutyForLight(float raw) {
    float t = (raw - LDR_DARK_RAW) / (float)(LDR_BRIGHT_RAW - LDR_DARK_RAW);   // 0 = dark, 1 = bright
    t = constrain(t, 0.0f, 1.0f);
    float pct = settings.nightBrightnessPct + (100 - settings.nightBrightnessPct) * t * t;
    return (int)(pct * 255 / 100 + 0.5f);
}

inline void backlightApply() {
    ledcWrite(BACKLIGHT_CHANNEL, backlightOn ? backlightDuty : 0);
}

// Call right after tft.init(): takes over the backlight pin and sets the first level
inline void backlightBegin() {
    analogSetPinAttenuation(LDR_PIN, ADC_0db);   // the LDR divider only reaches a few hundred mV
    ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ_HZ, BACKLIGHT_RES_BITS);
    ledcAttachPin(TFT_BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
    lightSmoothed = readLightRaw();
    backlightDuty = settings.autoBrightness ? dutyForLight(lightSmoothed) : 255;
    backlightApply();
}

inline void backlightSetOn(bool on) {
    backlightOn = on;
    backlightApply();
}

// Call on every loop() pass; samples the sensor every 100 ms
inline void backlightTick() {
    static unsigned long lastSample = 0, lastLog = 0;
    if (millis() - lastSample < 100) return;
    lastSample = millis();

    int raw = readLightRaw();
    // Exponential smoothing: each sample moves 100/LIGHT_SMOOTHING_MS of the way (~3 s to settle)
    lightSmoothed += (raw - lightSmoothed) * (100.0f / LIGHT_SMOOTHING_MS);
    int duty = settings.autoBrightness ? dutyForLight(lightSmoothed) : 255;
    if (abs(duty - backlightDuty) >= 2) {   // ignore tiny changes
        backlightDuty = duty;
        backlightApply();
    }
    if (LIGHT_LOG_INTERVAL_MS > 0 && millis() - lastLog >= LIGHT_LOG_INTERVAL_MS) {
        lastLog = millis();
        logDebug("LIGHT", "LDR raw " + String(raw) + ", smoothed " + String((int)lightSmoothed) +
                          " -> backlight " + String(backlightDuty * 100 / 255) + "%");
    }
}

#endif
