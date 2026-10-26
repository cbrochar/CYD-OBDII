#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include "config.h"
#include "dtc_descriptions.h"
#include "settings.h"

// ---- Layout --------------------------------------------------------------
// Screen (rotation 1) is 320x240.
//   y 0..23    : status bar, drawn straight to the TFT (never scrolls)
//   y 26..239  : 320x214 canvas (8-bit colour to save RAM) holding the
//                2x2 card grid (y 4..191) and the page dots (y ~204)
const int STATUS_BAR_H = 24;
const int CANVAS_Y     = 26;
const int CANVAS_H     = 214;
const int CARD_W       = 154;
const int CARD_H       = 92;
const int COL_X[2]     = {4, 162};
const int ROW_Y[2]     = {4, 100};
const int PIP_Y        = 204;

// ---- Metric table ----------------------------------------------------------
// Pages are filled in order, METRICS_PER_PAGE at a time.
// To add/reorder a value, edit this table (and TOTAL_PAGES in config.h).
// Values are stored WITHOUT units; the unit is drawn separately in a smaller font.
// poll reads that value from the truck; only on-screen metrics are polled, except that
// metrics with alarm limits are also read in the background (checkAlarms).
// alarmLow / alarmHigh: outside these the card flashes red (NO_ALARM = no limit). They
// point at the limits in settings (Settings page), or at any float constant.
struct Metric {
    const char* label;
    String*     value;
    const char* unit;
    uint32_t    color;
    void      (*poll)();
    const float* alarmLow;
    const float* alarmHigh;
    bool        logged;      // written by the drive log (false for values that can't be read yet)
};

static const Metric metrics[] = {
    // Page 0
    {"TRANSFER CASE",  &valTCase,  "",    TFT_ORANGE,  pollTCase,  NO_ALARM, NO_ALARM, false},
    {"TURBO BOOST",    &valBoost,  "PSI", TFT_CYAN,    pollBoost,  NO_ALARM, NO_ALARM, true },
    {"VEHICLE SPEED",  &valSpeed,  "MPH", TFT_SKYBLUE, pollSpeed,  NO_ALARM, NO_ALARM, true },
    {"TRANS TEMP",     &valTFT,    "F",   TFT_MAGENTA, pollTFT,    NO_ALARM, &settings.tftHighF, true },
    // Page 1
    {"FICM MAIN PWR",  &valFICM,   "V",   TFT_GREEN,   pollFICM,   &settings.ficmLowV, NO_ALARM, true },
    {"COOLANT TEMP",   &valECT,    "F",   TFT_RED,     pollECT,    NO_ALARM, &settings.ectHighF, true },
    {"SYSTEM VOLTS",   &valVolts,  "V",   TFT_YELLOW,  pollVolts,  &settings.voltsLowV, &settings.voltsHighV, true },
    {"ENGINE RPM",     &valRPM,    "",    TFT_WHITE,   pollRPM,    NO_ALARM, NO_ALARM, true },
    // Page 2 (ABS) - fronts on top, rear axle bottom-left;
    // the 4th slot is the ABS warning card (drawn by drawAbsStatusCard, not this table)
    {"LF WHEEL SPD",   &valWheelLF,   "MPH", TFT_SKYBLUE, pollWheelLF,   NO_ALARM, NO_ALARM, false},
    {"RF WHEEL SPD",   &valWheelRF,   "MPH", TFT_SKYBLUE, pollWheelRF,   NO_ALARM, NO_ALARM, false},
    {"REAR AXLE SPD",  &valWheelRear, "MPH", TFT_CYAN,    pollWheelRear, NO_ALARM, NO_ALARM, false},
};
static const int METRIC_COUNT = sizeof(metrics) / sizeof(metrics[0]);
static_assert(METRIC_COUNT <= METRIC_PAGES * METRICS_PER_PAGE,
              "Metric table has more entries than METRIC_PAGES * METRICS_PER_PAGE");
static_assert(METRIC_COUNT <= PAGE_ABS * METRICS_PER_PAGE + 3,
              "The ABS page's 4th slot is reserved for the ABS status card");
static_assert(PAGE_DTC == METRIC_PAGES && PAGE_LOG == PAGE_DTC + 1 && PAGE_SETTINGS == PAGE_LOG + 1 &&
              TOTAL_PAGES == METRIC_PAGES + 3,
              "Page order: metric pages, DTC page, LOG page, settings page");

inline bool metricVisible(int idx) {
    if (fullScreenMetric >= 0) return idx == fullScreenMetric;
    return currentPage < METRIC_PAGES && idx / METRICS_PER_PAGE == currentPage;
}

inline void pollVisibleMetrics() {
    for (int idx = 0; idx < METRIC_COUNT; idx++) {
        if (metricVisible(idx)) metrics[idx].poll();
    }
}

// ---- Gauge alarms ------------------------------------------------------------------
// Only live readings can alarm: never demo values, and never "---" (no reply yet).
// Alarms can be switched off on the Settings page.
inline bool metricHasAlarm(int idx) {
    return settings.alarmsOn && (metrics[idx].alarmLow || metrics[idx].alarmHigh);
}

inline bool isNumber(const String& v) {
    return v.length() > 0 && (isDigit(v[0]) || (v[0] == '-' && v.length() > 1 && isDigit(v[1])));
}

// -1 = below alarmLow, 1 = above alarmHigh, 0 = in range / no live value
inline int metricAlarmState(int idx) {
    const Metric& m = metrics[idx];
    const String& v = *m.value;
    if (!isConnected || !metricHasAlarm(idx) || !isNumber(v)) return 0;
    float x = v.toFloat();
    if (m.alarmLow && x < *m.alarmLow) return -1;
    if (m.alarmHigh && x > *m.alarmHigh) return 1;
    return 0;
}

// Value and unit as shown: temperatures are stored in F and speeds in MPH, and converted
// here if the Settings page says C or KM/H
inline String shownValue(int idx) {
    const Metric& m = metrics[idx];
    const String& v = *m.value;
    if (!isNumber(v)) return v;
    if (settings.celsius && strcmp(m.unit, "F") == 0) return String((v.toFloat() - 32) * 5 / 9, 0);
    if (settings.kmh && strcmp(m.unit, "MPH") == 0)   return String(v.toFloat() * 1.609344, 0);
    return v;
}

inline const char* shownUnit(int idx) {
    const char* u = metrics[idx].unit;
    if (settings.celsius && strcmp(u, "F") == 0) return "C";
    if (settings.kmh && strcmp(u, "MPH") == 0)   return "KM/H";
    return u;
}

inline int firstAlarmMetric() {
    for (int idx = 0; idx < METRIC_COUNT; idx++) {
        if (metricAlarmState(idx) != 0) return idx;
    }
    return -1;
}

// true during the "on" half of the alarm flash
inline bool alarmBlinkOn() { return (millis() / ALARM_BLINK_MS) % 2 == 0; }

// Called from updateVehicleData(): reads the alarm metrics that aren't on screen every
// ALARM_CHECK_INTERVAL_MS, so an alarm shows in the status bar on any page, and logs
// each alarm starting and clearing
inline void checkAlarms() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= ALARM_CHECK_INTERVAL_MS) {
        lastCheck = millis();
        for (int idx = 0; idx < METRIC_COUNT; idx++) {
            if (metricHasAlarm(idx) && !metricVisible(idx)) metrics[idx].poll();
        }
    }

    static int lastState[METRIC_COUNT] = {0};
    for (int idx = 0; idx < METRIC_COUNT; idx++) {
        int state = metricAlarmState(idx);
        if (state == lastState[idx]) continue;
        lastState[idx] = state;
        const Metric& m = metrics[idx];
        if (state != 0) {
            logDebug("ALARM", String(m.label) + (state < 0 ? " LOW: " : " HIGH: ") + *m.value + " " + m.unit +
                              " (limit " + String(state < 0 ? *m.alarmLow : *m.alarmHigh, 1) + ")");
        } else {
            logDebug("ALARM", String(m.label) + " back in range: " + *m.value + " " + m.unit);
        }
    }
}

#include "drive_log.h"   // needs the metrics table and alarm helpers above

// ---- Status bar --------------------------------------------------------------
// Drawn directly on the TFT. Only repaints when its text changes.
inline void updateStatusBar(bool force = false) {
    static String lastText = "";
    String text;
    uint16_t bg, fg;

    if (isConnected) {
        int alarm = firstAlarmMetric();
        if (alarm >= 0) {
            // e.g. "[!] TRANS TEMP HIGH: 224 F"
            const Metric& m = metrics[alarm];
            text = "[!] " + String(m.label) + (metricAlarmState(alarm) < 0 ? " LOW: " : " HIGH: ") +
                   shownValue(alarm) + " " + shownUnit(alarm);
            bg = TFT_RED;      fg = TFT_WHITE;
        } else if (hasDTCWarning) {
            text = "[!] STORED ERROR CODES DETECTED";
            bg = TFT_RED;      fg = TFT_WHITE;
        } else if (pendingTotal > 0) {
            text = "PENDING ERROR CODES - SEE CODES PAGE";
            bg = TFT_YELLOW;   fg = TFT_BLACK;
        } else {
            text = "MONITOR ACTIVE - TAP TO SWIPE";
            bg = TFT_DARKGREY; fg = TFT_BLACK;
        }
    } else if (btStackFailed) {
        text = "BT STACK FAILED - DEMO DATA";
        bg = TFT_RED;          fg = TFT_WHITE;
    } else if (btAttempting) {
        text = "BT OFFLINE - CONNECTING...";
        bg = TFT_YELLOW;       fg = TFT_BLACK;
    } else {
        unsigned long elapsed = millis() - lastBtRetry;
        unsigned long secs = (elapsed >= BT_RETRY_INTERVAL_MS) ? 0 : (BT_RETRY_INTERVAL_MS - elapsed) / 1000;
        text = "BT OFFLINE - DEMO DATA - RETRY " + String(secs) + "s";
        bg = TFT_YELLOW;       fg = TFT_BLACK;
    }

    if (!force && text == lastText) return;
    lastText = text;

    tft.fillRect(0, 0, 320, STATUS_BAR_H, bg);
    tft.setTextSize(1);
    tft.setTextColor(fg, bg);
    tft.drawCentreString(text, 160, 4, 2);
}

// ---- Cards / pages -------------------------------------------------------------
// Label (font 2) on top, big value (font 4, x2) below, small unit beside the value.
// If value + unit would not fit the card, the value drops to x1 automatically.
// A card in alarm has a red frame and flashes solid red.
inline void drawCardToCanvas(int x, int y, const char* label, const String& value,
                             const char* unit, uint32_t color, bool alarm = false) {
    const int PAD = 10;
    uint32_t bg = TFT_BLACK;
    if (alarm) {
        color = TFT_RED;
        if (alarmBlinkOn()) {
            bg = TFT_RED;
            canvas.fillRect(x, y, CARD_W, CARD_H, TFT_RED);
        }
    }
    uint32_t valueColor = (bg == TFT_RED) ? TFT_WHITE : color;
    canvas.drawRect(x, y, CARD_W, CARD_H, color);
    canvas.drawRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, color);

    // Label
    canvas.setTextColor(TFT_WHITE, bg);
    canvas.setTextSize(1);
    canvas.drawString(label, x + PAD, y + 8, 2);

    // Work out the largest value size that fits alongside the unit
    int unitW = 0;
    if (unit[0]) {
        canvas.setTextSize(1);
        unitW = canvas.textWidth(unit, 2) + 6;
    }
    int size = 2;
    canvas.setTextSize(size);
    if (canvas.textWidth(value, 4) + unitW > CARD_W - 2 * PAD) {
        size = 1;
        canvas.setTextSize(size);
    }
    int valW = canvas.textWidth(value, 4);
    int valH = canvas.fontHeight(4);

    // Centre the value vertically in the area below the label
    int areaTop = y + 28;
    int areaH   = CARD_H - 28 - 6;
    int valY    = areaTop + (areaH - valH) / 2;

    canvas.setTextColor(valueColor, bg);
    canvas.drawString(value, x + PAD, valY, 4);

    if (unit[0]) {
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_WHITE, bg);
        canvas.drawString(unit, x + PAD + valW + 6, valY + valH - 20, 2);
    }
}

// ABS status card (4th slot of the ABS page): a red warning triangle when the ABS
// module reports stored codes, green "OK" when clear, grey while not read yet.
inline void drawAbsStatusCard(int x, int y) {
    const int PAD = 10;
    uint32_t color = (absDtcTotal > 0) ? TFT_RED : (absDtcTotal == 0 ? TFT_GREEN : TFT_DARKGREY);
    canvas.drawRect(x, y, CARD_W, CARD_H, color);
    canvas.drawRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, color);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.drawString("ABS STATUS", x + PAD, y + 8, 2);

    if (absDtcTotal > 0) {
        // Warning triangle with a "!" in it
        int tx = x + PAD, ty = y + 32, tw = 50, th = 48;
        canvas.fillTriangle(tx + tw / 2, ty, tx, ty + th, tx + tw, ty + th, TFT_RED);
        canvas.setTextColor(TFT_BLACK, TFT_RED);
        canvas.drawCentreString("!", tx + tw / 2, ty + 18, 4);

        canvas.setTextColor(TFT_RED, TFT_BLACK);
        canvas.drawString("WARNING", tx + tw + 10, ty + 8, 2);
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        canvas.drawString(String(absDtcTotal) + (absDtcTotal == 1 ? " CODE" : " CODES"),
                          tx + tw + 10, ty + 28, 2);
    } else {
        canvas.setTextColor(color, TFT_BLACK);
        canvas.drawString(absDtcTotal == 0 ? "OK" : "NO DATA", x + PAD, y + 46, 4);
    }
}

// DTC list page: one framed panel, header with counts, codes in 3 columns x 5 rows:
// stored codes (Mode 03) in red, then pending codes (Mode 07) in yellow.
// Red frame when codes are stored, yellow when only pending, green when clear.
// Tapping a code opens a box with its description (dtcDetail).
const int DTC_X = 4, DTC_Y = 4, DTC_W = 312, DTC_H = 188;
const int DTC_COL_W = 100, DTC_ROW_H = 29, DTC_LIST_Y = DTC_Y + 38;

static int dtcDetail = -1;   // list slot whose description box is open (-1 = none)

// Codes in list order: stored first, then pending (at most MAX_DTCS shown)
inline int dtcShownCount() {
    return min(min(max(dtcTotal, 0), MAX_DTCS) + min(max(pendingTotal, 0), MAX_DTCS), MAX_DTCS);
}

inline String dtcAtSlot(int slot, bool& pending) {
    int stored = min(max(dtcTotal, 0), MAX_DTCS);
    pending = slot >= stored;
    return pending ? pendingCodes[slot - stored] : dtcCodes[slot];
}

// List slot under a canvas point, or -1
inline int dtcSlotAtPoint(int cx, int cy) {
    if (cy < DTC_LIST_Y - 4 || cx < DTC_X + 6) return -1;
    int col = (cx - DTC_X - 6) / DTC_COL_W, row = (cy - DTC_LIST_Y + 4) / DTC_ROW_H;
    if (col > 2 || row > 4) return -1;
    int slot = row * 3 + col;
    return slot < dtcShownCount() ? slot : -1;
}

// Draws text word-wrapped to width w; returns the y below the last line
inline int drawWrapped(const String& text, int x, int y, int w, int font, int lineH) {
    String line = "";
    int start = 0;
    while (start <= (int)text.length()) {
        int sp = text.indexOf(' ', start);
        if (sp < 0) sp = text.length();
        String word = text.substring(start, sp);
        String trial = line.length() ? line + " " + word : word;
        if (line.length() && canvas.textWidth(trial, font) > w) {
            canvas.drawString(line, x, y, font);
            y += lineH;
            line = word;
        } else {
            line = trial;
        }
        start = sp + 1;
    }
    if (line.length()) { canvas.drawString(line, x, y, font); y += lineH; }
    return y;
}

// Description box over the codes page for the tapped code
inline void drawDtcDetail() {
    bool pending;
    String code = dtcAtSlot(dtcDetail, pending);
    uint32_t color = pending ? TFT_YELLOW : TFT_RED;
    const int BX = 14, BY = 14, BW = 292, BH = 170;
    canvas.fillRoundRect(BX, BY, BW, BH, 8, TFT_BLACK);
    canvas.drawRoundRect(BX, BY, BW, BH, 8, color);
    canvas.drawRoundRect(BX + 1, BY + 1, BW - 2, BH - 2, 7, color);

    canvas.setTextSize(1);
    canvas.setTextColor(color, TFT_BLACK);
    canvas.drawString(code, BX + 12, BY + 10, 4);
    canvas.drawRightString(pending ? "PENDING" : "STORED", BX + BW - 12, BY + 14, 2);

    String text = describeDTC(code);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = drawWrapped(text.length() ? text : String("No description stored for this code."),
                        BX + 12, BY + 42, BW - 24, 2, 18);
    canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    y = drawWrapped(dtcKind(code), BX + 12, y + 4, BW - 24, 2, 18);
    if (pending) drawWrapped("Pending: seen once, not confirmed yet (no check-engine light).",
                             BX + 12, y + 4, BW - 24, 2, 18);

    canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    canvas.drawCentreString("TAP TO CLOSE", BX + BW / 2, BY + BH - 20, 2);
}

inline void drawDtcPageToCanvas() {
    const int X = DTC_X, Y = DTC_Y, W = DTC_W, H = DTC_H;
    const int COL_W = DTC_COL_W, ROW_H = DTC_ROW_H, LIST_Y = DTC_LIST_Y;

    int stored  = max(dtcTotal, 0);
    int pending = max(pendingTotal, 0);
    uint32_t color = stored > 0 ? TFT_RED : pending > 0 ? TFT_YELLOW
                   : (dtcTotal == 0 ? TFT_GREEN : TFT_DARKGREY);
    canvas.drawRect(X, Y, W, H, color);
    canvas.drawRect(X + 1, Y + 1, W - 2, H - 2, color);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.drawString("ERROR CODES", X + 10, Y + 8, 2);
    if (dtcTotal >= 0) {
        // "2 STORED  1 PENDING", each count in its list colour
        String pendingText = String(pending) + " PENDING";
        canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
        canvas.drawRightString(pendingText, X + W - 10, Y + 8, 2);
        canvas.setTextColor(stored > 0 ? TFT_RED : TFT_WHITE, TFT_BLACK);
        canvas.drawRightString(String(stored) + " STORED  ", X + W - 10 - canvas.textWidth(pendingText, 2), Y + 8, 2);
    }
    canvas.drawFastHLine(X + 10, Y + 28, W - 20, TFT_DARKGREY);

    if (stored == 0 && pending == 0) {
        canvas.setTextColor(color, TFT_BLACK);
        canvas.drawCentreString(dtcTotal == 0 ? "NO ERROR CODES" : "SCANNING...",
                                X + W / 2, Y + 90, 4);
        return;
    }

    int slot = 0;
    canvas.setTextColor(TFT_RED, TFT_BLACK);
    for (int i = 0; i < min(stored, MAX_DTCS) && slot < MAX_DTCS; i++, slot++) {
        canvas.drawString(dtcCodes[i], X + 10 + (slot % 3) * COL_W, LIST_Y + (slot / 3) * ROW_H, 4);
    }
    canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    for (int i = 0; i < min(pending, MAX_DTCS) && slot < MAX_DTCS; i++, slot++) {
        canvas.drawString(pendingCodes[i], X + 10 + (slot % 3) * COL_W, LIST_Y + (slot / 3) * ROW_H, 4);
    }
    if (slot <= 12) {   // room below the list for a hint
        canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
        canvas.drawCentreString("TAP A CODE FOR ITS MEANING", X + W / 2, Y + H - 18, 2);
    }
}

const int SNIFF_FOOTER_Y = 178;   // footer button row (same layout as the CAN repo's sniffer page)
const int SNIFF_BTN_Y    = SNIFF_FOOTER_Y + 4, SNIFF_BTN_H = 30, SNIFF_BTN_W = 100;
const int SNIFF_BTN_X[3] = {4, 110, 216};

inline void drawSnifferButton(int i, const String& label, uint32_t color, bool filled) {
    int x = SNIFF_BTN_X[i];
    if (filled) canvas.fillRoundRect(x, SNIFF_BTN_Y, SNIFF_BTN_W, SNIFF_BTN_H, 6, color);
    else        canvas.drawRoundRect(x, SNIFF_BTN_Y, SNIFF_BTN_W, SNIFF_BTN_H, 6, color);
    canvas.setTextColor(filled ? TFT_WHITE : color, filled ? color : TFT_BLACK);
    canvas.drawCentreString(label, x + SNIFF_BTN_W / 2, SNIFF_BTN_Y + 7, 2);
}

// ---- Settings page ---------------------------------------------------------------------
// Sections of rows (MORE moves to the next section). A row is a label and either one
// button that toggles (ON/OFF, F/C),  -  value  +  buttons, or (change == nullptr) just
// information. Footer: MORE, DEFAULTS
// (tap twice within 3 s), NEXT >. Changes are saved to flash by settingsTick().
struct SettingRow {
    const char* label;
    String (*text)();            // the value as shown
    void   (*change)(int dir);   // -1 / +1 from the - / + buttons, 0 for a toggle tap
    bool   toggle;
};

inline String fmtTemp(float f) {
    return settings.celsius ? String((f - 32) * 5 / 9, 0) + " C" : String(f, 0) + " F";
}
inline String fmtMinutes(int m) { return m == 0 ? String("never") : String(m) + " min"; }

// Moves v to the next / previous entry of a list of choices
inline int stepChoice(int v, const int* list, int n, int dir) {
    int i = 0;
    while (i < n - 1 && list[i] < v) i++;
    return list[constrain(i + dir, 0, n - 1)];
}
inline float stepValue(float v, float step, float lo, float hi, int dir) {
    v = roundf((v + dir * step) / step) * step;
    return constrain(v, lo, hi);
}

static const int SCREEN_OFF_CHOICES[] = {0, 1, 2, 5, 10, 30};
static const int SLEEP_CHOICES[]      = {0, 5, 10, 15, 30, 60};

static const SettingRow SETTINGS_DISPLAY[] = {
    {"Auto brightness",  []() -> String { return String(settings.autoBrightness ? "ON" : "OFF"); },
                         [](int) { settings.autoBrightness = !settings.autoBrightness; }, true},
    {"Night brightness", []() -> String { return String(settings.nightBrightnessPct) + "%"; },
                         [](int d) { settings.nightBrightnessPct = constrain(settings.nightBrightnessPct + 5 * d, 5, 100); }, false},
    {"Temperature",      []() -> String { return String(settings.celsius ? "C" : "F"); },
                         [](int) { settings.celsius = !settings.celsius; }, true},
    {"Speed",            []() -> String { return String(settings.kmh ? "KM/H" : "MPH"); },
                         [](int) { settings.kmh = !settings.kmh; }, true},
};
static const SettingRow SETTINGS_ALARMS[] = {
    {"Gauge alarms",     []() -> String { return String(settings.alarmsOn ? "ON" : "OFF"); },
                         [](int) { settings.alarmsOn = !settings.alarmsOn; }, true},
    {"FICM low",         []() -> String { return String(settings.ficmLowV, 1) + " V"; },
                         [](int d) { settings.ficmLowV = stepValue(settings.ficmLowV, 0.5, 40, 50, d); }, false},
    {"Trans temp high",  []() -> String { return fmtTemp(settings.tftHighF); },
                         [](int d) { settings.tftHighF = stepValue(settings.tftHighF, 5, 180, 260, d); }, false},
    {"Coolant high",     []() -> String { return fmtTemp(settings.ectHighF); },
                         [](int d) { settings.ectHighF = stepValue(settings.ectHighF, 5, 200, 260, d); }, false},
};
static const SettingRow SETTINGS_VOLTAGE[] = {
    {"Volts low",        []() -> String { return String(settings.voltsLowV, 1) + " V"; },
                         [](int d) { settings.voltsLowV = stepValue(settings.voltsLowV, 0.1, 10.5, 13.0, d); }, false},
    {"Volts high",       []() -> String { return String(settings.voltsHighV, 1) + " V"; },
                         [](int d) { settings.voltsHighV = stepValue(settings.voltsHighV, 0.1, 14.0, 16.0, d); }, false},
};
static const SettingRow SETTINGS_POWER[] = {
    {"Screen off after", []() -> String { return fmtMinutes(settings.screenOffMin); },
                         [](int d) { settings.screenOffMin = stepChoice(settings.screenOffMin, SCREEN_OFF_CHOICES, 6, d); }, false},
    {"Sleep after",      []() -> String { return fmtMinutes(settings.sleepMin); },
                         [](int d) { settings.sleepMin = stepChoice(settings.sleepMin, SLEEP_CHOICES, 6, d); }, false},
};

static const SettingRow SETTINGS_WIFI[] = {
    {"Phone hotspot",    []() -> String { return String(!settings.wifiOn ? "OFF" : wifiStartFailed ? "FAILED" : "ON"); },
                         [](int) { settings.wifiOn = !settings.wifiOn; }, true},
    {"Network",          []() -> String { return String(WIFI_AP_SSID); }, nullptr, false},
    {"Password",         []() -> String { return String(WIFI_AP_PASSWORD); }, nullptr, false},
    {"Then open",        []() -> String { return String("192.168.4.1"); }, nullptr, false},
};

struct SettingSection { const char* title; const SettingRow* rows; int count; };
static const SettingSection SETTING_SECTIONS[] = {
    {"DISPLAY", SETTINGS_DISPLAY, sizeof(SETTINGS_DISPLAY) / sizeof(SettingRow)},
    {"ALARMS",  SETTINGS_ALARMS,  sizeof(SETTINGS_ALARMS) / sizeof(SettingRow)},
    {"VOLTAGE", SETTINGS_VOLTAGE, sizeof(SETTINGS_VOLTAGE) / sizeof(SettingRow)},
    {"POWER",   SETTINGS_POWER,   sizeof(SETTINGS_POWER) / sizeof(SettingRow)},
    {"WIFI",    SETTINGS_WIFI,    sizeof(SETTINGS_WIFI) / sizeof(SettingRow)},
};
static const int SETTING_SECTION_COUNT = sizeof(SETTING_SECTIONS) / sizeof(SettingSection);

static int settingsSection = 0;
static unsigned long defaultsArmedAt = 0;   // first DEFAULTS tap; a second within 3 s resets

const int SET_ROW_Y0 = 28, SET_ROW_H = 30;   // at most 4 rows a section, tall enough for a fingertip
const int SET_MINUS_X = 182, SET_VALUE_X = 218, SET_PLUS_X = 282, SET_BTN_W = 34, SET_TOGGLE_W = 98;

inline void drawSettingsPageToCanvas() {
    const SettingSection& sec = SETTING_SECTIONS[settingsSection];
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    canvas.drawString("SETTINGS", 6, 4, 2);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.drawRightString(String(sec.title) + "  " + String(settingsSection + 1) + "/" + String(SETTING_SECTION_COUNT),
                           314, 4, 2);
    canvas.drawFastHLine(4, 22, 312, TFT_DARKGREY);

    for (int r = 0; r < sec.count; r++) {
        const SettingRow& row = sec.rows[r];
        int y = SET_ROW_Y0 + r * SET_ROW_H;
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        canvas.drawString(row.label, 6, y + 7, 2);
        String value = row.text();
        if (!row.change) {   // information only
            canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
            canvas.drawRightString(value, 314, y + 7, 2);
        } else if (row.toggle) {
            uint32_t c = (value == "OFF") ? TFT_DARKGREY : (value == "FAILED") ? TFT_RED : TFT_GREEN;
            canvas.drawRoundRect(SET_VALUE_X, y + 2, SET_TOGGLE_W, SET_ROW_H - 4, 5, c);
            canvas.setTextColor(c, TFT_BLACK);
            canvas.drawCentreString(value, SET_VALUE_X + SET_TOGGLE_W / 2, y + 7, 2);
        } else {
            canvas.drawRoundRect(SET_MINUS_X, y + 2, SET_BTN_W, SET_ROW_H - 4, 5, TFT_CYAN);
            canvas.drawRoundRect(SET_PLUS_X, y + 2, SET_BTN_W, SET_ROW_H - 4, 5, TFT_CYAN);
            canvas.setTextColor(TFT_CYAN, TFT_BLACK);
            canvas.drawCentreString("-", SET_MINUS_X + SET_BTN_W / 2, y + 7, 2);
            canvas.drawCentreString("+", SET_PLUS_X + SET_BTN_W / 2, y + 7, 2);
            canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
            canvas.drawCentreString(value, (SET_MINUS_X + SET_BTN_W + SET_PLUS_X) / 2, y + 7, 2);
        }
    }

    drawSnifferButton(0, "MORE", TFT_WHITE, false);
    bool armed = defaultsArmedAt && millis() - defaultsArmedAt < 3000;
    drawSnifferButton(1, armed ? "SURE?" : "DEFAULTS", armed ? TFT_RED : TFT_ORANGE, armed);
    drawSnifferButton(2, "NEXT >", TFT_WHITE, false);
}

// ---- LOG page (drive log) -------------------------------------------------------------
// REC / STOP the drive log, MARK a moment, NEXT page; shows the file, time, rows and size.
inline String fmtDuration(unsigned long ms) {
    unsigned long s = ms / 1000;
    char b[16];
    snprintf(b, sizeof(b), "%lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
    return String(b);
}

inline void drawLogPageToCanvas() {
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    canvas.drawString("DRIVE LOG", 6, 4, 2);
    canvas.setTextColor(driveLogging ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
    canvas.drawRightString(driveLogging ? "REC" : "STOPPED", 314, 4, 2);
    canvas.drawFastHLine(4, 22, 312, TFT_DARKGREY);

    if (driveLogging) {
        canvas.setTextColor(TFT_RED, TFT_BLACK);
        canvas.drawString(driveLogName, 10, 34, 4);
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        canvas.drawString("Time    " + fmtDuration(millis() - driveStartMs), 10, 72, 2);
        canvas.drawString("Rows    " + String(driveRows) + "      Marks  " + String(driveMarks), 10, 94, 2);
        canvas.drawString("Size    " + String((uint32_t)(driveFile.size() / 1024)) + " KB", 10, 116, 2);
        if (driveLogPaused()) {
            canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
            canvas.drawString("Paused: truck not answering", 10, 146, 2);
        }
    } else {
        canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        canvas.drawString("REC writes the gauges once a second to a", 10, 32, 2);
        canvas.drawString("CSV file on the SD card, for a spreadsheet.", 10, 50, 2);
        canvas.drawString("Only while the truck answers (no demo data).", 10, 68, 2);
        canvas.drawString("MARK notes a moment, e.g. start of a hill.", 10, 86, 2);
        if (driveLogError) {
            canvas.setTextColor(TFT_RED, TFT_BLACK);
            canvas.drawString("NO SD CARD: insert one and tap REC", 10, 120, 2);
        } else if (driveLogName.length()) {
            canvas.setTextColor(TFT_WHITE, TFT_BLACK);
            canvas.drawString("Last: " + driveLogName + ", " + String(driveRows) + " rows", 10, 120, 2);
        }
        canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
        canvas.drawString("Get the files from the card, or the phone's Logs tab.", 10, 150, 2);
    }

    if (driveLogging)       drawSnifferButton(0, "STOP", TFT_RED, true);
    else if (driveLogError) drawSnifferButton(0, "NO SD", TFT_DARKGREY, false);   // tap to retry
    else                    drawSnifferButton(0, "REC", TFT_RED, false);
    drawSnifferButton(1, driveMarks && driveLogging ? "MARK " + String(driveMarks) : String("MARK"),
                      driveLogging ? TFT_YELLOW : TFT_DARKGREY, false);
    drawSnifferButton(2, "NEXT >", TFT_WHITE, false);
}

// Builds one page (2x2 card grid or the DTC list, + page dots). Works the same connected or not.
inline void buildPage(int pageNum) {
    canvas.fillSprite(TFT_BLACK);

    if (pageNum == PAGE_DTC) {
        drawDtcPageToCanvas();
    } else if (pageNum == PAGE_LOG) {
        drawLogPageToCanvas();
    } else if (pageNum == PAGE_SETTINGS) {
        drawSettingsPageToCanvas();
    } else {
        for (int slot = 0; slot < METRICS_PER_PAGE; slot++) {
            int idx = pageNum * METRICS_PER_PAGE + slot;
            if (idx >= METRIC_COUNT) break;
            drawCardToCanvas(COL_X[slot % 2], ROW_Y[slot / 2],
                             metrics[idx].label, shownValue(idx),
                             shownUnit(idx), metrics[idx].color, metricAlarmState(idx) != 0);
        }
        if (pageNum == PAGE_ABS) drawAbsStatusCard(COL_X[1], ROW_Y[1]);
    }

    if (pageNum == PAGE_SETTINGS || pageNum == PAGE_LOG) return;   // footer buttons instead of page dots
    int pipStart = 160 - ((TOTAL_PAGES - 1) * 10);
    for (int i = 0; i < TOTAL_PAGES; i++) {
        uint32_t pipColor = (i == pageNum) ? TFT_WHITE : TFT_DARKGREY;
        canvas.fillCircle(pipStart + (i * 20), PIP_Y, 4, pipColor);
    }
}

// ---- Full-screen gauge --------------------------------------------------------
// One metric filling the canvas: label on top, value as large as fits, unit below.
inline void buildFullScreen(int idx) {
    const Metric& m = metrics[idx];
    // In alarm: red frame, and the whole screen flashes red
    bool alarm = metricAlarmState(idx) != 0;
    uint32_t frame = alarm ? TFT_RED : m.color;
    uint32_t bg = (alarm && alarmBlinkOn()) ? TFT_RED : TFT_BLACK;
    uint32_t valueColor = (bg == TFT_RED) ? TFT_WHITE : frame;
    canvas.fillSprite(bg);
    canvas.drawRect(4, 4, 312, CANVAS_H - 8, frame);
    canvas.drawRect(5, 5, 310, CANVAS_H - 10, frame);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, bg);
    canvas.drawCentreString(m.label, 160, 14, 4);

    // Largest font-4 scale (4 down to 1) that fits the width
    int size = 4;
    canvas.setTextSize(size);
    String value = shownValue(idx);
    while (size > 1 && canvas.textWidth(value, 4) > 292) canvas.setTextSize(--size);
    int valH = canvas.fontHeight(4);
    canvas.setTextColor(valueColor, bg);
    canvas.drawCentreString(value, 160, 46 + (116 - valH) / 2, 4);

    canvas.setTextSize(1);
    if (m.unit[0]) {
        canvas.setTextColor(TFT_WHITE, bg);
        canvas.drawCentreString(shownUnit(idx), 160, 160, 4);
    }
    canvas.setTextColor(bg == TFT_RED ? TFT_WHITE : TFT_DARKGREY, bg);
    canvas.drawCentreString("TAP TO RETURN", 160, CANVAS_H - 24, 2);
}

// ---- Tap menu --------------------------------------------------------------------
// Tapping a gauge highlights it and shows two buttons over the page.
// Button rectangles are in canvas coordinates.
const int MENU_BTN_Y = 62, MENU_BTN_H = 70, MENU_BTN_W = 136;
const int MENU_NEXT_X = 16, MENU_FULL_X = 168;

inline bool inButton(int cx, int cy, int bx) {
    return cx >= bx && cx < bx + MENU_BTN_W && cy >= MENU_BTN_Y && cy < MENU_BTN_Y + MENU_BTN_H;
}

inline void drawMenuButton(int bx, const char* line1, const char* line2, uint32_t color) {
    canvas.fillRoundRect(bx, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 8, TFT_BLACK);
    canvas.drawRoundRect(bx, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 8, color);
    canvas.drawRoundRect(bx + 1, MENU_BTN_Y + 1, MENU_BTN_W - 2, MENU_BTN_H - 2, 7, color);
    canvas.setTextSize(1);
    canvas.setTextColor(color, TFT_BLACK);
    canvas.drawCentreString(line1, bx + MENU_BTN_W / 2, MENU_BTN_Y + 10, 4);
    canvas.drawCentreString(line2, bx + MENU_BTN_W / 2, MENU_BTN_Y + 38, 4);
}

inline void drawTapMenu(int idx) {
    int slot = idx % METRICS_PER_PAGE;
    int x = COL_X[slot % 2], y = ROW_Y[slot / 2];
    canvas.drawRect(x - 2, y - 2, CARD_W + 4, CARD_H + 4, TFT_WHITE);   // highlight the tapped gauge
    canvas.drawRect(x - 3, y - 3, CARD_W + 6, CARD_H + 6, TFT_WHITE);

    drawMenuButton(MENU_NEXT_X, "NEXT", "PAGE", TFT_WHITE);
    drawMenuButton(MENU_FULL_X, "FULL", "SCREEN", metrics[idx].color);
}

// The canvas holds CANVAS_STRIP_H rows. draw() runs once per strip with the drawing
// shifted up by the strip's position (a viewport with a negative origin clips the rest),
// and each strip is pushed below the one before. With one strip this is a plain push.
const int CANVAS_STRIP_H = CANVAS_H / CANVAS_STRIPS;

template <typename Draw>
inline void pushCanvas(int x, Draw draw) {
    for (int s = 0; s < CANVAS_STRIPS; s++) {
        int y0 = s * CANVAS_STRIP_H;
        // The viewport must end at the strip's bottom edge: fillSprite() clears _yHeight
        // rows with memset when the origin is 0, so a taller viewport would overrun the buffer
        if (CANVAS_STRIPS > 1) canvas.setViewport(0, -y0, 320, y0 + CANVAS_STRIP_H);
        draw();
        if (CANVAS_STRIPS > 1) canvas.resetViewport();
        canvas.pushSprite(x, CANVAS_Y + y0);
    }
}

// Draws whatever should be on screen now (full-screen gauge, or page + tap menu)
inline void drawCurrentScreen() {
    if (dtcDetail >= dtcShownCount()) dtcDetail = -1;   // the list changed under it
    pushCanvas(0, [] {
        if (fullScreenMetric >= 0) {
            buildFullScreen(fullScreenMetric);
        } else {
            buildPage(currentPage);
            if (menuMetric >= 0) drawTapMenu(menuMetric);
            if (currentPage == PAGE_DTC && dtcDetail >= 0) drawDtcDetail();
        }
    });
}

// Animates horizontal page swiping transitions (works offline too)
inline void scrollToNextPage() {
    int nextPage = (currentPage + 1) % TOTAL_PAGES;
    logDebug("UI_NAV", "Triggering scroll: Swiping from Page " + String(currentPage) + " to Page " + String(nextPage));

    for (int offset = 0; offset <= 320; offset += 32) {
        pushCanvas(offset, [&] { buildPage(currentPage); });
        pushCanvas(offset - 320, [&] { buildPage(nextPage); });
        delay(5);
    }

    currentPage = nextPage;
    dtcDetail = -1;
}

// Which metric card is under a canvas point on the current page (-1 = none:
// DTC page, ABS status card, or an empty slot)
inline int metricAtPoint(int cx, int cy) {
    if (currentPage >= METRIC_PAGES) return -1;
    int slot = (cy < ROW_Y[1] ? 0 : 2) + (cx < COL_X[1] ? 0 : 1);
    int idx = currentPage * METRICS_PER_PAGE + slot;
    return (idx < METRIC_COUNT) ? idx : -1;
}

// Taps on the LOG page: only the footer buttons do something
inline void driveLogTap(int cx, int cy) {
    if (cy < SNIFF_FOOTER_Y) return;
    if (cx < SNIFF_BTN_X[1])      { if (driveLogging) driveLogStop(); else driveLogStart(); }
    else if (cx < SNIFF_BTN_X[2]) driveLogMark();
    else                          scrollToNextPage();
}

// Taps on the Settings page (see drawSettingsPageToCanvas)
inline void settingsTap(int cx, int cy) {
    const SettingSection& sec = SETTING_SECTIONS[settingsSection];
    if (cy >= SNIFF_FOOTER_Y) {
        if (cx < SNIFF_BTN_X[1]) {
            settingsSection = (settingsSection + 1) % SETTING_SECTION_COUNT;
        } else if (cx < SNIFF_BTN_X[2]) {
            if (defaultsArmedAt && millis() - defaultsArmedAt < 3000) {
                settingsDefaults();
                settingsChanged();
                defaultsArmedAt = 0;
                logDebug("SETTINGS", "Back to defaults");
            } else {
                defaultsArmedAt = millis();   // tap again to confirm
            }
        } else {
            scrollToNextPage();
        }
        return;
    }
    int r = (cy - SET_ROW_Y0) / SET_ROW_H;
    if (cy < SET_ROW_Y0 || r >= sec.count) return;
    const SettingRow& row = sec.rows[r];
    if (!row.change) return;   // information row
    int dir;
    if (row.toggle)                         dir = 0;
    else if (cx < SET_VALUE_X + 10)         dir = -1;   // the - button, with some slack
    else if (cx >= SET_PLUS_X - 10)         dir = 1;
    else return;
    if (cx < SET_MINUS_X - 20) return;      // taps on the label do nothing
    row.change(dir);
    settingsChanged();
    logDebug("SETTINGS", String(row.label) + " -> " + row.text());
}

// Handles one tap at screen coordinates (sx, sy).
//   Full screen     -> back to the page
//   Menu showing    -> NEXT PAGE / FULL SCREEN, or tap elsewhere to close it
//   Tap on a gauge  -> show the menu for that gauge
//   Codes page      -> tap a code for its description, tap again to close it
//   Tap elsewhere   -> next page (DTC page outside the codes, ABS status card)
inline void handleTap(int sx, int sy) {
    int cx = sx, cy = max(0, sy - CANVAS_Y);
    int dtcSlot = -1;

    if (fullScreenMetric >= 0) {
        logDebug("UI_NAV", "Leaving full screen");
        fullScreenMetric = -1;
    } else if (menuMetric >= 0) {
        int idx = menuMetric;
        menuMetric = -1;
        if (inButton(cx, cy, MENU_NEXT_X)) {
            scrollToNextPage();
        } else if (inButton(cx, cy, MENU_FULL_X)) {
            logDebug("UI_NAV", "Full screen: " + String(metrics[idx].label));
            fullScreenMetric = idx;
        }
    } else if (currentPage == PAGE_LOG) {
        driveLogTap(cx, cy);
    } else if (currentPage == PAGE_SETTINGS) {
        settingsTap(cx, cy);
    } else if (dtcDetail >= 0) {
        dtcDetail = -1;   // close the description box
    } else if (currentPage == PAGE_DTC && (dtcSlot = dtcSlotAtPoint(cx, cy)) >= 0) {
        dtcDetail = dtcSlot;
        bool pending;
        logDebug("UI_NAV", "Code details: " + dtcAtSlot(dtcSlot, pending));
    } else {
        int idx = metricAtPoint(cx, cy);
        if (idx < 0) {
            scrollToNextPage();
        } else {
            menuMetric = idx;
            menuShownAt = millis();
        }
    }
    lastBusQuery = 0;   // refresh straight away for whatever is now on screen
    drawCurrentScreen();
}

// Renders a high-contrast welcome screen before wireless networks engage
inline void drawSplashScreen() {
    tft.fillScreen(TFT_BLACK);

    tft.drawRect(5, 5, 310, 230, TFT_ORANGE);
    tft.drawRect(7, 7, 306, 226, TFT_DARKGREY);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);                       // font 4 at x1 (x2 would overflow the screen)
    tft.drawCentreString("FORD EXCURSION", 160, 50, 4);

    tft.setTextColor(TFT_ORANGE);
    tft.setTextSize(1);
    tft.drawCentreString("6.0L POWERSTROKE DIAGNOSTICS", 160, 110, 2);

    tft.fillRect(40, 150, 240, 4, TFT_DARKGREY);
    tft.fillRect(40, 150, 80, 4, TFT_ORANGE);

    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawString("Loading graphics subsystems...", 45, 170, 2);
}

#endif