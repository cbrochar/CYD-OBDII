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
struct Metric {
    const char* label;
    String*     value;
    const char* unit;
    uint32_t    color;
};

static const Metric metrics[] = {
    // Page 0
    {"TRANSFER CASE",  &valTCase,  "",    TFT_ORANGE},
    {"TURBO BOOST",    &valBoost,  "PSI", TFT_CYAN},
    {"VEHICLE SPEED",  &valSpeed,  "MPH", TFT_SKYBLUE},
    {"TRANS TEMP",     &valTFT,    "F",   TFT_MAGENTA},
    // Page 1
    {"FICM MAIN PWR",  &valFICM,   "V",   TFT_GREEN},
    {"COOLANT TEMP",   &valECT,    "F",   TFT_RED},
    {"SYSTEM VOLTS",   &valVolts,  "V",   TFT_YELLOW},
    {"ENGINE RPM",     &valRPM,    "",    TFT_WHITE},
    // Page 2 (ABS) - fronts on top, rear axle bottom-left;
    // the 4th slot is the ABS warning card (drawn by drawAbsStatusCard, not this table)
    {"LF WHEEL SPD",   &valWheelLF,   "MPH", TFT_SKYBLUE},
    {"RF WHEEL SPD",   &valWheelRF,   "MPH", TFT_SKYBLUE},
    {"REAR AXLE SPD",  &valWheelRear, "MPH", TFT_CYAN},
};
static const int METRIC_COUNT = sizeof(metrics) / sizeof(metrics[0]);
static_assert(METRIC_COUNT <= METRIC_PAGES * METRICS_PER_PAGE,
              "Metric table has more entries than METRIC_PAGES * METRICS_PER_PAGE");
static_assert(METRIC_COUNT <= PAGE_ABS * METRICS_PER_PAGE + 3,
              "The ABS page's 4th slot is reserved for the ABS status card");
static_assert(PAGE_DTC == METRIC_PAGES && TOTAL_PAGES == METRIC_PAGES + 1,
              "The DTC page must come right after the metric pages");

// ---- Status bar --------------------------------------------------------------
// Drawn directly on the TFT. Only repaints when its text changes.
inline void updateStatusBar(bool force = false) {
    static String lastText = "";
    String text;
    uint16_t bg, fg;

    if (isConnected) {
        if (hasDTCWarning) {
            text = "[!] STORED ERROR CODES DETECTED";
            bg = TFT_RED;      fg = TFT_WHITE;
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
inline void drawCardToCanvas(int x, int y, const char* label, const String& value,
                             const char* unit, uint32_t color) {
    const int PAD = 10;
    canvas.drawRect(x, y, CARD_W, CARD_H, color);
    canvas.drawRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, color);

    // Label
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
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

    canvas.setTextColor(color, TFT_BLACK);
    canvas.drawString(value, x + PAD, valY, 4);

    if (unit[0]) {
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
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

// DTC list page: one framed panel, header with count, codes in 3 columns x 5 rows.
// Red frame when codes are stored, green when clear.
inline void drawDtcPageToCanvas() {
    const int X = 4, Y = 4, W = 312, H = 188;
    const int COL_W = 100, ROW_H = 29, LIST_Y = Y + 38;

    uint32_t color = (dtcTotal > 0) ? TFT_RED : (dtcTotal == 0 ? TFT_GREEN : TFT_DARKGREY);
    canvas.drawRect(X, Y, W, H, color);
    canvas.drawRect(X + 1, Y + 1, W - 2, H - 2, color);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.drawString("STORED ERROR CODES", X + 10, Y + 8, 2);
    if (dtcTotal >= 0) canvas.drawRightString(String(dtcTotal) + " FOUND", X + W - 10, Y + 8, 2);
    canvas.drawFastHLine(X + 10, Y + 28, W - 20, TFT_DARKGREY);

    if (dtcTotal <= 0) {
        canvas.setTextColor(color, TFT_BLACK);
        canvas.drawCentreString(dtcTotal == 0 ? "NO STORED CODES" : "SCANNING...",
                                X + W / 2, Y + 90, 4);
        return;
    }

    canvas.setTextColor(TFT_RED, TFT_BLACK);
    int shown = min(dtcTotal, MAX_DTCS);
    for (int i = 0; i < shown; i++) {
        canvas.drawString(dtcCodes[i], X + 10 + (i % 3) * COL_W, LIST_Y + (i / 3) * ROW_H, 4);
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
                             metrics[idx].unit, metrics[idx].color);
        }
        if (pageNum == PAGE_ABS) drawAbsStatusCard(COL_X[1], ROW_Y[1]);
    }

    int pipStart = 160 - ((TOTAL_PAGES - 1) * 10);
    for (int i = 0; i < TOTAL_PAGES; i++) {
        uint32_t pipColor = (i == pageNum) ? TFT_WHITE : TFT_DARKGREY;
        canvas.fillCircle(pipStart + (i * 20), PIP_Y, 4, pipColor);
    }
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