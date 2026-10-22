#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include "config.h"

// ---- Layout --------------------------------------------------------------
// Screen (rotation 1) is 320x240.
//   y 0..23    : status bar, drawn straight to the TFT (never scrolls)
//   y 30..189  : 320x160 canvas holding the 2x2 card grid + page dots
const int STATUS_BAR_H = 24;
const int CANVAS_Y     = 30;
const int CARD_W       = 150;
const int CARD_H       = 70;
const int COL_X[2]     = {6, 164};
const int ROW_Y[2]     = {4, 80};
const int PIP_Y        = 150;

// ---- Metric table ----------------------------------------------------------
// Pages are filled in order, METRICS_PER_PAGE at a time.
// To add/reorder a value, edit this table (and TOTAL_PAGES in config.h).
struct Metric {
    const char* label;
    String*     value;
    uint32_t    color;
};

static const Metric metrics[] = {
    // Page 0
    {"TRANSFER CASE",     &valTCase,  TFT_ORANGE},
    {"TURBO BOOST",       &valBoost,  TFT_CYAN},
    {"FICM MAIN PWR",     &valFICM,   TFT_GREEN},
    {"COOLANT TEMP",      &valECT,    TFT_RED},
    // Page 1
    {"TRANSMISSION TMP",  &valTFT,    TFT_MAGENTA},
    {"SYSTEM VOLTS",      &valVolts,  TFT_YELLOW},
    {"ENGINE RPM",        &valRPM,    TFT_WHITE},
    {"VEHICLE SPEED",     &valSpeed,  TFT_SKYBLUE},
};
static const int METRIC_COUNT = sizeof(metrics) / sizeof(metrics[0]);
static_assert(METRIC_COUNT <= TOTAL_PAGES * METRICS_PER_PAGE,
              "Metric table has more entries than TOTAL_PAGES * METRICS_PER_PAGE");

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
    tft.setTextSize(1);                 // splash screen leaves size at 1, but be explicit
    tft.setTextColor(fg, bg);
    tft.drawCentreString(text, 160, 4, 2);
}

// ---- Cards / pages -------------------------------------------------------------
inline void drawCardToCanvas(int x, int y, const char* label, const String& value, uint32_t color) {
    canvas.drawRect(x, y, CARD_W, CARD_H, color);

    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextSize(1);
    canvas.drawString(label, x + 8, y + 8);

    canvas.setTextColor(color, TFT_BLACK);
    canvas.setTextSize(2);
    canvas.drawString(value, x + 8, y + 34);
}

// Builds one page (2x2 grid + page dots). Works the same connected or not.
inline void buildPage(int pageNum) {
    canvas.fillSprite(TFT_BLACK);

    for (int slot = 0; slot < METRICS_PER_PAGE; slot++) {
        int idx = pageNum * METRICS_PER_PAGE + slot;
        if (idx >= METRIC_COUNT) break;
        drawCardToCanvas(COL_X[slot % 2], ROW_Y[slot / 2],
                         metrics[idx].label, *metrics[idx].value, metrics[idx].color);
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

    for (int offset = 0; offset <= 320; offset += 20) {
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
    tft.setTextSize(2);
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