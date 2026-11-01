#ifndef BUS_SNIFFER_H
#define BUS_SNIFFER_H

#include <Arduino.h>
#include "config.h"
#include "sd_card.h"

// ---- Bus sniffer (SNIFF page) ---------------------------------------------------------
// Records what another tool asks the truck's modules, and what they answer, so the right
// PIDs and scaling can be worked out. Typical use: FORScan on a second adapter through a
// Y-cable shows a reading by name and value, and this captures the raw request and reply.
//
// START puts the ELM327 in monitor mode (ATMA). It only listens: silent monitoring is the
// ELM327's default, so it doesn't even acknowledge CAN frames. The gauges aren't read
// while it records. Every line goes to /SNIFnnn.TXT on the SD card with the seconds since
// START; MARK adds a numbered "MARK n" line (e.g. when FORScan switches reading, or the
// transfer case is shifted). STOP resets the adapter (ATZ + the usual setup) and the
// gauges carry on.
//
// The ELM327 stops monitoring on its own when its buffer overflows (BUFFER FULL) and shows
// its '>' prompt; the sniffer notes that in the file and starts ATMA again.
// Leaving the page is blocked while recording: a page swipe stalls the loop long enough to
// overflow the Bluetooth receive buffer.
// busSniffing is declared in obd_engine.h, which skips its own requests while it's set.

struct SniffMode {
    const char* name;      // shown on the page and written in the file
    const char* protocol;  // ATSPn
    const char* filterId;  // CAN: ATCF value (nullptr = every ID)
    const char* filterMask;
};

static const SniffMode SNIFF_MODES[] = {
    // Engine PCM requests (7E0) and replies (7E8), plus the other diagnostic IDs 7E1-7EF.
    // Filtered because a cheap ELM327 overflows on all the 500k bus traffic.
    {"CAN ENGINE", "ATSP6", "7E0", "7F0"},
    // Every CAN ID: shows which modules talk, but expect BUFFER FULL restarts
    {"CAN ALL",    "ATSP6", nullptr, nullptr},
    // ISO 9141 on OBD pin 7: the ABS and 4WD modules, per the workshop manual
    {"K-LINE",     "ATSP3", nullptr, nullptr},
};
static const int SNIFF_MODE_COUNT = sizeof(SNIFF_MODES) / sizeof(SniffMode);

const int SNIFF_RECENT = 6;   // lines shown on the page

static int      sniffMode       = 0;
static File     sniffFile;
static String   sniffName       = "";     // e.g. "SNIF003.TXT"
static String   sniffError      = "";     // why START failed, or why recording stopped
static String   sniffLine       = "";     // line being received
static String   sniffRecent[SNIFF_RECENT];
static int      sniffRecentNext = 0;
static uint32_t sniffLines = 0, sniffMarks = 0, sniffRestarts = 0;
static unsigned long sniffStartMs = 0, sniffLastFlushMs = 0;

inline void sniffWrite(const String& text, bool show = true) {
    sniffFile.println(String((millis() - sniffStartMs) / 1000.0, 3) + " " + text);
    if (!show) return;
    sniffRecent[sniffRecentNext] = text;
    sniffRecentNext = (sniffRecentNext + 1) % SNIFF_RECENT;
}

// Sends one setup command and notes the adapter's answer in the file (a "?" means this
// adapter doesn't support it, which matters when reading the capture)
inline void sniffSetup(const String& cmd) {
    String resp = elmCommand(cmd, cmd == "ATZ" ? 2500 : 500);
    resp.replace("\n", " ");
    resp.trim();
    sniffFile.println("# " + cmd + " -> " + resp);
}

inline bool sniffStart() {
    if (busSniffing) return true;
    sniffError = "";
    if (!isConnected) { sniffError = "Connect the adapter first"; return false; }
    if (!sdBegin())   { sniffError = "No SD card: insert one"; return false; }

    char name[16];
    int n = 1;
    for (; n <= 999; n++) {
        snprintf(name, sizeof(name), "/SNIF%03d.TXT", n);
        if (!SD.exists(name)) break;
    }
    if (n > 999) { sniffError = "SNIF001-999 exist: delete some"; return false; }
    sniffFile = SD.open(name, FILE_WRITE);
    if (!sniffFile) { sniffError = "Could not create the file"; sdReady = false; return false; }
    sniffName = String(name + 1);

    const SniffMode& m = SNIFF_MODES[sniffMode];
    logDebug("SNIFF", "Recording " + String(m.name) + " to " + sniffName);
    sniffFile.println("# CYD-OBDII bus sniffer");
    sniffFile.println("# Mode: " + String(m.name));
    sniffFile.println("# Lines: seconds since START, then what the adapter heard (ID/header first)");
    sniffStartMs = millis();
    sniffSetup("ATZ");
    sniffSetup("ATE0");
    sniffSetup("ATL0");
    sniffSetup("ATS1");     // spaces between bytes
    sniffSetup("ATH1");     // show the CAN ID / K-line header
    sniffSetup(m.protocol);
    if (strcmp(m.protocol, "ATSP6") == 0) {
        sniffSetup("ATCAF0");   // show every byte, including the ISO-TP frame byte
        if (m.filterId) {
            sniffSetup("ATCF" + String(m.filterId));
            sniffSetup("ATCM" + String(m.filterMask));
        }
    }
    currentHeader = "";        // the adapter was reset
    sniffFile.println("# Recording");
    sniffFile.flush();

    for (int i = 0; i < SNIFF_RECENT; i++) sniffRecent[i] = "";
    sniffRecentNext = 0;
    sniffLine = "";
    sniffLines = sniffMarks = sniffRestarts = 0;
    sniffStartMs = sniffLastFlushMs = millis();
    while (SerialBT.available()) SerialBT.read();
    SerialBT.print("ATMA\r");
    busSniffing = true;
    return true;
}

inline void sniffMark() {
    if (!busSniffing) return;
    sniffMarks++;
    sniffWrite("MARK " + String(sniffMarks));
    logDebug("SNIFF", "MARK " + String(sniffMarks));
}

// Stops recording and closes the file. With restore, the adapter is taken out of monitor
// mode and set up for the gauges again (skipped when the link is already gone).
inline void sniffStop(bool restore = true, const String& why = "") {
    if (!busSniffing) return;
    busSniffing = false;
    if (restore && isConnected) {
        SerialBT.print("\r");   // any character ends monitor mode
        unsigned long t = millis();
        String rest = "";
        while (millis() - t < 1500) {   // keep what arrives until the prompt
            if (!SerialBT.available()) continue;
            char c = SerialBT.read();
            if (c == '>') break;
            if (c == '\r' || c == '\n') { if (rest.length()) { sniffWrite(rest, false); sniffLines++; rest = ""; } }
            else rest += c;
        }
        elmCommand("ATZ", 2500);
        elmCommand("ATE0");
        elmCommand("ATL0");
        elmCommand("ATS0");
        elmCommand("ATH0");
        elmCommand("ATSP6");
        currentHeader = "";
    }
    sniffWrite("# Stopped" + (why.length() ? ": " + why : String("")) + ". " + String(sniffLines) + " lines, " +
               String(sniffMarks) + " marks, " + String(sniffRestarts) + " restarts", false);
    sniffFile.close();
    if (why.length()) sniffError = why;
    logDebug("SNIFF", "Stopped " + sniffName + ": " + String(sniffLines) + " lines" + (why.length() ? " (" + why + ")" : String("")));
}

// Call on every loop() pass: moves what the adapter heard into the file
inline void sniffTick() {
    if (!busSniffing) return;
    if (!isConnected) { sniffStop(false, "Bluetooth link lost"); return; }

    // At most 1 KB a pass, so touch and drawing still get a turn
    for (int i = 0; i < 1024 && SerialBT.available(); i++) {
        char c = SerialBT.read();
        if (c == '>') {
            // Monitoring ended by itself (usually BUFFER FULL): start it again
            if (sniffLine.length()) { sniffWrite(sniffLine); sniffLines++; sniffLine = ""; }
            sniffRestarts++;
            sniffWrite("# monitor stopped, restarted (" + String(sniffRestarts) + ")");
            SerialBT.print("ATMA\r");
        } else if (c == '\r' || c == '\n') {
            if (sniffLine.length()) { sniffWrite(sniffLine); sniffLines++; sniffLine = ""; }
        } else if (sniffLine.length() < 120) {
            sniffLine += c;
        }
    }

    if (millis() - sniffLastFlushMs > 2000) {   // a power cut loses at most 2 s
        sniffLastFlushMs = millis();
        sniffFile.flush();
    }
}

#endif
