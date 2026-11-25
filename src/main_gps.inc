// MuleSkin-CYD -- part of src/main.cpp, included where it stood:
// GPS and wardriving: the T-Watch S3 Plus's GNSS and WiGLE export, and the
// ATGM336H on the cyd-gps / cyd-ili9341-gps builds.
//
// One translation unit with main.cpp: this uses the globals and helpers
// declared above its #include there, and what follows it uses this, so
// it is not compiled on its own and its place in main.cpp matters.

#if defined(TWATCH_S3)
// ---- GPS and wardriving (the T-Watch S3 Plus) --------------------------------
// The S3 Plus carries a u-blox MIA-M10Q (an LS550G on some batches). Its
// sentences go to Gnss (gnss.h); a fix stamps what Wardrive (wardrive.h)
// writes. The plain S3 has no GNSS: the probe finds nothing, says so once,
// and powers the rails back down.
//
// The board variant (arduino-esp32 variants/lilygo_twatch_s3) has the ESP32
// receiving on GPIO41 and sending on 42; LilyGo's docs table names the same
// pins from the module's side, which reads the other way round. Power: BLDO1
// on revisions with BOOT/RST buttons, DC3 on earlier ones, and the LS550G
// version also wants DC4 at 850 mV (LilyGoLib docs/hardware).
//
// Console: GPS / GPS OFF / GPS STATUS; GPS FAKE lat lon (a bench fix, rows
// marked and left out of an export); WARDRIVE ON / OFF; WIGLE (the file, over
// USB), WIGLE ALL (bench rows too), WIGLE CLEAR.
static bool     s_gpsOn = false, s_gpsFound = false;
static uint8_t  s_gpsTry = 0;           // index into GPS_BAUDS, then the DC4 retry
static uint32_t s_gpsTryAt = 0, s_gpsSumAt = 0, s_gpsGoodAt = 0;
// Resting: indoors the GPS searches flat out all day and never fixes (3.7 h
// at work on 2026-09-29). After SEARCH_FIRST_MS with no fix it is powered
// down for REST_MS, then gets RETRY_MS to find the sky before resting again;
// a fix resets it to the long first search.
static const uint32_t GPS_SEARCH_FIRST_MS = 10u * 60u * 1000u;
static const uint32_t GPS_RETRY_MS        = 2u * 60u * 1000u;
static const uint32_t GPS_REST_MS         = 5u * 60u * 1000u;
static bool     s_gpsResting = false, s_gpsRetried = false;
static uint32_t s_gpsSearchFrom = 0, s_gpsRestUntil = 0, s_gpsLastFixAt = 0;
static const uint32_t GPS_BAUDS[] = { 38400, 9600, 115200 };
// The best since switched on, for a watch that was off the cable while it
// happened: GPS STATUS asks.
static uint32_t s_gpsOnAt = 0, s_gpsFirstFixMs = 0;
static uint8_t  s_gpsBestView = 0, s_gpsBestHeard = 0, s_gpsBestUsed = 0;
static bool     s_gpsSummaries = false;  // the 5 s lines, only when asked for on the console
extern volatile uint8_t g_consoleGps;    // 1 on, 2 off, 3 status, 4 fake, 5..7 wardrive/wigle
extern volatile int32_t g_consoleFakeLat7, g_consoleFakeLon7;

static void gpsPower(bool on, bool dc4) {
    if (on) {
        s_pmu.setBLDO1Voltage(3300); s_pmu.enableBLDO1();
        s_pmu.setDC3Voltage(3300);   s_pmu.enableDC3();
        if (dc4) { s_pmu.setDC4Voltage(850); s_pmu.enableDC4(); }
    } else {
        s_pmu.disableBLDO1(); s_pmu.disableDC3(); s_pmu.disableDC4();
    }
}

static void gpsOpen(uint32_t baud) {
    Serial1.end();
    Serial1.begin(baud, SERIAL_8N1, 41, 42);   // our RX 41, TX 42
    s_gpsTryAt = millis();
    s_gpsGoodAt = Gnss::good();
}

static void gpsStart(bool summaries) {
    if (!s_pmuOk) return;
    s_gpsOn = true; s_gpsFound = false; s_gpsTry = 0;
    s_gpsSummaries = summaries;
    s_gpsOnAt = millis(); s_gpsFirstFixMs = 0;
    s_gpsResting = false; s_gpsRetried = false; s_gpsSearchFrom = millis(); s_gpsLastFixAt = 0;
    s_gpsBestView = s_gpsBestHeard = s_gpsBestUsed = 0;
    Gnss::reset();
    gpsPower(true, false);
    delay(50);
    gpsOpen(GPS_BAUDS[0]);
    s_gpsSumAt = millis();
}

static void gpsStop() {
    s_gpsOn = false;
    s_gpsResting = false;
    Serial1.end();
    gpsPower(false, false);
}

#if MULESKIN_LORA
// New LoRa chat messages: a pink pill at the top of the main screen, under
// the GPS counter when that is showing, and only while there is something
// unread. A tap opens LORA CHATS. Drawn with the GPS counter, under the
// speech bubbles.
static int16_t s_loraBadgeX = -1, s_loraBadgeY = 0, s_loraBadgeW = 0, s_loraBadgeH = 0;
void twatchLoraBadge(TFT_eSPI& t) {
    s_loraBadgeX = -1;
    const uint16_t u = uiLoraChatUnread();
    if (!u || Settings::loraListen() == 0) return;
    char txt[20];
    snprintf(txt, sizeof txt, "LORA %u NEW", (unsigned)u);
    t.setTextSize(Theme::uiTextSize(t, 1));
    const int w = t.textWidth(txt) + 10, h = t.fontHeight() + 6;
    const int x = (t.width() - w) / 2;
    const int y = s_gpsOn ? 4 + h + 3 : 4;
    t.fillRoundRect(x, y, w, h, 4, Theme::BG);
    t.drawRoundRect(x, y, w, h, 4, Theme::PINK);
    t.setTextColor(Theme::PINK, Theme::BG);
    t.setCursor(x + 5, y + 3);
    t.print(txt);
    s_loraBadgeX = (int16_t)x; s_loraBadgeY = (int16_t)y; s_loraBadgeW = (int16_t)w; s_loraBadgeH = (int16_t)h;
}
// A finger's worth of slack around it: the pill is small on a watch.
static bool twatchLoraBadgeHit(int x, int y) {
    return s_loraBadgeX >= 0 && x >= s_loraBadgeX - 8 && x < s_loraBadgeX + s_loraBadgeW + 8 &&
           y >= s_loraBadgeY - 6 && y < s_loraBadgeY + s_loraBadgeH + 8;
}
// One soft buzz for a new message, not while the chats are open, and not
// more than once in thirty seconds however busy the channel is.
static void twatchLoraBuzzTick(uint32_t now) {
    static uint32_t lastCheck = 0, lastBuzz = 0;
    static uint16_t lastUnread = 0;
    if (now - lastCheck < 1000) return;
    lastCheck = now;
    const uint16_t u = uiLoraChatUnread();
    if (u > lastUnread && state != AppState::LORA_CHAT && (!lastBuzz || now - lastBuzz > 30000)) {
        lastBuzz = now;
        twatchBuzz(Buzz::MESSAGE);
    }
    lastUnread = u;
}
#endif

// While the GPS is on, a small counter at the top of the main screen: how
// many satellites it hears, how many it uses, FIX once it has one -- and,
// wardriving, how many rows are kept. Read without a cable, on a windowsill.
// Drawn by uiClearTick() straight after the background, like the corner
// clock, so MuleSkin's speech bubbles go over it rather than under it.
void twatchGpsBadge(TFT_eSPI& t) {
    if (!s_gpsOn) return;
    const Gnss::Sky k = Gnss::sky();
    const Gnss::Fix& f = Gnss::fix();
    const bool fix = Gnss::fresh(millis());
    char txt[40];
    if (s_gpsResting)                  snprintf(txt, sizeof txt, "GPS RESTING");
    else if (!s_gpsFound)              snprintf(txt, sizeof txt, "GPS STARTING");
    else if (fix && Wardrive::enabled()) snprintf(txt, sizeof txt, "GPS FIX %u  %lu ROWS", f.used, (unsigned long)Wardrive::count());
    else if (fix)                      snprintf(txt, sizeof txt, "GPS FIX  %u SATS", f.used);
    else                               snprintf(txt, sizeof txt, "GPS %u HEARD  %u USED", k.heard, f.used);
    t.setTextSize(Theme::uiTextSize(t, 1));
    const int w = t.textWidth(txt) + 10, h = t.fontHeight() + 6;
    const int x = (t.width() - w) / 2, y = 4;
    const uint16_t col = fix ? Theme::GREEN : Theme::CYAN;
    t.fillRoundRect(x, y, w, h, 4, Theme::BG);
    t.drawRoundRect(x, y, w, h, 4, col);
    t.setTextColor(col, Theme::BG);
    t.setCursor(x + 5, y + 3);
    t.print(txt);
}

// The WiGLE file over USB, between two marker lines tools/wigle_dump.py looks
// for. The native USB console drops what does not fit its buffer (see the
// setTxTimeoutMs(0) in setup), and the first export lost the end of the
// column line that way. So this writes only as much as the buffer has room
// for, and waits for the host to take the rest; a host that stops reading
// for two seconds ends the export rather than hanging the watch.
static bool usbWriteAll(const char* p, size_t n) {
    uint32_t waitFrom = millis();
    while (n) {
        const int room = Serial.availableForWrite();
        if (room <= 0) {
            if (millis() - waitFrom > 2000) return false;
            delay(1);
            continue;
        }
        const size_t k = (size_t)room < n ? (size_t)room : n;
        const size_t w = Serial.write((const uint8_t*)p, k);
        p += w; n -= w;
        if (w) waitFrom = millis();
    }
    return true;
}

static void wigleExport(bool includeFake) {
    char buf[512];   // the two header lines run past 330 with a long version string
    struct Ctx { bool fake, ok; uint32_t rows, skipped; char* buf; } c = { includeFake, true, 0, 0, buf };
    usbWriteAll("=== WIGLE BEGIN ===\n", 20);
    const size_t h = Wardrive::headerLines(buf, sizeof buf, FIRMWARE_VERSION, "twatch-s3", "LilyGo T-Watch S3 Plus", "LilyGo");
    c.ok = usbWriteAll(buf, h);
    if (c.ok) Wardrive::forEach([](const Wardrive::Record& r, void* p) {
        Ctx& c = *(Ctx*)p;
        if ((r.flags & Wardrive::F_FAKE) && !c.fake) { c.skipped++; return true; }
        const size_t n = Wardrive::csvRow(r, c.buf, 320);
        if (n && !usbWriteAll(c.buf, n)) { c.ok = false; return false; }
        if (n) c.rows++;
        return true;
    }, &c);
    const int n = snprintf(buf, sizeof buf, "=== WIGLE END %lu rows (%lu bench rows left out)%s ===\n",
                           (unsigned long)c.rows, (unsigned long)c.skipped, c.ok ? "" : " INCOMPLETE");
    usbWriteAll(buf, (size_t)n);
}

static void gpsTick() {
    const uint8_t cmd = g_consoleGps;
    if (cmd) g_consoleGps = 0;
    if (cmd == 1) { gpsStart(true); Serial.println("[gps] on"); }
    else if (cmd == 2) { gpsStop(); Serial.println("[gps] off"); }
    else if (cmd == 3) {
        const Gnss::Fix& f = Gnss::fix();
        Serial.printf("[gps] status: %s; on %lu s; most in view %u, heard %u, used %u; ",
                      s_gpsOn ? "on" : "off", s_gpsOn ? (unsigned long)((millis() - s_gpsOnAt) / 1000) : 0ul,
                      s_gpsBestView, s_gpsBestHeard, s_gpsBestUsed);
        if (s_gpsFirstFixMs) Serial.printf("FIRST FIX after %lu s%s, now %s at %ld,%ld\n",
                                           (unsigned long)(s_gpsFirstFixMs / 1000), Gnss::faked() ? " (fake)" : "",
                                           Gnss::fresh(millis()) ? "fixed" : "lost", (long)f.lat7, (long)f.lon7);
        else Serial.println("never had a fix");
        Serial.printf("[wardrive] %s; %lu rows kept of %lu; this boot %lu written, %lu skipped as repeats, %lu dropped\n",
                      Wardrive::enabled() ? "ON" : "off", (unsigned long)Wardrive::count(), (unsigned long)Wardrive::capacity(),
                      (unsigned long)Wardrive::written(), (unsigned long)Wardrive::skipped(), (unsigned long)Wardrive::dropped());
    }
    else if (cmd == 4) {
        const uint32_t e = Clock::isSet() ? Clock::nowEpoch() : 0;
        Gnss::fake(g_consoleFakeLat7, g_consoleFakeLon7, e, millis());
        if (!s_gpsFirstFixMs) s_gpsFirstFixMs = millis() - s_gpsOnAt + 1;
        Serial.printf("[gps] BENCH FIX at %ld,%ld%s -- rows written now are marked and left out of WIGLE\n",
                      (long)g_consoleFakeLat7, (long)g_consoleFakeLon7, e ? "" : "; the clock is not set, so no rows until it is");
    }
    else if (cmd == 5) { Wardrive::setEnabled(true);  if (!s_gpsOn) gpsStart(false); Serial.println("[wardrive] ON"); }
    else if (cmd == 6) { Wardrive::setEnabled(false); gpsStop(); Serial.println("[wardrive] off"); }
    else if (cmd == 7) wigleExport(false);
    else if (cmd == 8) wigleExport(true);
    else if (cmd == 9) { Wardrive::clear(); Serial.println("[wardrive] cleared"); }

    // A bench fix is held until a real one replaces it; refreshed here so it
    // does not go stale while the console test runs.
    if (Gnss::faked()) Gnss::fake(Gnss::fix().lat7, Gnss::fix().lon7, Clock::isSet() ? Clock::nowEpoch() : 0, millis());

    if (!s_gpsOn) { Wardrive::tick(millis()); return; }
    if (s_gpsResting) {
        if ((int32_t)(millis() - s_gpsRestUntil) < 0) { Wardrive::tick(millis()); return; }
        // Back on, on the rails and baud that worked, for a short look.
        s_gpsResting = false; s_gpsRetried = true; s_gpsSearchFrom = millis();
        gpsPower(true, s_gpsTry >= 3);
        delay(50);
        gpsOpen(GPS_BAUDS[s_gpsTry % 3]);
        Serial.println("[gps] awake again: looking for the sky");
    }
    while (Serial1.available()) Gnss::feed((char)Serial1.read(), millis());
    const uint32_t now = millis();
    if (Gnss::fresh(now) && !Gnss::faked()) { s_gpsLastFixAt = now; s_gpsRetried = false; }
    if (s_gpsFound && !Gnss::fresh(now)) {
        const uint32_t from = (int32_t)(s_gpsLastFixAt - s_gpsSearchFrom) > 0 ? s_gpsLastFixAt : s_gpsSearchFrom;
        if (now - from > (s_gpsRetried ? GPS_RETRY_MS : GPS_SEARCH_FIRST_MS)) {
            Serial1.end();
            gpsPower(false, false);
            s_gpsResting = true;
            s_gpsRestUntil = now + GPS_REST_MS;
            Serial.printf("[gps] no fix for %lu s: resting %lu s\n", (unsigned long)((now - from) / 1000),
                          (unsigned long)(GPS_REST_MS / 1000));
            Wardrive::tick(now);
            return;
        }
    }
    if (!s_gpsFound) {
        if (Gnss::good() - s_gpsGoodAt >= 3) {
            s_gpsFound = true;
            Serial.printf("[gps] a GNSS is talking at %lu baud\n", (unsigned long)GPS_BAUDS[s_gpsTry % 3]);
            // A GPS answered: this is an S3 Plus, and its bigger cell can
            // take the faster charge from now on.
            if (!Settings::watchPlus()) { Settings::setWatchPlus(); twatchApplyCharge(); }
        } else if (now - s_gpsTryAt > 2500) {
            // Three bauds with BLDO1 and DC3, then the same three with DC4 too.
            s_gpsTry++;
            if (s_gpsTry == 3) { gpsPower(true, true); delay(50); }
            if (s_gpsTry >= 6) {
                Serial.println("[gps] no GNSS answered: not an S3 Plus, or its GPS is not powered this way");
                gpsStop();
                // WARDRIVE on a watch with no GPS: say so and switch it off,
                // rather than leave a row reading GPS STARTING forever.
                if (Wardrive::enabled()) {
                    Wardrive::setEnabled(false);
                    Theme::showToast("NO GPS ON THIS WATCH", "Wardriving needs the S3 Plus", Theme::AMBER);
                }
                return;
            }
            gpsOpen(GPS_BAUDS[s_gpsTry % 3]);
        }
    }
    const Gnss::Sky k = Gnss::sky();
    const Gnss::Fix& f = Gnss::fix();
    if (k.view > s_gpsBestView) s_gpsBestView = k.view;
    if (k.heard > s_gpsBestHeard) s_gpsBestHeard = k.heard;
    if (f.used > s_gpsBestUsed) s_gpsBestUsed = f.used;
    if (f.valid && !Gnss::faked() && !s_gpsFirstFixMs) {
        s_gpsFirstFixMs = now - s_gpsOnAt;
        Serial.printf("[gps] FIRST FIX after %lu s, %u satellites\n", (unsigned long)(s_gpsFirstFixMs / 1000), f.used);
    }
    // Satellite time is the best clock this watch will ever be offered.
    if (f.valid && !Gnss::faked() && Gnss::utcEpoch() && !Clock::trusted()) {
        if (Clock::setEpoch(Gnss::utcEpoch())) Serial.println("[clock] set from GPS");
    }
    Wardrive::tick(now);
    if (s_gpsSummaries && s_gpsFound && now - s_gpsSumAt > 5000) {
        s_gpsSumAt = now;
        Serial.printf("[gps] %s; %u used; in view %u, heard %u\n",
                      Gnss::fresh(now) ? "FIX" : "no fix yet", f.used, k.view, k.heard);
    }
}

// For the WARDRIVE row: 0 off, 1 looking for the module, 2 no fix, 3 fix,
// 4 resting between looks.
uint8_t twatchGpsState() {
    if (!s_gpsOn) return 0;
    if (s_gpsResting) return 4;
    if (!s_gpsFound) return 1;
    return Gnss::fresh(millis()) ? 3 : 2;
}

// At boot: the store, and the GPS back on if wardriving was left on.
static void wardriveBegin() {
    Wardrive::begin();
    if (Wardrive::enabled()) gpsStart(false);
}
#endif

#if defined(CYD_GPS)
// ---- GPS on the CYD (cyd-gps builds) ------------------------------------------
// An ATGM336H with its TX on GPIO35 (P3), at 9600 baud. Its sentences go to
// Gnss (gnss.h); a fresh fix puts a position on each SD row and on the alert
// card, and its time sets a clock that has no network time this boot.
// No power control (the CYD has none) and no baud search.
//
// Not behind CYD: the emulator's squachsim-live compiles this file with no
// board macro, and its Serial2 replays the NMEA file named by SQUACHSIM_NMEA.
//
// Console: GPS STATUS; GPS FAKE lat lon (a bench fix, marked FAKE on the
// screens and the SD rows, and never used for the clock); GPS OFF ends it.
// Where the module is, per board: the CYD's ATGM336H on GPIO35 at 9600 on the
// third UART, by default. The Marauder V8's GPS talks on GPIO14 at 115200
// (found on the board, 2026-10-12), and the C5 has no third UART, so it says
// so in its own build flags.
#ifndef CYD_GPS_RX
#define CYD_GPS_RX 35
#endif
#ifndef CYD_GPS_BAUD
#define CYD_GPS_BAUD 9600
#endif
#ifndef CYD_GPS_SERIAL
#define CYD_GPS_SERIAL Serial2
#endif
static bool     s_cydGpsClockSet = false;   // set from GPS once this boot
static uint32_t s_cydGpsFirstFixMs = 0;     // millis() of the first real fix, 0 until then

// The module's baud is a guess until a sentence arrives. The CYD's ATGM336H
// talks at 9600, the T-Deck Plus's u-blox MIA-M10Q at 38400 out of the box
// (upstream SquachWatch v1.39.0: a two-hour walk logged no sentences at
// all), the Marauder V8's at 115200. So: the board's own first, and if
// nothing parses in twenty seconds, the next of the usual ones, round and
// round until one does.
static const uint32_t kCydGpsBauds[] = { CYD_GPS_BAUD, 9600, 38400, 115200 };
static uint8_t  s_cydGpsBaudIdx = 0;
static uint32_t s_cydGpsBaudAt = 0;

static void cydGpsStart() {
    Gnss::reset();
    // TX -1: the module's RX is not wired. The core fills in its default
    // pins only when both are negative, so TX stays unassigned here; the
    // default TX2 is GPIO17, the status light.
#if defined(CARDPUTER_ADV)
    // The Grove port's M5 Unit GPS at 115200 baud is about 11 KB a second at
    // most; the default 256-byte buffer overflows in one slow frame and the
    // lost bytes break sentences. (Upstream SquachWatch v1.43.)
    CYD_GPS_SERIAL.setRxBufferSize(2048);
#endif
    CYD_GPS_SERIAL.begin(kCydGpsBauds[s_cydGpsBaudIdx], SERIAL_8N1, CYD_GPS_RX, -1);
    s_cydGpsBaudAt = millis();
    Wardrive::sdBegin();
}

static void cydGpsBaudTick(uint32_t now) {
    if (Gnss::good() + Gnss::bad() || now - s_cydGpsBaudAt < 20000) return;
    const uint8_t n = sizeof kCydGpsBauds / sizeof kCydGpsBauds[0];
    // The next one, skipping the board's own where it repeats a later entry.
    s_cydGpsBaudIdx = Gnss::nextBaud(kCydGpsBauds, n, s_cydGpsBaudIdx);
    s_cydGpsBaudAt = now;
    CYD_GPS_SERIAL.updateBaudRate(kCydGpsBauds[s_cydGpsBaudIdx]);
    Serial.printf("[gps] nothing heard: trying %lu baud\n", (unsigned long)kCydGpsBauds[s_cydGpsBaudIdx]);
}

static const char* cydWardriveStateName(Wardrive::SdState st) {
    switch (st) {
        case Wardrive::SdState::OFF:             return "off";
        case Wardrive::SdState::NO_CARD:         return "NO CARD";
        case Wardrive::SdState::LOW_MEMORY:      return "LOW MEMORY";
        case Wardrive::SdState::WAITING_FOR_FIX: return "WAITING FOR FIX";
        case Wardrive::SdState::LOGGING:         return "ON";
    }
    return "?";
}

static void cydGpsTick(uint32_t now) {
    cydGpsBaudTick(now);
    const uint8_t cmd = g_consoleGps;
    if (cmd) g_consoleGps = 0;
    if (cmd == 2) { Gnss::reset(); Serial.println("[gps] reset: any bench fix is gone"); }
    else if (cmd == 3) {
        const Gnss::Sky k = Gnss::sky();
        const Gnss::Fix& f = Gnss::fix();
        Serial.printf("[gps] status: %lu baud; sentences %lu good, %lu bad; in view %u, heard %u, used %u; ",
                      (unsigned long)kCydGpsBauds[s_cydGpsBaudIdx],
                      (unsigned long)Gnss::good(), (unsigned long)Gnss::bad(), k.view, k.heard, f.used);
        if (s_cydGpsFirstFixMs) Serial.printf("first fix %lu s after boot; ", (unsigned long)(s_cydGpsFirstFixMs / 1000));
        else                    Serial.print(Gnss::faked() ? "no real fix yet; " : "no fix yet; ");
        if (f.valid) {
            char la[16], lo[16];
            SdRow::degreesText(la, sizeof la, f.lat7, 7);
            SdRow::degreesText(lo, sizeof lo, f.lon7, 7);
            Serial.printf("%s at %s,%s\n",
                          Gnss::faked() ? "bench fix" : Gnss::fresh(now) ? "fixed" : "lost", la, lo);
        } else {
            Serial.println("no position");
        }
        const char* fn = Wardrive::sdFileName();
        Serial.printf("[wardrive] %s; file %s; written %lu, skipped %lu as repeats, dropped %lu\n",
                      cydWardriveStateName(Wardrive::sdState(now)), *fn ? fn : "none yet",
                      (unsigned long)Wardrive::written(), (unsigned long)Wardrive::skipped(),
                      (unsigned long)Wardrive::dropped());
    }
    else if (cmd == 4) {
        Gnss::fake(g_consoleFakeLat7, g_consoleFakeLon7, Clock::isSet() ? Clock::nowEpoch() : 0, now);
        char la[16], lo[16];
        SdRow::degreesText(la, sizeof la, g_consoleFakeLat7, 7);
        SdRow::degreesText(lo, sizeof lo, g_consoleFakeLon7, 7);
        Serial.printf("[gps] BENCH FIX at %s,%s: SD rows written now carry FAKE, and the clock is left alone\n",
                      la, lo);
    }
    else if (cmd == 5) {
        Wardrive::setEnabled(true);
        Serial.println("[wardrive] ON: rows go to the SD card once the GPS has a real fix");
    }
    else if (cmd == 6) { Wardrive::setEnabled(false); Serial.println("[wardrive] off"); }
    else if (cmd >= 7 && cmd <= 9) {
        const char* fn = Wardrive::sdFileName();
        Serial.printf("[wardrive] the WiGLE file is on the SD card: %s\n", *fn ? fn : "none yet");
    }
    else if (cmd) Serial.println("[gps] not on this board");

    // A bench fix is held until a real one replaces it; refreshed here so it
    // does not go stale while the console test runs.
    if (Gnss::faked()) Gnss::fake(Gnss::fix().lat7, Gnss::fix().lon7, Clock::isSet() ? Clock::nowEpoch() : 0, now);

    while (CYD_GPS_SERIAL.available()) Gnss::feed((char)CYD_GPS_SERIAL.read(), now);
    Wardrive::sdTick(now, engine.sd().ready());

    const Gnss::Fix& f = Gnss::fix();
    if (f.valid && !Gnss::faked() && !s_cydGpsFirstFixMs) {
        s_cydGpsFirstFixMs = now ? now : 1;
        Serial.printf("[gps] FIRST FIX after %lu s, %u satellites\n", (unsigned long)(now / 1000), f.used);
    }
    // Network time stays the preferred source: a synced clock is never
    // overwritten, and a later NTP sync replaces this one. Once a boot, then
    // again only if something left the clock untrusted.
    if (Gnss::fresh(now) && !Gnss::faked() && Gnss::utcEpoch() && !Clock::synced() &&
        (!s_cydGpsClockSet || !Clock::trusted())) {
        if (Clock::setEpoch(Gnss::utcEpoch())) {
            s_cydGpsClockSet = true;
            Serial.println("[clock] set from GPS");
        }
    }
}
#endif
