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

// Background Bluetooth Connection Sequence
inline void tryBluetoothConnect() {
    logDebug("BT_CON", "Attempting background handshake with 'OBDII'...");
    if (SerialBT.connect(const_cast<uint8_t*>(ELM_MAC))) {
        logDebug("BT_CON", "Success! RFCOMM data channel initialized.");
        myELM.begin(SerialBT, false, 2000);
        
        SerialBT.print("ATSP1\r");
        delay(400);
        while(SerialBT.available()) SerialBT.read(); // Clear channel cache
        
        isConnected = true;
        tft.fillScreen(TFT_BLACK);
        logDebug("SYS_INIT", "Link active. Resuming standard sensor loops.");
    } else {
        logDebug("BT_WARN", "Handshake failed. Maintaining offline graphic state.");
        isConnected = false;
    }
}

// Scan for Stored Diagnostic Trouble Codes (Mode 03)
inline void checkStoredDTCs() {
    if (!isConnected) return;
    
    logDebug("OBD_DTC", "Scanning Mode 03 diagnostic trouble registers...");
    SerialBT.print("03\r");
    delay(100);
    String resp = "";
    while(SerialBT.available()) {
        char c = SerialBT.read();
        if(c != ' ' && c != '\r' && c != '\n') resp += c;
    }
    
    if (resp.length() > 4 && !resp.startsWith("4300")) {
        hasDTCWarning = true;
        logDebug("OBD_ALERT", "Active Diagnostic Trouble Codes identified! Raw: " + resp);
    } else {
        hasDTCWarning = false;
        logDebug("OBD_DTC", "No trouble codes found on module.");
    }
}

// Main Parametric Scanning Sweep Routine
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

    // 2. Turbo Boost Calculations (MAP - BARO)
    float mapPSI = 0.0, baroPSI = 14.7;
    String rawMAP = queryCustomPID("7E0", "221440");
    if (rawMAP.startsWith("621440")) mapPSI = parseHexBytes(rawMAP, 6) * 0.03625;
    String rawBARO = queryCustomPID("7E0", "221442");
    if (rawBARO.startsWith("621442")) baroPSI = parseHexBytes(rawBARO, 6) * 0.03625;
    float boost = mapPSI - baroPSI;
    valBoost = String(boost < 0 ? 0.0 : boost, 1) + " PSI";

    // 3. FICM Module Status (Powerstroke Main Board Health)
    String rawFICM = queryCustomPID("7E0", "2209D0");
    if (rawFICM.startsWith("6209D0")) {
        float volts = (float)parseHexBytes(rawFICM, 6) / 100.0;
        valFICM = String(volts, 1) + "V";
        if (volts < 45.0) {
            logDebug("SYS_ALERT", "CRITICAL - FICM MAIN POWER DIPPED DANGEROUSLY TO: " + valFICM);
        }
    }

    // 4. Engine Coolant Temperature (ECT)
    String rawECT = queryCustomPID("7E0", "221139");
    if (rawECT.startsWith("621139")) {
        float ectC = (int16_t)parseHexBytes(rawECT, 6) / 256.0;
        valECT = String((ectC * 1.8) + 32, 0) + " F";
    }

    // 5. Transmission Fluid Temperature (TFT)
    String rawTFT = queryCustomPID("7E0", "221E1C");
    if (rawTFT.startsWith("6211E1")) {
         float tftF = ((int16_t)parseHexBytes(rawTFT, 6) / 8.0);
         valTFT = String(tftF, 0) + " F";
    }

    // 6. System Battery Volts (Evaluated via local internal hardware ADC)
    float battery = myELM.batteryVoltage();
    if(myELM.nb_rx_state == ELM_SUCCESS) {
        valVolts = String(battery, 1) + "V";
    } else {
        logDebug("ELM_ERR", "ELMduino tracking state failure reading voltage.");
    }

    checkStoredDTCs();
    logDebug("SYS_POLL", "Sweep done. TCase:" + valTCase + " | Boost:" + valBoost + " | FICM:" + valFICM);
}

#endif
