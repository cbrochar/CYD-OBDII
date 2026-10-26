#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "esp_bt.h"
#include "obd_engine.h"
#include "display_ui.h"
#include "power.h"
#include "wifi_portal.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    logDebug("SYS_INIT", "Powering up ESP32 core subsystems...");
    settingsLoad();   // before anything uses brightness, alarm or power settings
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

    canvas.setColorDepth(8);   // 8-bit sprite: 320 x 107 (two strips) = ~34 KB
    if (canvas.createSprite(320, CANVAS_STRIP_H) == nullptr) {   // CANVAS_STRIPS in config.h
        logDebug("GFX_FATAL", "Canvas sprite allocation failed (out of RAM)!");
    }

    // Start in demo mode: default values on screen, touch + paging fully usable
    loadDemoValues();

    logDebug("SYS_MEM", "Free heap before Bluetooth: " + String(ESP.getFreeHeap() / 1024) + " KB");
    // The core starts Bluetooth in dual mode (Classic + BLE). Only Classic is used, so hand
    // the BLE controller's memory back to the heap and start the controller Classic-only;
    // the phone hotspot needs that memory. SerialBT.begin() then uses this controller.
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    btCfg.mode = ESP_BT_MODE_CLASSIC_BT;
    if (esp_bt_controller_init(&btCfg) != ESP_OK || esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
        logDebug("BT_WARN", "Classic-only Bluetooth controller start failed; SerialBT will try the default");
    }

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
    logDebug("SYS_MEM", "Free heap after Bluetooth: " + String(ESP.getFreeHeap() / 1024) + " KB, largest block " +
                        String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB");

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

    // 7. Save Settings page changes to flash
    settingsTick();

    // 8. Phone hotspot (Settings page, WIFI)
    wifiTick();
}