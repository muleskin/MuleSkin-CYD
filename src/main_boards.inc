// MuleSkin-CYD -- part of src/main.cpp, included where it stood:
// Board power and batteries: the T-Watch S3's AXP2101 rails, haptic motor,
// motion sensor, clock chip, battery, radio self-heal and crown; the battery
// divider on the Freenove S3s and the T-Deck Plus; the Marauder V8's gauge.
//
// One translation unit with main.cpp: this uses the globals and helpers
// declared above its #include there, and what follows it uses this, so
// it is not compiled on its own and its place in main.cpp matters.

#if defined(TWATCH_S3)
// The T-Watch S3's AXP2101 gates the screen backlight (ALDO2), the touch
// chip (ALDO3), the RTC's supply (ALDO1), the radio (ALDO4) and the haptic
// driver (BLDO2). Nothing on the display or touch bus answers until these
// are on, so this runs first thing in setup(). Rails and volts are LilyGo's
// own from LilyGoLib.cpp; the charger is left at the chip's defaults.
static XPowersAXP2101 s_pmu;
static bool           s_pmuOk = false;
static void twatchPowerUp() {
    Wire1.begin(10, 11);
    s_pmuOk = s_pmu.begin(Wire1, AXP2101_SLAVE_ADDRESS, 10, 11);
    if (!s_pmuOk) { Serial.println("[pmu] AXP2101 did not answer -- screen may stay dark"); return; }
    // The chip's own rail, set rather than assumed: a full power-off (a dead
    // battery) puts the PMU back to its register defaults. It read 3300
    // either way on 2026-09-23; the deaf radios that morning were not this.
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.enableBattVoltageMeasure();
    Serial.printf("[pmu] DC1 %u mV (%s), ALDO4 %u mV, VBUS %u mV, SYS %u mV, VBUS limit code %u, charge code %u\n",
                  (unsigned)s_pmu.getDC1Voltage(), s_pmu.isEnableDC1() ? "on" : "off",
                  (unsigned)s_pmu.getALDO4Voltage(), (unsigned)s_pmu.getVbusVoltage(),
                  (unsigned)s_pmu.getSystemVoltage(), (unsigned)s_pmu.getVbusCurrentLimit(),
                  (unsigned)s_pmu.getChargerConstantCurr());
    s_pmu.setDC1Voltage(3300);
    // The USB input limit and the charge current. After a full power-off the
    // PMU is back to its register defaults, and with the cell flat the
    // system rail is whatever the USB path is allowed to pass: not enough
    // for a radio burst. 500 mA from USB, 200 mA into the cell (LilyGo's
    // own example uses 1500 and 200).
    s_pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_500MA);
    s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
    s_pmu.setSysPowerDownVoltage(2600);
    s_pmu.setALDO1Voltage(3300); s_pmu.enableALDO1();   // RTC
    s_pmu.setALDO2Voltage(3300); s_pmu.enableALDO2();   // backlight supply
    s_pmu.setALDO3Voltage(3300); s_pmu.enableALDO3();   // touch
    s_pmu.setALDO4Voltage(3300); s_pmu.enableALDO4();   // radio
    s_pmu.setBLDO2Voltage(3300); s_pmu.enableBLDO2();   // DRV2605 haptics

    s_pmu.disableDC2(); s_pmu.disableDC4(); s_pmu.disableDC5();
    s_pmu.disableDLDO2();
    // The crown is the PMU's power key. A short press is read as an IRQ flag
    // from the loop (see twatchCrownTick); a long press powers the watch off
    // in the chip itself, which is what a watch's crown should do.
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    // The clock chip's backup cell. LilyGo's own setup charges it; ours did
    // not, so the chip would lose the time the first time the battery ran
    // flat.
    s_pmu.setButtonBatteryChargeVoltage(3300);
    s_pmu.enableButtonBatteryCharge();
    delay(20);
    Serial.printf("[pmu] AXP2101 up: battery %d%%, %s\n", s_pmu.getBatteryPercent(),
                  s_pmu.isVbusIn() ? "on USB" : "on battery");
}
#endif

#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(TDECK_PLUS)
// GPIO9 on the 2.8" and 4.0", GPIO8 on the 3.5" (Freenove's battery sketch).
// The T-Deck Plus is the same shape: its 2000 mAh cell through a 1:2 divider
// on GPIO4 (LilyGo's BOARD_BAT_ADC), a charger whose LED is the only status.
#if defined(FREENOVE_S3_35)
#define BOARD_BAT_ADC 8
#elif defined(TDECK_PLUS)
#define BOARD_BAT_ADC 4
#else
#define BOARD_BAT_ADC 9
#endif
// The Freenove S3's battery: a 1-cell LiPo on its JST, a TP4054 charging it
// from USB, and a 200K/200K divider from the cell to GPIO9 (ADC1, so WiFi does
// not take it away). No fuel gauge, and the charger's CHRG line goes to an LED
// rather than a GPIO -- so a voltage and an estimate from it, never "charging".
// On the cable the cell reads the charger's ~4.2 V, which is the honest reading.
static uint16_t boardBatteryMv() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BOARD_BAT_ADC);
    return (uint16_t)(sum / 8 * 2);   // the divider halves it
}
// Resting LiPo, per cell: rough, but a percent that moves the right way.
static uint8_t boardBatteryPct(uint16_t mv) {
    static const uint16_t MV[]  = { 3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200 };
    static const uint8_t  PCT[] = {    0,    5,   10,   20,   30,   40,   50,   60,   75,   90,  100 };
    if (mv <= MV[0]) return 0;
    for (uint8_t i = 1; i < sizeof MV / sizeof MV[0]; i++)
        if (mv <= MV[i]) return (uint8_t)(PCT[i-1] + (uint32_t)(PCT[i] - PCT[i-1]) * (mv - MV[i-1]) / (MV[i] - MV[i-1]));
    return 100;
}
// The SYSTEM page's BATTERY row: "81% 4.12V". Under 2.5 V there is no cell on
// the connector at all (the divider just floats), and it says so.
void boardBatteryLine(char* out, size_t n) {
    static uint32_t at = 0;
    static char     line[16] = "";
    const uint32_t now = millis();
    if (!line[0] || now - at >= 2000) {   // eight ADC reads, not eight per frame
        at = now;
        const uint16_t mv = boardBatteryMv();
        if (mv < 2500) snprintf(line, sizeof line, "NONE");
        else snprintf(line, sizeof line, "%u%% %u.%02uV", (unsigned)boardBatteryPct(mv),
                      (unsigned)(mv / 1000), (unsigned)(mv % 1000 / 10));
    }
    snprintf(out, n, "%s", line);
}
#endif

#if defined(MARAUDER_V8)
// The Marauder V8's battery: a MAX17048 fuel gauge at 0x36 on SDA 5 / SCL 4
// (Marauder's configs.h; its boot log says "Detected MAX17048"). Unlike the
// Freenove's divider it knows the charge itself -- SOC, its own model of the
// cell, not a voltage table -- and which way the charge is going (CRATE), so
// "on the cable" is read, not guessed. (Upstream SquachWatch v1.36.0.)
static const uint8_t GAUGE_ADDR = 0x36;
static bool s_gaugeOk = false;
static bool gaugeRead16(uint8_t reg, uint16_t& v) {
    Wire.beginTransmission(GAUGE_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(GAUGE_ADDR, (uint8_t)2) != 2) return false;
    v = (uint16_t)(Wire.read() << 8);
    v |= (uint16_t)Wire.read();
    return true;
}
static void gaugeBegin() {
    Wire.begin(5, 4, 400000);
    uint16_t ver = 0;
    s_gaugeOk = gaugeRead16(0x08, ver);
    Serial.printf("[batt] MAX17048 %s (version 0x%04X)\n", s_gaugeOk ? "answered" : "DID NOT ANSWER", (unsigned)ver);
}
// The SYSTEM page's BATTERY row: "81% 4.12V", with "+" while it charges. One
// reading every two seconds.
void boardBatteryLine(char* out, size_t n) {
    static uint32_t at = 0;
    static char     line[20] = "";
    const uint32_t now = millis();
    if (!line[0] || now - at >= 2000) {
        at = now ? now : 1;
        uint16_t vcell = 0, soc = 0, crate = 0;
        if (!s_gaugeOk || !gaugeRead16(0x02, vcell) || !gaugeRead16(0x04, soc)) {
            snprintf(line, sizeof line, "NO GAUGE");
        } else {
            const uint16_t mv  = (uint16_t)((uint32_t)vcell * 78125u / 1000000u);   // 78.125 uV a step
            const uint8_t  pct = (uint8_t)min<uint16_t>(soc >> 8, 100);             // whole percent in the high byte
            // CRATE: signed, 0.208 %/hour a step. Charging when the charge
            // climbs faster than about 1 %/hour, so the gauge's own jitter
            // at rest is not read as a cable.
            const bool charging = gaugeRead16(0x16, crate) && (int16_t)crate > 5;
            if (mv < 2500) snprintf(line, sizeof line, "NONE");
            else snprintf(line, sizeof line, "%u%% %u.%02uV%s", (unsigned)pct, (unsigned)(mv / 1000),
                          (unsigned)(mv % 1000 / 10), charging ? "+" : "");
        }
    }
    snprintf(out, n, "%s", line);
}
#endif

#if defined(TWATCH_S3)
// One battery sample into the black box. See BattRecord for what it holds
// and why. Printed too, so a bench run shows the same line the ring keeps.
// ---- the haptic motor: a DRV2605 on the power chip's bus ------------------
// Driven by register, no library: out of standby, the ERM effect library,
// internal trigger, then a sequence of effect numbers and waits and GO.
// Effect 1 is a strong click, 47 a full buzz; a byte with the top bit set is
// a wait of that many tens of milliseconds.
static const uint8_t DRV_ADDR = 0x5A;
static bool s_drvOk = false;
static uint32_t s_drvAwakeAt = 0;   // when the last buzz woke it; 0 = in standby

static bool drvWrite(uint8_t reg, uint8_t v) {
    Wire1.beginTransmission(DRV_ADDR);
    Wire1.write(reg);
    Wire1.write(v);
    return Wire1.endTransmission() == 0;
}
static uint8_t drvRead(uint8_t reg) {
    Wire1.beginTransmission(DRV_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return 0;
    if (Wire1.requestFrom(DRV_ADDR, (uint8_t)1) != 1) return 0;
    return Wire1.read();
}

// BUZZ STRENGTH. Library 1 is the driver's table for a 1.3 V motor and was
// what this shipped with; the watch's motor is a 3 V ERM, so library 2 drives
// it as hard as it is rated for, and MAX lifts the overdrive clamp from the
// default 3.1 V to about 3.6 V -- only ever for the fraction of a second an
// effect lasts. Called at init and whenever the row is tapped; the chip is
// out of standby on both paths, which the register writes need.
static void twatchHapticApply() {
    const uint8_t lvl = Settings::buzzStrength();
    drvWrite(0x03, lvl == 0 ? 0x01 : 0x02);  // LIBRARY
    drvWrite(0x17, lvl == 2 ? 0xA5 : 0x8C);  // OD_CLAMP: 5.6 V * n / 255
}

static void twatchHapticBegin() {
    if (!s_pmuOk) return;
    s_drvOk = drvWrite(0x01, 0x00);          // MODE: out of standby, internal trigger
    if (!s_drvOk) { Serial.println("[buzz] DRV2605 did not answer"); return; }
    drvWrite(0x02, 0x00);                    // no real-time input
    twatchHapticApply();                     // library and drive clamp from BUZZ STRENGTH
    drvWrite(0x1A, drvRead(0x1A) & 0x7F);    // FEEDBACK: ERM, not LRA
    drvWrite(0x1D, drvRead(0x1D) | 0x20);    // CONTROL3: ERM open loop
    drvWrite(0x01, 0x40);                    // STANDBY until a buzz: a few uA instead of ~0.5 mA
    Serial.println("[buzz] DRV2605 ready");
}

static void twatchBuzz(Buzz kind) {
    if (!s_drvOk) return;
    if (kind != Buzz::SAMPLE && !Settings::buzz()) { Serial.println("[buzz] alert, but BUZZ is off"); return; }
    if (kind != Buzz::SAMPLE && state == AppState::MEETING) return;   // the sign is up: no buzzing
    // A room of Ring cameras alerts every few seconds. One buzz in ten
    // seconds for those; a device you asked to WATCH always gets through.
    static uint32_t lastAt = 0;
    const uint32_t now = millis();
    if (kind == Buzz::ALERT && lastAt && now - lastAt < 10000) return;
    lastAt = now;
    uint8_t seq[8] = { 0 };
    // Effects from the driver's library: 1 strong click, 14 strong buzz,
    // 15 a 750 ms alert, 16 a 1000 ms alert, 47 buzz. A byte with the top bit
    // set is a wait of that many tens of milliseconds.
    const uint8_t lvl = Settings::buzzStrength();
    if (kind == Buzz::WATCH) {               // three long buzzes
        const uint8_t e = lvl == 2 ? 16 : 47;
        seq[0] = e; seq[1] = 0x80 | 12; seq[2] = e; seq[3] = 0x80 | 12; seq[4] = e;
    } else if (kind == Buzz::MESSAGE) {      // one click: a message, not an alert
        seq[0] = lvl == 0 ? 1 : 14;
    } else {                                 // a double click, or a double buzz
        const uint8_t e = lvl == 0 ? 1 : lvl == 1 ? 14 : 15;
        seq[0] = e; seq[1] = 0x80 | 10; seq[2] = e;
    }
    drvWrite(0x01, 0x00);                    // out of standby for this one
    s_drvAwakeAt = millis();
    for (uint8_t i = 0; i < 8; i++) drvWrite(0x04 + i, seq[i]);
    const bool go = drvWrite(0x0C, 0x01);    // GO
    Serial.printf("[buzz] %s%s\n", kind == Buzz::WATCH ? "watch alert" : kind == Buzz::ALERT ? "alert" : kind == Buzz::MESSAGE ? "lora message" : "sample",
                  go ? "" : " -- the motor driver did not answer");
}

// ---- the motion sensor: a BMA423 on the same bus --------------------------
// Used for one thing so far: whether the watch is sitting still, which is
// what tells a nightstand from a wrist. Raw readings only -- the step counter
// and tilt features need a program loaded into the chip, and "has it moved"
// does not. Twice a second, one six-byte read.
static const uint8_t BMA_ADDR = 0x19;
static bool     s_bmaOk = false;
static uint64_t s_moveBits[2] = { 0, 0 };   // one bit a second, the last 128 s; bit 0 is now
static uint32_t s_moveSec = 0;              // the second bit 0 stands for
static int16_t  s_acc[3];
static bool     s_accHave = false;
// A reading this far from the last one is movement. +-4 g at twelve bits is
// 512 a g, so about 0.06 g: well above the chip's noise, well below a wrist.
static const int16_t  MOVE_LSB = 30;
// Still is fewer than this many seconds with movement in the last two
// minutes. A sleeper turning over is a few; a walk is most of them.
static const uint8_t  MOVE_SECS = 10;

static bool bmaWrite(uint8_t reg, uint8_t v) {
    Wire1.beginTransmission(BMA_ADDR);
    Wire1.write(reg);
    Wire1.write(v);
    const bool ok = Wire1.endTransmission() == 0;
    delay(1);   // in its power-save mode the chip wants a gap between writes
    return ok;
}
static bool bmaRead(uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire1.beginTransmission(BMA_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(BMA_ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire1.read();
    return true;
}

static void twatchMotionBegin() {
    if (!s_pmuOk) return;
    uint8_t id = 0;
    if (!bmaRead(0x00, &id, 1) || id != 0x13) { Serial.printf("[motion] BMA423 did not answer (id %02X)\n", id); return; }
    bmaWrite(0x7C, 0x00);    // PWR_CONF: power save off while it is set up
    bmaWrite(0x40, 0x15);    // ACC_CONF: averaging mode, 2 samples, 12.5 Hz
    bmaWrite(0x41, 0x01);    // ACC_RANGE: +-4 g
    bmaWrite(0x7D, 0x04);    // PWR_CTRL: accelerometer on
    bmaWrite(0x7C, 0x03);    // PWR_CONF: power save back on, a few uA
    s_bmaOk = true;
    Serial.println("[motion] BMA423 ready");
}

static uint8_t movedSecs() {
    return (uint8_t)(__builtin_popcountll(s_moveBits[0]) + __builtin_popcountll(s_moveBits[1]));
}

// No sensor, no verdict: without one the watch counts as moving, which is
// how AUTO SNOOZE has always behaved.
static bool twatchStill() { return s_bmaOk && s_accHave && movedSecs() < MOVE_SECS; }

static void twatchMotionTick(uint32_t now) {
    if (!s_bmaOk) return;
    static uint32_t last = 0;
    if (now - last < 500) return;
    last = now;
    uint8_t b[6];
    if (!bmaRead(0x12, b, 6)) return;
    bool moved = false;
    for (uint8_t k = 0; k < 3; k++) {
        const int16_t a = (int16_t)((uint16_t)b[2 * k + 1] << 8 | b[2 * k]) >> 4;
        if (s_accHave && abs(a - s_acc[k]) > MOVE_LSB) moved = true;
        s_acc[k] = a;
    }
    const bool wasStill = twatchStill();
    // Slide the history along to this second.
    const uint32_t sec = now / 1000, n = sec - s_moveSec;
    s_moveSec = sec;
    if (n >= 128)      { s_moveBits[0] = s_moveBits[1] = 0; }
    else if (n >= 64)  { s_moveBits[1] = s_moveBits[0] << (n - 64); s_moveBits[0] = 0; }
    else if (n)        { s_moveBits[1] = (s_moveBits[1] << n) | (s_moveBits[0] >> (64 - n)); s_moveBits[0] <<= n; }
    if (moved) s_moveBits[0] |= 1;
    if (!s_accHave) { s_accHave = true; return; }
    const bool isStill = twatchStill();
    if (isStill != wasStill) Serial.printf("[motion] %s\n", isStill ? "still" : "moving");
}

// ---- the clock chip: a PCF8563 on the same bus, with a backup cell ---------
// Kept in UTC. Read once at boot, after Clock::begin(); written whenever the
// clock learns the real time from anywhere, through Clock::onSet().
static const uint8_t RTC_ADDR = 0x51;
// Our mark in the chip's minute-alarm register, with its disabled bit set
// (and alarm interrupts off in control 2), so nothing acts on it. A chip without it holds somebody else's time:
// the factory firmware leaves China time in it, eight hours off UTC, and
// believing that moved the clock by eight hours.
static const uint8_t RTC_MARK = 0xC2;   // alarm disabled, minute 42
static bool s_rtcLoading = false;   // our own set at boot: nothing to write back

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's).
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}
static void civilFromDays(int32_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

static void rtcWrite(uint32_t epoch) {
    if (s_rtcLoading || !s_pmuOk) return;
    int y; unsigned mo, d;
    civilFromDays((int32_t)(epoch / 86400u), y, mo, d);
    const uint32_t sod = epoch % 86400u;
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x00);
    Wire1.write(0x00);                                   // control 1: running
    Wire1.write(0x00);                                   // control 2: no alarms
    Wire1.write(bin2bcd((uint8_t)(sod % 60)));           // seconds, VL cleared
    Wire1.write(bin2bcd((uint8_t)(sod / 60 % 60)));
    Wire1.write(bin2bcd((uint8_t)(sod / 3600)));
    Wire1.write(bin2bcd((uint8_t)d));
    Wire1.write((uint8_t)((epoch / 86400u + 4) % 7));    // 1970-01-01 was a Thursday
    Wire1.write(bin2bcd((uint8_t)mo));
    Wire1.write(bin2bcd((uint8_t)(y % 100)));
    bool ok = Wire1.endTransmission() == 0;
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x09);
    Wire1.write(RTC_MARK);                               // minute alarm: off, and our mark
    ok = (Wire1.endTransmission() == 0) && ok;
    Serial.printf("[rtc] clock chip %s %04d-%02u-%02u %02lu:%02lu UTC\n", ok ? "set to" : "FAILED at",
                  y, mo, d, (unsigned long)(sod / 3600), (unsigned long)(sod / 60 % 60));
}

// 0 when the chip is missing, has lost the time (its VL flag), or holds
// something no build could believe.
static uint32_t rtcRead(bool* lost = nullptr, bool* foreign = nullptr) {
    if (lost) *lost = false;
    if (foreign) *foreign = false;
    if (!s_pmuOk) return 0;
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x09);
    if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(RTC_ADDR, (uint8_t)1) != 1) return 0;
    if (Wire1.read() != RTC_MARK) { if (foreign) *foreign = true; return 0; }
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x02);
    if (Wire1.endTransmission(false) != 0) return 0;
    if (Wire1.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return 0;
    uint8_t r[7];
    for (uint8_t i = 0; i < 7; i++) r[i] = Wire1.read();
    if (r[0] & 0x80) { if (lost) *lost = true; return 0; }   // VL: the backup ran out
    const int y = 2000 + bcd2bin(r[6]);
    const unsigned mo = bcd2bin(r[5] & 0x1F), d = bcd2bin(r[3] & 0x3F);
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    const uint32_t days = (uint32_t)daysFromCivil(y, mo, d);
    return days * 86400u + bcd2bin(r[2] & 0x3F) * 3600u + bcd2bin(r[1] & 0x7F) * 60u + bcd2bin(r[0] & 0x7F);
}

// At boot: the chip's time, when it has one worth believing, sets the clock
// as a real answer -- trusted, not the ten-minute note's guess.
static void twatchRtcBegin() {
    bool lost = false, foreign = false;
    const uint32_t t = rtcRead(&lost, &foreign);
    if (t) {
        s_rtcLoading = true;
        const bool ok = Clock::setEpoch(t) && Clock::isSet();
        s_rtcLoading = false;
        Serial.printf("[rtc] clock chip says %lu: %s\n", (unsigned long)t, ok ? "clock set from it" : "not believable");
    } else {
        Serial.printf("[rtc] clock chip has no time (%s)\n", lost ? "backup ran out" : foreign ? "not set by MuleSkin" : "not answering");
    }
    Clock::onSet(rtcWrite);
    // A clock that already knew the time (kept by the ESP32 across a soft
    // reset) goes to a chip that did not.
    if (!t && Clock::trusted()) rtcWrite(Clock::nowEpoch());
}

// The WATCH page's BATTERY row: "81% 4.12V", or "CHG 81%" on the cable.
void twatchBatteryLine(char* out, size_t n) {
    if (!s_pmuOk) { snprintf(out, n, "?"); return; }
    static uint32_t at = 0;
    static char     line[16] = "";
    const uint32_t now = millis();
    if (!line[0] || now - at >= 2000) {   // two I2C reads, not one per frame
        at = now;
        const int pct = s_pmu.getBatteryPercent();
        if (s_pmu.isCharging()) snprintf(line, sizeof line, "CHG %d%%", pct);
        else snprintf(line, sizeof line, "%d%% %u.%02uV", pct, (unsigned)(s_pmu.getBattVoltage() / 1000),
                      (unsigned)(s_pmu.getBattVoltage() % 1000 / 10));
    }
    snprintf(out, n, "%s", line);
}

// The chip's own temperature sensor, read at most every two seconds: the
// settings row asks every frame, and each read starts and stops the sensor.
int twatchChipC() {
    static uint32_t at = 0;
    static int c = 0;
    const uint32_t now = millis();
    if (!at || now - at >= 2000) { at = now ? now : 1; c = (int)lroundf(temperatureRead()); }
    return c;
}

// CLOCK CHECK: the ESP32's 40 MHz crystal against the clock chip's own
// 32 kHz one. Both radios tune off the ESP32's crystal, and so does micros();
// a crystal that started up off-frequency would leave the CPU, the screen and
// USB working and both radios deaf -- and only a power cut would restart it,
// which is exactly how the deaf spells behave (2026-09-24). The clock chip's
// seconds tick is the yardstick: catch one tick, count XTAL_SECS of them,
// catch the last, and compare with micros(). Catching a tick means polling
// the chip for up to a second, so the screen stalls twice, briefly.
// Healthy is within about +-30 ppm (both crystals together); the radios need
// the ESP32's within about +-25 ppm of true.
static const uint32_t XTAL_SECS = 60;
static uint8_t  s_xtalState = 0;          // 0 never run, 1 counting, 2 done, 3 failed
static uint32_t s_xtalT0 = 0, s_xtalStartMs = 0;
static uint8_t  s_xtalSec0 = 0;
static int32_t  s_xtalPpm = 0;
static int      s_xtalChipC = 0;
// Polls the clock chip's seconds until they change; the micros() of the
// change and the new second. False if the chip did not answer or never ticked.
static bool rtcTick(uint32_t& atUs, uint8_t& sec) {
    auto readSec = [](uint8_t& s) -> bool {
        Wire1.beginTransmission(RTC_ADDR);
        Wire1.write(0x02);
        if (Wire1.endTransmission(false) != 0) return false;
        if (Wire1.requestFrom(RTC_ADDR, (uint8_t)1) != 1) return false;
        s = bcd2bin(Wire1.read() & 0x7F);
        return true;
    };
    uint8_t first;
    if (!readSec(first)) return false;
    const uint32_t t0 = millis();
    while (millis() - t0 < 1500) {
        uint8_t s;
        if (!readSec(s)) return false;
        if (s != first) { atUs = micros(); sec = s; return true; }
    }
    return false;
}
static void twatchXtalStart() {
    if (s_xtalState == 1) return;
    if (!s_pmuOk || !rtcTick(s_xtalT0, s_xtalSec0)) {
        s_xtalState = 3;
        Serial.println("[xtal] the clock chip did not tick; no check");
        return;
    }
    s_xtalState = 1;
    s_xtalStartMs = millis();
    Serial.printf("[xtal] counting %lu clock-chip seconds against micros()...\n", (unsigned long)XTAL_SECS);
}
static void twatchXtalTick(uint32_t now) {
    if (s_xtalState != 1 || now - s_xtalStartMs < (XTAL_SECS - 1) * 1000u) return;
    uint32_t t1; uint8_t s1;
    if (!rtcTick(t1, s1)) { s_xtalState = 3; Serial.println("[xtal] lost the clock chip's tick"); return; }
    const uint32_t us = t1 - s_xtalT0;
    const uint32_t n  = (us + 500000u) / 1000000u;   // whole seconds the chip counted
    if (!n || (s_xtalSec0 + n) % 60 != s1) { s_xtalState = 3; Serial.println("[xtal] the seconds did not add up"); return; }
    s_xtalPpm   = (int32_t)(((int64_t)us - (int64_t)n * 1000000) * 1000000 / ((int64_t)n * 1000000));
    s_xtalChipC = twatchChipC();
    s_xtalState = 2;
    Serial.printf("[xtal] %lu s by the clock chip took %lu us by the ESP32: %+ld ppm (chip %d C; radios heard %lu adverts, %lu frames since boot)\n",
                  (unsigned long)n, (unsigned long)us, (long)s_xtalPpm, s_xtalChipC,
                  (unsigned long)advertsSeen(), (unsigned long)wifiFramesSeen());
}

// DC1 -- the ESP32 and its radio -- in the chip's automatic PWM/PFM. The
// STEADY POWER row that forced PWM was a test for the deaf radios, which
// were never the supply's fault (the slim WiFi re-init, fixed in v1.21.0);
// set at boot so a watch that saved STEADY ON goes back to automatic.
// The charge current. 200 mA suits the plain T-Watch S3's small cell; the
// S3 Plus has about twice the cell and took 3 h 18 min from empty to 94% at
// 200 mA while running (2026-09-29). 400 mA on a watch known to be a Plus.
static void twatchApplyCharge() {
    if (!s_pmuOk) return;
    s_pmu.setChargerConstantCurr(Settings::watchPlus() ? XPOWERS_AXP2101_CHG_CUR_400MA : XPOWERS_AXP2101_CHG_CUR_200MA);
    Serial.printf("[pmu] charge %s\n", Settings::watchPlus() ? "400 mA (S3 Plus)" : "200 mA");
}

static void twatchApplySteady() {
    if (!s_pmuOk) return;
    s_pmu.settDC1WorkModeToPwm(0);
}

static void twatchBatterySample(uint8_t why) {
    if (!s_pmuOk) return;
    BlackBox::BattRecord r;
    memset(&r, 0, sizeof r);
    r.mv       = s_pmu.getBattVoltage();
    r.pct      = (uint8_t)s_pmu.getBatteryPercent();
    r.why      = why;
    r.epoch    = Clock::trusted() ? Clock::nowEpoch() : 0;
    r.upSec    = millis() / 1000u;
    r.cpuMhz10 = (uint8_t)(getCpuFrequencyMhz() / 10);
    if (s_pmu.isVbusIn())   r.flags |= BlackBox::BATT_USB;
    if (s_pmu.isCharging()) r.flags |= BlackBox::BATT_CHARGING;
    if (!s_panelAsleep)     r.flags |= BlackBox::BATT_SCREEN_ON;
    if (!s_radiosResting)   r.flags |= BlackBox::BATT_RADIOS_ON;
    r.chipC   = (int8_t)twatchChipC();
    r.steady  = 0;
    r.adverts = advertsSeen();
    r.frames  = wifiFramesSeen();
    BlackBox::noteBattery(r);
    static const char* const WHY[] = { "timer", "boot", "usb", "screen", "radio reset", "self-heal" };
    Serial.printf("[batt] %u mV  %u%%  %s%s  screen %s  cpu %u MHz  up %lu s  chip %d C  heard %lu/%lu  (%s)\n",
                  (unsigned)r.mv, (unsigned)r.pct, (r.flags & BlackBox::BATT_USB) ? "on USB" : "on battery",
                  (r.flags & BlackBox::BATT_CHARGING) ? ", charging" : "", s_panelAsleep ? "off" : "on",
                  (unsigned)getCpuFrequencyMhz(), (unsigned long)r.upSec, (int)r.chipC,
                  (unsigned long)r.adverts, (unsigned long)r.frames, WHY[why < 6 ? why : 0]);
}

// Every ten minutes, at boot, and whenever the cable or the screen changes
// state: the points where the slope of the curve changes.
static void twatchBatteryTick(uint32_t now) {
    if (!s_pmuOk) return;
    static uint32_t lastAt = 0;
    static bool first = true, wasUsb = false, wasAsleep = false;
    if (first) {
        first = false; lastAt = now;
        wasUsb = s_pmu.isVbusIn(); wasAsleep = s_panelAsleep;
        twatchBatterySample(BlackBox::BATT_WHY_BOOT);
        return;
    }
    if (wasAsleep != s_panelAsleep) {
        wasAsleep = s_panelAsleep; lastAt = now;
        twatchBatterySample(BlackBox::BATT_WHY_SCREEN);
        return;
    }
    if (now - lastAt < 600000u) return;
    lastAt = now;
    const bool usb = s_pmu.isVbusIn();   // one I2C read, ten minutes apart
    if (usb != wasUsb) { wasUsb = usb; twatchBatterySample(BlackBox::BATT_WHY_USB); return; }
    twatchBatterySample(BlackBox::BATT_WHY_TIMER);
}

// The radio duty cycle. The radios are on the cable, on with the screen
// (someone is looking), on through an alert, and on whenever anything else
// has the radio (an update, a raw scan). Otherwise they run for the on part
// of the cycle and rest for the rest of it. What "rest" means is the
// engine's business: see DetectionEngine::restRadios().
static void twatchRadioTick(uint32_t now) {
    static uint32_t phaseAt = 0, usbAt = 0;
    static bool     usb = true;
    // A rest cut short by a Bluetooth arrival is paid back on the next one,
    // so the WiFi on-time is the same as the plain cycle: the window just
    // lands when something turned up instead of on the clock.
    static uint32_t owedMs = 0, arrivalsAt = 0;
    bleArrivalsRoll(now);
    if (now - usbAt >= 2000) { usbAt = now; usb = s_pmuOk && s_pmu.isVbusIn(); s_onUsb = usb; }
    // BLE LISTEN, on battery; the cable pays for the full 75 either way.
    setScanWindowBase(usb ? 75 : Settings::bleListen());
    // RADIO TEST on the console forces BLE+5/30 whatever the settings say.
    const uint8_t duty = g_consoleRadioTest ? 3 : Settings::radioDuty();
    // Only a device you asked to WATCH holds the radios awake. Every alert
    // did, and a room with a Ring camera and a Flipper in it alerts every
    // few seconds, so WiFi never got a rest at home.
    const bool alerting = (state == AppState::WATCH_ALERT);
    const bool wantOn = duty == 0 || (!g_consoleRadioTest && (usb || !s_panelAsleep)) || alerting ||
                        state == AppState::UPDATE || OtaWifi::state() != OtaWifi::State::OFF;
    uint32_t onMs = 5000, offMs = 25000;
    if (duty == 2) { onMs = 10000; offMs = 50000; }
    if (wantOn) {
        static uint32_t saidAt = 0;
        if (g_consoleRadioTest && now - saidAt >= 10000) {
            saidAt = now;
            Serial.printf("[radio] held awake: duty %u, alerting %d, app state %u, update state %u\n",
                          (unsigned)duty, (int)alerting, (unsigned)state, (unsigned)OtaWifi::state());
        }
        if (s_radiosResting) { engine.wakeRadios(); s_radiosResting = false; Serial.println("[radio] awake"); }
        phaseAt = now;
        return;
    }
    if (!s_radiosResting) {
        if (now - phaseAt < onMs) return;
        if (engine.restRadios(duty != 3)) {
            s_radiosResting = true;
            arrivalsAt = bleArrivals();
            Serial.printf("[radio] resting: %s\n", duty == 3 ? (owedMs ? "WiFi off, BLE on, paying back an early window" : "WiFi off, BLE on") : "WiFi and BLE off");
        }
        phaseAt = now;   // either way: a refused rest tries again after another on-window
    } else {
        const uint32_t rested = now - phaseAt;
        // Only with Bluetooth listening, only once a rest has settled, and
        // not on a rest that is paying back the last early one.
        const bool arrival = duty == 3 && !owedMs && rested >= 3000 && rested < offMs &&
                             bleArrivals() != arrivalsAt;
        if (arrival || rested >= offMs + owedMs) {
            owedMs = arrival ? offMs - rested : 0;
            engine.wakeRadios(); s_radiosResting = false; phaseAt = now;
            if (arrival) Serial.printf("[radio] awake early: a new Bluetooth device, %lu s into the rest\n", (unsigned long)(rested / 1000));
            else         Serial.println("[radio] awake (timed)");
        }
    }
}

// The radio self-heal. The watch's radios have gone deaf more than once --
// started, configured, hearing nothing -- sometimes both, sometimes only
// Bluetooth (2026-09-24 at work: WiFi came back by itself, Bluetooth only
// after the crown switched the watch off and on). Bluetooth is the tell: it
// listens nearly all the time, and there is hardly a place with no adverts in
// it. So: two minutes of Bluetooth listening without one advert, at any point
// in the day and not just after boot, and the watch restarts itself.
//
// Never the software kind (ESP.restart): parts of the chip, the radio's
// analog side among them, stay powered through that, and every cure seen so
// far was deeper -- the cable reset of an esptool MAC check (a whole-chip
// reset) and the crown's off-and-on. Firmware cannot reach either. The PMU's
// own "restart the SoC" (reg 0x10 bit 1) was tried first and HUNG the watch
// on the wrist until a crown hard-off (2026-09-24), so it is out. What is
// left is a second of deep sleep: the radio, the CPU and the digital core are
// powered down, and the chip's RTC timer, which needs nothing from the PMU,
// wakes it into a full boot. At most HEAL_MAX in a row: the count clears the
// moment an advert arrives, so a field with no radios in it costs two
// restarts and then waits, rather than looping.
static const uint8_t  HEAL_MAX       = 2;
static const uint32_t HEAL_DEAF_MS   = 120000;   // of Bluetooth listening, not of clock
// Power the chip down for a second and let its own timer bring it back into
// a full boot, leaving `count` in RTC memory (which deep sleep keeps) for the
// next boot to read. The black box gets a battery line first, with why, so a
// morning read-out shows every one of these.
static void twatchPowerCycle(uint8_t count, uint8_t why) {
    twatchBatterySample(why);
    Serial.println("[heal] a second of deep sleep, then a full boot");
    serialFlush();
    g_healWord = HEAL_MAGIC | (uint32_t)count;
    engine.restRadios(true);   // WiFi stopped and Bluetooth's scan asked to stop, as a rest does
    delay(100);
    esp_sleep_enable_timer_wakeup(1000000ULL);
    esp_deep_sleep_start();
}

static bool bleShouldHear() {
    // Resting with Bluetooth off (the 5/30 and 10/60 duties), a raw scan, an
    // update: none of those are the radio's fault.
    if (s_radiosResting && Settings::radioDuty() != 3) return false;
    if (onRawScanScreen() || state == AppState::UPDATE || OtaWifi::state() != OtaWifi::State::OFF) return false;
    return true;
}
static void twatchRadioHealTick(uint32_t now) {
    static bool     said = false, gaveUp = false;
    static uint32_t lastAdv = 0, lastAt = 0, deafMs = 0;
    if (!said) {
        said = true;
        lastAt = now; lastAdv = advertsSeen();
        if (s_healCount) Serial.printf("[heal] this boot is self-heal restart %u of %u\n", (unsigned)s_healCount, (unsigned)HEAL_MAX);
    }
    const bool forced = g_consoleHeal;
    g_consoleHeal = false;
    if (!forced) {
        if (now - lastAt < 1000) return;
        const uint32_t adv = advertsSeen();
        if (adv != lastAdv) {
            lastAdv = adv; deafMs = 0; gaveUp = false;
            if (s_healCount) {
                Serial.printf("[heal] hearing again after %u self-heal restart(s)\n", (unsigned)s_healCount);
                s_healCount = 0;   // the next deaf spell starts from one
            }
        } else if (bleShouldHear()) {
            deafMs += now - lastAt;
        }
        lastAt = now;
        if (deafMs < HEAL_DEAF_MS) return;
        deafMs = 0;
    }
    if (s_healCount >= HEAL_MAX) {
        if (!gaveUp) Serial.printf("[heal] still deaf after %u restarts; waiting until it hears something\n", (unsigned)s_healCount);
        gaveUp = true;
        return;
    }
    const uint8_t next = (uint8_t)(s_healCount + 1);
    Serial.printf("[heal] Bluetooth deaf: no adverts in %lu s of listening (%lu WiFi frames since boot)%s; restarting (%u of %u)\n",
                  (unsigned long)(HEAL_DEAF_MS / 1000), (unsigned long)wifiFramesSeen(), forced ? " (bench test)" : "",
                  (unsigned)next, (unsigned)HEAL_MAX);
    twatchPowerCycle(next, BlackBox::BATT_WHY_HEAL);
}

// Polled ten times a second: one I2C read of the PMU's interrupt flags. A
// short press of the crown counts as a touch, which is what wakes the screen
// and holds off the timeout; nothing else is bound to it yet.
static void twatchCrownTick(uint32_t now) {
    static uint32_t at = 0;
    if (!s_pmuOk || now - at < 100) return;
    at = now;
    // The haptic driver back to standby once the longest pattern (three
    // buzzes, about 1.5 s) is surely over.
    if (s_drvAwakeAt && now - s_drvAwakeAt > 3000) { s_drvAwakeAt = 0; drvWrite(0x01, 0x40); }
    s_pmu.getIrqStatus();
    if (s_pmu.isPekeyShortPressIrq()) pocketPress(now);
    s_pmu.clearIrqStatus();
}
#endif
