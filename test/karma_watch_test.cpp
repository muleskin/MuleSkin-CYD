// MuleSkin-CYD — karma access points (src/karma_watch.cpp).
//
// What this guards: one BSSID under five different network names inside five
// minutes is a karma AP, said once; the same name again and again is an
// ordinary AP, however often it beacons; a hidden (empty) name counts for
// nothing; two real APs side by side, each with its own name, never add up;
// names spread over longer than the window never add up either.
#include "test_util.h"
#include "karma_watch.h"
#include <cstdio>

using namespace KarmaWatch;

int main() {
    const uint8_t pine[6] = { 0x00, 0x13, 0x37, 0xa0, 0xb0, 0xc0 };
    char ssid[20];

    suite("one address, many names");
    {
        clear();
        int told = 0;
        for (int i = 0; i < THRESHOLD - 1; i++) {
            snprintf(ssid, sizeof ssid, "Net%d", i);
            told += note(pine, ssid, 1000 + i * 1000);
        }
        ck("four names: not yet", told == 0);
        ck("the fifth: a karma AP", note(pine, "Starbucks WiFi", 6000));
        ck("...said once", !note(pine, "xfinitywifi", 7000) && !note(pine, "Net9", 8000));
    }

    suite("an ordinary access point");
    {
        clear();
        int told = 0;
        for (int i = 0; i < 500; i++) told += note(pine, "HomeNet", 1000 + i * 100);
        ck("one name, beaconing all day", told == 0);
        ck("a hidden network is no name", !note(pine, "", 2000) && !note(pine, "", 2100));
    }

    suite("many access points, a name each");
    {
        clear();
        int told = 0;
        for (int i = 0; i < 12; i++) {
            const uint8_t ap[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)i };
            snprintf(ssid, sizeof ssid, "Office-%d", i);
            for (int k = 0; k < 3; k++) told += note(ap, ssid, 1000 + i * 200 + k * 50);
        }
        ck("never add up", told == 0);
    }

    suite("names spread out");
    {
        clear();
        int told = 0;
        for (int i = 0; i < 8; i++) {
            snprintf(ssid, sizeof ssid, "Roam%d", i);
            told += note(pine, ssid, 1000 + i * (WINDOW_MS / 2));
        }
        ck("two names a window never reach five", told == 0);
    }

    suite("full table");
    {
        clear();
        for (int i = 0; i < SLOTS + 4; i++) {
            const uint8_t ap[6] = { 0x10, 0x20, 0x30, 0x40, 0x51, (uint8_t)i };
            note(ap, "x", 1000 + i);
        }
        int told = 0;
        for (int i = 0; i < THRESHOLD; i++) {
            snprintf(ssid, sizeof ssid, "Lure%d", i);
            told += note(pine, ssid, 5000 + i);
        }
        ck("a new karma AP is still caught", told == 1);
    }
    return report();
}
