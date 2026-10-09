// MuleSkin-CYD — a tracker travelling with you is called out, and the
// ordinary cases are not.
//
// What this guards: the one alert here meant for someone being followed. A
// false alarm teaches people to ignore it (a tag on a table at home, a friend
// walking with you for five minutes), and a miss is the failure that matters
// (a busy street that overwrote the tag's LOG row, a tag that comes and goes
// in the walk-past gaps).
#include "test_util.h"
#include "tracker_follow.h"

using namespace TrackerFollow;

static const uint32_t MIN = 60000u;

int main() {
    const uint8_t tag[6]   = { 0xC0, 0x01, 0x02, 0x03, 0x04, 0x05 };
    const uint8_t other[6] = { 0xC0, 0x01, 0x02, 0x03, 0x04, 0x06 };

    suite("with you, on the move");
    {
        Table t;
        t.heard(tag, 1 * MIN, 1 * MIN, 100);
        t.heard(tag, 1 * MIN, 22 * MIN, 130);           // still here, 30 new devices since
        ck("21 minutes and the crowd changed: due", t.due(22 * MIN, 130) == 0);
        ck("says 21 minutes", t.minutes(0) == 21);
        t.e[0].alerted = true;
        ck("announced once, not again", t.due(23 * MIN, 140) == -1);
    }

    suite("not yet");
    {
        Table t;
        t.heard(tag, 1 * MIN, 15 * MIN, 100);
        ck("15 minutes is not twenty", t.due(15 * MIN, 150) == -1);
    }
    {
        Table t;
        t.heard(tag, 1 * MIN, 25 * MIN, 100);
        ck("at home: hardly anything new around (5)", t.due(25 * MIN, 105) == -1);
    }
    {
        Table t;
        t.heard(tag, 1 * MIN, 22 * MIN, 100);
        ck("not heard for 5 minutes: it stayed behind", t.due(27 * MIN, 140) == -1);
    }

    suite("a busy street overwrote its LOG row");
    {
        Table t;
        t.heard(tag, 1 * MIN, 8 * MIN, 100);
        // The row was evicted and the tag re-added: a fresh first-seen.
        t.heard(tag, 9 * MIN, 22 * MIN, 190);
        ck("the table keeps the first sighting: due", t.due(22 * MIN, 190) == 0);
        ck("...21 minutes, not 13", t.minutes(0) == 21);
    }

    suite("gone long enough, it starts over");
    {
        Table t;
        t.heard(tag, 1 * MIN, 8 * MIN, 100);
        t.expire(19 * MIN);                              // eleven minutes unheard
        t.heard(tag, 19 * MIN, 30 * MIN, 200);
        ck("a fresh start: 11 minutes is not due", t.due(30 * MIN, 200) == -1);
    }

    suite("the table fills");
    {
        Table t;
        for (int i = 0; i < SLOTS; i++) {
            uint8_t m[6] = { 1, 2, 3, 4, 5, (uint8_t)i };
            t.heard(m, (uint32_t)i * MIN, (uint32_t)(i + 1) * MIN, 0);
        }
        t.heard(other, 30 * MIN, 30 * MIN, 0);           // a ninth tag
        bool hasNinth = false, hasOldest = false;
        for (const Entry& x : t.e) {
            if (memcmp(x.mac, other, 6) == 0) hasNinth = true;
            if (x.mac[0] == 1 && x.mac[5] == 0) hasOldest = true;   // heard at 1 min, the oldest
        }
        ck("the ninth is in", hasNinth);
        ck("...in the slot heard longest ago", !hasOldest);
    }

    suite("millis() wrapping");
    {
        Table t;
        const uint32_t base = 0xFFFFFFFFu - 5 * MIN;
        t.heard(tag, base, base + 21 * MIN, 0);          // last has wrapped past 0
        ck("21 minutes across the wrap: due", t.due(base + 21 * MIN, 20) == 0);
    }

    return report();
}
