// MuleSkin-CYD — a tracker travelling with you.
//
// A tag planted on someone (an AirTag, Tile, SmartTag or Find My Device tag
// separated from its owner) keeps one address for about a day, while a tag
// near its owner rotates every ~15 minutes. So a tag that stays with you for
// twenty minutes WHILE the devices around you keep changing -- you are on the
// move, and it is not -- is the pattern worth an alert, whatever the place.
//
// The board's LOG is a 64-row ring that overwrites by insertion order, so on a
// busy street a tag's row can be overwritten and re-added with a new
// first-seen time. This table (8 tags) keeps the first-seen across that, and
// measures "the devices around you keep changing" by the engine's running
// count of new rows (DetectionEngine::newRows()), which no eviction resets.
// Header-only and pure: test/tracker_follow_test.cpp.
#pragma once
#include <stdint.h>
#include <string.h>

namespace TrackerFollow {

constexpr uint32_t WITH_YOU_MS   = 20u * 60u * 1000u;  // heard over at least this long
constexpr uint32_t STILL_HERE_MS =  3u * 60u * 1000u;  // ...and as recently as this
constexpr uint32_t FORGET_MS     = 10u * 60u * 1000u;  // a gap this long starts it over
constexpr uint32_t CHURN_MIN     = 12;                 // new devices since it was first heard
constexpr uint8_t  SLOTS         = 8;

struct Entry {
    uint8_t  mac[6];
    uint32_t first, last;   // millis()
    uint32_t seqAtFirst;    // newRows() when it was first heard
    bool     used, alerted;
};

struct Table {
    Entry e[SLOTS] = {};

    // A tracker's row: when it was first and last heard (millis()), and the
    // engine's new-row count now.
    void heard(const uint8_t mac[6], uint32_t firstSeen, uint32_t lastSeen, uint32_t seq) {
        for (Entry& x : e) {
            if (x.used && memcmp(x.mac, mac, 6) == 0) {
                if ((int32_t)(lastSeen - x.last) > 0) x.last = lastSeen;
                return;
            }
        }
        // New: a free slot, else the one heard longest ago.
        Entry* slot = nullptr;
        for (Entry& x : e) if (!x.used) { slot = &x; break; }
        if (!slot) {
            slot = &e[0];
            for (Entry& x : e) if ((int32_t)(slot->last - x.last) > 0) slot = &x;
        }
        memcpy(slot->mac, mac, 6);
        slot->first = firstSeen;
        slot->last = lastSeen;
        slot->seqAtFirst = seq;
        slot->used = true;
        slot->alerted = false;
    }

    // Forget tags not heard for FORGET_MS: when one comes back it starts over.
    void expire(uint32_t now) {
        for (Entry& x : e) if (x.used && now - x.last > FORGET_MS) x.used = false;
    }

    // The first tag that is following and has not been announced, or -1.
    int due(uint32_t now, uint32_t seq) const {
        for (int i = 0; i < SLOTS; i++) {
            const Entry& x = e[i];
            if (!x.used || x.alerted) continue;
            if (now - x.last > STILL_HERE_MS) continue;
            if (x.last - x.first < WITH_YOU_MS) continue;
            if (seq - x.seqAtFirst < CHURN_MIN) continue;
            return i;
        }
        return -1;
    }

    // Minutes it has been with you, for the alert card.
    uint16_t minutes(int i) const { return (uint16_t)((e[i].last - e[i].first) / 60000u); }
};

}  // namespace TrackerFollow
