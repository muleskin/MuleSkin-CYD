// MuleSkin-CYD — on-device diagnostics screen implementation
#include "ui_diagnostics.h"
#include "clock.h"
#include "theme.h"
#include "detection.h"
#include <Arduino.h>
#include <stdarg.h>

static void backButtonRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    w = 120;
    h = g.h;
    x = (screenW - w) / 2;
    y = g.y;
}

void uiDiagnosticsInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

bool uiDiagnosticsHitBack(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    backButtonRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

static int drawLine(TFT_eSPI& t, int y, uint16_t labelColor, const char* label, const char* fmt, ...) {
    t.setTextColor(labelColor, Theme::BG);
    t.setCursor(6, y);
    t.print(label);

    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(6 + t.textWidth(label) + 6, y);
    t.print(buf);
    return y + t.fontHeight() + 2;
}

void uiDiagnosticsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, const DiagnosticsInfo& info) {
    (void)now;
    int w = t.width(), h = t.height();

    Theme::drawTitleBar(t, ">> DIAGNOSTICS <<");

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    int bodyTop = 16, bodyBottom = bar.y - 4;
    t.fillRect(0, bodyTop, w, bodyBottom - bodyTop, Theme::BG);

    t.setTextSize(1);
    t.setTextWrap(false);
    int y = bodyTop + 2;

    y = drawLine(t, y, Theme::CYAN, "BOARD:", "%s (%s)", info.boardName,
                 info.usingCapTouch ? "capacitive" : "resistive");
    y = drawLine(t, y, Theme::CYAN, "RESET:", "%s", info.resetReason);
    // Only after an actual panic, and only when the breadcrumb survived. A
    // blank line here means a clean boot, not a missing feature.
    if (info.crash.valid || info.crash.haveDump) {
        if (info.crash.valid)
            y = drawLine(t, y, Theme::RED, "LAST CRASH:", "%lum%lus up, %lu free, %lu block",
                         (unsigned long)(info.crash.uptimeMs / 60000),
                         (unsigned long)((info.crash.uptimeMs / 1000) % 60),
                         (unsigned long)info.crash.heapFree,
                         (unsigned long)info.crash.heapBlock);
        else
            y = drawLine(t, y, Theme::RED, "LAST CRASH:", "no breadcrumb survived");
        if (info.crash.haveDump) {
            // Where it died, from the core dump. With these two lines and the
            // ELF of the build that crashed, addr2line names the functions --
            // which is the difference between "it crashed" and a fix.
            y = drawLine(t, y, Theme::RED, "  IN:", "%s @ %08lX%s", info.crash.task,
                         (unsigned long)info.crash.pc, info.crash.dumpOlder ? " (old fw)" : "");
            char bt[40] = "--";
            size_t o = 0;
            for (uint8_t i = 0; i < info.crash.btN && i < 3; i++)
                o += snprintf(bt + o, sizeof bt - o, "%s%08lX", i ? " " : "",
                              (unsigned long)info.crash.bt[i]);
            y = drawLine(t, y, Theme::RED, "  BT:", "%s", bt);
        } else {
            y = drawLine(t, y, Theme::RED, "  ON:", "screen %u, %lu detections",
                         (unsigned)info.crash.screen, (unsigned long)info.crash.lifetime);
        }
    }
    // Uptime next to the reset reason on purpose: together they answer
    // "did this thing restart on me", which is one question and not two.
    {
        char up[24];
        Clock::formatUptime(up, sizeof(up));
        y = drawLine(t, y, Theme::CYAN, "UPTIME:", "%s", up);
        char clk[32];
        Clock::formatClock(clk, sizeof(clk));
        y = drawLine(t, y, Theme::CYAN, "CLOCK:", "%s%s", clk,
                     Clock::trusted() ? "" : "  (TIME <epoch> over serial)");
    }
    // Full at boot, the store is erased whole -- see main.cpp nvsFullPercent().
    y = drawLine(t, y, info.nvsPercent >= 75 ? Theme::AMBER : Theme::CYAN, "SETTINGS:", "%u%% of the store%s",
                 (unsigned)info.nvsPercent, info.nvsPercent >= 75 ? " - BACK UP" : "");
    y = drawLine(t, y, info.pushWarn ? Theme::AMBER : Theme::CYAN, "PUSH:", "%s", info.push);
    y = drawLine(t, y, info.wifiWarn ? Theme::AMBER : Theme::CYAN, "WIFI:", "%s", info.wifiJoin);
    y = drawLine(t, y, Theme::CYAN, "HEAP:", "%lu free / %lu largest",
                 (unsigned long)info.freeHeap, (unsigned long)info.largestBlock);
    // Where the heap went on the way up, in KB: free/largest with WiFi up,
    // with Bluetooth up, and at the first pass of loop().
    {
        const BootHeap bh = bootHeap();
        y = drawLine(t, y, Theme::CYAN, "BOOT:", "wifi %lu/%lu  ble %lu/%lu  loop %lu/%lu KB",
                     (unsigned long)(bh.wifiFree / 1024), (unsigned long)(bh.wifiLargest / 1024),
                     (unsigned long)(bh.bleFree / 1024),  (unsigned long)(bh.bleLargest / 1024),
                     (unsigned long)(info.loopFree / 1024), (unsigned long)(info.loopLargest / 1024));
    }
    y = drawLine(t, y, Theme::CYAN, "SLOT:", "%s  other: %s",
                 info.otaSlot ? info.otaSlot : "?", info.otaOther ? info.otaOther : "none");
    // The black box: what is kept in flash across restarts. The newest crash
    // kept by date and version, which is what a photo of this screen needs
    // to match it to a build; BLACKBOX on the console has the rest.
    if (!info.bbReady) {
        y = drawLine(t, y, Theme::CYAN, "KEPT:", "off (flash space in use)");
    } else if (!info.bbCrashes) {
        y = drawLine(t, y, Theme::CYAN, "KEPT:", "%u seen, no crashes", (unsigned)info.bbKept);
    } else {
        char when[12] = "";
        if (info.bbHaveLast) Clock::formatEpochStamp(info.bbLast.epoch, when, sizeof when);
        // The version only where it fits: portrait is forty characters.
        const bool wide = w >= 300;
        y = drawLine(t, y, Theme::RED, "KEPT:", "%u seen, %u crash%s, last %s%s%s",
                     (unsigned)info.bbKept, (unsigned)info.bbCrashes, info.bbCrashes == 1 ? "" : "es",
                     when, wide ? " v" : "", wide ? info.bbLast.version : "");
    }
    y += 4;

    if (info.hasRaw) {
        y = drawLine(t, y, Theme::VAPOR_PURPLE, "RAW TOUCH:", "%s a=%d b=%d",
                     info.rawTouching ? "DOWN" : "up", info.rawA, info.rawB);
    }
    y = drawLine(t, y, Theme::VAPOR_PURPLE, "MAPPED:", "%s x=%d y=%d",
                 info.touchValid ? "valid" : "--", info.mappedX, info.mappedY);
    y += 4;

    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL SOURCE:", "%s",
                 info.calSource ? info.calSource
                                : info.usingSavedCal ? "saved" : "compiled-in default");
    y = drawLine(t, y, Theme::VAPOR_PINK, "CAL RANGE:", "A[%d,%d] B[%d,%d]",
                 info.calA0, info.calA1, info.calB0, info.calB1);
    y += 4;

    // Frame cost. The push is a fixed byte count over SPI, so it moves
    // only if the bus clock does -- kept as its own figure so a change to
    // SPI_FREQUENCY shows up here unambiguously instead of being averaged
    // into one "it feels smoother" number. fps is derived from the whole
    // frame, not the push alone. On one line with FRAME since the crash
    // report grew a line: the screen already ran into the BACK button.
    {
        uint32_t fus = info.frameUs ? info.frameUs : 1;
        y = drawLine(t, y, Theme::AMBER, "FRAME:", "%lu.%lu ms (%lu fps), push %lu.%lu",
                     (unsigned long)(fus / 1000),
                     (unsigned long)((fus % 1000) / 100),
                     (unsigned long)(1000000UL / fus),
                     (unsigned long)(info.pushUs / 1000),
                     (unsigned long)((info.pushUs % 1000) / 100));
        // FRAME above is this screen, which has no backdrop. LAST is the
        // screen you came from -- its whole frame, which is the frame rate
        // you actually saw there -- and the part of it its background took.
        if (info.lastScreenName && info.lastScreenUs) {
            const uint32_t lus = info.lastScreenUs;
            y = drawLine(t, y, Theme::AMBER, "LAST:", "%s %lu.%lu ms (%lu fps), bg %lu.%lu",
                         info.lastScreenName,
                         (unsigned long)(lus / 1000), (unsigned long)((lus % 1000) / 100),
                         (unsigned long)(1000000UL / lus),
                         (unsigned long)(info.bgUs / 1000), (unsigned long)((info.bgUs % 1000) / 100));
        } else {
            y = drawLine(t, y, Theme::AMBER, "LAST:", "no screen timed yet, bg %lu.%lu",
                         (unsigned long)(info.bgUs / 1000), (unsigned long)((info.bgUs % 1000) / 100));
        }
    }
    y += 4;

    y = drawLine(t, y, Theme::GREEN, "LOG:", "%u entries, %lu lifetime",
                 (unsigned)eng.logCount(), (unsigned long)eng.lifetimeTotal());


    int bx, by, bw, bh;
    backButtonRect(w, h, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "[ BACK ]", false);
}
