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

// The night hours for everything else that keeps them (NIGHT POWER): NIGHT
// DIM's when it is set, else 11 PM-5 AM, as PUSH AT NIGHT does.
inline bool nightHours(uint8_t p, uint8_t hour) {
    if (p && p < PRESET_N) return active(p, hour);
    return hour >= 23 || hour < 5;
}

// NIGHT POWER: through the night hours, half a minute after the last touch,
// DARK turns the backlight off (a tap wakes it and is not taken as a press),
// and DARK+WIFI also rests the WiFi sniffer 25 s of every 30 on the main
// screen. Bluetooth never rests: a tracker that walks past at 3 AM is still
// caught. An alert lights the screen and wakes the radios like a touch.
constexpr const char* POWER_LABELS[] = { "OFF", "DARK", "DARK+WIFI" };
constexpr uint8_t POWER_N   = sizeof(POWER_LABELS) / sizeof(POWER_LABELS[0]);
constexpr uint32_t POWER_IDLE_MS = 30000;
constexpr uint32_t WIFI_ON_MS    = 5000;
constexpr uint32_t WIFI_REST_MS  = 25000;
inline const char* powerLabel(uint8_t m) { return m < POWER_N ? POWER_LABELS[m] : "OFF"; }

// SNOOZE's MORNING step: the hour the night ends -- NIGHT DIM's end when it
// is set, else 7 AM -- and how long from h:m (local) until it next comes
// round. Exactly on the hour is a whole day: the morning just begun is not
// the one meant.
inline uint8_t morningHour(uint8_t p) { return (p && p < PRESET_N) ? PRESETS[p].end : 7; }
inline uint32_t msUntilHour(uint8_t h, uint8_t m, uint8_t target) {
    const int now = h * 60 + m;
    int mins = target * 60 - now;
    if (mins <= 0) mins += 24 * 60;
    return (uint32_t)mins * 60000u;
}

// The night backlight: a quarter of the day setting, never fully off.
inline uint8_t duty(uint8_t day) {
    const uint8_t q = (uint8_t)(day / 4);
    return q < 8 ? 8 : q;
}

}  // namespace NightMode
