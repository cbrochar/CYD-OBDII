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

// Low-Level Custom Ford J1850 Query Pipe with Diagnostics
inline String queryCustomPID(const char* header, const char* pid) {
    if (!isConnected) return "";

    logDebug("OBD_BUS", "Setting target header to ATSH" + String(header));
    SerialBT.print("ATSH");
    SerialBT.print(header);
    SerialBT.print("\r");
    delay(60);
    while(SerialBT.available()) SerialBT.read(); // Flush channel echoes

    logDebug("OBD_BUS", "Sending hex query: " + String(pid));
    SerialBT.print(pid);
    SerialBT.print("\r");

    unsigned long timeout = millis();
    String response = "";
    while ((millis() - timeout < 700)) {
        if (SerialBT.available()) {
            char c = SerialBT.read();
            if (c == '\r' || c == '\n') {
                if (response.length() > 0) break;
            } else if (c != ' ') {
                response += c;
            }
        }
    }

    if (response.length() == 0) {
        logDebug("OBD_WARN", "Timeout waiting for bus response on PID " + String(pid));
    } else {
        logDebug("OBD_BUS", "Raw payload answer received: " + response);
    }
    return response;
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
    SerialBT.print("ATSH");
    SerialBT.print(header);
    SerialBT.print("\r");
    delay(60);
    while(SerialBT.available()) SerialBT.read();
    SerialBT.print(cmd);
    SerialBT.print("\r");

    String resp = "";
    unsigned long start = millis();
    while (millis() - start < 1500) {
        if (!SerialBT.available()) continue;
        char c = SerialBT.read();
        if (c == '>') break;
        if (c == '\r' || c == '\n') resp += '\n';
        else if (c != ' ') resp += c;
    }

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
    return payload;
}

// Scan for Stored Diagnostic Trouble Codes (Mode 03) and fill dtcCodes / dtcTotal.
// On CAN the reply is "43", a count byte, then 2 bytes per code, e.g. "430204702291".
inline void checkStoredDTCs() {
    if (!isConnected) return;

    logDebug("OBD_DTC", "Scanning Mode 03 diagnostic trouble registers...");
    String payload = queryMultiFrame("7E0", "03", "43");   // engine PCM only (replies from 7E8)

    int found = 0;
    bool gotReply = payload.length() >= 4;
    if (gotReply) {
        int count = strtol(payload.substring(2, 4).c_str(), NULL, 16);
        for (int i = 4; i + 4 <= (int)payload.length() && found < count; i += 4) {
            uint16_t raw = parseHexBytes(payload, i);
            if (raw == 0) continue;
            if (found < MAX_DTCS) dtcCodes[found] = decodeDTC(raw);
            found++;
        }
    }

    if (!gotReply) {
        // Keep the last good list rather than claiming the codes cleared
        return;
    }

    dtcTotal = found;
    hasDTCWarning = found > 0;
    if (found > 0) {
        logDebug("OBD_ALERT", String(found) + " stored Diagnostic Trouble Code(s) identified! Payload: " + payload);
    } else {
        logDebug("OBD_DTC", "No trouble codes found on module.");
    }
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

inline void updateAbsData() {
    readWheelSpeed(ABS_PID_LF, valWheelLF);
    readWheelSpeed(ABS_PID_RF, valWheelRF);
    readWheelSpeed(ABS_PID_REAR, valWheelRear);
    checkAbsDTCs();
}

// Main Parametric Scanning Sweep Routine (only called while connected)
inline void updateVehicleData() {
    if (!isConnected) return;

    logDebug("SYS_POLL", "Executing routine vehicle sensor sweep...");

    // 1. Transfer Case Status (4x4 Control Module)
    String raw4x4 = queryCustomPID("418F02", "2211B2");
    if (raw4x4.startsWith("6211B2") && raw4x4.length() >= 8) {
        char state = raw4x4.charAt(6);
        if (state == '1') valTCase = "2H";
        else if (state == '2') valTCase = "4H";
        else if (state == '4') valTCase = "4L";
    }

    // 2. Turbo Boost = MAP - BARO (standard PIDs 0B and 33: A kPa, confirmed on this truck)
    //    Baro barely changes, so it is read once and cached. PID 0B tops out at 255 kPa,
    //    so the display caps at roughly 22 psi of boost.
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

    // 3. FICM Module Status (Powerstroke Main Board Health)
    String rawFICM = queryCustomPID("7E0", "2209D0");
    if (rawFICM.startsWith("6209D0")) {
        float volts = (float)parseHexBytes(rawFICM, 6) / 100.0;
        valFICM = String(volts, 1);
        if (volts < 45.0) {
            logDebug("SYS_ALERT", "CRITICAL - FICM MAIN POWER DIPPED DANGEROUSLY TO: " + valFICM);
        }
    }

    // 4. Engine Coolant Temperature (ECT)
    String rawECT = queryCustomPID("7E0", "221139");
    if (rawECT.startsWith("621139")) {
        float ectC = (int16_t)parseHexBytes(rawECT, 6) / 256.0;
        valECT = String((ectC * 1.8) + 32, 0);
    }

    // 5. Transmission Fluid Temperature (TFT)
    // FIX: response prefix now matches the PID that is actually requested (221E1C -> 621E1C).
    String rawTFT = queryCustomPID("7E0", "221E1C");
    if (rawTFT.startsWith("621E1C")) {
         float tftF = ((int16_t)parseHexBytes(rawTFT, 6) / 8.0);
         valTFT = String(tftF, 0);
    }

    // 6. System Volts (standard Mode 01 PID 42, module voltage: ((A*256)+B)/1000)
    String rawVolts = queryCustomPID("7E0", "0142");
    if (rawVolts.startsWith("4142") && rawVolts.length() >= 8) {
        valVolts = String(parseHexBytes(rawVolts, 4) / 1000.0, 1);
    }

    // 7. Engine RPM (standard Mode 01 PID 0C: ((A*256)+B)/4)
    String rawRPM = queryCustomPID("7E0", "010C");
    if (rawRPM.startsWith("410C") && rawRPM.length() >= 8) {
        valRPM = String((int)(parseHexBytes(rawRPM, 4) / 4));
    }

    // 8. Vehicle Speed (standard Mode 01 PID 0D: A km/h)
    String rawSpeed = queryCustomPID("7E0", "010D");
    if (rawSpeed.startsWith("410D") && rawSpeed.length() >= 6) {
        long kph = strtol(rawSpeed.substring(4, 6).c_str(), NULL, 16);
        valSpeed = String((int)(kph * 0.621371));
    }

    // 9. ABS wheel speeds + ABS codes: only polled while the ABS page is on screen (saves
    //    bus time). Speeds are blanked otherwise so stale ones are never shown; the ABS
    //    code count is kept, since a stored fault doesn't go stale.
    if (currentPage == PAGE_ABS) {
        updateAbsData();
    } else {
        valWheelLF = valWheelRF = valWheelRear = "---";
    }

    checkStoredDTCs();
    logDebug("SYS_POLL", "Sweep done. TCase:" + valTCase + " | Boost:" + valBoost + " | FICM:" + valFICM);
}

#endif