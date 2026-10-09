// MuleSkin-CYD — AUTO TIME asks the network when it should, and not otherwise.
//
// What this guards: AUTO TIME pauses detection while it joins WiFi, so asking
// too often costs scanning, and never asking leaves the clock to drift. The
// schedule is all arithmetic on millis(), which wraps every 49.7 days -- a
// board that has been up that long must not decide its last sync was in the
// future and never ask again.
#include "test_util.h"
#include "auto_time.h"

using namespace AutoTime;

int main() {
    suite("first sync after boot");
    ck("not in the first two minutes", !due(FIRST_MS - 1, 0, 0));
    ck("due at two minutes when nothing answered yet", due(FIRST_MS, 0, 0));

    suite("daily");
    const uint32_t t = 10u * 60u * 1000u;
    ck("not an hour after a sync", !due(t + HOUR_MS, t, 0));
    ck("not a minute short of a day", !due(t + DAY_MS - 60000u, t, 0));
    ck("due a day after the last sync", due(t + DAY_MS, t, 0));

    suite("an hour between tries");
    ck("a failed try waits an hour", !due(FIRST_MS + 30u * 60u * 1000u, 0, FIRST_MS));
    ck("...and then tries again", due(FIRST_MS + HOUR_MS, 0, FIRST_MS));
    ck("the hour holds even when a day is due", !due(t + DAY_MS + 1000u, t, t + DAY_MS));

    suite("millis() wrapping");
    const uint32_t nearWrap = 0xFFFFFFFFu - 5u * 60u * 1000u;   // five minutes before the wrap
    ck("a sync just before the wrap is not a day old just after it",
       !due(nearWrap + 10u * 60u * 1000u, nearWrap, 0));         // now has wrapped past 0
    ck("...and is due a day later, across the wrap", due(nearWrap + DAY_MS, nearWrap, 0));
    ck("a try just before the wrap still holds the hour", !due(nearWrap + 20u * 60u * 1000u, 0, nearWrap));

    return report();
}
