// MuleSkin-CYD -- TODAY counts the day it says it does.
//
// What this guards: the screen walks the black box newest first and buckets
// each sighting by local hour; anything from another day (yesterday's late
// evening above all) must not land in today's bars.
#include "test_util.h"
#include "ui_today.h"

int main() {
    Today::Stats s;
    Today::clear(s);
    suite("bucketing");
    Today::add(s, 126, 281, 9, 6, 1000, 126, 281);
    Today::add(s, 126, 281, 9, 6, 1100, 126, 281);
    Today::add(s, 126, 281, 23, 1, 2000, 126, 281);
    ck("two at nine", s.perHour[9] == 2);
    ck("one at eleven at night", s.perHour[23] == 1);
    ck("by type", s.perType[6] == 2 && s.perType[1] == 1);
    ck("total", s.total == 3);
    ck("first and last", s.firstEpoch == 1000 && s.lastEpoch == 2000);

    suite("other days stay out");
    Today::add(s, 126, 280, 23, 1, 900, 126, 281);     // yesterday, late
    Today::add(s, 125, 281, 9, 1, 800, 126, 281);      // same day number, last year
    ck("yesterday's 11 PM is not today's", s.perHour[23] == 1 && s.total == 3);
    ck("first is still today's first", s.firstEpoch == 1000);

    suite("odd input");
    Today::add(s, 126, 281, 24, 1, 3000, 126, 281);
    Today::add(s, 126, 281, -1, 1, 3000, 126, 281);
    ck("hours outside 0-23 are dropped", s.total == 3);
    Today::add(s, 126, 281, 5, 200, 3000, 126, 281);
    ck("an unknown type still counts the hour", s.perHour[5] == 1 && s.total == 4);
    return report();
}
