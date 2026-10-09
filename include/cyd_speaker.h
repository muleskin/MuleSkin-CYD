// MuleSkin-CYD — the 2.8" CYD's speaker connector (ESP32-2432S028R: the
// two-pin "SPEAK" header, GPIO 26 into an SC8002B amplifier). A small 8 ohm
// speaker plugged in there gives the alert chirp the CrowPanel's buzzer
// gives; with nothing plugged in it is silent and costs nothing.
//
// The same calls as CrowBuzzer (include/buzzer.h picks one), so the BUZZER
// setting and its rules in main.cpp -- one chirp for a device never logged
// before, never dimmed, never at night, never under the meeting sign --
// apply unchanged. A square wave on LEDC channel 6 (0-5 are the backlight
// and the status light).
#pragma once
#include <stdint.h>

namespace CydSpeaker {
void begin();               // once in setup(): silent
void chirp(uint16_t ms);    // a tone of `ms` ms (20..500); one already sounding is extended
void tick();                // from loop(), every pass: ends the tone on time
void quiet();               // off now
}
