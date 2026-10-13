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

    suite("what is not an advert");
    TrackerVerdict v;
    ck("nothing", !trackerVerdict(nullptr, 0, v));
    const uint8_t broken[] = { 0x1E, 0xFF, 0x4C };          // a length running past the end
    ck("a structure longer than the advert", !trackerVerdict(broken, sizeof broken, v));
    return report();
}
