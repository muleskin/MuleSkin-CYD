// MuleSkin-CYD — TODAY: what has been around today, from the black box.
//
// Opened by tapping the clock on the main screen. A bar per hour of the local
// day, the types seen most, and the first and last sighting. Counted from the
// black box (so restarts don't lose the morning) when the screen opens and
// every 30 s after; only sightings with a wall-clock time count, so without
// the clock set it says how to set it.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include <string.h>

void uiTodayInit(TFT_eSPI& t);
void uiTodayTick(TFT_eSPI& t, uint32_t now);
bool uiTodayHitBack(int x, int y, int screenW, int screenH);

namespace Today {
// The counting, apart from the drawing (test/today_test.cpp): sightings
// bucketed by local hour and type for the local day `yday`/`year`.
struct Stats {
    uint16_t perHour[24];
    uint16_t perType[32];
    uint16_t total;
    uint32_t firstEpoch, lastEpoch;   // 0 when none
};
inline void clear(Stats& s) { memset(&s, 0, sizeof s); }
// One sighting at local time (year, yday, hour) for `type`; counted only when
// it is on the day asked for.
inline void add(Stats& s, int year, int yday, int hour, uint8_t type, uint32_t epoch,
                int wantYear, int wantYday) {
    if (year != wantYear || yday != wantYday || hour < 0 || hour > 23) return;
    if (s.perHour[hour] < 0xFFFF) s.perHour[hour]++;
    if (type < 32 && s.perType[type] < 0xFFFF) s.perType[type]++;
    if (s.total < 0xFFFF) s.total++;
    if (!s.firstEpoch || epoch < s.firstEpoch) s.firstEpoch = epoch;
    if (epoch > s.lastEpoch) s.lastEpoch = epoch;
}
// The screen's current counts: the emulator fills them to render a busy day.
Stats& current();
}  // namespace Today
