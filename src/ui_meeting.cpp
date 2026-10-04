// MuleSkin-CYD — IN A MEETING. See include/ui_meeting.h.
//
// A sign, not a screen with anything going on: solid red, IN A / MEETING in
// the headline face at its XL size in white, and BACK where the bar's middle
// button sits on every other screen. The lettering is drawn steady -- the
// headline face's glitch bursts are style on the main screen and a fault on a
// sign somebody is meant to read from across a room.
//
// Detection keeps running behind it, and keeps logging; nothing takes the
// screen over while the sign is up (main.cpp's MEETING case checks for no
// alerts). The power saver leaves it lit.
#include "ui_meeting.h"
#include "theme.h"

static const uint16_t SIGN_RED = 0xF800;   // full red, not the theme's: it is a sign

static void backRect(int w, int h, int& x, int& y, int& bw, int& bh) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(w, h);
    x = g.x[1]; y = g.y; bw = g.w[1]; bh = g.h;
}

void uiMeetingInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), SIGN_RED);
}

void uiMeetingTick(TFT_eSPI& t, uint32_t now) {
    (void)now;
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, SIGN_RED);

    int bx, by, bw, bh;
    backRect(w, h, bx, by, bw, bh);

    // Two lines, centred in the space above the button.
    static const char* L1 = "IN A";
    static const char* L2 = "MEETING";
    const int lineH = 38, gap = 14;                  // XL's cap height, and air between
    const int blockH = lineH * 2 + gap;
    const int top = (by - blockH) / 2;
    const int w1 = Theme::bangersTextWidth(L1, Theme::BangersSize::XL);
    const int w2 = Theme::bangersTextWidth(L2, Theme::BangersSize::XL);
    Theme::drawBangersSteady(t, (w - w1) / 2, top, L1, Theme::WHITE, Theme::BangersSize::XL);
    Theme::drawBangersSteady(t, (w - w2) / 2, top + lineH + gap, L2, Theme::WHITE, Theme::BangersSize::XL);

    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}

bool uiMeetingHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x < bx + bw && y >= by - 6 && y < by + bh + 6;
}
