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
// alerts), the status light and buzzers stay quiet, and the power saver
// leaves it lit. A tap on the sign sets a timer -- BACK IN 15 / 30 / 60 MIN
// -- and the sign takes itself down when it runs out.
#include "ui_meeting.h"
#include "theme.h"

static const uint16_t SIGN_RED = 0xF800;   // full red, not the theme's: it is a sign
static const uint16_t SIGN_PINK = 0xFC10;  // the hint line: readable on red, quieter than white

static const uint8_t TIMER_MIN[] = { 0, 15, 30, 60 };
static uint8_t  s_timerIdx = 0;
static uint32_t s_timerEnd = 0;            // millis() the timer runs out; 0 = none

static void backRect(int w, int h, int& x, int& y, int& bw, int& bh) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(w, h);
    x = g.x[1]; y = g.y; bw = g.w[1]; bh = g.h;
}

void uiMeetingInit(TFT_eSPI& t) {
    s_timerIdx = 0;
    s_timerEnd = 0;
    t.fillRect(0, 0, t.width(), t.height(), SIGN_RED);
}

void uiMeetingCycleTimer(uint32_t now) {
    s_timerIdx = (uint8_t)((s_timerIdx + 1) % sizeof TIMER_MIN);
    s_timerEnd = TIMER_MIN[s_timerIdx] ? now + TIMER_MIN[s_timerIdx] * 60000u : 0;
    if (s_timerEnd == 0 && TIMER_MIN[s_timerIdx]) s_timerEnd = 1;
}

bool uiMeetingTimerDone(uint32_t now) {
    return s_timerEnd && (int32_t)(now - s_timerEnd) >= 0;
}

void uiMeetingTick(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, SIGN_RED);

    int bx, by, bw, bh;
    backRect(w, h, bx, by, bw, bh);

    // Two lines, centred in the space above the button.
    static const char* L1 = "IN A";
    static const char* L2 = "MEETING";
    const int lineH = 38, gap = 14;                  // XL's cap height, and air between
    const int timerH = 30;                           // the BACK IN line under the words
    const int blockH = lineH * 2 + gap + timerH;
    const int top = (by - blockH) / 2;
    const int w1 = Theme::bangersTextWidth(L1, Theme::BangersSize::XL);
    const int w2 = Theme::bangersTextWidth(L2, Theme::BangersSize::XL);
    Theme::drawBangersSteady(t, (w - w1) / 2, top, L1, Theme::WHITE, Theme::BangersSize::XL);
    Theme::drawBangersSteady(t, (w - w2) / 2, top + lineH + gap, L2, Theme::WHITE, Theme::BangersSize::XL);

    // The timer, or how to set one.
    char line[32];
    if (s_timerEnd) {
        const uint32_t left = (int32_t)(s_timerEnd - now) > 0 ? s_timerEnd - now : 0;
        const uint32_t mins = (left + 59999u) / 60000u;
        if (mins <= 1) snprintf(line, sizeof line, "BACK IN 1 MIN");
        else           snprintf(line, sizeof line, "BACK IN %lu MIN", (unsigned long)mins);
        t.setTextSize(2);
        t.setTextColor(Theme::WHITE, SIGN_RED);
    } else {
        snprintf(line, sizeof line, "TAP THE SIGN FOR A TIMER");
        t.setTextSize(1);
        t.setTextColor(SIGN_PINK, SIGN_RED);
    }
    const int ty = top + lineH * 2 + gap + 12;
    t.setCursor((w - t.textWidth(line)) / 2, ty);
    t.print(line);
    t.setTextSize(1);

    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}

bool uiMeetingHitSign(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backRect(screenW, screenH, bx, by, bw, bh);
    return x >= 0 && x < screenW && y >= 0 && y < by - 10;
}

bool uiMeetingHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x < bx + bw && y >= by - 6 && y < by + bh + 6;
}
