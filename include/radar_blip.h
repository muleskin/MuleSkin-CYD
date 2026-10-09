// MuleSkin-CYD — where a detection sits on the radar scope. Pure arithmetic,
// kept apart from the drawing so test/radar_blip_test.cpp can check it.
//
// The board has no direction finding, so a blip's bearing is only a fixed,
// arbitrary place per device: a hash of its address. Its distance from the
// centre is the signal -- strong in the middle, faint at the rim.
#pragma once
#include <stdint.h>

namespace RadarBlip {

// 0..255 around the scope, 0 = north, clockwise; the same every call.
uint8_t bearing(const uint8_t mac[6]);

// How far out, in 1/256ths of the rim: -35 dBm and stronger at 31 (12%),
// -100 and weaker at 256, linear between.
uint16_t radius256(int8_t rssi);

// The point at `bearing`, `r` pixels from (cx, cy), in screen space (y down).
// Integer table, no trig per call.
void point(uint8_t bearing, int r, int cx, int cy, int& x, int& y);

// Paint: 255 as the sweep arm crosses the blip, down to 85 a turn later.
// `behind` = (sweep - bearing) as uint8, how far the arm has gone past it.
uint8_t paint(uint8_t behind);

}  // namespace RadarBlip
