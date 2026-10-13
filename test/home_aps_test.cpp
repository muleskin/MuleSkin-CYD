// MuleSkin-CYD — which access points are "home" (src/home_aps.cpp).
//
// What this guards: before anything is learned the home name alone counts;
// once the board has joined through its router, a hotspot of the same name
// elsewhere does not -- but the router's other band does; a mesh node counts
// once learned; the oldest learned makes room; the list survives a restart.
#include "test_util.h"
#include "home_aps.h"

using namespace HomeAps;

int main() {
    const uint8_t router[6]   = { 0xa0, 0x36, 0xbc, 0x11, 0x22, 0x30 };
    const uint8_t band5[6]    = { 0xa0, 0x36, 0xbc, 0x11, 0x22, 0x34 };   // the same box, 5 GHz
    const uint8_t guest[6]    = { 0xa2, 0x36, 0xbc, 0x11, 0x22, 0x31 };   // locally administered copy
    const uint8_t street[6]   = { 0xa0, 0x36, 0xbc, 0x7e, 0x01, 0x30 };   // same maker, another house
    const uint8_t meshNode[6] = { 0x58, 0xef, 0x68, 0x01, 0x02, 0x03 };

    suite("nothing learned: the name alone");
    {
        Set s;
        ck("any address is home", s.matches(street) && s.matches(router));
    }

    suite("joined through the router");
    {
        Set s;
        ck("learned, new", s.learn(router) && s.count() == 1);
        ck("...not twice", !s.learn(router) && s.count() == 1);
        ck("the router is home", s.matches(router));
        ck("its other band too", s.matches(band5));
        ck("and its guest network", s.matches(guest));
        ck("a same-named hotspot down the street is not", !s.matches(street));
        ck("nor a mesh node not yet joined", !s.matches(meshNode));
        s.learn(meshNode);
        ck("the mesh node once joined", s.matches(meshNode) && s.matches(router));
        const uint8_t zero[6] = { 0 };
        ck("an all-zero address is not learned", !s.learn(zero) && s.count() == 2);
    }

    suite("full: the oldest learned goes");
    {
        Set s;
        uint8_t m[6] = { 0x10, 0, 0, 0, 0, 0 };
        for (uint8_t i = 0; i < SLOTS; i++) { m[1] = i; s.learn(m); }
        m[1] = 0x40; s.learn(m);
        uint8_t first[6] = { 0x10, 0, 0, 0, 0, 0 }, second[6] = { 0x10, 1, 0, 0, 0, 0 };
        ck("the newest is in", s.matches(m) && s.count() == SLOTS);
        ck("the first learned made room", !s.matches(first) && s.matches(second));
    }

    suite("kept across a restart");
    {
        Set a;
        a.learn(router); a.learn(meshNode);
        uint8_t blob[2 + SLOTS * 6];
        const size_t n = a.save(blob, sizeof blob);
        Set b; b.load(blob, n);
        ck("loaded", b.count() == 2 && b.matches(band5) && !b.matches(street));
        Set c; c.load(blob, n - 1);
        ck("a blob of the wrong size is ignored", c.count() == 0);
    }
    return report();
}
