// MuleSkin-CYD — WIFI TIME. See include/ui_timesync.h.
//
// One screen for the whole job: which network it is joining, then that it is
// asking a time server, then the answer -- the time itself, big, once it is
// set. Nothing here is tappable until the job is over: the WiFi task cannot be
// stopped half way through a join, so BACK only appears when there is nothing
// left to wait for.
#include "ui_timesync.h"
#include "theme.h"
#include "ota_wifi.h"
#include "clock.h"
#include <stdio.h>

static void backRect(int w, int h, int& x, int& y, int& bw, int& bh) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(w, h);
    x = g.x[1]; y = g.y; bw = g.w[1]; bh = g.h;
}

static void centred(TFT_eSPI& t, int w, int y, uint16_t col, const char* s) {
    t.setTextColor(col, Theme::BG);
    t.setCursor((w - t.textWidth(s)) / 2, y);
    t.print(s);
}

void uiTimeSyncInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiTimeSyncTick(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawListHeading(t, "WIFI TIME", Theme::VAPOR_PINK);
    t.setTextSize(1);
    const int y = Theme::LIST_TOP + Theme::LIST_HEADING_H + 12;
    char line[64];
    const char* net = OtaWifi::timeSyncNetwork();
    // Three dots that take turns, so a slow join still looks alive.
    char dots[4] = "...";
    for (int i = 0; i < 3; i++) if ((int)((now / 300) % 4) <= i) dots[i] = ' ';

    switch (OtaWifi::timeSyncState()) {
        case OtaWifi::TimeSync::JOINING:
            if (net[0]) snprintf(line, sizeof line, "Joining %s", net);
            else        snprintf(line, sizeof line, "Looking for your network");
            centred(t, w, y, Theme::WHITE, line);
            centred(t, w, y + 14, Theme::W95_LIGHT, "Detection pauses while the radio is busy.");
            t.setTextSize(3);
            centred(t, w, y + 50, Theme::CYAN, dots);
            break;
        case OtaWifi::TimeSync::ASKING:
            snprintf(line, sizeof line, "Joined %s", net);
            centred(t, w, y, Theme::WHITE, line);
            centred(t, w, y + 14, Theme::W95_LIGHT, "Asking a time server for the time.");
            t.setTextSize(3);
            centred(t, w, y + 50, Theme::CYAN, dots);
            break;
        case OtaWifi::TimeSync::DONE: {
            centred(t, w, y, Theme::GREEN, "Time set from the network.");
            char hm[8], tm[12], dt[24];
            bool pm = false;
            Clock::formatTime(hm, sizeof hm, true, &pm);
            snprintf(tm, sizeof tm, "%s %s", hm, pm ? "PM" : "AM");
            Clock::formatDate(dt, sizeof dt);
            t.setTextSize(4);
            if (t.textWidth(tm) > w - 8) t.setTextSize(3);   // 240-wide rotation
            centred(t, w, y + 30, Theme::CYAN, tm);
            t.setTextSize(2);
            centred(t, w, y + 72, Theme::WHITE, dt);
            t.setTextSize(1);
            centred(t, w, y + 98, Theme::W95_LIGHT, "It shows at the top of the main screen.");
            break;
        }
        case OtaWifi::TimeSync::NO_JOIN:
            snprintf(line, sizeof line, "Could not join %s.", net[0] ? net : "a saved network");
            centred(t, w, y, Theme::AMBER, line);
            centred(t, w, y + 14, Theme::W95_LIGHT, "Out of range, or the password changed?");
            centred(t, w, y + 28, Theme::W95_LIGHT, "SETTINGS > SYSTEM > WIFI NETWORKS");
            break;
        case OtaWifi::TimeSync::NO_ANSWER:
            snprintf(line, sizeof line, "Joined %s, but", net);
            centred(t, w, y, Theme::AMBER, line);
            centred(t, w, y + 14, Theme::AMBER, "no time server answered.");
            centred(t, w, y + 28, Theme::W95_LIGHT, "Does that network reach the internet?");
            break;
        case OtaWifi::TimeSync::NO_SAVED:
            centred(t, w, y, Theme::AMBER, "No WiFi network saved.");
            centred(t, w, y + 14, Theme::W95_LIGHT, "SETTINGS > SYSTEM > WIFI NETWORKS");
            break;
        case OtaWifi::TimeSync::NO_MEMORY:
            centred(t, w, y, Theme::AMBER, "Not enough memory to start.");
            centred(t, w, y + 14, Theme::W95_LIGHT, "Try again in a moment.");
            break;
        default:
            break;
    }
    t.setTextSize(1);

    if (!OtaWifi::timeSyncBusy()) {
        int bx, by, bw, bh;
        backRect(w, h, bx, by, bw, bh);
        Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
    }
}

bool uiTimeSyncHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x < bx + bw && y >= by - 6 && y < by + bh + 6;
}
