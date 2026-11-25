// MuleSkin-CYD -- part of src/main.cpp, included where it stood:
// CHARGE MODE (radios, light and backlight off while the cell charges) and
// the BOOT button that starts and ends it.
//
// One translation unit with main.cpp: this uses the globals and helpers
// declared above its #include there, and what follows it uses this, so
// it is not compiled on its own and its place in main.cpp matters.

#if defined(ESP32) && !defined(TWATCH_S3)
// CHARGE MODE: the board keeps running while it charges -- there is no off
// switch that leaves the charger on -- and a running board takes most of
// what a small charger gives (about 200 mA of it). This stops the WiFi and
// Bluetooth radios, the status light and the backlight and drops the CPU to
// 80 MHz, so the charger's current goes into the cell. The screen shows how
// long it has been charging for fifteen seconds; a tap on the dark screen
// shows it again for five, and a tap while it shows wakes the board. The board cannot tell when the cell is
// full, so the time is all there is to go on.
static bool     s_chargeMode = false, s_chargeWasDown = false;
static bool chargeModeOn() { return s_chargeMode; }   // the key cursor: any key wakes it
// Asked for from the Settings row (or CHARGE on the console) and entered at
// the top of the NEXT loop: entered in the middle of a pass, the rest of
// that pass went on drawing the Settings screen over the charge screen.
volatile bool   g_consoleCharge = false;
static bool     s_chargeWanted = false;
static uint32_t s_chargeAt = 0, s_chargeLitAt = 0, s_chargeLitUntil = 0, s_chargeDrawnAt = 0;

static void chargeBacklight(uint8_t duty) {
#if defined(CROWPANEL7)
    CrowBL::set(duty);
#elif defined(TAB5)
    Tab5Panel::backlight(duty);
#elif defined(TDECK_PLUS)
    tdeckBacklight(duty);
#elif defined(WAVESHARE_C5)
    WsC5::setBacklight(duty);
#else
    ledcWrite(BL_CH_ORIG, duty);
    ledcWrite(BL_CH_CAP,  duty);
    ledcWrite(BL_CH_AWOK, duty);
#endif
}

static void chargeDraw(uint32_t now) {
    s_chargeDrawnAt = now;
    const int w = tft.width(), h = tft.height();
    PANEL.fillRect(0, 0, tft.width(), tft.height(), TFT_BLACK);
    const uint32_t mins = (now - s_chargeAt) / 60000UL;
    char line[32];
    PANEL.setTextDatum(MC_DATUM);
#if defined(SQW_SMALL)
    // 135 rows: the layout below put its title and its prompt off the top
    // and bottom edges. Any key wakes it here (see stickPoll), so it says
    // PRESS. (Upstream SquachWatch v1.42.)
    PANEL.setTextColor(Theme::CYAN, TFT_BLACK);
    PANEL.setTextSize(2);
    PANEL.drawString("CHARGE MODE", w / 2, 16);
    PANEL.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof line, "Charging %luh %02lum", (unsigned long)(mins / 60), (unsigned long)(mins % 60));
    PANEL.drawString(line, w / 2, 44);
    PANEL.setTextSize(1);
    PANEL.setTextColor(Theme::blend(TFT_BLACK, TFT_WHITE, 170), TFT_BLACK);
    PANEL.drawString("Radios off so it fills faster.", w / 2, 72);
    PANEL.drawString("About 4-6 hours from empty.", w / 2, 86);
    PANEL.setTextColor(Theme::GREEN, TFT_BLACK);
    PANEL.setTextSize(2);
    PANEL.drawString("PRESS TO WAKE UP", w / 2, 114);
    PANEL.setTextDatum(TL_DATUM);
    panelShow();
    (void)h;
    return;
#endif
    PANEL.setTextColor(Theme::CYAN, TFT_BLACK);
    PANEL.setTextSize(3);
    PANEL.drawString("CHARGE MODE", w / 2, h / 2 - 50);
    PANEL.setTextColor(TFT_WHITE, TFT_BLACK);
    PANEL.setTextSize(2);
    snprintf(line, sizeof line, "Charging %luh %02lum", (unsigned long)(mins / 60), (unsigned long)(mins % 60));
    PANEL.drawString(line, w / 2, h / 2 - 10);
    PANEL.setTextSize(1);
    PANEL.setTextColor(Theme::blend(TFT_BLACK, TFT_WHITE, 170), TFT_BLACK);
    // Two lines, not one: at size 1 the sentence was 324 px, wider than a
    // portrait 240 px screen (and a landscape 320), and lost "Radios" and
    // "faster." off its ends. (Upstream SquachWatch, 2026-11-15.)
    PANEL.drawString("Radios and screen are off", w / 2, h / 2 + 20);
    PANEL.drawString("so the battery fills faster.", w / 2, h / 2 + 32);
    PANEL.drawString("About 4-6 hours from empty.", w / 2, h / 2 + 46);
    PANEL.setTextColor(Theme::GREEN, TFT_BLACK);
    PANEL.setTextSize(2);
    PANEL.drawString("TAP TO WAKE UP", w / 2, h / 2 + 72);
    PANEL.setTextDatum(TL_DATUM);
    panelShow();
}

static void enterChargeMode() {
    const uint32_t now = millis();
    s_chargeMode = true;
    s_chargeAt = now;
    s_chargeLitAt = now;
    s_chargeLitUntil = now + 15000;
    s_chargeWasDown = true;   // the tap that chose the row is not the first tap here
    engine.restRadios(true);
    StatusLight::off();
    setCpuFrequencyMhz(80);
    s_cpuMhzApplied = 80;
    chargeBacklight(Settings::brightness() / 2);
    chargeDraw(now);
    Serial.println("[charge] CHARGE MODE on: radios, light and backlight off, CPU 80 MHz");
}

static void exitChargeMode() {
    const uint32_t mins = (millis() - s_chargeAt) / 60000UL;
    s_chargeMode = false;
#if SQW_BOOT_BTN
    s_crownDark = false;
#endif
    engine.wakeRadios();
    applyCpuClock();
    applyBrightness();
    lastTouch = millis();
    FramePush::invalidate();
    enterClear();
    Serial.printf("[charge] CHARGE MODE off after %lu min\n", (unsigned long)mins);
}

static void chargeModeTick(uint32_t now) {
    const TouchPoint tp = pollTouch();
    const bool down = tp.valid;
    const bool lit = (int32_t)(s_chargeLitUntil - now) > 0;
    // A touch panel can drop out for a poll or two in the middle of one tap;
    // a new tap only counts once the finger has been off for 150 ms, so one
    // tap never reads as two and wakes the board by accident.
    static uint32_t upAt = 0;
    if (down != s_chargeWasDown) Serial.printf("[charge] touch %s at %d,%d (lit %d)\n", down ? "down" : "up", tp.x, tp.y, (int)lit);
    if (!down && s_chargeWasDown) upAt = now;
    if (down && !s_chargeWasDown && now - upAt > 150) {
        if (lit && now - s_chargeLitAt > 800) { s_chargeWasDown = true; exitChargeMode(); return; }
        // A tap on the dark screen: the charging time, for five seconds.
        s_chargeLitAt = now;
        s_chargeLitUntil = now + 5000;
        chargeBacklight(Settings::brightness() / 2);
        chargeDraw(now);
    }
    s_chargeWasDown = down;
    // Asked again, not reused: a tap above has just lit the screen, and the
    // answer from the top of this tick put it straight back out.
    const bool litNow = s_chargeLitUntil && (int32_t)(s_chargeLitUntil - now) > 0;
    if (litNow && now - s_chargeDrawnAt > 30000) chargeDraw(now);
    if (!litNow && s_chargeLitUntil) { chargeBacklight(0); s_chargeLitUntil = 0; Serial.println("[charge] screen dark"); }
    // No light sleep here. It was tried (2026-09-29) and the chip sometimes
    // never woke from it -- the RTC watchdog reset the board after 5 h one
    // time and 20 min the next (black box: WDT, boots 50 and 51). Light
    // sleep with the Bluetooth controller still powered is not dependable
    // on this chip, and the saving was about 25 mA. An idle wait instead.
    delay(40);
}

#if SQW_BOOT_BTN
// BOOT, polled every pass (and every 40 ms in charge mode). Released before
// 1.2 s it is a short press: the screen off, or back on. Held past 1.2 s it
// starts CHARGE MODE, without waiting for the release. In charge mode either
// wakes the board. 30 ms of contact before anything counts.
static void bootButtonTick(uint32_t now) {
    static bool was = false, longDone = false;
    static uint32_t downAt = 0;
    const bool down = digitalRead(0) == LOW;
    if (down && !was) { downAt = now; longDone = false; }
    if (down && !longDone && now - downAt >= 1200) {
        longDone = true;
        Serial.println("[button] long press");
        if (s_chargeMode) exitChargeMode(); else s_chargeWanted = true;
    }
    if (!down && was && !longDone && now - downAt >= 30) {
        Serial.println("[button] short press");
        if (s_chargeMode) exitChargeMode();
        else pocketPress(now);
    }
    was = down;
}
#endif
#endif
