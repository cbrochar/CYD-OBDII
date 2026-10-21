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
    tft.drawString("Starting Setup...", 20, 40, 2);

    SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin();
    touch.setRotation(1);
    logDebug("TOUCH_INIT", "XPT2046 Driver successfully initialized.");

    canvas.createSprite(320, 160);

    logDebug("BT_CON", "Initializing Bluetooth Classic stack as Master mode...");
    if (!SerialBT.begin("CYD_Diag_Dash", true)) {
        logDebug("BT_FATAL", "Bluetooth Classic stack initialization failed!");
        while(1);
    }
    delay(200); // Allow time for the Bluetooth stack to stabilize

    SerialBT.setPin("1234");
    tryBluetoothConnect();
    if (!isConnected) {
        buildPage(currentPage);
        canvas.pushSprite(0, 40);
    }
}

void loop() {
    // 1. Process Physical Touch Swipe Telemetry
    if (isConnected && touch.touched()) {
        TS_Point pt = touch.getPoint();
        if (millis() - lastTouchDebounce > 400) {
            lastTouchDebounce = millis();
            logDebug("TOUCH_ACT", "Screen contact detected! Coordinates -> RawX: " + String(pt.x) + ", RawY: " + String(pt.y));
            scrollToNextPage();
        }
    } 

    // 2. Refresh Vehicle Data (Every 2 seconds when connected)
    if (isConnected && (millis() - lastBusQuery > 2000)) {
        lastBusQuery = millis();
        updateVehicleData();
        
        buildPage(currentPage);
        canvas.pushSprite(0, 40);
    }

    // 3. Background Reconnection Handler (Retries every 10 seconds if disconnected)
    if (!isConnected && (millis() - lastBtRetry > 10000)) {
        lastBtRetry = millis();
        tryBluetoothConnect();
        
        buildPage(currentPage);
        canvas.pushSprite(0, 40);
    }
}