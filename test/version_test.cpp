// MuleSkin-CYD — release versions compare and print the way the boards need.
//
// What this guards: a board only announces an update when the site's version
// is NEWER than its own (OtaCore::noteAvailable), and both sides have tripped
// on format before -- the manifests said "03.01" (two parts, never newer than
// anything) and untagged builds report a bare commit hash. And the splash
// shows the tag as "V.x.y.z" whatever tail git describe put on it.
#include "test_util.h"
#include "version_cmp.h"
#include <cstring>

int main() {
    suite("Version::newer");
    ck("3.1.3 is newer than 3.1.2", Version::newer("3.1.3", "3.1.2"));
    ck("3.1.2 is not newer than 3.1.3", !Version::newer("3.1.2", "3.1.3"));
    ck("equal is not newer", !Version::newer("3.1.2", "3.1.2"));
    ck("numbers, not text: 3.1.10 beats 3.1.9", Version::newer("3.1.10", "3.1.9"));
    ck("a minor bump beats any patch", Version::newer("3.2.0", "3.1.99"));
    ck("a leading v on either side", Version::newer("v3.1.3", "3.1.2") && Version::newer("3.1.3", "V3.1.2"));
    ck("a describe tail counts as its tag", !Version::newer("3.1.2", "v3.1.2-8-gf03ba9d"));
    ck("...so the next release beats it", Version::newer("3.1.3", "v3.1.2-8-gf03ba9d"));
    ck("-dirty counts as its tag", !Version::newer("3.1.2", "v3.1.2-dirty"));
    ck("two parts (\"03.01\") is never newer", !Version::newer("03.01", "3.1.0"));
    ck("...and nothing is newer than it", !Version::newer("9.9.9", "03.01"));
    ck("a bare hash is not a version", !Version::newer("3.1.3", "8e4b088"));
    ck("nullptr is not a version", !Version::newer(nullptr, "3.1.2"));

    suite("Version::label (the splash subtitle)");
    char b[24];
    Version::label(b, sizeof b, "v3.1.2");               ck("v3.1.2 -> V.3.1.2", strcmp(b, "V.3.1.2") == 0);
    Version::label(b, sizeof b, "v3.1.1-8-gf03ba9d");    ck("the describe tail is left off", strcmp(b, "V.3.1.1") == 0);
    Version::label(b, sizeof b, "v3.1.2-dirty");         ck("so is -dirty", strcmp(b, "V.3.1.2") == 0);
    Version::label(b, sizeof b, "3.10.0");               ck("no v, two digits", strcmp(b, "V.3.10.0") == 0);
    Version::label(b, sizeof b, "8e4b088");              ck("an untagged hash comes through", strcmp(b, "8e4b088") == 0);
    Version::label(b, sizeof b, "f03ba9d-dirty-and-more"); ck("...cut to 12", strcmp(b, "f03ba9d-dirt") == 0);
    Version::label(b, 6, "v3.1.2");                      ck("a short buffer is not overrun", strlen(b) == 5);

    return report();
}
