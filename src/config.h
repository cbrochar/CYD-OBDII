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

// Touch calibration: raw XPT2046 readings at the screen edges (rotation 1).
// Typical CYD values; if taps land on the wrong card, tap each corner and copy the
// RawX/RawY numbers from the TOUCH_ACT serial log into these four constants.
const int TOUCH_RAW_X_MIN = 200,  TOUCH_RAW_X_MAX = 3700;
const int TOUCH_RAW_Y_MIN = 240,  TOUCH_RAW_Y_MAX = 3800;

// How long the tap menu (NEXT PAGE / FULL SCREEN) stays up if nothing is pressed
const unsigned long MENU_TIMEOUT_MS = 4000;

// The gauge area is drawn off-screen in CANVAS_STRIPS horizontal strips, each pushed to
// the screen in turn. Fewer strips draw faster, more strips need less RAM. Here:
// two half-height strips: Bluetooth Classic leaves too little RAM for the WiFi hotspot
// with a full-size canvas, and this frees about 34 KB.
const int CANVAS_STRIPS = 2;

// Display layout / paging
const int METRICS_PER_PAGE = 4;   // 2 x 2 grid of cards
const int METRIC_PAGES     = 3;   // card pages; must match the metric table in display_ui.h (12 metrics)
const int TOTAL_PAGES      = 5;   // metric pages + the DTC list page + settings
const int PAGE_ABS         = 2;   // ABS wheel speeds (only polled while this page is shown)
const int PAGE_DTC         = 3;   // stored trouble code list
const int PAGE_SETTINGS    = 4;   // settings (saved in flash)

// Stored trouble codes kept for the DTC page (3 columns x 5 rows); pending codes too
const int MAX_DTCS = 15;

// Gauge alarms: outside these limits a gauge flashes red and the status bar shows it on
// every page. Gauges with a limit are also read in the background (every
// ALARM_CHECK_INTERVAL_MS) while they're off screen. Which gauge uses which limit is set
// in the metric table in display_ui.h; NO_ALARM there means no limit.
// These limits are the defaults: they can be changed on the Settings page.
#define NO_ALARM nullptr
const float ALARM_FICM_LOW_V      = 45.0;    // FICM main power below this (nominal ~48 V)
const float ALARM_TFT_HIGH_F      = 220;     // transmission fluid above this
const float ALARM_ECT_HIGH_F      = 230;     // coolant above this
const float ALARM_VOLTS_LOW_V     = 11.8;    // system voltage below this (not charging)
const float ALARM_VOLTS_HIGH_V    = 15.0;    // system voltage above this (overcharging)
const unsigned long ALARM_CHECK_INTERVAL_MS = 2000;
const unsigned long ALARM_BLINK_MS          = 500;

// Live data refresh: the visible page is re-read this often (a refresh takes roughly
// 100 ms per value, so in practice the page updates as fast as the truck answers)
const unsigned long POLL_INTERVAL_MS     = 250;
// Engine trouble codes are re-scanned this often when not on the DTC page
const unsigned long DTC_SCAN_INTERVAL_MS = 10000;

// How long the background task waits between Bluetooth connection attempts
const unsigned long BT_RETRY_INTERVAL_MS = 10000;

// Auto brightness (brightness.h): the light sensor sets the backlight between
// BRIGHTNESS_MIN_PCT (reading at LDR_DARK_RAW or darker) and 100% (at LDR_BRIGHT_RAW).
// To recalibrate, set LIGHT_LOG_INTERVAL_MS and read the LIGHT lines in the serial log.
#define LDR_PIN       34
const bool AUTO_BRIGHTNESS                = true;    // default; false = always full brightness
const int  BRIGHTNESS_MIN_PCT             = 10;      // default night level (Settings page)
const int  LDR_DARK_RAW                   = 2000;    // the CYD sensor reads HIGHER in the dark: ~2600 fully dark, 100-350 shaded
const int  LDR_BRIGHT_RAW                 = 0;       // and 0 in normal room light or brighter
const unsigned long LIGHT_SMOOTHING_MS    = 3000;
const unsigned long LIGHT_LOG_INTERVAL_MS = 0;       // e.g. 500 to log readings for calibration

// Battery protection (power.h): with no activity (the PCM answering, or a tap) the
// backlight goes off, then the board deep-sleeps. It wakes on a tap, or every
// WAKE_CHECK_INTERVAL_MS to try the adapter for WAKE_CHECK_MS with the screen off, and
// stays awake if the truck answers. 0 turns that step off.
const unsigned long SCREEN_OFF_AFTER_MS    = 2UL * 60 * 1000;
const unsigned long SLEEP_AFTER_MS         = 5UL * 60 * 1000;
const unsigned long WAKE_CHECK_INTERVAL_MS = 5UL * 60 * 1000;
const unsigned long WAKE_CHECK_MS          = 30UL * 1000;

// REPLACE THESE 6 HEX VALUES with your adapter's exact physical MAC address bytes!
const uint8_t ELM_MAC[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

// Phone hotspot (wifi_portal.h), switched on from the Settings page. Join this network on a
// phone and open http://192.168.4.1 for live gauges, codes, SD logs and firmware updates.
// The password must be at least 8 characters.
#define WIFI_AP_SSID      "CYD-OBDII"
#define WIFI_AP_PASSWORD  "powerstroke"

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
extern int menuMetric;              // metric whose tap menu is showing (-1 = none)
extern unsigned long menuShownAt;
extern int fullScreenMetric;        // metric shown full screen (-1 = normal pages)

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
extern String pendingCodes[MAX_DTCS];   // pending codes (Mode 07): detected once, not yet confirmed
extern int pendingTotal;            // -1 = not read yet
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