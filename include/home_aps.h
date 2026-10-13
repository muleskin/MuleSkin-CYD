// MuleSkin-CYD — which access points are "home".
//
// Home is the saved WiFi network marked USE (PUSH AT HOME, and the away-days
// count of away_days.h). Recognised by its name alone, any network with the
// same name counts: a "xfinitywifi" or "NETGEAR" hotspot down the street
// would make the board think it was home, and the days spent there would not
// count toward NEAR YOU ON N DAYS. So the board also learns the hardware
// address (BSSID) of the access point it actually joins on that network --
// every join for the clock, an update or a push -- and from then on a beacon
// of the home name counts only from one of those.
//
// A router's other band, or its guest network, usually differs from it only
// in the last byte (and sometimes the "locally administered" bit of the
// first): those count as the same box. A mesh node is a different box; it is
// learned the first time the board joins through it. Until anything has been
// learned -- or after the home network is changed -- the name alone counts,
// as before.
//
// Pure logic, host-tested (test/home_aps_test.cpp). DetectionEngine holds one;
// main.cpp keeps it in NVS ("homeaps") with the home name it belongs to.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace HomeAps {

const uint8_t SLOTS = 4;   // home access points remembered; the oldest learned makes room

class Set {
public:
    // The board joined the home network through this access point. True when
    // it was new (time to save).
    bool    learn(const uint8_t bssid[6]);
    // A beacon of the home name from this address: is it home? With nothing
    // learned, any address is.
    bool    matches(const uint8_t bssid[6]) const;
    uint8_t count() const { return _n; }
    const uint8_t* at(uint8_t i) const { return i < _n ? _b[i] : nullptr; }
    void    clear() { _n = 0; _next = 0; }
    // As bytes for NVS, and back; load() ignores a blob of the wrong size.
    size_t  save(uint8_t* out, size_t cap) const;
    void    load(const uint8_t* in, size_t n);
private:
    uint8_t _b[SLOTS][6];
    uint8_t _n = 0, _next = 0;
};

// Same box: equal but for the last byte and the first byte's locally
// administered bit.
bool sameBox(const uint8_t a[6], const uint8_t b[6]);

}  // namespace HomeAps
