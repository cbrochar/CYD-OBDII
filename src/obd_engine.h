#ifndef OBD_ENGINE_H
#define OBD_ENGINE_H

#include <Arduino.h>
#include "config.h"

// Parse two raw response hex string bytes into an unsigned int
inline uint16_t parseHexBytes(String rawResponse, int startCharIndex) {
    if(rawResponse.length() < startCharIndex + 4) return 0;
    String byteHex = rawResponse.substring(startCharIndex, startCharIndex + 4);
    return (uint16_t) strtol(byteHex.c_str(), NULL, 16);
}

// Send one command and read everything up to the ELM '>' prompt, so each reply is
// always matched to the command that asked for it (fixed delays let replies arrive
// one query late over Bluetooth). Returns the reply without spaces, lines split by '\n'.
inline String elmCommand(const String& cmd, unsigned long timeoutMs = 500) {
    while (SerialBT.available()) SerialBT.read();   // drop anything stale
    SerialBT.print(cmd);
    SerialBT.print("\r");

    String resp = "";
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (!SerialBT.available()) continue;
        char c = SerialBT.read();
        if (c == '>') return resp;
        if (c == '\r' || c == '\n') {
            if (resp.length() > 0 && !resp.endsWith("\n")) resp += '\n';
        } else if (c != ' ') {
            resp += c;
        }
    }
    logDebug("OBD_WARN", "Timeout waiting for '>' after " + cmd + ". Raw: " + resp);
    return resp;
}

// millis() of the last valid reply from the engine PCM (7E0). Used by power.h: a
// Bluetooth link alone doesn't mean the truck is on, but a PCM reply does.
static unsigned long lastPcmReplyMs = 0;

// Header the ELM is currently set to; ATSH is only sent when it changes.
// Cleared on every new connection, since the adapter is re-initialised.
static String currentHeader = "";

inline void setHeader(const char* header) {
    if (currentHeader == header) return;
    logDebug("OBD_BUS", "Setting target header to ATSH" + String(header));
    elmCommand("ATSH" + String(header));
    currentHeader = header;
}

// Single-frame query. Returns the reply line for this service (e.g. "41..." for 01,
// "62..." for 22), or the raw reply (NODATA etc.) so callers' prefix checks fail.
inline String queryCustomPID(const char* header, const char* pid) {
    if (!isConnected) return "";

    setHeader(header);
    String resp = elmCommand(pid);

    char replyService[3] = {(char)(pid[0] + 4), pid[1], 0};   // 01 -> 41, 22 -> 62
    int lineStart = 0;
    while (lineStart < (int)resp.length()) {
        int lineEnd = resp.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = resp.length();
        String line = resp.substring(lineStart, lineEnd);
        lineStart = lineEnd + 1;
        if (line.startsWith(replyService)) {
            logDebug("OBD_BUS", String(pid) + " -> " + line);
            if (strcmp(header, "7E0") == 0) lastPcmReplyMs = millis();
            return line;
        }
    }
    resp.replace("\n", " ");
    logDebug("OBD_WARN", String(pid) + " -> no valid reply: " + resp);
    return resp;
}

// Bluetooth connection attempt. Blocking, so it runs inside btConnectTask (core 0),
// NOT in loop(). It must not touch the display or the metric Strings.
inline void tryBluetoothConnect() {
    logDebug("BT_CON", "Attempting background handshake with 'OBDII'...");
    if (SerialBT.connect(const_cast<uint8_t*>(ELM_MAC))) {
        logDebug("BT_CON", "Success! RFCOMM data channel initialized.");
        myELM.begin(SerialBT, false, 2000, '6');

        // Truck is ISO 15765-4 CAN, 11-bit, 500 kbps (protocol 6, confirmed by the
        // obd_logger capture). Fixed protocol skips the ~6 s auto-search.
        SerialBT.print("ATSP6\r");
        delay(400);
        while(SerialBT.available()) SerialBT.read(); // Clear channel cache
        currentHeader = "";

        isConnected = true;   // set last; loop() notices the change and updates the UI
        logDebug("SYS_INIT", "Link active. Resuming standard sensor loops.");
    } else {
        logDebug("BT_WARN", "Handshake failed. Continuing in demo mode.");
        isConnected = false;
    }
}

// Background task: keeps retrying the connection while offline, so loop()
// (touch + drawing) is never blocked by a slow/failed connect.
inline void btConnectTask(void* param) {
    for (;;) {
        if (!isConnected) {
            btAttempting = true;
            tryBluetoothConnect();
            btAttempting = false;
            lastBtRetry = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(BT_RETRY_INTERVAL_MS));
    }
}

inline void startBluetoothTask() {
    xTaskCreatePinnedToCore(btConnectTask, "btConnect", 8192, NULL, 1, NULL, 0);
}

// Turn a raw 2-byte DTC into its text form, e.g. 0x0470 -> "P0470", 0x4123 -> "C0123"
inline String decodeDTC(uint16_t raw) {
    static const char SYSTEMS[] = {'P', 'C', 'B', 'U'};
    char buf[6];
    snprintf(buf, sizeof(buf), "%c%X%03X", SYSTEMS[raw >> 14], (raw >> 12) & 0x3, raw & 0xFFF);
    return String(buf);
}

// Send a request that may get a multi-frame CAN reply and return the joined payload.
// Single frame: one line starting with replyPrefix. Multi-frame: a length line ("00A")
// then "0:..." "1:..." lines, so read until the ELM '>' prompt and stitch them together.
// Returns "" if no line starting with replyPrefix came back.
inline String queryMultiFrame(const char* header, const char* cmd, const char* replyPrefix) {
    setHeader(header);
    String resp = elmCommand(cmd, 1500);

    String payload = "";
    int lineStart = 0;
    while (lineStart < (int)resp.length()) {
        int lineEnd = resp.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = resp.length();
        String line = resp.substring(lineStart, lineEnd);
        lineStart = lineEnd + 1;

        if (line.length() > 2 && line.charAt(1) == ':') payload += line.substring(2);   // "0:4304..."
        else if (line.startsWith(replyPrefix))           payload += line;                // single frame
    }

    if (!payload.startsWith(replyPrefix)) {
        logDebug("OBD_WARN", "No valid reply to " + String(cmd) + ". Raw: " + resp);
        return "";
    }
    if (strcmp(header, "7E0") == 0) lastPcmReplyMs = millis();
    return payload;
}

// Read a code list from the engine PCM (replies from 7E8) into codes[] and return how
// many there are, or -1 if it didn't answer. On CAN the reply is the service reply byte
// ("43" for Mode 03, "47" for Mode 07), a count byte, then 2 bytes per code,
// e.g. "430204702291" = P0470, P2291.
inline int readDtcList(const char* service, const char* replyPrefix, String* codes) {
    String payload = queryMultiFrame("7E0", service, replyPrefix);
    if (payload.length() < 4) return -1;

    int count = strtol(payload.substring(2, 4).c_str(), NULL, 16);
    int found = 0;
    for (int i = 4; i + 4 <= (int)payload.length() && found < count; i += 4) {
        uint16_t raw = parseHexBytes(payload, i);
        if (raw == 0) continue;
        if (found < MAX_DTCS) codes[found] = decodeDTC(raw);
        found++;
    }
    if (found > 0) logDebug("OBD_DTC", "Mode " + String(service) + " payload: " + payload);
    return found;
}

// Scan for stored codes (Mode 03: confirmed, check-engine light) into dtcCodes / dtcTotal
// and pending codes (Mode 07: seen once, not confirmed yet) into pendingCodes / pendingTotal.
// If the PCM doesn't answer, the last good list is kept rather than claiming they cleared.
inline void checkStoredDTCs() {
    if (!isConnected) return;

    logDebug("OBD_DTC", "Scanning Mode 03 (stored) and Mode 07 (pending) trouble codes...");
    int stored = readDtcList("03", "43", dtcCodes);
    if (stored >= 0) {
        dtcTotal = stored;
        hasDTCWarning = stored > 0;
        if (stored > 0) logDebug("OBD_ALERT", String(stored) + " stored Diagnostic Trouble Code(s) identified!");
    }

    int pending = readDtcList("07", "47", pendingCodes);
    if (pending >= 0) {
        pendingTotal = pending;
        if (pending > 0) logDebug("OBD_ALERT", String(pending) + " pending trouble code(s) (not confirmed yet)");
    }
    if (stored == 0 && pending == 0) logDebug("OBD_DTC", "No trouble codes found on module.");
}

// ---- ABS module ------------------------------------------------------------
// Ford HS-CAN ABS module: requests to 760, replies from 768 (the ELM listens on
// header + 8 automatically). Typical Ford ABS address, not yet confirmed on this truck.
// NOTE: the wheel-speed PIDs and scale below are UNVERIFIED placeholders.
// Confirm the exact PIDs/scaling for your truck's ABS module (e.g. in FORScan)
// before trusting these numbers. Cards just show "---" if the module doesn't answer.
const char* const ABS_HEADER           = "760";
const char* const ABS_PID_LF           = "222B00";
const char* const ABS_PID_RF           = "222B01";
const char* const ABS_PID_REAR         = "222B02";   // single rear-axle sensor in the differential
const float       ABS_WHEEL_KPH_PER_BIT = 0.01;

// Count the ABS module's stored codes for the warning card (fills absDtcTotal).
// Ford modules of this era read DTCs with KWP service 18 ("1800FF00" = all codes);
// the reply is "58", a count byte, then 3 bytes per code (2 code + 1 status).
// UNVERIFIED on this truck: if the module doesn't answer, the card shows "NO DATA".
inline void checkAbsDTCs() {
    String payload = queryMultiFrame(ABS_HEADER, "1800FF00", "58");
    if (payload.length() < 4) return;   // keep the last known state
    absDtcTotal = strtol(payload.substring(2, 4).c_str(), NULL, 16);
    if (absDtcTotal > 0) {
        logDebug("OBD_ALERT", String(absDtcTotal) + " ABS trouble code(s) stored! Payload: " + payload);
    }
}

// Query one ABS wheel-speed PID; updates the value only on a valid "62xxxx" reply
inline void readWheelSpeed(const char* pid, String& out) {
    String raw = queryCustomPID(ABS_HEADER, pid);
    String expected = "6" + String(pid + 1);   // 22xxxx -> 62xxxx
    if (raw.startsWith(expected) && raw.length() >= 10) {
        float kph = parseHexBytes(raw, 6) * ABS_WHEEL_KPH_PER_BIT;
        out = String((int)(kph * 0.621371));
    }
}

inline void pollWheelLF()   { readWheelSpeed(ABS_PID_LF,   valWheelLF); }
inline void pollWheelRF()   { readWheelSpeed(ABS_PID_RF,   valWheelRF); }
inline void pollWheelRear() { readWheelSpeed(ABS_PID_REAR, valWheelRear); }

// ---- Engine / chassis values (one function per card) ------------------------

// Transfer Case Status (4x4 Control Module)
inline void pollTCase() {
    String raw4x4 = queryCustomPID("418F02", "2211B2");
    if (raw4x4.startsWith("6211B2") && raw4x4.length() >= 8) {
        char state = raw4x4.charAt(6);
        if (state == '1') valTCase = "2H";
        else if (state == '2') valTCase = "4H";
        else if (state == '4') valTCase = "4L";
    }
}

// Turbo Boost = MAP - BARO (standard PIDs 0B and 33: A kPa, confirmed on this truck)
// Baro barely changes, so it is read once and cached. PID 0B tops out at 255 kPa,
// so the display caps at roughly 22 psi of boost.
inline void pollBoost() {
    static float baroKPa = 0.0;
    if (baroKPa <= 0.0) {
        String rawBARO = queryCustomPID("7E0", "0133");
        if (rawBARO.startsWith("4133") && rawBARO.length() >= 6) {
            baroKPa = strtol(rawBARO.substring(4, 6).c_str(), NULL, 16);
        }
    }
    String rawMAP = queryCustomPID("7E0", "010B");
    if (rawMAP.startsWith("410B") && rawMAP.length() >= 6) {
        float mapKPa = strtol(rawMAP.substring(4, 6).c_str(), NULL, 16);
        float boost = (mapKPa - (baroKPa > 0.0 ? baroKPa : 101.3)) * 0.145038;
        valBoost = String(boost < 0 ? 0.0 : boost, 1);
    }
}

// FICM Main Power (Powerstroke injector driver supply, ~48 V nominal)
// Raw value is volts x 256: e.g. 0x2F80 = 12160 -> 47.5 V (seen in the truck log).
inline void pollFICM() {
    String rawFICM = queryCustomPID("7E0", "2209D0");
    if (rawFICM.startsWith("6209D0") && rawFICM.length() >= 10) {
        float volts = (float)parseHexBytes(rawFICM, 6) / 256.0;
        valFICM = String(volts, 1);
        if (volts < 45.0) {
            logDebug("SYS_ALERT", "CRITICAL - FICM MAIN POWER DIPPED DANGEROUSLY TO: " + valFICM);
        }
    }
}

// Engine Coolant Temperature (ECT)
inline void pollECT() {
    String rawECT = queryCustomPID("7E0", "221139");
    if (rawECT.startsWith("621139") && rawECT.length() >= 10) {
        float ectC = (int16_t)parseHexBytes(rawECT, 6) / 256.0;
        valECT = String((ectC * 1.8) + 32, 0);
    }
}

// Transmission Fluid Temperature (TFT)
inline void pollTFT() {
    String rawTFT = queryCustomPID("7E0", "221E1C");
    if (rawTFT.startsWith("621E1C") && rawTFT.length() >= 10) {
         float tftF = ((int16_t)parseHexBytes(rawTFT, 6) / 8.0);
         valTFT = String(tftF, 0);
    }
}

// System Volts (standard Mode 01 PID 42, module voltage: ((A*256)+B)/1000)
inline void pollVolts() {
    String rawVolts = queryCustomPID("7E0", "0142");
    if (rawVolts.startsWith("4142") && rawVolts.length() >= 8) {
        valVolts = String(parseHexBytes(rawVolts, 4) / 1000.0, 1);
    }
}

// Engine RPM (standard Mode 01 PID 0C: ((A*256)+B)/4)
inline void pollRPM() {
    String rawRPM = queryCustomPID("7E0", "010C");
    if (rawRPM.startsWith("410C") && rawRPM.length() >= 8) {
        valRPM = String((int)(parseHexBytes(rawRPM, 4) / 4));
    }
}

// Vehicle Speed (standard Mode 01 PID 0D: A km/h)
inline void pollSpeed() {
    String rawSpeed = queryCustomPID("7E0", "010D");
    if (rawSpeed.startsWith("410D") && rawSpeed.length() >= 6) {
        long kph = strtol(rawSpeed.substring(4, 6).c_str(), NULL, 16);
        valSpeed = String((int)(kph * 0.621371));
    }
}

// Reads only the values on screen (the current page's cards, or the one full-screen
// gauge) using each metric's poll function. Defined in display_ui.h with the metric table.
inline void pollVisibleMetrics();
// Reads alarm gauges that are off screen and logs alarms (display_ui.h)
inline void checkAlarms();

// Main polling routine (only called while connected). Only the values being shown
// are read, so each refresh is a handful of requests instead of all of them.
inline void updateVehicleData() {
    if (!isConnected) return;

    unsigned long start = millis();
    pollVisibleMetrics();
    checkAlarms();
    if (currentPage == PAGE_ABS && fullScreenMetric < 0) checkAbsDTCs();   // ABS status card

    // ABS speeds are blanked off their page so stale ones are never shown when you
    // swipe back; the ABS code count is kept, since a stored fault doesn't go stale.
    if (currentPage != PAGE_ABS) valWheelLF = valWheelRF = valWheelRear = "---";

    // Engine codes: every refresh on the DTC page, otherwise every DTC_SCAN_INTERVAL_MS
    // (the status bar warning still needs them)
    static unsigned long lastDtcScan = 0;
    if (currentPage == PAGE_DTC || lastDtcScan == 0 || millis() - lastDtcScan > DTC_SCAN_INTERVAL_MS) {
        lastDtcScan = millis();
        checkStoredDTCs();
    }

    logDebug("SYS_POLL", (fullScreenMetric >= 0 ? "Full-screen gauge " + String(fullScreenMetric)
                                                 : "Page " + String(currentPage))
                         + " refreshed in " + String(millis() - start) + " ms");
}

#endif