// MuleSkin-CYD -- HUMAN SENSING's catch as a detection (DetectionType::MOTION),
// and SENSE ARM: the rules, without the radio, so the host tests can hold
// them (test/sense_motion_test.cpp). main.cpp does the work.
#pragma once
#include <stdint.h>
#include "state.h"

namespace SenseMotion {

// One row per network sensed through -- the room, as near as the board can
// tell one -- under an address made from its name, like LoRaWAN's operators
// (Lora::OpAlert::macFor): 02:4D:56 ("MV", locally administered) and three
// bytes of an FNV-1a hash. The same name is the same row across restarts,
// which is what IGNORE and SNOOZE need to stick to a room.
inline void macFor(const char* ssid, uint8_t mac[6]) {
    uint32_t h = 2166136261u;
    for (const char* p = ssid ? ssid : ""; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    mac[0] = 0x02; mac[1] = 0x4D; mac[2] = 0x56;
    mac[3] = (uint8_t)(h >> 16); mac[4] = (uint8_t)(h >> 8); mac[5] = (uint8_t)h;
}

// The grade: how far the motion model trusts its own score (SenseReport::rel).
inline Confidence confFor(uint8_t rel) {
    return rel >= 70 ? Confidence::HIGH_CONF : rel >= 40 ? Confidence::MED_CONF : Confidence::LOW_CONF;
}

// SENSE ARM (Settings): HUMAN SENSING without its screen. While armed the
// detector is paused, exactly as with the screen open -- one radio.
enum Arm : uint8_t { ARM_OFF = 0, ARM_NIGHT = 1, ARM_ALWAYS = 2, ARM_N = 3 };
inline const char* armLabel(uint8_t a) {
    return a == ARM_NIGHT ? "AT NIGHT" : a == ARM_ALWAYS ? "ALWAYS" : "OFF";
}
// Whether the setting arms it now. AT NIGHT needs a clock it can believe:
// without one it is not night, and the detector keeps the radio.
inline bool armedNow(uint8_t arm, bool clockTrusted, bool night) {
    if (arm == ARM_ALWAYS) return true;
    return arm == ARM_NIGHT && clockTrusted && night;
}
// The screens that need the radio themselves, or the detector: armed
// sensing lets go while one is up and picks up again after.
inline bool armYields(AppState s) {
    switch (s) {
        case AppState::HUNT:          // the detector's readings are the gauge
        case AppState::UPDATE:
        case AppState::AUTO_UPDATE:
        case AppState::TIME_SYNC:
        case AppState::WIFI_NETS:     // a scan
        case AppState::WIFI_ADD:
        case AppState::WIFI_PASS:     // ...and the join after it
            return true;
        default:
            return false;
    }
}

}  // namespace SenseMotion
