// MuleSkin-CYD — a retired background stays retired.
//
// What this guards: the background is saved to NVS as a raw byte, so
// WIREFRAME TUNNEL could not be deleted from the middle of the enum
// without moving every board that had SYNTHWAVE or BLACK saved onto
// something else. The number therefore survives its background. Two
// things have to hold for that to be safe, and neither is visible by
// reading the enum: the picker must never hand it out again, and a
// board that already had it saved must come up somewhere sensible
// rather than on a band nothing paints.
#include "test_util.h"
#include "settings.h"
#include "theme.h"
#include "clock.h"
#include <Preferences.h>
#include <cstdlib>

// The two things settings.cpp reaches for that are not under test.
namespace Theme { void applyPalette(uint8_t) {} }
namespace Clock {
uint8_t     zoneCount()        { return 1; }
uint8_t     zoneStep(uint8_t, int) { return 0; }
const char* zoneName(uint8_t)  { return "UTC"; }
void        applyZone(uint8_t) {}
}

using Settings::Background;

int main() {
    // A real file behind the store, so "a restart" means what it says.
    setenv("MULESKINSIM_NVS", "out", 1);
    remove("out/settings.nvs");

    Settings::load();

    suite("The picker never lands on it");
    // Forward and back, further than a full lap each way: the ring skips
    // anything unselectable, and one lap of each direction would miss a
    // skip that only fires from one side.
    bool seenFwd = false, seenBack = false;
    for (int i = 0; i < Settings::BACKGROUND_COUNT * 2; i++) {
        Settings::cycleBackground();
        if (Settings::background() == Background::TUNNEL) seenFwd = true;
    }
    for (int i = 0; i < Settings::BACKGROUND_COUNT * 2; i++) {
        Settings::cyclePrevBackground();
        if (Settings::background() == Background::TUNNEL) seenBack = true;
    }
    ck("two laps forward never reach it", !seenFwd);
    ck("two laps back never reach it",    !seenBack);
    ck("and it says so when asked",       !Settings::backgroundSelectable(Background::TUNNEL));

    suite("RADAR is the only background on offer");
    // The skip is a `continue` in a bounded loop: with one entry left the
    // failure mode to rule out is a spin or a step off it, not a stuck ring.
    Settings::cycleBackground();
    ck("cycling forward stays on RADAR", Settings::background() == Background::RADAR);
    Settings::cyclePrevBackground();
    ck("and so does cycling back", Settings::background() == Background::RADAR);
    {
        int offered = 0;
        for (uint8_t v = 0; v < Settings::BACKGROUND_COUNT; v++)
            if (Settings::backgroundSelectable((Background)v)) offered++;
        ck("exactly one is selectable", offered == 1 && Settings::backgroundSelectable(Background::RADAR));
    }

    suite("A board that had it saved");
    // Written the way an older firmware wrote it -- straight to the key,
    // behind the API that now refuses the value.
    {
        Preferences p;
        p.begin("settings", false);
        p.putUChar("bg", (uint8_t)Background::TUNNEL);
        p.end();
    }
    Settings::load();                       // as a reboot does
    ck("it does not come back", Settings::background() != Background::TUNNEL);
    ck("and it lands on something the picker offers",
       Settings::backgroundSelectable(Settings::background()));

    suite("Every saved background boots to RADAR");
    // Whatever an older firmware saved -- any of the old choices, or a byte
    // past the end of the enum -- the board comes up on the one there is.
    for (uint8_t v = 0; v <= Settings::BACKGROUND_COUNT; v++) {
        Preferences p;
        p.begin("settings", false);
        p.putUChar("bg", v);
        p.end();
        Settings::load();
        if (Settings::background() != Background::RADAR) {
            ck("a saved background did not move to RADAR", false);
            break;
        }
    }
    ck("all of them land on RADAR", true);

    suite("MULESKIN gave the main screen to RADAR");
    {
        Preferences p;
        p.begin("settings", false);
        p.putUChar("bg", (uint8_t)Background::MULESKIN);
        p.end();
        Settings::load();
        ck("a board saved on MULESKIN boots to RADAR", Settings::background() == Background::RADAR);
        ck("and it is not offered any more", !Settings::backgroundSelectable(Background::MULESKIN));
    }

    return report();
}
