// MuleSkin-CYD — the ST77922's touch, over I2C. See st77922_touch.h.
#include "st77922_touch.h"
#if defined(FREENOVE_S3_35)
#include <Arduino.h>
#include <Wire.h>

namespace St77922Touch {

// Registers, from Freenove's ST77922_Touch.h.
static const uint16_t REG_FW_VER     = 0x0000;
static const uint16_t REG_STATUS     = 0x0001;
static const uint16_t REG_MAX_TOUCH  = 0x0009;
static const uint16_t REG_TOUCH_INFO = 0x0010;
static const uint16_t REG_POINT0     = 0x0014;   // 7 bytes a point: flags/xh, xl, yh, yl, ...

static bool    s_up = false;
static uint8_t s_maxPoints = 0;
static int     s_irq = -1;

static bool readRegs(uint16_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint16_t)ADDR, (size_t)n) != n) return false;
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)Wire.read();
    return true;
}

void begin(int sda, int scl, int rst, int irq) {
    Wire.begin(sda, scl, 100000);
    s_irq = irq;
    pinMode(irq, INPUT);
    pinMode(rst, OUTPUT);
    digitalWrite(rst, HIGH);
    delay(10);
    digitalWrite(rst, LOW);
    delay(100);
    digitalWrite(rst, HIGH);
    delay(100);
    // Freenove's driver spins until the status register's low nibble clears;
    // bounded here, so a silent chip does not hang the boot.
    uint8_t st = 0xFF;
    for (int i = 0; i < 50 && readRegs(REG_STATUS, &st, 1) && (st & 0x0F); i++) delay(10);
    uint8_t fw[2] = { 0, 0 };
    s_up = readRegs(REG_FW_VER, fw, 2) && readRegs(REG_MAX_TOUCH, &s_maxPoints, 1) && s_maxPoints > 0;
    if (s_maxPoints > 10) s_maxPoints = 10;
    // It answers its identity at once and then refuses point reads for about
    // a second while it settles (measured: NACKs from 0.95 s to 2.1 s after
    // boot). Wait that out here, quietly, rather than let the first screen
    // log a hundred bus errors.
    int warm = 0;
    if (s_up) {
        uint8_t p[4];
        for (; warm < 150 && !readRegs(REG_POINT0, p, sizeof p); warm++) delay(20);
    }
    if (s_up) Serial.printf("[touch] ST77922 TDDI answered: firmware %u.%u, %u points, status 0x%02X, ready after %d ms\n",
                            (unsigned)fw[0], (unsigned)fw[1], (unsigned)s_maxPoints, (unsigned)st, warm * 20);
}

bool probe() { return s_up; }

bool read(uint16_t& x, uint16_t& y) {
    if (!s_up) return false;
    // A refused read is not retried for a while: the Wire library logs every
    // one, and a chip that is busy stays busy for milliseconds, not microseconds.
    static uint32_t s_nextTry = 0;
    static bool     s_down = false;
    static uint16_t s_x = 0, s_y = 0;
    static uint32_t s_seenAt = 0;
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextTry) < 0) return s_down;
    // Freenove's sequence: TOUCH_INFO first, and the points only when its
    // bit 3 says there is a new report. Between reports the finger is where
    // the last one put it -- for a moment; a report that stops coming means
    // the finger has gone.
    uint8_t info = 0;
    if (!readRegs(REG_TOUCH_INFO, &info, 1)) { s_nextTry = now + 50; return false; }
    if (info & 0x08) {
        // The whole report, every point, as Freenove's driver reads it: the
        // chip only clears its new-report flag once all of it has been read.
        // Reading just the first point left it reporting the first tap forever
        // (seen on the board 2026-10-09: one tap, then "down" for a minute).
        uint8_t all[7 * 10];
        const size_t n = (size_t)7 * (s_maxPoints ? s_maxPoints : 1);
        if (!readRegs(REG_POINT0, all, n)) { s_nextTry = now + 50; return false; }
        const uint8_t* p = all;
        s_seenAt = now;
        const bool down = (p[0] & 0x80) != 0;
        if (down) {
            s_x = (uint16_t)(((p[0] & 0x3F) << 8) | p[1]);
            s_y = (uint16_t)(((p[2] & 0x3F) << 8) | p[3]);
        }
        s_down = down && s_x < 320 && s_y < 480;
    } else if (s_down && now - s_seenAt > 80) {
        s_down = false;
    }
    x = s_x; y = s_y;
    return s_down;
}

}  // namespace St77922Touch
#endif  // FREENOVE_S3_35
