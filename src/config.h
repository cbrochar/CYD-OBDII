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
const int METRIC_PAGES     = 3;   // card pages; must match the metric table in display_ui.h (12 metrics)
const int TOTAL_PAGES      = 4;   // metric pages + the DTC list page
const int PAGE_ABS         = 2;   // ABS wheel speeds (only polled while this page is shown)
const int PAGE_DTC         = 3;   // stored trouble code list

// Stored trouble codes kept for the DTC page (3 columns x 5 rows)
const int MAX_DTCS = 15;

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
extern String valWheelLF;
extern String valWheelRF;
extern String valWheelRear;        // single rear-axle (differential) speed sensor
extern bool hasDTCWarning;
extern String dtcCodes[MAX_DTCS];   // decoded codes, e.g. "P0470"
extern int dtcTotal;                // codes reported by the ECU (-1 = not read yet)
extern int absDtcTotal;             // codes reported by the ABS module (-1 = not read yet)

// Fill all metrics with demo values (used while Bluetooth is offline)
void loadDemoValues();
// Blank all metrics to "---" (used when a real link comes up, until real data arrives)
void clearLiveValues();

// Reusable Timestamped Debug Logger
inline void logDebug(String system, String message) {
    Serial.printf("[%010lu] (%s): %s\n", millis(), system.c_str(), message.c_str());
}

#endif