// MuleSkin-CYD — which access points are "home". See include/home_aps.h.
#include "home_aps.h"
#include <string.h>

namespace HomeAps {

bool sameBox(const uint8_t a[6], const uint8_t b[6]) {
    return a && b && (a[0] | 0x02) == (b[0] | 0x02) && memcmp(a + 1, b + 1, 4) == 0;
}

bool Set::learn(const uint8_t bssid[6]) {
    if (!bssid) return false;
    static const uint8_t zero[6] = { 0 };
    if (memcmp(bssid, zero, 6) == 0) return false;
    for (uint8_t i = 0; i < _n; i++)
        if (memcmp(_b[i], bssid, 6) == 0) return false;
    uint8_t i;
    if (_n < SLOTS) i = _n++;
    else { i = _next; _next = (uint8_t)((_next + 1) % SLOTS); }
    memcpy(_b[i], bssid, 6);
    return true;
}

bool Set::matches(const uint8_t bssid[6]) const {
    if (!_n) return true;
    for (uint8_t i = 0; i < _n; i++)
        if (sameBox(_b[i], bssid)) return true;
    return false;
}

size_t Set::save(uint8_t* out, size_t cap) const {
    const size_t n = 2 + (size_t)_n * 6;
    if (!out || cap < n) return 0;
    out[0] = _n;
    out[1] = _next;
    memcpy(out + 2, _b, (size_t)_n * 6);
    return n;
}

void Set::load(const uint8_t* in, size_t n) {
    if (!in || n < 2 || in[0] > SLOTS || in[1] >= SLOTS || n != 2 + (size_t)in[0] * 6) return;
    _n = in[0];
    _next = in[1];
    memcpy(_b, in + 2, (size_t)_n * 6);
}

}  // namespace HomeAps
