// MuleSkin-CYD — NIGHT DIM's hours, the ones across midnight above all.
#include "test_util.h"
#include "night_mode.h"
#include <string.h>

using namespace NightMode;

int main() {
    suite("OFF");
    bool any = false;
    for (int h = 0; h < 24; h++) any |= active(0, (uint8_t)h);
    ck("never dims", !any);
    ck("an out-of-range preset is OFF", !active(PRESET_N, 23));

    suite("10PM-6AM (across midnight)");
    ck("9 PM is day", !active(1, 21));
    ck("10 PM is night", active(1, 22));
    ck("midnight is night", active(1, 0));
    ck("5 AM is night", active(1, 5));
    ck("6 AM is day again", !active(1, 6));
    ck("noon is day", !active(1, 12));

    suite("12AM-7AM (not across midnight)");
    ck("11 PM is day", !active(3, 23));
    ck("midnight is night", active(3, 0));
    ck("6 AM is night", active(3, 6));
    ck("7 AM is day", !active(3, 7));

    suite("NIGHT POWER's hours: NIGHT DIM's, else 11 PM-5 AM");
    ck("with 10PM-6AM set, 10 PM is night", nightHours(1, 22));
    ck("...and 5 AM", nightHours(1, 5));
    ck("with NIGHT DIM off, 11 PM is night", nightHours(0, 23));
    ck("...4 AM too", nightHours(0, 4));
    ck("...but not 5 AM", !nightHours(0, 5));
    ck("...nor 10 PM", !nightHours(0, 22));
    ck("the three labels", !strcmp(powerLabel(0), "OFF") && !strcmp(powerLabel(2), "DARK+WIFI") &&
                           !strcmp(powerLabel(9), "OFF"));

    suite("SNOOZE until morning");
    ck("NIGHT DIM 10PM-6AM: the morning is 6", morningHour(1) == 6);
    ck("NIGHT DIM off: 7", morningHour(0) == 7);
    ck("11:30 PM to 7 AM is 7 h 30 min", msUntilHour(23, 30, 7) == 450u * 60000u);
    ck("2:15 AM to 7 AM is 4 h 45 min", msUntilHour(2, 15, 7) == 285u * 60000u);
    ck("7:00 AM: the next morning, a day off", msUntilHour(7, 0, 7) == 1440u * 60000u);
    ck("9 AM: tomorrow's, 22 h", msUntilHour(9, 0, 7) == 1320u * 60000u);

    suite("every preset is eight hours or so, and labelled");
    bool sane = true;
    for (uint8_t p = 1; p < PRESET_N; p++) {
        int n = 0;
        for (int h = 0; h < 24; h++) n += active(p, (uint8_t)h);
        if (n < 6 || n > 9 || !label(p)[0]) sane = false;
    }
    ck("6-9 night hours each", sane);

    suite("duty");
    ck("a quarter of the day setting", duty(200) == 50);
    ck("never below 8", duty(16) == 8 && duty(0) == 8);

    return report();
}
