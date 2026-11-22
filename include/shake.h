// MuleSkin-CYD: shake and tap, on the small boards' BMI270 (StickS3,
// Cardputer ADV). (Upstream SquachWatch v1.42/v1.43, where a knock pokes
// Squachy; MuleSkin has no Squachy to poke.)
//
// No touch panel on these boards, so the case itself is a finger: a WIGGLE
// (picking the fob up and jiggling it) or a SHAKE wakes a screen POWER SAVER
// has dimmed, and a TAP on the case (one sharp knock, on its own) keeps an
// awake one awake. The accelerometer's own FIFO is read from loop(), so a
// knock a few milliseconds long is never missed between frames. Nothing here
// sleeps or takes an interrupt.
#pragma once
#include <stdint.h>

#if !defined(ESP32)
// The PC emulator has no IMU: nothing here ever happens.
namespace Shake {
    enum class Event : uint8_t { NONE, SHAKE, TAP, WIGGLE };
    inline bool begin() { return false; }
    inline bool present() { return false; }
    inline Event poll(uint32_t) { return Event::NONE; }
    inline void setTrace(bool) {}
}
#else
namespace Shake {
    // WIGGLE is a lighter shake, only for waking a dimmed screen: someone
    // picking the fob up and jiggling it to see it. SHAKE is a real one.
    enum class Event : uint8_t { NONE, SHAKE, TAP, WIGGLE };
    // Finds the chip on Wire (already begun by the board's own setup),
    // uploads its configuration and starts the accelerometer. False if there
    // is no BMI270 answering; poll() then always says NONE.
    bool begin();
    bool present();
    // Call every loop. At most one event per call.
    Event poll(uint32_t now);
    // IMU on the console: every excursion from 1 g, for tuning.
    void setTrace(bool on);
}
#endif
