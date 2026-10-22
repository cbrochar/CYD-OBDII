#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <BluetoothSerial.h>
#include "ELMduino.h"

// Hardware Pin Definitions for the Cheap Yellow Display (CYD)
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33

// Display layout / paging
const int METRICS_PER_PAGE = 4;   // 2 x 2 grid of cards
const int TOTAL_PAGES      = 2;   // must match the metric table in display_ui.h (8 metrics)

// How long the background task waits between Bluetooth connection attempts
const unsigned long BT_RETRY_INTERVAL_MS = 10000;

// REPLACE THESE 6 HEX VALUES with your adapter's exact physical MAC address bytes!
const uint8_t ELM_MAC[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

// Global Shared Subsystem Instances
extern TFT_eSPI tft;
extern TFT_eSprite canvas;
extern BluetoothSerial SerialBT;
extern ELM327 myELM;
extern XPT2046_Touchscreen touch;

// Program Tracking States
// (volatile: written by the Bluetooth task on core 0, read by loop() on core 1)
extern volatile bool isConnected;
extern volatile bool btAttempting;    // true while a connection attempt is in progress
extern volatile bool btStackFailed;   // true if SerialBT.begin() failed
extern unsigned long lastBusQuery;
extern unsigned long lastTouchDebounce;
extern unsigned long lastBtRetry;
extern int currentPage;

// Shared Metric Buffers
extern String valTCase;
extern String valBoost;
extern String valFICM;
extern String valECT;
extern String valTFT;
extern String valVolts;
extern String valRPM;
extern String valSpeed;
extern bool hasDTCWarning;

// Fill all metrics with demo values (used while Bluetooth is offline)
void loadDemoValues();
// Blank all metrics to "---" (used when a real link comes up, until real data arrives)
void clearLiveValues();

// Reusable Timestamped Debug Logger
inline void logDebug(String system, String message) {
    Serial.printf("[%010lu] (%s): %s\n", millis(), system.c_str(), message.c_str());
}

#endif