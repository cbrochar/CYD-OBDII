#ifndef SD_CARD_H
#define SD_CARD_H

#include <Arduino.h>
#include <SD.h>
#include "config.h"

// ---- MicroSD card -------------------------------------------------------------------
// The CYD's SD slot is on VSPI (the default SPI object); touch is bit-banged so it doesn't
// need that bus. sdBegin() can be called again at any time: a card inserted later is found.

static bool sdReady = false;

inline bool sdBegin() {
    if (sdReady) return true;
    SD.end();
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    // 3 open files (the drive log, plus a phone download or log listing) instead of the
    // default 5: each one reserves a ~4 KB sector buffer
    sdReady = SD.begin(SD_CS_PIN, SPI, 20000000, "/sd", 3);
    if (sdReady) {
        logDebug("SD", "Free heap after mounting: " + String(ESP.getFreeHeap() / 1024) + " KB");
        logDebug("SD", "Card found: " + String((uint32_t)(SD.cardSize() / (1024 * 1024))) + " MB, " +
                       String((uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024))) + " MB free");
    } else {
        logDebug("SD", "No SD card (or not FAT32 formatted)");
    }
    return sdReady;
}

#endif
