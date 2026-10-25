#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "obd_engine.h"
#include "display_ui.h"
#include "power.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    logDebug("SYS_INIT", "Powering up ESP32 core subsystems...");
    powerBegin();

    tft.init();
    backlightBegin();   // PWM backlight, level set from the light sensor (off during a timed check)
    tft.invertDisplay(true);
    tft.setRotation(1);
    drawSplashScreen();
    delay(2500); // 2.5 second hold so the display text is readable
    tft.fillScreen(TFT_BLACK);

    // Touch uses the default SPI object (VSPI); the TFT is on HSPI via -D USE_HSPI_PORT
    SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin();
    touch.setRotation(1);
    logDebug("TOUCH_INIT", "XPT2046 Driver successfully initialized.");

    canvas.setColorDepth(8);   // 8-bit sprite: 320x214 = ~67 KB instead of ~137 KB
    if (canvas.createSprite(320, CANVAS_H) == nullptr) {
        logDebug("GFX_FATAL", "Canvas sprite allocation failed (out of RAM)!");
    }

    // Start in demo mode: default values on screen, touch + paging fully usable
    loadDemoValues();

    logDebug("BT_CON", "Initializing Bluetooth Classic stack as Master mode...");
    if (!SerialBT.begin("CYD_Diag_Dash", true)) {
        // No longer fatal: keep running in demo mode so the UI can still be tested
        logDebug("BT_FATAL", "Bluetooth Classic stack initialization failed! Staying in demo mode.");
        btStackFailed = true;
    } else {
        delay(200); // Allow time for the Bluetooth stack to stabilize
        SerialBT.setPin("1234");
        btAttempting = true;       // so the status bar says "CONNECTING..." from the start
        startBluetoothTask();      // connect attempts + retries happen in the background
    }

    updateStatusBar(true);
    drawCurrentScreen();
    if (!wokeByTimer) noteActivity();   // the idle timers start once the screen is up
}

void loop() {
    static bool wasConnected = false;
    static unsigned long lastBarCheck = 0;

    // 0. Detect a dropped link (the background task will start reconnecting)
    if (isConnected && !SerialBT.connected()) {
        logDebug("BT_WARN", "Bluetooth link lost. Falling back to demo mode.");
        isConnected = false;
        lastBtRetry = millis();
    }

    // 1. React to connect / disconnect transitions
    if (isConnected != wasConnected) {
        wasConnected = isConnected;
        if (isConnected) clearLiveValues();   // don't show demo numbers as if they were real
        else             loadDemoValues();
        lastBusQuery = 0;                     // force an immediate refresh
        updateStatusBar(true);
        drawCurrentScreen();
    }

    // 2. Touch: one action per tap (finger must lift before the next tap counts, so a
    //    held finger can't hit a menu button that appears under it)
    static bool wasTouched = false;
    bool touchedNow = touch.touched();
    if (touchedNow && !wasTouched && millis() - lastTouchDebounce > 250) {
        lastTouchDebounce = millis();
        TS_Point pt = touch.getPoint();
        int sx = constrain(map(pt.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, 319), 0, 319);
        int sy = constrain(map(pt.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, 239), 0, 239);
        logDebug("TOUCH_ACT", "Tap -> RawX: " + String(pt.x) + ", RawY: " + String(pt.y) +
                              " | Screen: " + String(sx) + "," + String(sy));
        if (screenIsOff) noteActivity();   // this tap only wakes the screen
        else { noteActivity(); handleTap(sx, sy); }
    }
    wasTouched = touchedNow;

    // Close the tap menu if nothing was pressed
    if (menuMetric >= 0 && millis() - menuShownAt > MENU_TIMEOUT_MS) {
        menuMetric = -1;
        drawCurrentScreen();
    }

    // 3. Refresh data + page every POLL_INTERVAL_MS (live data only when connected)
    if (millis() - lastBusQuery > POLL_INTERVAL_MS) {
        lastBusQuery = millis();
        if (isConnected) updateVehicleData();
        drawCurrentScreen();
    }

    // 4. Keep the status bar current (retry countdown, DTC warning); repaints only on change
    if (millis() - lastBarCheck > 250) {
        lastBarCheck = millis();
        updateStatusBar();
    }

    // 5. Battery protection: the PCM answering means the truck is on
    static unsigned long seenPcmReply = 0;
    if (lastPcmReplyMs != seenPcmReply) {
        seenPcmReply = lastPcmReplyMs;
        noteActivity();
    }
    powerTick();

    // 6. Auto brightness from the light sensor
    backlightTick();
}