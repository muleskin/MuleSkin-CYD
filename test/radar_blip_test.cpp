// MuleSkin-CYD — radar blips land where they should.
//
// What this guards: a blip is the main screen's only picture of what is
// around, and it is also a tap target. Its place has to be stable (a device
// that jumps around the scope frame to frame reads as several), its distance
// has to follow the signal the right way round (stronger is closer to the
// middle), and the table-driven trig has to put north up and east right.
#include "test_util.h"
#include "radar_blip.h"
#include <cstdlib>

using namespace RadarBlip;

int main() {
    suite("bearing");
    const uint8_t a[6] = { 0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03 };
    const uint8_t b[6] = { 0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x04 };
    ck("the same address, the same bearing", bearing(a) == bearing(a));
    ck("a one-bit change moves it", bearing(a) != bearing(b));
    // Spread: 256 sequential addresses should not pile onto a few bearings.
    int used[256] = { 0 }, distinct = 0;
    for (int i = 0; i < 256; i++) {
        uint8_t m[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)i };
        if (!used[bearing(m)]++) distinct++;
    }
    ck("256 neighbours cover over 140 bearings", distinct > 140);

    suite("radius from signal");
    ck("-35 dBm sits at 12% of the rim", radius256(-35) == 31);
    ck("stronger than -35 stays there", radius256(-20) == 31);
    ck("-100 dBm is at the rim", radius256(-100) == 256);
    ck("weaker than -100 stays at the rim", radius256(-120) == 256);
    ck("stronger is closer to the middle", radius256(-50) < radius256(-70) && radius256(-70) < radius256(-90));

    suite("point (screen space, y down)");
    int x, y;
    point(0, 100, 160, 120, x, y);   ck("bearing 0 is straight up", x == 160 && y == 20);
    point(64, 100, 160, 120, x, y);  ck("64 is to the right", x == 260 && y == 120);
    point(128, 100, 160, 120, x, y); ck("128 is straight down", x == 160 && y == 220);
    point(192, 100, 160, 120, x, y); ck("192 is to the left", x == 60 && y == 120);
    point(32, 100, 0, 0, x, y);      ck("32 is up and right at 45 degrees", x == 71 && y == -71);
    bool onCircle = true;
    for (int bb = 0; bb < 256; bb++) {
        point((uint8_t)bb, 90, 0, 0, x, y);
        const int r2 = x * x + y * y;
        if (r2 < 89 * 89 || r2 > 91 * 91) onCircle = false;
    }
    ck("every bearing lands on the circle (+/-1 px)", onCircle);

    suite("paint");
    ck("full as the arm crosses", paint(0) == 255);
    ck("a third a turn later", paint(255) == 85);
    ck("fading in between", paint(10) > paint(100) && paint(100) > paint(200));

    return report();
}
