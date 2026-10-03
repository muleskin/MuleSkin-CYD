// MuleSkin-CYD — wardrive capture and storage, src/wardrive.cpp, against
// the emulator's 16 sectors of RAM flash.
//
// What this guards: rows only with a fix, the same network not written over
// and over from one spot, the ring wrapping without losing its order, and a
// reboot finding everything again.
#include "wardrive.h"
#include "gnss.h"
#include "test_util.h"
#include <cstring>
#include <vector>

static void mac(uint8_t* m, uint32_t k) { m[0] = 0x02; m[1] = 0xAA; m[2] = (uint8_t)(k >> 24); m[3] = (uint8_t)(k >> 16); m[4] = (uint8_t)(k >> 8); m[5] = (uint8_t)k; }

static std::vector<Wardrive::Record> all() {
    std::vector<Wardrive::Record> v;
    Wardrive::forEach([](const Wardrive::Record& r, void* c) { ((std::vector<Wardrive::Record>*)c)->push_back(r); return true; }, &v);
    return v;
}

int main() {
    const uint32_t T0 = 1790553600u;             // 2026-09-28 00:00 UTC
    uint8_t m[6];

    suite("Nothing without the switch, nothing without a fix");
    ck("mounts", Wardrive::begin());
    ck("starts empty", Wardrive::count() == 0);
    mac(m, 1);
    Gnss::reset();
    Gnss::fake(407128000, -740060000, T0, 1000);
    Wardrive::noteWifi(m, "Off", 0, 6, -50);
    Wardrive::tick(1100);
    ck("switched off: no row", Wardrive::count() == 0);
    Wardrive::setEnabled(true);
    Gnss::reset();
    Wardrive::noteWifi(m, "NoFix", 0, 6, -50);
    Wardrive::tick(1100);
    ck("no fix: no row", Wardrive::count() == 0);
    Gnss::fake(407128000, -740060000, T0, 1000);
    Wardrive::tick(1200);
    ck("and a queue from before the fix is not stamped with it", Wardrive::count() == 0);

    suite("A row, stamped");
    Wardrive::noteWifi(m, "MuleSkinNet", 0x0495, 6, -48);
    Wardrive::tick(1300);
    std::vector<Wardrive::Record> v = all();
    ck("one row", v.size() == 1 && Wardrive::count() == 1);
    ck("its network", v.size() == 1 && v[0].kind == Wardrive::KIND_WIFI && !memcmp(v[0].mac, m, 6) &&
                      v[0].nameLen == 11 && !memcmp(v[0].name, "MuleSkinNet", 11) && v[0].auth == 0x0495 && v[0].channel == 6);
    ck("where and when", v.size() == 1 && v[0].lat7 == 407128000 && v[0].lon7 == -740060000 && v[0].epoch == T0);
    ck("marked as a bench row", v.size() == 1 && (v[0].flags & Wardrive::F_FAKE));

    suite("Not again from the same spot");
    for (int i = 0; i < 5; i++) { Wardrive::noteWifi(m, "MuleSkinNet", 0x0495, 6, -48); Wardrive::tick(1400); }
    ck("five more hearings, still one row", Wardrive::count() == 1 && Wardrive::skipped() == 5);
    Gnss::fake(407128000 + 6000, -740060000, T0 + 10, 2000);     // about 67 m north
    Wardrive::noteWifi(m, "MuleSkinNet", 0x0495, 6, -60);
    Wardrive::tick(2100);
    ck("from 67 m away: a new row", Wardrive::count() == 2);
    Gnss::fake(407128000 + 6000, -740060000, T0 + 10 + 301, 3000);
    Wardrive::noteWifi(m, "MuleSkinNet", 0x0495, 6, -61);
    Wardrive::tick(3100);
    ck("five minutes later, same spot: a new row", Wardrive::count() == 3);

    suite("Bluetooth");
    mac(m, 2);
    Wardrive::noteBle(m, "Tile", -70, true, 0x00C7);
    Wardrive::tick(3200);
    v = all();
    ck("a Bluetooth row with its company", v.size() == 4 && v[3].kind == Wardrive::KIND_BLE &&
                                           (v[3].flags & Wardrive::F_HAS_MFGR) && v[3].auth == 0x00C7 && v[3].channel == 0);
    uint8_t same[6]; memcpy(same, m, 6);
    Wardrive::noteWifi(same, "Clash", 0, 1, -70);
    Wardrive::tick(3300);
    ck("the same address over WiFi is a different thing", Wardrive::count() == 5);

    suite("A reboot finds it all again");
    ck("remounts", Wardrive::begin());
    ck("same count", Wardrive::count() == 5);
    ck("same order", all().size() == 5 && all()[0].epoch == T0);

    suite("Wrapping");
    const uint32_t cap = Wardrive::capacity();
    uint32_t t = 4000;
    for (uint32_t k = 100; k < 100 + cap + 200; k++) {
        mac(m, k);
        Gnss::fake(407128000, -740060000, T0 + 1000 + k, t);
        Wardrive::noteWifi(m, "w", 0, 1, -80);
        Wardrive::tick(t);
        t += 10;
    }
    const uint32_t kept = Wardrive::count();
    ck("full, less the sector being reused", kept <= cap && kept >= cap - 63);
    v = all();
    bool ordered = true;
    for (size_t i = 1; i < v.size(); i++) if (v[i].epoch < v[i - 1].epoch) ordered = false;
    ck("oldest first, all the way round", ordered && v.size() == kept);
    ck("the newest is the last one written", !v.empty() && v.back().epoch == T0 + 1000 + 100 + cap + 199);
    ck("still there after a reboot", Wardrive::begin() && Wardrive::count() == kept && all().back().epoch == v.back().epoch);

    suite("Clear");
    Wardrive::clear();
    ck("empty", Wardrive::count() == 0 && all().empty());
    ck("and still empty after a reboot", Wardrive::begin() && Wardrive::count() == 0);
    mac(m, 9);
    Gnss::fake(407128000, -740060000, T0 + 99999, t);
    Wardrive::noteWifi(m, "after", 0, 1, -80);
    Wardrive::tick(t);
    ck("and writes again", Wardrive::count() == 1);

    return report();
}
