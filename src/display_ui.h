#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include "config.h"

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
// alarmLow / alarmHigh: outside these the card flashes red (NO_ALARM = no limit).
struct Metric {
    const char* label;
    String*     value;
    const char* unit;
    uint32_t    color;
    void      (*poll)();
    float       alarmLow;
    float       alarmHigh;
};

static const Metric metrics[] = {
    // Page 0
    {"TRANSFER CASE",  &valTCase,  "",    TFT_ORANGE,  pollTCase,  NO_ALARM, NO_ALARM},
    {"TURBO BOOST",    &valBoost,  "PSI", TFT_CYAN,    pollBoost,  NO_ALARM, NO_ALARM},
    {"VEHICLE SPEED",  &valSpeed,  "MPH", TFT_SKYBLUE, pollSpeed,  NO_ALARM, NO_ALARM},
    {"TRANS TEMP",     &valTFT,    "F",   TFT_MAGENTA, pollTFT,    NO_ALARM, ALARM_TFT_HIGH_F},
    // Page 1
    {"FICM MAIN PWR",  &valFICM,   "V",   TFT_GREEN,   pollFICM,   ALARM_FICM_LOW_V, NO_ALARM},
    {"COOLANT TEMP",   &valECT,    "F",   TFT_RED,     pollECT,    NO_ALARM, ALARM_ECT_HIGH_F},
    {"SYSTEM VOLTS",   &valVolts,  "V",   TFT_YELLOW,  pollVolts,  ALARM_VOLTS_LOW_V, ALARM_VOLTS_HIGH_V},
    {"ENGINE RPM",     &valRPM,    "",    TFT_WHITE,   pollRPM,    NO_ALARM, NO_ALARM},
    // Page 2 (ABS) - fronts on top, rear axle bottom-left;
    // the 4th slot is the ABS warning card (drawn by drawAbsStatusCard, not this table)
    {"LF WHEEL SPD",   &valWheelLF,   "MPH", TFT_SKYBLUE, pollWheelLF,   NO_ALARM, NO_ALARM},
    {"RF WHEEL SPD",   &valWheelRF,   "MPH", TFT_SKYBLUE, pollWheelRF,   NO_ALARM, NO_ALARM},
    {"REAR AXLE SPD",  &valWheelRear, "MPH", TFT_CYAN,    pollWheelRear, NO_ALARM, NO_ALARM},
};
static const int METRIC_COUNT = sizeof(metrics) / sizeof(metrics[0]);
static_assert(METRIC_COUNT <= METRIC_PAGES * METRICS_PER_PAGE,
              "Metric table has more entries than METRIC_PAGES * METRICS_PER_PAGE");
static_assert(METRIC_COUNT <= PAGE_ABS * METRICS_PER_PAGE + 3,
              "The ABS page's 4th slot is reserved for the ABS status card");
static_assert(PAGE_DTC == METRIC_PAGES && TOTAL_PAGES == METRIC_PAGES + 1,
              "The DTC page must come right after the metric pages");

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
inline bool metricHasAlarm(int idx) {
    return !isnan(metrics[idx].alarmLow) || !isnan(metrics[idx].alarmHigh);
}

// -1 = below alarmLow, 1 = above alarmHigh, 0 = in range / no live value
inline int metricAlarmState(int idx) {
    const Metric& m = metrics[idx];
    const String& v = *m.value;
    if (!isConnected || !metricHasAlarm(idx) || v.length() == 0) return 0;
    if (!isDigit(v[0]) && !(v[0] == '-' && v.length() > 1 && isDigit(v[1]))) return 0;
    float x = v.toFloat();
    if (!isnan(m.alarmLow) && x < m.alarmLow) return -1;
    if (!isnan(m.alarmHigh) && x > m.alarmHigh) return 1;
    return 0;
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
                              " (limit " + String(state < 0 ? m.alarmLow : m.alarmHigh, 1) + ")");
        } else {
            logDebug("ALARM", String(m.label) + " back in range: " + *m.value + " " + m.unit);
        }
    }
}

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
                   *m.value + " " + m.unit;
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
inline void drawDtcPageToCanvas() {
    const int X = 4, Y = 4, W = 312, H = 188;
    const int COL_W = 100, ROW_H = 29, LIST_Y = Y + 38;

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
}

// Builds one page (2x2 card grid or the DTC list, + page dots). Works the same connected or not.
inline void buildPage(int pageNum) {
    canvas.fillSprite(TFT_BLACK);

    if (pageNum == PAGE_DTC) {
        drawDtcPageToCanvas();
    } else {
        for (int slot = 0; slot < METRICS_PER_PAGE; slot++) {
            int idx = pageNum * METRICS_PER_PAGE + slot;
            if (idx >= METRIC_COUNT) break;
            drawCardToCanvas(COL_X[slot % 2], ROW_Y[slot / 2],
                             metrics[idx].label, *metrics[idx].value,
                             metrics[idx].unit, metrics[idx].color, metricAlarmState(idx) != 0);
        }
        if (pageNum == PAGE_ABS) drawAbsStatusCard(COL_X[1], ROW_Y[1]);
    }

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
    while (size > 1 && canvas.textWidth(*m.value, 4) > 292) canvas.setTextSize(--size);
    int valH = canvas.fontHeight(4);
    canvas.setTextColor(valueColor, bg);
    canvas.drawCentreString(*m.value, 160, 46 + (116 - valH) / 2, 4);

    canvas.setTextSize(1);
    if (m.unit[0]) {
        canvas.setTextColor(TFT_WHITE, bg);
        canvas.drawCentreString(m.unit, 160, 160, 4);
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

// Draws whatever should be on screen now (full-screen gauge, or page + tap menu)
inline void drawCurrentScreen() {
    if (fullScreenMetric >= 0) {
        buildFullScreen(fullScreenMetric);
    } else {
        buildPage(currentPage);
        if (menuMetric >= 0) drawTapMenu(menuMetric);
    }
    canvas.pushSprite(0, CANVAS_Y);
}

// Animates horizontal page swiping transitions (works offline too)
inline void scrollToNextPage() {
    int nextPage = (currentPage + 1) % TOTAL_PAGES;
    logDebug("UI_NAV", "Triggering scroll: Swiping from Page " + String(currentPage) + " to Page " + String(nextPage));

    for (int offset = 0; offset <= 320; offset += 32) {
        buildPage(currentPage);
        canvas.pushSprite(offset, CANVAS_Y);

        buildPage(nextPage);
        canvas.pushSprite(offset - 320, CANVAS_Y);
        delay(5);
    }

    currentPage = nextPage;
}

// Which metric card is under a canvas point on the current page (-1 = none:
// DTC page, ABS status card, or an empty slot)
inline int metricAtPoint(int cx, int cy) {
    if (currentPage >= METRIC_PAGES) return -1;
    int slot = (cy < ROW_Y[1] ? 0 : 2) + (cx < COL_X[1] ? 0 : 1);
    int idx = currentPage * METRICS_PER_PAGE + slot;
    return (idx < METRIC_COUNT) ? idx : -1;
}

// Handles one tap at screen coordinates (sx, sy).
//   Full screen     -> back to the page
//   Menu showing    -> NEXT PAGE / FULL SCREEN, or tap elsewhere to close it
//   Tap on a gauge  -> show the menu for that gauge
//   Tap elsewhere   -> next page (DTC page, ABS status card)
inline void handleTap(int sx, int sy) {
    int cx = sx, cy = max(0, sy - CANVAS_Y);

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