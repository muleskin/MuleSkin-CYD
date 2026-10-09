// MuleSkin-CYD — AUTO TIME's schedule: is a clock join due now? Header-only and
// pure, so test/auto_time_test.cpp checks it, wraps of millis() included.
//
//   - Once a day after the last time the network answered (any sync).
//   - If the network has not answered this boot, once, a couple of minutes in.
//   - An hour between tries, whatever happened, so an unreachable network is
//     not asked every frame.
#pragma once
#include <stdint.h>

namespace AutoTime {

constexpr uint32_t DAY_MS   = 24u * 60u * 60u * 1000u;
constexpr uint32_t HOUR_MS  = 60u * 60u * 1000u;
constexpr uint32_t FIRST_MS = 2u * 60u * 1000u;

// lastSyncMs / lastTryMs: millis() of the last network answer / the last try,
// 0 for never (Clock::lastSyncMs() never hands out a real 0).
inline bool due(uint32_t now, uint32_t lastSyncMs, uint32_t lastTryMs) {
    if (lastTryMs && now - lastTryMs < HOUR_MS) return false;
    if (lastSyncMs) return now - lastSyncMs >= DAY_MS;
    return now >= FIRST_MS;
}

}  // namespace AutoTime
