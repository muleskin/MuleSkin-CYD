// MuleSkin-CYD — detection rules delivered without a firmware release.
//
// What this guards: the extra rules come over the air, signed, and are parsed
// on the board. A rule that does not parse must be skipped rather than
// guessed at, and no extra rule may change what the built-in tables already
// say -- an update to the list can only ever add.
#include "test_util.h"
#include "signatures.h"
#include <cstring>
#include <cstdio>

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

    // The set the flasher site actually signs and serves, read the way
    // build_flasher_bins.sh reads it: comments and blank lines dropped, the
    // SERIAL line apart, the rest joined with ';'. Every line must parse --
    // a typo there would otherwise ship as a rule the boards quietly skip.
    suite("the shipped set (web-flasher/signatures.txt)");
    {
        FILE* f = fopen("../web-flasher/signatures.txt", "r");
        if (!f) f = fopen("web-flasher/signatures.txt", "r");
        ck("found it", f != nullptr);
        static char body[4096];
        size_t used = 0;
        uint16_t lines = 0, ouis = 0;
        char line[256];
        while (f && fgets(line, sizeof line, f)) {
            char* s = line;
            while (*s == ' ' || *s == '\t') s++;
            size_t L = strlen(s);
            while (L && (s[L - 1] == '\r' || s[L - 1] == '\n' || s[L - 1] == ' ')) s[--L] = 0;
            if (!L || s[0] == '#' || strncmp(s, "SERIAL", 6) == 0) continue;
            if (used) body[used++] = ';';
            memcpy(body + used, s, L);
            used += L;
            body[used] = 0;
            lines++;
            if (strncmp(s, "OUI", 3) == 0) ouis++;
        }
        if (f) fclose(f);
        ck("under the boards' 2000 bytes", used <= 2000);
        ck("within 64 OUI rules", ouis <= 64);
        ck("every line parses", setExtraRules(body) == lines);
        const uint8_t hik[6] = { 0xbc, 0xad, 0x28, 0, 0, 0 };
        ck("a shipped prefix matches", lookupOui(hik) == DetectionType::CAMERA);
    }

    suite("replacing the set");
    setExtraRules("");
    ck("an empty set clears the old one", extraRuleCount() == 0 && lookupOui(a) == DetectionType::UNKNOWN);
    setExtraRules(nullptr);
    ck("nullptr is an empty set", extraRuleCount() == 0);
    return report();
}
