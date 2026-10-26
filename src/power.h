#ifndef POWER_H
#define POWER_H

#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "brightness.h"
#include "esp_sleep.h"

// ---- Battery protection ------------------------------------------------------------
// The OBD port's power pin is live with the key off, so when the truck goes quiet the
// display steps down:
//   no activity for SCREEN_OFF_AFTER_MS -> backlight off (a tap turns it back on)
//   no activity for SLEEP_AFTER_MS      -> deep sleep
// "Activity" is the PCM answering a request, or a tap. A Bluetooth link alone doesn't
// count: the adapter is powered from the OBD port and stays connected with the key off.
//
// Deep sleep ends on a tap, or every WAKE_CHECK_INTERVAL_MS for a timed check: the board
// starts with the screen off and tries the adapter for WAKE_CHECK_MS. If the PCM answers
// (key on) the screen comes on; otherwise it goes back to sleep. Waking restarts the
// firmware, like a power-on.

static unsigned long lastActivityMs = 0;
static bool screenIsOff  = false;
static bool wokeByTimer  = false;   // timed check in progress (screen stays off)

inline void setScreenOn(bool on) {
    if (on == !screenIsOff) return;
    screenIsOff = !on;
    backlightSetOn(on);
    logDebug("POWER", on ? "Screen on" : "No activity, screen off (tap to wake)");
}

inline void noteActivity() {
    lastActivityMs = millis();
    if (wokeByTimer) {
        wokeByTimer = false;
        logDebug("POWER", "Truck is on: staying awake");
    }
    setScreenOn(true);
}

// Call first thing in setup(), before backlightBegin(): releases the backlight pin held
// low during deep sleep, logs why the board woke up, and keeps the screen dark during a
// timed check
inline void powerBegin() {
    gpio_hold_dis((gpio_num_t)TFT_BACKLIGHT_PIN);
    gpio_deep_sleep_hold_dis();
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT1:
            logDebug("POWER", "Woke from deep sleep: screen tap");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            logDebug("POWER", "Woke for a timed check: is the truck on? (screen stays off)");
            wokeByTimer = true;
            screenIsOff = true;
            backlightOn = false;
            break;
        default:
            break;
    }
    lastActivityMs = millis();
}

inline void enterDeepSleep(const String& why) {
    logDebug("POWER", why + ", entering deep sleep (wakes on a tap" +
                      (WAKE_CHECK_INTERVAL_MS > 0 ? ", or in " + String(WAKE_CHECK_INTERVAL_MS / 1000) +
                                                    " s to check the truck)" : String(")")));
    if (settingsDirty) settingsSave();
    Serial.flush();

    tft.writecommand(0x10);                       // ILI9341 sleep in: panel draws almost nothing
    ledcDetachPin(TFT_BACKLIGHT_PIN);             // PWM stops in deep sleep; hold the pin low instead
    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, LOW);
    gpio_hold_en((gpio_num_t)TFT_BACKLIGHT_PIN);  // keep the backlight off while asleep
    gpio_deep_sleep_hold_en();

    // The touch controller pulls its IRQ line low while the screen is pressed
    esp_sleep_enable_ext1_wakeup(1ULL << XPT2046_IRQ, ESP_EXT1_WAKEUP_ALL_LOW);
    if (WAKE_CHECK_INTERVAL_MS > 0) esp_sleep_enable_timer_wakeup((uint64_t)WAKE_CHECK_INTERVAL_MS * 1000ULL);

    esp_deep_sleep_start();
}

// Call on every loop() pass, after the Bluetooth and touch handling
inline void powerTick() {
    unsigned long idle = millis() - lastActivityMs;
    if (wokeByTimer) {
        if (idle > WAKE_CHECK_MS) enterDeepSleep("Timed check: no reply from the truck");
        return;
    }
    unsigned long screenOffMs = settings.screenOffMin * 60000UL, sleepMs = settings.sleepMin * 60000UL;
    if (screenOffMs > 0 && !screenIsOff && idle > screenOffMs) setScreenOn(false);
    if (sleepMs > 0 && idle > sleepMs) enterDeepSleep("No activity for " + String(settings.sleepMin) + " min");
}

#endif
