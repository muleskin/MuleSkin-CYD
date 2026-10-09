// MuleSkin-CYD — detection rules delivered without a firmware release.
//
// What this guards: the extra rules come over the air, signed, and are parsed
// on the board. A rule that does not parse must be skipped rather than
// guessed at, and no extra rule may change what the built-in tables already
// say -- an update to the list can only ever add.
#include "test_util.h"
#include "signatures.h"
#include <cstring>

int main() {
    suite("parsing");
    const uint16_t n = setExtraRules(
        "OUI 12:34:56 FLOCK HIGH NewFlockCo;"
        "OUI ab:cd:ef airtag;"                 // lower case type, no confidence, no vendor
        "SSID Spycam- CAMERA SpyCamInc;"
        "OUI zz:34:56 FLOCK;"                  // bad hex
        "OUI 12:34 FLOCK;"                     // too short
        "OUI 11:22:33 NOTATYPE;"               // unknown type
        "WIBBLE 1 2 3;"                        // unknown kind
        "SSID Twin- EVIL_TWIN");
    ck("four good rules out of eight", n == 4 && extraRuleCount() == 4);

    suite("lookups");
    const uint8_t a[6] = { 0x12, 0x34, 0x56, 1, 2, 3 };
    Confidence c = Confidence::LOW_CONF;
    ck("an extra OUI matches", lookupOui(a, &c) == DetectionType::FLOCK);
    ck("...with its confidence", c == Confidence::HIGH_CONF);
    ck("...and its vendor", ouiVendorName(a) && strcmp(ouiVendorName(a), "NewFlockCo") == 0);
    const uint8_t b[6] = { 0xAB, 0xCD, 0xEF, 0, 0, 0 };
    c = Confidence::LOW_CONF;
    ck("type names are case-insensitive", lookupOui(b, &c) == DetectionType::AIRTAG);
    ck("no confidence given means MED", c == Confidence::MED_CONF);
    ck("no vendor given means none", ouiVendorName(b) == nullptr);
    ck("an extra SSID prefix matches", lookupSsid("spycam-livingroom") == DetectionType::CAMERA);
    ck("...with its vendor", ssidVendorName("Spycam-1") && strcmp(ssidVendorName("Spycam-1"), "SpyCamInc") == 0);
    ck("EVIL_TWIN names EVIL TWIN", lookupSsid("Twin-1") == DetectionType::EVILTWIN);

    suite("the built-in tables win");
    const uint8_t* k = kOuiTable[0].b;
    const DetectionType builtIn = kOuiTable[0].type;
    char rule[64];
    snprintf(rule, sizeof rule, "OUI %02x:%02x:%02x %s", k[0], k[1], k[2],
             builtIn == DetectionType::AIRTAG ? "FLOCK" : "AIRTAG");
    setExtraRules(rule);
    const uint8_t km[6] = { k[0], k[1], k[2], 0, 0, 0 };
    ck("an extra rule can't change a known prefix", lookupOui(km) == builtIn);
    ck("nor its vendor", ouiVendorName(km) == kOuiTable[0].name);

    suite("replacing the set");
    setExtraRules("");
    ck("an empty set clears the old one", extraRuleCount() == 0 && lookupOui(a) == DetectionType::UNKNOWN);
    setExtraRules(nullptr);
    ck("nullptr is an empty set", extraRuleCount() == 0);
    return report();
}
