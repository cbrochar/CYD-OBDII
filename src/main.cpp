#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "obd_engine.h"
#include "display_ui.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    logDebug("SYS_INIT", "Powering up ESP32 core subsystems...");

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.init();
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
    buildPage(currentPage);
    canvas.pushSprite(0, CANVAS_Y);
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
        buildPage(currentPage);
        canvas.pushSprite(0, CANVAS_Y);
    }

    // 2. Touch: swipe to the next page (works connected or not)
    if (touch.touched()) {
        TS_Point pt = touch.getPoint();
        if (millis() - lastTouchDebounce > 400) {
            lastTouchDebounce = millis();
            logDebug("TOUCH_ACT", "Screen contact detected! Coordinates -> RawX: " + String(pt.x) + ", RawY: " + String(pt.y));
            scrollToNextPage();
        }
    }

    // 3. Refresh data + page every 2 seconds (live data only when connected)
    if (millis() - lastBusQuery > 2000) {
        lastBusQuery = millis();
        if (isConnected) updateVehicleData();
        buildPage(currentPage);
        canvas.pushSprite(0, CANVAS_Y);
    }

    // 4. Keep the status bar current (retry countdown, DTC warning); repaints only on change
    if (millis() - lastBarCheck > 250) {
        lastBarCheck = millis();
        updateStatusBar();
    }
}