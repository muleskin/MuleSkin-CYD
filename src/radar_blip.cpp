// MuleSkin-CYD — radar blip geometry. See include/radar_blip.h.
#include "radar_blip.h"
#include <math.h>

namespace RadarBlip {

uint8_t bearing(const uint8_t mac[6]) {
    // FNV-1a over the address; the top byte spreads best.
    uint32_t h = 2166136261u;
    for (int k = 0; k < 6; k++) { h ^= mac[k]; h *= 16777619u; }
    return (uint8_t)(h >> 24);
}

uint16_t radius256(int8_t rssi) {
    int f = (-35 - (int)rssi) * 256 / 65;      // 0 at -35, 256 at -100
    if (f < 0) f = 0;
    if (f > 256) f = 256;
    return (uint16_t)(31 + f * (256 - 31) / 256);
}

// sin over a quarter turn, Q14: round(16384 * sin(i * 2pi / 256)), i = 0..64.
// A constant, so it sits in flash -- the RAM it used as a table built at
// first use moved the heap's layout enough to cost the largest free block
// 4 KB (radar_blip_test checks the circle it draws).
static const int16_t s_sin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756,
    5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102,
    9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811,
    14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143,
    16207, 16261, 16305, 16340, 16364, 16379, 16384,
};
static int sin256(uint8_t a) {                 // Q14, a in 256ths of a turn
    const uint8_t q = a & 63;
    switch (a >> 6) {
        case 0:  return  s_sin[q];
        case 1:  return  s_sin[64 - q];
        case 2:  return -s_sin[q];
        default: return -s_sin[64 - q];
    }
}

void point(uint8_t b, int r, int cx, int cy, int& x, int& y) {
    const int sn = sin256(b), cs = sin256((uint8_t)(b + 64));
    // Rounded, not truncated, so the four quarters mirror exactly.
    x = cx + (r * sn + (sn >= 0 ? 8192 : -8192)) / 16384;
    y = cy - (r * cs + (cs >= 0 ? 8192 : -8192)) / 16384;
}

uint8_t paint(uint8_t behind) {
    return (uint8_t)(255 - behind * 170 / 255);
}

}  // namespace RadarBlip
