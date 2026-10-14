// MuleSkin-CYD — the Freenove ESP32-S3 Display 3.5"'s touch controller.
//
// The ST77922 is a TDDI part: the touch sensing lives in the display chip,
// and it answers on I2C at 0x55 with 16-bit register addresses (Freenove's
// ST77922_Touch.cpp, Libraries/FNK0104N). It shares the bus with the ES8311
// audio codec at 0x18, which nothing here talks to. Reports panel pixels in
// the panel's own portrait frame, 320 x 480, which is what TouchFit wants
// (the fit is then a plain pass-through, like the GT911 boards').
#pragma once
#include <stdint.h>

namespace St77922Touch {
    static const uint8_t ADDR = 0x55;
    // The native range TouchFit maps from: panel pixels, edges at 0 and the
    // panel size, the CrowPanel's convention.
    static const uint16_t NX_MIN = 0, NX_MAX = 320, NY_MIN = 0, NY_MAX = 480;

    // Starts I2C on these pins, pulses the controller's reset (active low)
    // and waits for it to report ready. Once, from setup().
    void begin(int sda, int scl, int rst, int irq);
    // True if the controller answered after begin(): its firmware register
    // reads and it reports at least one touch point.
    bool probe();
    // True and fills x/y (portrait panel pixels) while a finger is down.
    bool read(uint16_t& x, uint16_t& y);
}
