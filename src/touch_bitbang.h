#ifndef TOUCH_BITBANG_H
#define TOUCH_BITBANG_H

#include <Arduino.h>

// ---- XPT2046 touch controller over bit-banged SPI ---------------------------------
// The CYD's TFT uses one hardware SPI bus and the SD card slot needs the other, so the
// touch controller (slow, < 2.5 MHz) is driven by toggling its pins directly. The read
// sequence, thresholds and averaging are the same as Paul Stoffregen's
// XPT2046_Touchscreen library (MIT), so raw values and TOUCH_RAW_* calibration match.

struct TS_Point {
    int16_t x, y, z;
};

class TouchBitbang {
public:
    TouchBitbang(uint8_t clk, uint8_t mosi, uint8_t miso, uint8_t cs, uint8_t irq)
        : clk(clk), mosi(mosi), miso(miso), cs(cs), irq(irq) {}

    void begin() {
        pinMode(cs, OUTPUT);   digitalWrite(cs, HIGH);
        pinMode(clk, OUTPUT);  digitalWrite(clk, LOW);
        pinMode(mosi, OUTPUT); digitalWrite(mosi, LOW);
        pinMode(miso, INPUT);
        pinMode(irq, INPUT);
    }
    void setRotation(uint8_t r) { rotation = r % 4; }
    bool touched()      { update(); return zraw >= Z_THRESHOLD; }
    TS_Point getPoint() { update(); return {xraw, yraw, zraw}; }

private:
    static const int Z_THRESHOLD     = 400;
    static const int Z_THRESHOLD_INT = 75;
    static const unsigned long MSEC_THRESHOLD = 3;

    uint8_t clk, mosi, miso, cs, irq;
    uint8_t rotation = 1;
    int16_t xraw = 0, yraw = 0, zraw = 0;
    unsigned long msraw = 0x80000000;
    bool awake = false;   // pen down seen on the IRQ line, keep reading until it lifts

    // SPI mode 0, MSB first: set data, clock high, sample, clock low (~400 kHz)
    uint16_t transfer(uint16_t out, int bits) {
        uint16_t in = 0;
        for (int i = bits - 1; i >= 0; i--) {
            digitalWrite(mosi, (out >> i) & 1);
            delayMicroseconds(1);
            digitalWrite(clk, HIGH);
            delayMicroseconds(1);
            in = (in << 1) | digitalRead(miso);
            digitalWrite(clk, LOW);
        }
        return in;
    }
    uint16_t transfer16(uint16_t out) { return transfer(out, 16); }

    static int16_t besttwoavg(int16_t x, int16_t y, int16_t z) {
        int16_t da = abs(x - y), db = abs(x - z), dc = abs(z - y);
        if (da <= db && da <= dc) return (x + y) >> 1;
        if (db <= da && db <= dc) return (x + z) >> 1;
        return (y + z) >> 1;
    }

    void update() {
        // The IRQ line goes low while the screen is pressed (it replaces the library's interrupt)
        if (digitalRead(irq) == LOW) awake = true;
        if (!awake) return;
        unsigned long now = millis();
        if (now - msraw < MSEC_THRESHOLD) return;

        int16_t data[6];
        digitalWrite(cs, LOW);
        transfer(0xB1 /* Z1 */, 8);
        int16_t z1 = transfer16(0xC1 /* Z2 */) >> 3;
        int z = z1 + 4095;
        int16_t z2 = transfer16(0x91 /* X */) >> 3;
        z -= z2;
        if (z >= Z_THRESHOLD) {
            transfer16(0x91 /* X */);   // dummy X measure, 1st is always noisy
            data[0] = transfer16(0xD1 /* Y */) >> 3;
            data[1] = transfer16(0x91 /* X */) >> 3;   // make 3 x-y measurements
            data[2] = transfer16(0xD1 /* Y */) >> 3;
            data[3] = transfer16(0x91 /* X */) >> 3;
        } else {
            data[0] = data[1] = data[2] = data[3] = 0;
        }
        data[4] = transfer16(0xD0 /* Y */) >> 3;   // last Y, then power down (IRQ re-enabled)
        data[5] = transfer16(0) >> 3;
        digitalWrite(cs, HIGH);

        if (z < 0) z = 0;
        if (z < Z_THRESHOLD) {
            zraw = 0;
            if (z < Z_THRESHOLD_INT) awake = false;
            return;
        }
        zraw = z;

        int16_t x = besttwoavg(data[0], data[2], data[4]);
        int16_t y = besttwoavg(data[1], data[3], data[5]);
        msraw = now;
        switch (rotation) {
            case 0:  xraw = 4095 - y; yraw = x;        break;
            case 1:  xraw = x;        yraw = y;        break;
            case 2:  xraw = y;        yraw = 4095 - x; break;
            default: xraw = 4095 - x; yraw = 4095 - y; break;
        }
    }
};

#endif
