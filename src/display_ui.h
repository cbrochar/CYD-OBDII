#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include "config.h"

// Renders individual parameter card grids onto the canvas frame
inline void drawCardToCanvas(int x, int y, String label, String value, uint32_t color) {
    canvas.drawRect(x, y, 140, 75, color);
    
    // FIX: Set a solid black text background so it contrasts against the canvas
    canvas.setTextColor(TFT_WHITE, TFT_BLACK); 
    canvas.setTextSize(1);
    canvas.drawString(label, x + 8, y + 8);
    
    // FIX: Set the metric value background to solid black as well
    canvas.setTextColor(color, TFT_BLACK);
    canvas.setTextSize(2);
    canvas.drawString(value, x + 8, y + 35);
}

// Renders a stylized vector image showing a disconnected status
inline void drawNoConnectionGraphic() {
    canvas.fillSprite(TFT_BLACK);
    
    canvas.fillRect(0, 0, 320, 24, TFT_YELLOW);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawCentreString("BLUETOOTH LINK OFFLINE", 160, 6, 2);

    int centerX = 160;
    int centerY = 90;

    canvas.drawTriangle(centerX, centerY - 40, centerX - 45, centerY + 30, centerX + 45, centerY + 30, TFT_YELLOW);
    canvas.drawTriangle(centerX, centerY - 39, centerX - 44, centerY + 29, centerX + 44, centerY + 29, TFT_YELLOW);
    
    canvas.fillRect(centerX - 3, centerY - 20, 6, 25, TFT_RED);
    canvas.fillCircle(centerX, centerY + 15, 3, TFT_RED);

    canvas.setTextColor(TFT_DARKGREY);
    canvas.setTextSize(1);
    canvas.drawCentreString("Searching for adapter 'OBDII'...", centerX, centerY + 45, 2);
}

// Builds out specific screen matrices depending on page parameters
inline void buildPage(int pageNum) {
    if (!isConnected) {
        drawNoConnectionGraphic();
        return;
    }

    canvas.fillSprite(TFT_BLACK);

    if (hasDTCWarning) {
        canvas.fillRect(0, 0, 320, 24, TFT_RED);
        // FIX: Text color is white, background color is red for contrast
        canvas.setTextColor(TFT_WHITE, TFT_RED);
        canvas.setTextSize(1);
        canvas.drawCentreString("[!] STORED ERROR CODES DETECTED", 160, 6, 2);
    } else {
        canvas.fillRect(0, 0, 320, 24, TFT_DARKGREY);
        // FIX: Text color is black, background color matches the dark grey bar
        canvas.setTextColor(TFT_BLACK, TFT_DARKGREY);
        canvas.setTextSize(1);
        canvas.drawCentreString("MONITOR ACTIVE - TAP TO SWIPE", 160, 6, 2);
    }

    
    switch(pageNum) {
        case 0:
            drawCardToCanvas(10, 40, "TRANSFER CASE", valTCase, TFT_ORANGE);
            drawCardToCanvas(170, 40, "TURBO BOOST", valBoost, TFT_CYAN);
            break;
        case 1:
            drawCardToCanvas(10, 40, "FICM MAIN PWR", valFICM, TFT_GREEN);
            drawCardToCanvas(170, 40, "COOLANT TEMP", valECT, TFT_RED);
            break;
        case 2:
            drawCardToCanvas(10, 40, "TRANSMISSION TMP", valTFT, TFT_MAGENTA);
            drawCardToCanvas(170, 40, "SYSTEM VOLTS", valVolts, TFT_YELLOW);
            break;
    }

    for(int i=0; i<TOTAL_PAGES; i++) {
        uint32_t pipColor = (i == pageNum) ? TFT_WHITE : TFT_DARKGREY;
        canvas.fillCircle(140 + (i * 20), 140, 4, pipColor);
    }
}

// Animates horizontal page swiping transitions
inline void scrollToNextPage() {
    if (!isConnected) return; 
    
    int nextPage = (currentPage + 1) % TOTAL_PAGES;
    logDebug("UI_NAV", "Triggering scroll: Swiping from Page " + String(currentPage) + " to Page " + String(nextPage));
    
    for (int offset = 0; offset <= 320; offset += 20) {
        buildPage(currentPage);
        canvas.pushSprite(offset, 40);

        buildPage(nextPage);
        canvas.pushSprite(offset - 320, 40);
        delay(5);
    }
    
    currentPage = nextPage;
}

// Renders a high-contrast welcome screen before wireless networks engage
inline void drawSplashScreen() {
    tft.fillScreen(TFT_BLACK);
    
    // Draw a styling accent border frame
    tft.drawRect(5, 5, 310, 230, TFT_ORANGE);
    tft.drawRect(7, 7, 306, 226, TFT_DARKGREY);
    
    // Welcome Text Strings
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawCentreString("FORD EXCURSION", 160, 50, 4);
    
    tft.setTextColor(TFT_ORANGE);
    tft.setTextSize(1);
    tft.drawCentreString("6.0L POWERSTROKE DIAGNOSTICS", 160, 110, 2);
    
    // Loading indicator bar line
    tft.fillRect(40, 150, 240, 4, TFT_DARKGREY);
    tft.fillRect(40, 150, 80, 4, TFT_ORANGE); // Partial loading simulation line
    
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawString("Loading graphics subsystems...", 45, 170, 2);
}


#endif
