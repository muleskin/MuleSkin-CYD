// MuleSkin-CYD — ASTM F3411 "Remote ID" decoder
//
// The DRONE detection has always been a presence flag: service UUID 0xFFFA
// is in the advert, so something compliant is overhead. The advert also
// contains a 25-byte message saying WHICH aircraft, where it is, and -- in
// the System message -- where its OPERATOR is standing. That last one is
// the reason this file exists. Everything else on this device tells you a
// thing is nearby; this tells you where the person holding it is.
//
// Scope, honestly: this decodes the Bluetooth LEGACY form, which is AD type
// 0x16 service data under UUID 0xFFFA carrying one message per
// advertisement, and the WiFi Beacon form, a vendor element packing several
// messages together (mergeBeacon). It does not decode Bluetooth 5 Long
// Range, and it cannot: the ESP32-WROOM in this board is BLE 4.2 and
// Espressif document that it has no hardware support for Coded PHY or
// extended advertising, which is where the second half of the drones are.
// Nor the WiFi NAN form, which rides action frames, not beacons.
//
// Source for every offset and constant below: opendroneid-core-c, the
// reference implementation of the standard.
//   https://github.com/opendroneid/opendroneid-core-c
#pragma once
#include <stdint.h>

namespace RemoteId {

// One aircraft, assembled over several adverts.
//
// A drone does not send all of this at once. Basic ID, Location and System
// are separate messages broadcast in rotation, so this fills in over a
// second or two and each `have` flag says whether that part has arrived
// yet. Anything not yet seen must not be drawn as zero -- zero is a real
// coordinate in the Gulf of Guinea.
struct Info {
    bool     haveBasic    = false;
    bool     haveLoc      = false;
    bool     haveOperator = false;
    char     serial[21]   = {0};   // ODID_ID_SIZE is 20, plus the terminator
    uint8_t  uaType       = 0;
    float    lat = 0, lon = 0;     // the aircraft, degrees
    float    altM         = 0;     // geodetic altitude, metres
    float    opLat = 0, opLon = 0; // whoever is flying it, degrees
    uint32_t at           = 0;     // millis() of the last message merged in
};

// "QUAD", "PLANE", "HELI"... short enough for a 320px row.
const char* uaTypeName(uint8_t t);

// Pulls one message out of a raw advertisement and merges it into `out`,
// leaving fields the message did not carry alone. Returns true if this
// advert actually contained a Remote ID message.
//
// Merging rather than returning a fresh struct is the whole point: the
// caller keeps one Info per aircraft and feeds every advert through it.
bool merge(const uint8_t* payload, uint8_t len, Info& out, uint32_t now);

// Whether a legacy advert carries Remote ID at all. The service data is the
// only place it can be: the 25-byte message fills the whole 31-byte advert,
// so a real drone has no room left to list 0xFFFA as a service UUID too.
bool present(const uint8_t* payload, uint8_t len);

// The WiFi Beacon form: `ies` is a beacon's tagged parameters (everything
// after the 12 fixed bytes). True when a Remote ID element was found, with
// whatever it carried merged into `out`.
bool mergeBeacon(const uint8_t* ies, uint16_t len, Info& out, uint32_t now);

void reset(Info& out);

}  // namespace RemoteId
