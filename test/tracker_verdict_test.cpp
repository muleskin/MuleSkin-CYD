// MuleSkin-CYD — trackers from their raw advertisements (trackerVerdict).
//
// What this guards: the step that turns a Bluetooth advert, as the radio
// received it, into "which tracker, away from its owner or not, how sure".
// The detector calls this same function, so these are the board's decisions.
// test/tracker_adverts.txt holds the adverts: real ones captured from AirTags
// on the bench, and hand-built ones for the other families from their
// published layouts. Each line names the verdict it must get.
#include "test_util.h"
#include "signatures.h"
#include <cstdio>
#include <cstring>
#include <string>

static DetectionType typeNamed(const char* s) {
    if (!strcmp(s, "AIRTAG"))      return DetectionType::AIRTAG;
    if (!strcmp(s, "GOOGLE_TAG"))  return DetectionType::GOOGLE_TAG;
    if (!strcmp(s, "SAMSUNG_TAG")) return DetectionType::SAMSUNG_TAG;
    if (!strcmp(s, "TILE"))        return DetectionType::TILE;
    return DetectionType::UNKNOWN;
}

static Confidence confNamed(const char* s) {
    return !strcmp(s, "HIGH") ? Confidence::HIGH_CONF : !strcmp(s, "MED") ? Confidence::MED_CONF : Confidence::LOW_CONF;
}

int main() {
    suite("captured and built adverts, test/tracker_adverts.txt");
    FILE* f = fopen("tracker_adverts.txt", "r");
    ck("the file is there", f != nullptr);
    char line[512];
    unsigned n = 0, wrong = 0;
    while (f && fgets(line, sizeof line, f)) {
        // Strip the comment, then: TYPE AWAY CONF HEX (the hex may be split by spaces).
        if (char* c = strchr(line, '#')) *c = '\0';
        char type[16], away[4], conf[8];
        int used = 0;
        if (sscanf(line, "%15s %3s %7s %n", type, away, conf, &used) != 3) continue;
        std::string hex;
        for (const char* p = line + used; *p; p++)
            if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) hex += *p;
        uint8_t adv[64];
        uint8_t len = 0;
        for (size_t i = 0; i + 1 < hex.size() && len < sizeof adv; i += 2)
            adv[len++] = (uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16);
        TrackerVerdict v;
        const bool got = trackerVerdict(adv, len, v);
        bool ok;
        if (!strcmp(type, "NONE")) ok = !got;
        else ok = got && v.type == typeNamed(type) && v.away == (away[0] == '1') && v.conf == confNamed(conf);
        if (!ok) {
            wrong++;
            printf("    wrong: %s %s %s %s -> %s\n", type, away, conf, hex.c_str(),
                   got ? (v.away ? "a tracker, away" : "a tracker, not away") : "not a tracker");
        }
        n++;
    }
    if (f) fclose(f);
    char what[64];
    snprintf(what, sizeof what, "all %u adverts get their verdict", n);
    ck(what, n >= 14 && wrong == 0);

    suite("what the advert says about the owner");
    {
        auto ownerOf = [](const char* hex) {
            uint8_t adv[64]; uint8_t len = 0;
            for (size_t i = 0; hex[i] && hex[i + 1] && len < sizeof adv; i += 2) {
                char b[3] = { hex[i], hex[i + 1], 0 };
                adv[len++] = (uint8_t)strtoul(b, nullptr, 16);
            }
            TrackerVerdict v;
            return trackerVerdict(adv, len, v) ? (int)v.owner : -1;
        };
        ck("a separated AirTag is AWAY",
           ownerOf("1eff4c001219201e0d170d9e43033f8263145ac325a09835de70b93f59007e") == OWNER_AWAY);
        ck("'maintained' set is WITH its owner",
           ownerOf("1eff4c00121934b1cf5dbe4ec79616334f50cbd62ffe6b13924a9be37d0087") == OWNER_WITH);
        ck("the nearby frame is WITH its owner", ownerOf("02011a07ff4c0012020000") == OWNER_WITH);
        ck("an AirTag's pairing block is NOT SET UP",
           ownerOf("1eff4c00071905005510000001171700112233445566778899aabbccddeeff") == OWNER_UNSET);
        ck("AirPods are not a tracker at all",
           ownerOf("1eff4c000719010220752b0011223344556677889900aabbccddeeff00112233") == -1);
        ck("Find Hub 0x41 is AWAY", ownerOf("0201061916aafe41000102030405060708090a0b0c0d0e0f1011121300") == OWNER_AWAY);
        ck("Find Hub 0x40 is WITH", ownerOf("0201061916aafe40000102030405060708090a0b0c0d0e0f1011121300") == OWNER_WITH);
        ck("a SmartTag connected to its owner is WITH",
           ownerOf("15165afdb5000102030405060708090a0b0c0d0e0f10111213") == OWNER_WITH);
        ck("Tile says nothing", ownerOf("0303edfe") == OWNER_UNKNOWN);
    }

    suite("what is not an advert");
    TrackerVerdict v;
    ck("nothing", !trackerVerdict(nullptr, 0, v));
    const uint8_t broken[] = { 0x1E, 0xFF, 0x4C };          // a length running past the end
    ck("a structure longer than the advert", !trackerVerdict(broken, sizeof broken, v));
    return report();
}
