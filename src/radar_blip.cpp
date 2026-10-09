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

// sin over a quarter turn, Q14, built once.
static int16_t s_sin[65];
static bool    s_ready = false;
static int sin256(uint8_t a) {                 // Q14, a in 256ths of a turn
    if (!s_ready) {
        for (int i = 0; i <= 64; i++) s_sin[i] = (int16_t)lroundf(16384.0f * sinf((float)i * 6.2831853f / 256.0f));
        s_ready = true;
    }
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
