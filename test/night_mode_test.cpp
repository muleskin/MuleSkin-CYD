// MuleSkin-CYD — NIGHT DIM's hours, the ones across midnight above all.
#include "test_util.h"
#include "night_mode.h"

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
