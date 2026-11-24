// SenseMotion -- include/sense_motion.h -- HUMAN SENSING's catch as a
// Detection (its address and grade) and SENSE ARM's rules.
#include "sense_motion.h"
#include "test_util.h"
#include <cstring>

using namespace SenseMotion;

int main() {
    suite("One address per room");
    {
        uint8_t a[6], b[6], c[6];
        macFor("HomeNet", a);
        macFor("HomeNet", b);
        macFor("HomeNet-5G", c);
        ck("the same network is the same row", memcmp(a, b, 6) == 0);
        ck("another network is another row", memcmp(a, c, 6) != 0);
        ck("02:4D:56, as the emulator profile says", a[0] == 0x02 && a[1] == 0x4D && a[2] == 0x56);
        ck("locally administered, never a real maker's", (a[0] & 0x02) && !(a[0] & 0x01));
        uint8_t e[6], n[6];
        macFor("", e);
        macFor(nullptr, n);
        ck("no network and a null name agree", memcmp(e, n, 6) == 0);
    }

    suite("Graded by the model's own trust");
    ck("70 is sure", confFor(70) == Confidence::HIGH_CONF);
    ck("69 is likely", confFor(69) == Confidence::MED_CONF);
    ck("40 is likely", confFor(40) == Confidence::MED_CONF);
    ck("39 is a maybe", confFor(39) == Confidence::LOW_CONF);

    suite("SENSE ARM");
    ck("OFF is never armed", !armedNow(ARM_OFF, true, true));
    ck("ALWAYS is armed by day", armedNow(ARM_ALWAYS, true, false));
    ck("...and on a clock it cannot believe", armedNow(ARM_ALWAYS, false, false));
    ck("AT NIGHT, at night", armedNow(ARM_NIGHT, true, true));
    ck("AT NIGHT, not by day", !armedNow(ARM_NIGHT, true, false));
    ck("AT NIGHT, not on an unset clock", !armedNow(ARM_NIGHT, false, true));
    ck("the labels", !strcmp(armLabel(ARM_OFF), "OFF") && !strcmp(armLabel(ARM_NIGHT), "AT NIGHT") &&
                     !strcmp(armLabel(ARM_ALWAYS), "ALWAYS"));

    suite("Screens that take the radio back");
    ck("HUNT needs the detector", armYields(AppState::HUNT));
    ck("an update needs the radio", armYields(AppState::UPDATE) && armYields(AppState::AUTO_UPDATE));
    ck("WIFI NETWORKS scans and joins", armYields(AppState::WIFI_NETS) && armYields(AppState::WIFI_ADD) &&
                                        armYields(AppState::WIFI_PASS));
    ck("WIFI TIME joins", armYields(AppState::TIME_SYNC));
    ck("the main screen keeps it armed", !armYields(AppState::CLEAR));
    ck("...and an alert card, and the lock", !armYields(AppState::ALERT) && !armYields(AppState::LOCKED));

    return report();
}
