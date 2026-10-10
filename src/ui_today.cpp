// MuleSkin-CYD — TODAY. See include/ui_today.h.
#include "ui_today.h"
#include "theme.h"
#include "state.h"
#include "clock.h"
#include "blackbox.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static Today::Stats s_stats;
static uint32_t     s_countedAt = 0;
static bool         s_haveDay   = false;
Today::Stats& Today::current() { return s_stats; }
static void count();
void Today::recount() { count(); }

static void count() {
    Today::clear(s_stats);
    s_haveDay = Clock::trusted();
    if (!s_haveDay) return;
    const time_t nowT = (time_t)Clock::nowEpoch();
    struct tm today;
    localtime_r(&nowT, &today);
    struct Ctx { int year, yday; };
    Ctx ctx = { today.tm_year, today.tm_yday };
    // Newest first: once a record is from before today, everything after it is
    // older still, so the walk can stop there.
    BlackBox::forEachDetection([](const BlackBox::DetRecord& r, void* p) {
        const Ctx& c = *(const Ctx*)p;
        if (!r.epoch) return true;                    // clock not set then
        const time_t e = (time_t)r.epoch;
        struct tm lt;
        localtime_r(&e, &lt);
        if (lt.tm_year < c.year || (lt.tm_year == c.year && lt.tm_yday < c.yday)) return false;
        Today::add(s_stats, lt.tm_year, lt.tm_yday, lt.tm_hour, r.type, r.epoch, c.year, c.yday);
        return true;
    }, &ctx);
    s_countedAt = millis();
}

static void backRect(int w, int h, int& x, int& y, int& bw, int& bh) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(w, h);
    x = g.x[1]; y = g.y; bw = g.w[1]; bh = g.h;
}

void uiTodayInit(TFT_eSPI& t) {
    count();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static void hhmm(uint32_t epoch, char* out, size_t n) {
    const time_t e = (time_t)epoch;
    struct tm lt;
    localtime_r(&e, &lt);
    int h = lt.tm_hour % 12;
    if (!h) h = 12;
    snprintf(out, n, "%d:%02d %s", h, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

void uiTodayTick(TFT_eSPI& t, uint32_t now) {
    if (now - s_countedAt > 30000) count();
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawListHeading(t, "TODAY", Theme::CYAN);
    int bx, by, bw, bh;
    backRect(w, h, bx, by, bw, bh);
    t.setTextSize(1);

    if (!s_haveDay) {
        const char* a = "The clock isn't set, so there is no today.";
        const char* b = "Tap WIFI TIME on the main screen first.";
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor((w - t.textWidth(a)) / 2, h / 2 - 12); t.print(a);
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        t.setCursor((w - t.textWidth(b)) / 2, h / 2 + 2);  t.print(b);
        Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
        return;
    }

    // The date and the total, under the heading.
    char line[64], date[24];
    Clock::formatDate(date, sizeof date);
    const int top = Theme::LIST_TOP + Theme::LIST_HEADING_H + 6;
    snprintf(line, sizeof line, "%s  -  %u sighting%s", date, (unsigned)s_stats.total, s_stats.total == 1 ? "" : "s");
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor((w - t.textWidth(line)) / 2, top);
    t.print(line);

    // A bar per hour. The current hour is drawn brighter.
    const int chartL = 14, chartR = w - 8;
    const int chartTop = top + 16, chartBot = by - 70 > chartTop + 40 ? by - 70 : chartTop + 40;
    uint16_t peak = 1;
    for (int i = 0; i < 24; i++) if (s_stats.perHour[i] > peak) peak = s_stats.perHour[i];
    const int slot = (chartR - chartL) / 24, barW = slot > 3 ? slot - 2 : slot;
    const int nowHour = Clock::hour();
    for (int i = 0; i < 24; i++) {
        const int x = chartL + i * slot;
        const int bhgt = s_stats.perHour[i] ? 1 + (chartBot - chartTop - 1) * s_stats.perHour[i] / peak : 0;
        if (bhgt) t.fillRect(x, chartBot - bhgt, barW, bhgt, i == nowHour ? Theme::VAPOR_YELLOW : Theme::CYAN);
        else      t.drawFastHLine(x, chartBot - 1, barW, Theme::W95_DKSHADOW);
    }
    t.drawFastHLine(chartL, chartBot, chartR - chartL, Theme::W95_LIGHT);
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    static const char* const LBL[] = { "12A", "6A", "12P", "6P" };
    for (int k = 0; k < 4; k++) { t.setCursor(chartL + k * 6 * slot, chartBot + 3); t.print(LBL[k]); }
    snprintf(line, sizeof line, "peak %u", (unsigned)peak);
    t.setCursor(chartR - t.textWidth(line), chartTop - 10 > top + 8 ? chartTop - 10 : top + 8);
    if (s_stats.total) t.print(line);

    // The types seen most, in their own colours.
    int y = chartBot + 16, x = 6;
    if (!s_stats.total) {
        const char* none = "Nothing yet today.";
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        t.setCursor((w - t.textWidth(none)) / 2, y + 4);
        t.print(none);
    } else {
        bool used[32] = { false };
        for (int shown = 0; shown < 6; shown++) {
            int best = -1;
            for (int ty = 1; ty < (int)DetectionType::COUNT && ty < 32; ty++)
                if (!used[ty] && s_stats.perType[ty] && (best < 0 || s_stats.perType[ty] > s_stats.perType[best])) best = ty;
            if (best < 0) break;
            used[best] = true;
            snprintf(line, sizeof line, "%s %u", detectionTypeName((DetectionType)best), (unsigned)s_stats.perType[best]);
            const int tw = t.textWidth(line);
            if (x + tw > w - 6) { x = 6; y += 11; }
            t.setTextColor(Theme::colorFor((DetectionType)best), Theme::BG);
            t.setCursor(x, y);
            t.print(line);
            x += tw + 12;
        }
        char a[12], b[12];
        hhmm(s_stats.firstEpoch, a, sizeof a);
        hhmm(s_stats.lastEpoch, b, sizeof b);
        snprintf(line, sizeof line, "first %s   last %s", a, b);
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        t.setCursor((w - t.textWidth(line)) / 2, by - 14);
        t.print(line);
    }
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}

bool uiTodayHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x < bx + bw && y >= by - 6 && y < by + bh + 6;
}
