// MuleSkin-CYD — karma access points. See include/karma_watch.h.
#include "karma_watch.h"
#include <string.h>

namespace KarmaWatch {
namespace {

struct Slot {
    uint8_t  bssid[6];
    uint32_t first;               // the window's start
    uint32_t used;                // last heard: which slot goes when full
    uint32_t names[THRESHOLD];    // FNV-1a of each distinct SSID heard
    uint8_t  count;
    bool     told;
};
Slot    s_slot[SLOTS];
uint8_t s_n = 0;

uint32_t fnv(const char* s) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x01000193u;
    return h;
}

}  // namespace

bool note(const uint8_t bssid[6], const char* ssid, uint32_t now) {
    if (!bssid || !ssid || !ssid[0]) return false;
    Slot* s = nullptr;
    for (uint8_t i = 0; i < s_n && !s; i++)
        if (memcmp(s_slot[i].bssid, bssid, 6) == 0) s = &s_slot[i];
    if (!s) {
        if (s_n < SLOTS) {
            s = &s_slot[s_n++];
        } else {
            s = &s_slot[0];
            for (uint8_t i = 1; i < SLOTS; i++)
                if ((int32_t)(s_slot[i].used - s->used) < 0) s = &s_slot[i];
        }
        memset(s, 0, sizeof *s);
        memcpy(s->bssid, bssid, 6);
        s->first = now;
    }
    s->used = now;
    // A new window once the old one is over -- unless it already told, which
    // stands for as long as the slot does: one alert per karma AP.
    if (!s->told && (int32_t)(now - s->first) > (int32_t)WINDOW_MS) {
        s->first = now;
        s->count = 0;
    }
    if (s->told) return false;
    const uint32_t h = fnv(ssid);
    for (uint8_t i = 0; i < s->count; i++)
        if (s->names[i] == h) return false;
    s->names[s->count++] = h;
    if (s->count < THRESHOLD) return false;
    s->told = true;
    return true;
}

void clear() { s_n = 0; }

}  // namespace KarmaWatch
