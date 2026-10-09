// MuleSkin-CYD — NIGHT DIM: the hours the screen runs at a quarter of its
// brightness. Header-only and pure, so test/night_mode_test.cpp checks the
// windows that cross midnight.
//
// Only the backlight changes -- scanning runs at full rate all night, since a
// detector that misses things after dark is not one. A touch or an alert
// brings full brightness back for half a minute (main.cpp nightDimNow()).
#pragma once
#include <stdint.h>

namespace NightMode {

struct Window { uint8_t start, end; const char* label; };   // local hours, [start, end)

// Preset 0 is OFF. The setting stores the index.
constexpr Window PRESETS[] = {
    { 0,  0, "OFF" },
    { 22, 6, "10PM-6AM" },
    { 23, 7, "11PM-7AM" },
    { 0,  7, "12AM-7AM" },
};
constexpr uint8_t PRESET_N = sizeof(PRESETS) / sizeof(PRESETS[0]);

// Is `hour` (0..23, local) inside preset `p`'s window?
inline bool active(uint8_t p, uint8_t hour) {
    if (p == 0 || p >= PRESET_N) return false;
    const Window& w = PRESETS[p];
    return w.start < w.end ? (hour >= w.start && hour < w.end)
                           : (hour >= w.start || hour < w.end);   // across midnight
}

inline const char* label(uint8_t p) { return p < PRESET_N ? PRESETS[p].label : "OFF"; }

// The night backlight: a quarter of the day setting, never fully off.
inline uint8_t duty(uint8_t day) {
    const uint8_t q = (uint8_t)(day / 4);
    return q < 8 ? 8 : q;
}

}  // namespace NightMode
