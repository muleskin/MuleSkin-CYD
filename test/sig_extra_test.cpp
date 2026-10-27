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
#include <string>
#include <strings.h>
#include <vector>
#include <cctype>
#include <cstdlib>

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

    suite("Bluetooth rules");
    {
        const uint16_t got = setExtraRules(
            "UUID 0xFDAB TILE TileFDAB;"           // a value the built-in table lacks
            "CID 0x0FAB CAMERA SomeCamCo;"
            "NAME Spyglass- META Spyglass;"
            "UUID 0xFEED CAMERA Nope;"             // built in already: Tile keeps it
            "UUID 0xZZZZ TILE;"                    // bad hex
            "CID 0x12345 TILE;"                    // five digits
            "NAME ThisNameIsFarTooLongToKeep TILE"); // over 15
        ck("four good of seven", got == 4);
        ck("an extra UUID matches", lookupUuid(0xFDAB) == DetectionType::TILE);
        ck("...with its vendor", uuidName(0xFDAB) && strcmp(uuidName(0xFDAB), "TileFDAB") == 0);
        ck("an extra company ID matches", lookupMfgId(0x0FAB) == DetectionType::CAMERA);
        ck("...with its vendor", mfgIdName(0x0FAB) && strcmp(mfgIdName(0x0FAB), "SomeCamCo") == 0);
        ck("an extra name prefix matches, any case", lookupBtName("SPYGLASS-0042") == DetectionType::META);
        ck("...with its vendor", btNameVendor("spyglass-1") && strcmp(btNameVendor("spyglass-1"), "Spyglass") == 0);
        ck("a built-in UUID keeps its meaning", lookupUuid(0xFEED) == DetectionType::TILE);
        ck("nothing else matches", lookupUuid(0x1234) == DetectionType::UNKNOWN && lookupMfgId(0x1234) == DetectionType::UNKNOWN);
    }

    suite("DROP and GRADE: the two that can change a built-in row");
    {
        char r[160];
        snprintf(r, sizeof r, "DROP OUI %02x:%02x:%02x;DROP UUID 0xFEED;DROP CID 0x0E29;DROP SSID AB2-;DROP NAME Flipper;"
                              "GRADE %02x:%02x:%02x HIGH;DROP WIBBLE x;GRADE 12:34 LOW",
                 k[0], k[1], k[2], kOuiTable[1].b[0], kOuiTable[1].b[1], kOuiTable[1].b[2]);
        ck("six good of eight", setExtraRules(r) == 6);
        ck("a dropped prefix is gone", lookupOui(km) == DetectionType::UNKNOWN && ouiVendorName(km) == nullptr);
        ck("a dropped UUID is gone", lookupUuid(0xFEED) == DetectionType::UNKNOWN && uuidName(0xFEED) == nullptr);
        ck("the other Tile UUID is not", lookupUuid(0xFEEC) == DetectionType::TILE);
        ck("a dropped company ID is gone", lookupMfgId(0x0E29) == DetectionType::UNKNOWN);
        ck("a dropped SSID prefix is gone", lookupSsid("AB2-123456") == DetectionType::UNKNOWN);
        ck("...and only that one", lookupSsid("AB3-123456") == DetectionType::AXON);
        ck("a dropped name is gone", lookupBtName("Flipper Zeroth") == DetectionType::UNKNOWN);
        const uint8_t g6[6] = { kOuiTable[1].b[0], kOuiTable[1].b[1], kOuiTable[1].b[2], 1, 2, 3 };
        Confidence c = Confidence::LOW_CONF;
        ck("a regraded prefix keeps its type", lookupOui(g6, &c) == kOuiTable[1].type);
        ck("...and takes the new grade", c == Confidence::HIGH_CONF);
        setExtraRules("");
        c = Confidence::LOW_CONF;
        lookupOui(g6, &c);
        ck("an empty set puts every row back", c == kOuiTable[1].conf && lookupUuid(0xFEED) == DetectionType::TILE);
    }

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

    // The set that actually ships: web-flasher/signatures.txt, made into a
    // body the way tools/build_flasher_bins.sh does (comments and SERIAL out,
    // one space between words, ";" between rules). Every line has to parse
    // on the board -- one that does not is silently skipped there.
    suite("the shipped set, web-flasher/signatures.txt");
    {
        FILE* f = fopen("../web-flasher/signatures.txt", "r");
        ck("it is there", f != nullptr);
        std::string body;
        unsigned rules = 0;
        char line[256];
        while (f && fgets(line, sizeof line, f)) {
            std::string w, l;
            for (const char* p = line; ; p++) {
                if (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') { w += *p; continue; }
                if (!w.empty()) { l += (l.empty() ? "" : " ") + w; w.clear(); }
                if (!*p) break;
            }
            if (l.empty() || l[0] == '#' || strncasecmp(l.c_str(), "SERIAL ", 7) == 0) continue;
            body += (body.empty() ? "" : ";") + l;
            rules++;
        }
        if (f) fclose(f);
        ck("under the boards' 2000 bytes", body.size() <= 2000);
        // Empty is allowed (set 6: everything built in); then nothing to parse.
        ck("every rule parses", setExtraRules(body.c_str()) == rules);
        const uint8_t dji[6] = { 0x60, 0x60, 0x1F, 1, 2, 3 };
        ck("a DJI address is a drone", lookupOui(dji) == DetectionType::DRONE);
        ck("TELLO- is a drone", lookupSsid("TELLO-C48BF0") == DetectionType::DRONE);
        setExtraRules("");

        // Nothing in the set repeats the firmware's built-in tables, or
        // itself: an added rule that is already there is a slot (and some of
        // 2000 bytes) spent on nothing -- and a sign the set was written
        // without looking. DROP and GRADE are left out: built-in rows are
        // exactly what they are for.
        suite("the shipped set repeats nothing");
        std::vector<std::string> seen, dupes;
        size_t at = 0;
        while (at <= body.size()) {
            size_t end = body.find(';', at);
            if (end == std::string::npos) end = body.size();
            const std::string r = body.substr(at, end - at);
            at = end + 1;
            char kind[8] = "", key[40] = "";
            if (sscanf(r.c_str(), "%7s %39s", kind, key) != 2) continue;
            if (!strcasecmp(kind, "DROP") || !strcasecmp(kind, "GRADE")) continue;
            std::string id = std::string(kind) + " " + key;
            for (auto& c : id) c = (char)toupper((unsigned char)c);
            bool built = false;
            if (!strcasecmp(kind, "OUI")) {
                unsigned b[3];
                if (sscanf(key, "%x:%x:%x", &b[0], &b[1], &b[2]) == 3) {
                    const uint8_t mac[6] = { (uint8_t)b[0], (uint8_t)b[1], (uint8_t)b[2], 0, 0, 1 };
                    built = lookupOui(mac) != DetectionType::UNKNOWN;
                }
            } else if (!strcasecmp(kind, "SSID")) {
                built = lookupSsid(key) != DetectionType::UNKNOWN;
            } else if (!strcasecmp(kind, "NAME")) {
                built = lookupBtName(key) != DetectionType::UNKNOWN;
            } else if (!strcasecmp(kind, "UUID")) {
                built = lookupUuid((uint16_t)strtoul(key, nullptr, 16)) != DetectionType::UNKNOWN;
            } else if (!strcasecmp(kind, "CID")) {
                built = lookupMfgId((uint16_t)strtoul(key, nullptr, 16)) != DetectionType::UNKNOWN;
            }
            if (built) dupes.push_back(id + " (built in)");
            for (auto& s : seen) if (s == id) dupes.push_back(id + " (twice)");
            seen.push_back(id);
        }
        for (auto& d : dupes) printf("    repeated: %s\n", d.c_str());
        ck("no rule repeats a built-in one or another rule", dupes.empty());
    }
    return report();
}
