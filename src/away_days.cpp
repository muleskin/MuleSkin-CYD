// MuleSkin-CYD — away-from-home tracker days. See include/away_days.h.
#include "away_days.h"
#include <string.h>

namespace AwayDays {

int Table::find(const uint8_t mac[6]) const {
    for (uint8_t i = 0; i < _n; i++)
        if (memcmp(_s[i].mac, mac, 6) == 0) return i;
    return -1;
}

void Table::note(const uint8_t mac[6], uint32_t day, bool away) {
    if (!mac) return;
    int i = find(mac);
    if (i < 0) {
        if (!away) return;                     // at home: nothing to start counting
        if (_n < SLOTS) {
            i = _n++;
        } else {
            i = 0;                             // full: the one heard longest ago goes
            for (uint8_t k = 1; k < SLOTS; k++)
                if (_s[k].lastDay < _s[i].lastDay) i = k;
        }
        memset(&_s[i], 0, sizeof _s[i]);
        memcpy(_s[i].mac, mac, 6);
    }
    Slot& s = _s[i];
    if (!away || s.lastDay == day) return;     // home, or already counted today
    s.lastDay = day;
    if (s.days < 255) s.days++;
    _dirty = true;
}

uint8_t Table::days(const uint8_t mac[6]) const {
    const int i = find(mac);
    return i < 0 ? 0 : _s[i].days;
}

bool Table::due(const uint8_t mac[6], uint32_t day) {
    const int i = find(mac);
    if (i < 0 || _s[i].days < DAYS_ALERT || _s[i].toldDay == day) return false;
    _s[i].toldDay = day;
    _dirty = true;
    return true;
}

bool Table::remove(const uint8_t mac[6]) {
    const int i = find(mac);
    if (i < 0) return false;
    _s[i] = _s[--_n];
    _dirty = true;
    return true;
}

size_t Table::save(uint8_t* out, size_t cap) const {
    const size_t n = 1 + (size_t)_n * sizeof(Slot);
    if (!out || cap < n) return 0;
    out[0] = _n;
    memcpy(out + 1, _s, (size_t)_n * sizeof(Slot));
    return n;
}

void Table::load(const uint8_t* in, size_t n) {
    if (!in || n < 1 || in[0] > SLOTS || n != 1 + (size_t)in[0] * sizeof(Slot)) return;
    _n = in[0];
    memcpy(_s, in + 1, (size_t)_n * sizeof(Slot));
    _dirty = false;
}

}  // namespace AwayDays
