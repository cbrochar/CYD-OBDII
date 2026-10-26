#ifndef DRIVE_LOG_H
#define DRIVE_LOG_H

#include <Arduino.h>
#include "config.h"
#include "sd_card.h"

// ---- Drive log -----------------------------------------------------------------------
// REC on the LOG page writes the gauge values to /DRIVEnnn.CSV once every
// DRIVE_LOG_INTERVAL_MS, for charting in a spreadsheet:
//     seconds,TURBO BOOST (PSI),VEHICLE SPEED (MPH),...,alarms,mark,codes
//     12.0,14.2,58,...,TRANS TEMP,MARK 1,P0470 P2291 pending:P0128
// Values are always in F and MPH (the units setting only changes the screen), so a file
// never mixes units. Only gauges marked "logged" in the metrics[] table are written, and
// while recording they are read even when they're not on screen. Rows are only written
// while the truck answers: never demo values (and, in the CAN version, not while the
// sniffer page has the bus in listen-only mode). The codes column is filled on the first
// row and whenever the stored or pending codes change.
// Included from display_ui.h after the metrics table, which it reads.

static bool     driveLogging   = false;
static bool     driveLogError  = false;   // no card, or a write failed
static File     driveFile;
static String   driveLogName   = "";      // e.g. "DRIVE003.CSV"
static uint32_t driveRows      = 0;
static uint32_t driveMarks     = 0;
static bool     driveMarkPending = false;  // a MARK goes into the next row
static unsigned long driveStartMs = 0, driveLastRowMs = 0, driveLastFlushMs = 0;
static String   driveLastCodes = "";

inline String driveCodesText() {
    String s = "";
    for (int i = 0; i < min(max(dtcTotal, 0), MAX_DTCS); i++) s += (s.length() ? " " : "") + dtcCodes[i];
    for (int i = 0; i < min(max(pendingTotal, 0), MAX_DTCS); i++) s += (s.length() ? " " : "") + String("pending:") + pendingCodes[i];
    return s.length() ? s : String("none");
}

// True while the gauges can't be read (no truck, or the CAN sniffer has the bus)
inline bool driveLogPaused() {
#ifdef CAN_LOGGER_H
    if (canListenOnly) return true;
#endif
    return !isConnected;
}

inline void driveLogStop() {
    if (!driveLogging) return;
    driveLogging = false;
    driveFile.close();
    logDebug("SD", "Stopped drive log " + driveLogName + ": " + String(driveRows) + " rows, " + String(driveMarks) + " marks");
}

inline void driveLogFail(const String& why) {
    logDebug("SD_ERR", why + ". Drive log stopped.");
    driveLogging = false;
    driveFile.close();
    sdReady = false;
    driveLogError = true;
}

inline bool driveLogStart() {
    if (driveLogging) return true;
    driveLogError = false;
    if (!sdBegin()) { driveLogError = true; return false; }

    char name[16];
    int n = 1;
    for (; n <= 999; n++) {
        snprintf(name, sizeof(name), "/DRIVE%03d.CSV", n);
        if (!SD.exists(name)) break;
    }
    driveFile = SD.open(name, FILE_WRITE);
    if (!driveFile) { driveLogFail("Could not create the drive log file"); return false; }
    driveLogName = String(name + 1);

    String header = "seconds";
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (!metrics[i].logged) continue;
        header += "," + String(metrics[i].label) + (metrics[i].unit[0] ? " (" + String(metrics[i].unit) + ")" : String(""));
    }
    driveFile.println(header + ",alarms,mark,codes");
    driveFile.flush();

    driveRows = driveMarks = 0;
    driveMarkPending = false;
    driveLastCodes = "";
    driveStartMs = driveLastRowMs = driveLastFlushMs = millis();
    driveLogging = true;
    logDebug("SD", "Drive log recording to " + driveLogName);
    return true;
}

inline void driveLogMark() {
    if (!driveLogging) return;
    driveMarks++;
    driveMarkPending = true;
    logDebug("SD", "Drive log MARK " + String(driveMarks));
}

inline void driveLogRow() {
    String row = String((millis() - driveStartMs) / 1000.0, 1);
    String alarms = "";
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (!metrics[i].logged) continue;
        const String& v = *metrics[i].value;
        row += "," + (isNumber(v) ? v : String(""));
        if (metricAlarmState(i) != 0) alarms += (alarms.length() ? " " : "") + String(metrics[i].label);
    }
    row += "," + alarms + "," + (driveMarkPending ? "MARK " + String(driveMarks) : String(""));
    String codes = driveCodesText();
    row += "," + (codes != driveLastCodes ? codes : String(""));
    driveLastCodes = codes;
    driveMarkPending = false;

    if (driveFile.println(row) == 0) { driveLogFail("SD write failed"); return; }
    driveRows++;
    if (millis() - driveLastFlushMs > DRIVE_LOG_FLUSH_MS) {
        driveLastFlushMs = millis();
        driveFile.flush();
    }
}

// Call on every loop() pass. Reads the logged gauges that aren't on screen one at a time,
// spread over the interval (a Bluetooth adapter takes ~100 ms per read, so reading them
// all at once would stall the touch screen), and writes a row every DRIVE_LOG_INTERVAL_MS.
inline void driveLogTick() {
    if (!driveLogging || driveLogPaused()) return;

    static int nextPoll = 0;
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll >= DRIVE_LOG_INTERVAL_MS / METRIC_COUNT) {
        lastPoll = millis();
        for (int tries = 0; tries < METRIC_COUNT; tries++) {
            int i = nextPoll;
            nextPoll = (nextPoll + 1) % METRIC_COUNT;
            if (metrics[i].logged && !metricVisible(i)) { metrics[i].poll(); break; }
        }
    }

    if (millis() - driveLastRowMs >= DRIVE_LOG_INTERVAL_MS) {
        driveLastRowMs = millis();
        driveLogRow();
    }
}

#endif
