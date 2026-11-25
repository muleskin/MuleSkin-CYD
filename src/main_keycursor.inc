// MuleSkin-CYD -- part of src/main.cpp, included where it stood:
// The small boards' keys standing in for a finger: the StickS3 and Cardputer
// key cursor, the T-Deck Plus trackball, Cardputer typing, the lists that
// scroll under the cursor and the focus highlight (SQW_KEYCURSOR builds).
//
// One translation unit with main.cpp: this uses the globals and helpers
// declared above its #include there, and what follows it uses this, so
// it is not compiled on its own and its place in main.cpp matters.

#if SQW_KEYCURSOR
// ---- the small M5 boards' keys, standing in for a finger ---------------------
// From upstream SquachWatch v1.31.0, reworked in v1.42 (one button scheme,
// whole-button highlight, lists that scroll and loop, real stops on every
// screen), laid out for MuleSkin's own screens. No touch panel, so the keys
// drive the same tap handling every screen already has:
//   StickS3:   FRONT click steps to the next stop, FRONT held selects;
//              SIDE click is BACK, SIDE held is HOME.
//   Cardputer: the arrows (and TAB) step, ENTER or space selects; ESC is
//              BACK, ESC held 3 s is HOME; DEL is BACK too. Letters press
//              the button whose label starts with them (see the hotkeys).
//   T-Deck:    the ball rolls the cursor, a press of it presses there;
//              Backspace off a text screen is BACK.
// A stop is wherever the screen's own hit test answers, found by sweeping
// it, so the cursor lands on what the screen really draws. BACK is a tap on
// the screen's own way back (stickBackPoint), never a guessed spot. A screen
// with no stops of its own gets BACK and a grid, which reaches everything,
// slowly.
static bool           s_stickKey2Was = false, s_stickKey2Long = false, s_stickKey1Was = false;
static uint32_t       s_stickKey2At = 0;
static const uint32_t STICK_HOLD_MS = 600, STICK_SHOW_MS = 8000;
#if defined(SQW_TRACKBALL)
static const uint32_t TDECK_CURSOR_MS = 15000;   // the T-Deck's highlight, after the ball last moved
#endif
// kind 0 presses; 1 scrolls the list down, 2 up; 3 presses and holds for a
// long press (a LOG row's WATCH/HUNT panel opens on a hold).
struct StickPt { int16_t x, y; uint8_t kind; };
static uint32_t       s_stickHoldUntil = 0;   // a scripted press, held for the screen
static StickPt        s_stickHoldPt;
static const int      STICK_KEY1 = 11, STICK_KEY2 = 12;
// What a board's keys ask of the cursor this frame. `press` is a level --
// held is held -- and the rest happen once. `select` and `back` each become
// a scripted tap (see stickPoll), at the cursor or at the screen's own way
// back. `ch` is a character typed on the Cardputer's keyboard while a text
// screen is up; `hot` a letter pressed anywhere else, for the hotkeys.
struct SmallKeys { bool press, next, prev, home, back, select; char ch, hot; };
#if SQW_DEVTOOLS
// Dev builds only: keys typed on the serial console (KEY NEXT / PREV / HOME /
// BACK / SELECT / PRESS [ms], TYPE text), so a screen can be reached without
// a hand on the device. Set by clock.cpp's console, merged into the real keys
// by stickPoll(). (Upstream SquachWatch v1.42.)
volatile uint8_t  g_devKey = 0;          // 1 next, 2 prev, 3 home, 4 press, 5 back, 6 select
volatile uint16_t g_devKeyMs = 0;        // how long PRESS holds
volatile bool     g_devShot = false;     // SHOT: dump the next frame
char              g_devType[48] = "";    // TYPE text: keys from the console, one a poll
volatile uint8_t  g_devTypeAt = 0, g_devTypeN = 0;
static uint32_t   s_devPressUntil = 0;
#if defined(CARDPUTER_ADV)
static bool cardTyping();   // with the keyboard, below
#endif
static void devKeysMerge(SmallKeys& k) {
    const uint8_t key = g_devKey;
    if (key) {
        g_devKey = 0;
        if (key == 1) k.next = true;
        else if (key == 2) k.prev = true;
        else if (key == 3) k.home = true;
        else if (key == 4) s_devPressUntil = millis() + (g_devKeyMs ? g_devKeyMs : 120);
        else if (key == 5) k.back = true;
        else if (key == 6) k.select = true;
    }
    if (s_devPressUntil) {
        if ((int32_t)(millis() - s_devPressUntil) < 0) k.press = true;
        else s_devPressUntil = 0;
    }
#if defined(CARDPUTER_ADV)
    // TYPE: as if typed on the keyboard, a character a poll. '~' is ENTER
    // and '^' is DEL (a console line cannot carry them).
    if (g_devTypeAt < g_devTypeN) {
        char c = g_devType[g_devTypeAt++];
        if (c == '~') c = '\n'; else if (c == '^') c = '\b';
        if (cardTyping()) k.ch = c;
        else if (c >= 'a' && c <= 'z') k.hot = c;
    }
#endif
}
#endif
#if defined(CARDPUTER_ADV)
// The Cardputer ADV's keyboard: a TCA8418 at 0x34 on SDA 8 / SCL 9, scanning
// a 7x8 matrix that M5 lays out as four rows of fourteen (their remap, from
// M5Cardputer's TCA8418 reader). Polled: the chip keeps a ten-event FIFO.
static bool s_cardKeysOk = false, s_cardEnter = false;
static bool cardReg(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(0x34);
    Wire.write(reg); Wire.write(v);
    return Wire.endTransmission() == 0;
}
static int cardRead(uint8_t reg) {
    Wire.beginTransmission(0x34);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x34, 1) != 1) return -1;
    return Wire.read();
}
static bool cardKeysBegin() {
    Wire.begin(8, 9, 400000);
    bool ok = cardReg(0x1D, 0x7F);      // rows 0-6 are keypad rows
    ok = cardReg(0x1E, 0xFF) && ok;     // columns 0-7
    ok = cardReg(0x1F, 0x00) && ok;
    ok = cardReg(0x01, 0x01) && ok;     // key events into the FIFO
    for (int i = 0; i < 12 && (cardRead(0x03) & 0x0F) > 0; i++) cardRead(0x04);   // whatever was held at boot
    cardReg(0x02, 0x0F);
    s_cardKeysOk = ok;
    return ok;
}
// ESC: a tap is BACK, held for CARD_ESC_HOME_MS it is HOME, from anywhere.
static bool     s_cardEscDown = false, s_cardEscFired = false;
static uint32_t s_cardEscAt = 0;
static const uint32_t CARD_ESC_HOME_MS = 3000;
static bool     s_cardShift = false, s_cardFn = false;
// M5's own layout (M5Cardputer's Keyboard_def.h): four rows of fourteen,
// each key's plain and shifted character. 0 marks a key that types nothing:
// DEL, TAB, FN, SHIFT, ENTER, CTRL, OPT, ALT (handled by position below).
static const char CARD_KEYMAP[4][14][2] = {
    { {'`','~'}, {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'}, {'5','%'}, {'6','^'}, {'7','&'}, {'8','*'}, {'9','('}, {'0',')'}, {'-','_'}, {'=','+'}, {0,0} },
    { {0,0},     {'q','Q'}, {'w','W'}, {'e','E'}, {'r','R'}, {'t','T'}, {'y','Y'}, {'u','U'}, {'i','I'}, {'o','O'}, {'p','P'}, {'[','{'}, {']','}'}, {'\\','|'} },
    { {0,0},     {0,0},     {'a','A'}, {'s','S'}, {'d','D'}, {'f','F'}, {'g','G'}, {'h','H'}, {'j','J'}, {'k','K'}, {'l','L'}, {';',':'}, {'\'','"'}, {0,0} },
    { {0,0},     {0,0},     {0,0},     {'z','Z'}, {'x','X'}, {'c','C'}, {'v','V'}, {'b','B'}, {'n','N'}, {'m','M'}, {',','<'}, {'.','>'}, {'/','?'}, {' ',' '} },
};
// A text screen is up: letters type, and the arrow keys (; , . /) type their
// own characters unless FN is held. ESC still goes back, and held, home.
// The WiFi password keyboard is also MY ALERTS' pattern and label keyboard;
// the PIN pad takes digits (and letters, which it ignores).
static char s_cardHot = 0;   // a hotkey letter, for loop()
static bool stickCursorShown(uint32_t now);
static bool cardTyping() {
    return state == AppState::WIFI_PASS || state == AppState::PIN_ENTRY || state == AppState::LOCKED;
}
static SmallKeys smallKeysRead() {
    SmallKeys k = {};
    if (!s_cardKeysOk) return k;
    const uint32_t now = millis();
    if (s_cardEscDown && !s_cardEscFired && now - s_cardEscAt >= CARD_ESC_HOME_MS) {
        s_cardEscFired = true;
        k.home = true;
    }
    for (int i = 0; i < 10; i++) {
        const int cnt = cardRead(0x03);
        if (cnt < 0 || (cnt & 0x0F) == 0) break;
        const int ev = cardRead(0x04);
        if (ev <= 0) break;
        const bool down = (ev & 0x80) != 0;
        const int  code = (ev & 0x7F) - 1;
        const int  row = (code % 10) % 4, col = (code / 10) * 2 + ((code % 10) > 3 ? 1 : 0);
        if (row == 2 && col == 1) { s_cardShift = down; continue; }       // SHIFT
        if (row == 2 && col == 0) { s_cardFn = down; continue; }          // FN
        const bool typing = cardTyping();
        if (typing && !(row == 0 && col == 0)) {
            if (!down) continue;
            const bool arrow = (row == 2 && col == 11) || (row == 3 && col >= 10 && col <= 12);
            if (arrow && s_cardFn) {                                      // FN + arrow still moves
                if (row == 3 && col >= 11) k.next = true; else k.prev = true;
                continue;
            }
            if (row == 0 && col == 13)      k.ch = '\b';                     // DEL
            else if (row == 2 && col == 13) k.ch = '\n';                     // ENTER
            else if (row == 1 && col == 0)  k.ch = '\t';                     // TAB: SHOW/HIDE
            else if (CARD_KEYMAP[row][col][0]) k.ch = CARD_KEYMAP[row][col][s_cardShift ? 1 : 0];
            continue;
        }
        const bool enter = (row == 2 && col == 13) || (row == 3 && col == 13);     // ENTER, or the space bar
        if (enter) { s_cardEnter = down; continue; }
        if (row == 0 && col == 0) {                                          // ESC
            if (down) { s_cardEscDown = true; s_cardEscFired = false; s_cardEscAt = now; }
            else {
                if (s_cardEscDown && !s_cardEscFired) k.back = true;
                s_cardEscDown = false;
            }
            continue;
        }
        if (!down) continue;
        if ((row == 3 && (col == 11 || col == 12)) || (row == 1 && col == 0)) k.next = true;   // down, right, TAB
        else if ((row == 2 && col == 11) || (row == 3 && col == 10))          k.prev = true;   // up, left
        else if (row == 0 && col == 13)                                       k.back = true;   // DEL
        else if (CARD_KEYMAP[row][col][0] >= 'a' && CARD_KEYMAP[row][col][0] <= 'z') k.hot = CARD_KEYMAP[row][col][0];
    }
    cardReg(0x02, 0x0F);
    // ENTER is a level, as a finger is: held is held, so a long press is
    // still one. With the cursor hidden (the main screen, a while after the
    // last key) it only shows the cursor, as the T-Deck's ball does, and
    // presses nothing until it is let go.
    static bool enterWas = false, enterShowOnly = false;
    if (s_cardEnter && !enterWas) {
        enterShowOnly = !stickCursorShown(now);
        if (enterShowOnly) k.select = true;
    }
    enterWas = s_cardEnter;
    k.press = s_cardEnter && !enterShowOnly;
    return k;
}
#elif defined(SQW_TRACKBALL)
// The T-Deck's trackball: four hall sensors round the ball, one line each
// (up GPIO3, down 15, left 1, right 2: LilyGo's utilities.h), each toggling
// as the ball rolls that way, and the ball itself a button on GPIO0 (BOOT,
// LOW while pressed). Every edge is counted in an interrupt; a few edges the
// same way make one step of the cursor: down and right to the next stop, up
// and left to the one before. The press is the press at the cursor, held as
// long as the ball is, so a long press is still one. With the cursor hidden,
// the first roll or press only shows it. The keyboard's Backspace, off a
// text screen, is BACK (tdeckKeysTick). Console: TBALL traces it, TBALL n
// sets edges a step.
static const int TB_UP = 3, TB_DOWN = 15, TB_LEFT = 1, TB_RIGHT = 2, TB_PRESS = 0;
static volatile uint16_t s_tbEdges[4];   // up, down, left, right
static void IRAM_ATTR tbIsrUp()    { s_tbEdges[0]++; }
static void IRAM_ATTR tbIsrDown()  { s_tbEdges[1]++; }
static void IRAM_ATTR tbIsrLeft()  { s_tbEdges[2]++; }
static void IRAM_ATTR tbIsrRight() { s_tbEdges[3]++; }
volatile uint8_t g_ballEdgesPerStep = 4;   // TBALL n on the console
volatile bool    g_ballTrace = false;      // TBALL: print the edges as they come
static uint16_t s_tbSeen[4];
static int16_t  s_tbAcc = 0;               // edges towards the next stop (+) or the last (-)
static uint32_t s_tbLastEdge = 0;
static bool     s_tbPressRaw = false, s_tbPress = false, s_tbPressShowOnly = false;
static uint32_t s_tbPressAt = 0;
static bool     s_tbOk = false;
static bool     s_tdeckBackKey = false;    // Backspace off a text screen
static const uint32_t TB_ROLL_GAP_MS = 400;   // a pause this long starts a new roll
static bool stickCursorShown(uint32_t now);
static bool stickListBusy();
static void tdeckBallBegin() {
    static const int pins[4] = { TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT };
    static void (*const isr[4])() = { tbIsrUp, tbIsrDown, tbIsrLeft, tbIsrRight };
    for (int i = 0; i < 4; i++) {
        pinMode(pins[i], INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(pins[i]), isr[i], CHANGE);
        s_tbSeen[i] = s_tbEdges[i];
    }
    pinMode(TB_PRESS, INPUT_PULLUP);
    s_tbOk = true;
    Serial.printf("[keys] trackball on GPIO %d/%d/%d/%d, press GPIO %d, %u edges a step\n",
                  TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT, TB_PRESS, (unsigned)g_ballEdgesPerStep);
}
// TBALL on the console: every edge counted since boot and each line's level
// now, so a ball that seems dead can be told from one nobody is rolling.
void tdeckBallStatus() {
    Serial.printf("[ball] %s; edges since boot up %u down %u left %u right %u; lines up %d down %d left %d right %d press %d\n",
                  s_tbOk ? "on" : "NOT STARTED",
                  (unsigned)s_tbEdges[0], (unsigned)s_tbEdges[1], (unsigned)s_tbEdges[2], (unsigned)s_tbEdges[3],
                  digitalRead(TB_UP), digitalRead(TB_DOWN), digitalRead(TB_LEFT), digitalRead(TB_RIGHT), digitalRead(TB_PRESS));
}
// Anything for stickPoll to do: a roll not yet taken, the ball down, BACK.
static bool tdeckBallPending() {
#if SQW_DEVTOOLS
    if (g_devKey || s_devPressUntil) return true;   // KEY NEXT / PRESS from the console
#endif
    if (!s_tbOk) return s_tdeckBackKey;
    for (int i = 0; i < 4; i++) if (s_tbEdges[i] != s_tbSeen[i]) return true;
    const int step = g_ballEdgesPerStep ? g_ballEdgesPerStep : 1;
    return s_tdeckBackKey || s_tbPress || s_tbPressRaw || digitalRead(TB_PRESS) == LOW ||
           s_tbAcc >= step || s_tbAcc <= -step;
}
// Off the glass: nothing the ball did before the finger counts.
static void tdeckBallForget() {
    for (int i = 0; i < 4; i++) s_tbSeen[i] = s_tbEdges[i];
    s_tbAcc = 0;
}
static SmallKeys smallKeysRead() {
    SmallKeys k = {};
    if (s_tdeckBackKey) { s_tdeckBackKey = false; k.back = true; }
    if (!s_tbOk) return k;
    const uint32_t now = millis();
    uint16_t d[4];
    for (int i = 0; i < 4; i++) {
        const uint16_t e = s_tbEdges[i];
        d[i] = (uint16_t)(e - s_tbSeen[i]);
        s_tbSeen[i] = e;
    }
    const int step = g_ballEdgesPerStep ? g_ballEdgesPerStep : 1;
    const int fwd = d[1] + d[3], bwd = d[0] + d[2];
    if (fwd || bwd) {
        if (now - s_tbLastEdge > TB_ROLL_GAP_MS) s_tbAcc = 0;   // a new roll: the last one's leftovers don't count
        s_tbLastEdge = now;
        int acc = s_tbAcc + fwd - bwd;
        // A hard flick queues at most three steps, not a page of them.
        if (acc > 3 * step) acc = 3 * step; else if (acc < -3 * step) acc = -3 * step;
        s_tbAcc = (int16_t)acc;
        if (g_ballTrace) Serial.printf("[ball] up %u down %u left %u right %u -> %d\n", d[0], d[1], d[2], d[3], acc);
    }
    // One step a poll; the rest wait while a list step settles.
    if (!stickListBusy()) {
        if (s_tbAcc >= step)       { k.next = true; s_tbAcc -= step; }
        else if (s_tbAcc <= -step) { k.prev = true; s_tbAcc += step; }
    }
    const bool raw = digitalRead(TB_PRESS) == LOW;
    if (raw != s_tbPressRaw) { s_tbPressRaw = raw; s_tbPressAt = now; }
    else if (raw != s_tbPress && now - s_tbPressAt >= 20) {   // 20 ms steady: a press, not a bounce
        s_tbPress = raw;
        if (raw) {
            s_tbPressShowOnly = !stickCursorShown(now);
            if (s_tbPressShowOnly) k.select = true;   // with the cursor hidden: only shows it
            if (g_ballTrace) Serial.printf("[ball] press%s\n", s_tbPressShowOnly ? " (shows the cursor)" : "");
        }
    }
    // POCKET MODE: the press may be its own (held 2 s, or waking a dark
    // screen); then nothing else sees it, nor the ball rolled in the dark.
    if (tdeckPocketBall(s_tbPress, s_tbPressAt, now)) { s_tbAcc = 0; return SmallKeys{}; }
    k.press = s_tbPress && !s_tbPressShowOnly;
    return k;
}
#else
// The StickS3, the same scheme as upstream's FUD build: FRONT (KEY1) click
// steps to the next stop, FRONT held selects. SIDE (KEY2) click is BACK,
// SIDE held is HOME. A click acts on release, so a hold never also counts
// as a click.
static bool     s_stickFrontWas = false, s_stickFrontLong = false;
static uint32_t s_stickFrontAt = 0;
static const uint32_t STICK_SELECT_MS = 500;
static SmallKeys smallKeysRead() {
    SmallKeys k = {};
    const uint32_t now = millis();
    const bool k1 = digitalRead(STICK_KEY1) == LOW;
    if (k1 && !s_stickFrontWas) { s_stickFrontAt = now; s_stickFrontLong = false; }
    if (k1 && !s_stickFrontLong && now - s_stickFrontAt >= STICK_SELECT_MS) { s_stickFrontLong = true; k.select = true; }
    if (!k1 && s_stickFrontWas && !s_stickFrontLong && now - s_stickFrontAt > 25) k.next = true;
    s_stickFrontWas = k1;
    const bool k2 = digitalRead(STICK_KEY2) == LOW;
    if (k2 && !s_stickKey2Was) { s_stickKey2At = now; s_stickKey2Long = false; }
    if (k2 && !s_stickKey2Long && now - s_stickKey2At >= STICK_HOLD_MS) { s_stickKey2Long = true; k.home = true; }
    if (!k2 && s_stickKey2Was && !s_stickKey2Long && now - s_stickKey2At > 25) k.back = true;
    s_stickKey2Was = k2;
    return k;
}
#endif
static uint8_t        s_stickStop = 0;
static AppState       s_stickStopState = AppState::BOOT;
static uint32_t       s_stickShownAt = 0;       // the cursor shows for a while after a key
static bool           s_stickHome = false;      // home asked for: loop() goes there
static int8_t         s_stickDrag = 0;          // a scripted drag under way: +1 down the list, -1 up
static uint8_t        s_stickDragStep = 0;
static const uint8_t  STICK_STOPS_MAX = 24;

// What is under a point on a list screen, as that screen's own hit test sees
// it, or -1. The stops are found by sweeping this over the screen, so they
// are wherever the rows really are, scrolled or not.
static int stickListId(int x, int y) {
    const int w = tft.width(), h = tft.height();
    switch (state) {
        case AppState::SETTINGS: {
            if (uiSettingsConfirmRow() != SettingsRow::NONE) {
                // The drawn buttons only: the hit test also cancels on a tap
                // outside the panel, and a stop out there lands nowhere visible.
                const SettingsConfirmTap c = uiSettingsConfirmButtonAt(x, y, w, h);
                return c == SettingsConfirmTap::NONE ? -1 : 1000 + (int)c;
            }
            const SettingsRow r = uiSettingsHitTest(*canvas, x, y, w, h);
            if (r == SettingsRow::NONE) return -1;
            // BRIGHT answers only on its two buttons, each its own stop, so
            // the cursor lands on - or + and never on the label.
            if (r == SettingsRow::BRIGHTNESS) {
                const int d = uiSettingsStepHit(*canvas, x, y, w, h);
                return d ? 3000 + (d > 0 ? 1 : 0) : -1;
            }
            return (int)r;
        }
        case AppState::POWER_SAVER: {
            const PowerRow r = uiPowerHitTest(*canvas, x, y, w, h);
            return r == PowerRow::NONE ? -1 : (int)r;
        }
        case AppState::SECURITY: {
            const SecurityRow r = uiSecurityHitTest(*canvas, x, y, w, h);
            return r == SecurityRow::NONE ? -1 : (int)r;
        }
        case AppState::STATUS_LIGHT: {
            const LightRow r = uiLightHitTest(*canvas, x, y, w, h);
            return r == LightRow::NONE ? -1 : (int)r;
        }
        case AppState::LOG: {
            if (s_confirmPending) {
                const LogConfirmTap c = uiLogHitConfirm(x, y, w, h);
                return c == LogConfirmTap::NONE ? -1 : 2000 + (int)c;
            }
            const int r = uiLogRowAt(*canvas, x, y, w, h);
            return (r >= 0 && r < (int)uiLogRowCount(engine)) ? r : -1;
        }
        case AppState::DETECTION_FILTER: {
            const DetectionType r = uiDetFilterHitTest(*canvas, x, y, w, h);
            return r == DetectionType::COUNT ? -1 : (int)r;
        }
        case AppState::IGNORE_LIST: {
            const uint8_t r = uiIgnoreListHitRemove(*canvas, x, y, w, h);
            return r == 0xFF ? -1 : (int)r;
        }
        default: return -1;
    }
}
static bool stickIsList() {
    return state == AppState::SETTINGS || state == AppState::POWER_SAVER ||
           state == AppState::SECURITY || state == AppState::DETECTION_FILTER ||
           state == AppState::STATUS_LIGHT || state == AppState::LOG ||
           state == AppState::IGNORE_LIST;
}

// Screens whose every target is found by sweeping one id over the screen:
// each button, and each row with its index, its own id. The stops come out
// in reading order and the highlight grows from the same ids. Only hit tests
// that answer without acting.
static bool stickIsSweep() {
    return state == AppState::COLOR_CHECK || state == AppState::UPDATE ||
           state == AppState::WIFI_NETS || state == AppState::WIFI_ADD ||
           state == AppState::BEACON_WARN || state == AppState::MY_ALERTS ||
           state == AppState::AUTO_UPDATE || state == AppState::HUNT ||
           state == AppState::MEETING || state == AppState::TODAY ||
           state == AppState::TIME_SYNC;
}
static int stickSweepId(int x, int y) {
    const int w = tft.width(), h = tft.height();
    switch (state) {
        case AppState::COLOR_CHECK: {
            const ColorCheckTap c = uiColorCheckHitTest(x, y, w, h);
            return c == ColorCheckTap::NONE ? -1 : (int)c;
        }
        case AppState::UPDATE: {
            int net = 0;
            const UpdateHit u = uiUpdateHitTest(*canvas, x, y, &net);
            return u == UpdateHit::NONE ? -1 : (int)u * 100 + (u == UpdateHit::NETWORK ? net : 0);
        }
        case AppState::WIFI_NETS: {
            int row = 0;
            const WifiNetsHit r = uiWifiNetsHit(*canvas, x, y, &row);
            return r == WifiNetsHit::NONE ? -1 : (int)r * 100 + (r == WifiNetsHit::ROW ? row : 0);
        }
        case AppState::WIFI_ADD: {
            int row = 0;
            const WifiAddHit r = uiWifiAddHit(*canvas, x, y, engine, &row);
            return r == WifiAddHit::NONE ? -1 : (int)r * 100 + (r == WifiAddHit::ROW ? row : 0);
        }
        case AppState::MY_ALERTS: {
            int slot = 0;
            const MyAlertsHit r = uiMyAlertsHit(*canvas, x, y, &slot);
            return r == MyAlertsHit::NONE ? -1 : (int)r * 100 + (r == MyAlertsHit::ROW ? slot : 0);
        }
        case AppState::BEACON_WARN: {
            const BeaconWarnHit r = uiBeaconWarnHitTest(*canvas, x, y);
            return r == BeaconWarnHit::NONE ? -1 : (int)r;
        }
        case AppState::AUTO_UPDATE: {
            const AutoUpdateHit r = uiAutoUpdateHit(*canvas, x, y);
            return r == AutoUpdateHit::NONE ? -1 : (int)r;
        }
        case AppState::HUNT:
            if (uiHuntHitBack(x, y, w, h)) return 1;
            if (uiHuntHitStop(x, y, w, h)) return 2;
            return -1;
        case AppState::MEETING:
            if (uiMeetingHitBack(x, y, w, h)) return 2;
            if (uiMeetingHitSign(x, y, w, h)) return 1;
            return -1;
        case AppState::TODAY:
            return uiTodayHitBack(x, y, w, h) ? 1 : -1;
        case AppState::TIME_SYNC:
            // BACK is only drawn once the job is over.
            return (!OtaWifi::timeSyncBusy() && uiTimeSyncHitBack(x, y, w, h)) ? 1 : -1;
        default: return -1;
    }
}

// The middle of everywhere `hit` answers, as a stop; false if nowhere does.
template <typename F>
static bool stickSweep(F hit, StickPt& at) {
    const int w = tft.width(), h = tft.height();
    int32_t sx = 0, sy = 0, c = 0;
    for (int y = 1; y < h; y += 3)
        for (int x = 2; x < w; x += 4)
            if (hit(x, y)) { sx += x; sy += y; c++; }
    if (!c) return false;
    int cx = sx / c, cy = sy / c;
    // The middle of a region that is not one rectangle can fall outside it,
    // on something else. Never aim there: take the nearest point that does
    // answer.
    if (!hit(cx, cy)) {
        int32_t best = INT32_MAX; int bx = cx, by = cy;
        for (int y = 1; y < h; y += 3)
            for (int x = 2; x < w; x += 4)
                if (hit(x, y)) {
                    const int32_t d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    if (d < best) { best = d; bx = x; by = y; }
                }
        cx = bx; cy = by;
    }
    at = { (int16_t)cx, (int16_t)cy, 0 };
    return true;
}
static bool clearClockAt(int x, int y) { return uiClearClockHit(x, y); }
static bool clearWatchAt(int x, int y) { return uiClearWatchPillHit(x, y); }

// How many of a list screen's stops are its rows (they come first). The
// stops after them are BACK, or LOG's button bar.
static uint8_t s_stickRowsN = 0;
// Whether the screen has anything worth pointing at yet: a scan's results.
static bool    s_stickHasRows = false;
// A screen that hands over its own targets as rectangles (TERMINAL MODE:
// its hit tests act, so they cannot be swept): the stops are the centres,
// and the highlight fills the rectangle itself. A zero rectangle is a stop
// with no button to light (the menu, the clock): a frame round its point.
// (Upstream v1.43.)
struct StickRect { int16_t x, y, w, h; };
static StickRect s_stickRect[STICK_STOPS_MAX];
static bool      s_stickRectOn = false;

// How tall `id` is where it is, measured down a column through (cx, cy).
template <typename F>
static int stickIdHeight(F idAt, int id, int cx, int cy) {
    const int h = tft.height();
    int y0 = cy, y1 = cy;
    while (y0 > 0 && idAt(cx, y0 - 1) == id) y0--;
    while (y1 < h - 1 && idAt(cx, y1 + 1) == id) y1++;
    return y1 - y0 + 1;
}
// Rows cut off by the buttons or the top edge are shorter than whole ones.
// They are not stops: the next step scrolls them into view instead of
// lighting a sliver half under BACK. `group` puts ids that are rows of one
// list together (the rest, single buttons, are never dropped).
struct StickCand { int id; int16_t x, y; int16_t ht; int group; };
static uint8_t stickDropPartial(StickCand* c, uint8_t n) {
    uint8_t k = 0;
    for (uint8_t i = 0; i < n; i++) {
        int maxH = 0, members = 0;
        for (uint8_t j = 0; j < n; j++)
            if (c[j].group == c[i].group && c[i].group >= 0) { members++; if (c[j].ht > maxH) maxH = c[j].ht; }
        const bool partial = c[i].group >= 0 && members >= 2 && c[i].ht * 4 < maxH * 3;
        if (!partial) c[k++] = c[i];
    }
    return k;
}

static uint8_t stickStopsBuild(StickPt* out, uint8_t cap) {
    const int16_t w = (int16_t)tft.width(), h = (int16_t)tft.height();
    uint8_t n = 0;
    s_stickRowsN = 0;
    s_stickHasRows = false;
    s_stickRectOn = false;
    auto add = [&](int x, int y, uint8_t kind = 0) { if (n < cap) out[n++] = { (int16_t)x, (int16_t)y, kind }; };
    if (s_infoPending && (state == AppState::LOG || state == AppState::ALERT)) {
        add(w / 2, h - 17);             // MORE / GOT IT
        return n;
    }
    if (state == AppState::CLEAR && uiZoneCardWanted()) {
        // The time zone card is up over everything: its buttons are the stops.
        int16_t xs[3], ys[3];
        uiZoneCardStops(w, h, xs, ys);
        for (int i = 0; i < 3; i++) add(xs[i], ys[i]);
        return n;
    }
    if (state == AppState::CLEAR && Settings::boringMode()) {
        // TERMINAL MODE: the bar, the clock, the menu and the watch pill as
        // on the radar, then the terminal's headings, rows, scroll arrows and
        // LATEST, each lit whole. A heading sorts (again flips it), a row is
        // picked (again opens its WATCH / HUNT / INFO / IGNORE panel), LATEST
        // opens its device. (Upstream v1.43.)
        auto addR = [&](int x, int y, int rw, int rh) {
            if (n < cap) { s_stickRect[n] = { (int16_t)x, (int16_t)y, (int16_t)rw, (int16_t)rh }; add(x + rw / 2, y + rh / 2); }
        };
        auto addP = [&](int x, int y) {   // no button to light: a frame round it
            if (n < cap) { s_stickRect[n] = { 0, 0, 0, 0 }; add(x, y); }
        };
        const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
        for (int b = 0; b < 3; b++) addR(bar.x[b], bar.y, bar.w[b], bar.h);   // WIFI TIME, LOG, IN MEETING
        StickPt c;
        if (stickSweep(clearClockAt, c)) addP(c.x, c.y);
        addP(13, 11);                   // the menu
        if (stickSweep(clearWatchAt, c)) addP(c.x, c.y);
        int16_t rx[STICK_STOPS_MAX], ry[STICK_STOPS_MAX], rw[STICK_STOPS_MAX], rh[STICK_STOPS_MAX];
        const uint8_t k = uiTerminalStops(rx, ry, rw, rh, STICK_STOPS_MAX);
        for (uint8_t i = 0; i < k; i++) addR(rx[i], ry[i], rw[i], rh[i]);
        s_stickRectOn = true;
        return n;
    }
    if (state == AppState::CLEAR) {
        // The bar first, left to right (WIFI TIME, LOG, IN MEETING), then
        // the clock (TODAY, once it is set), the menu, the watch pill while
        // one is up, and the radar, for a blip near the middle.
        const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
        for (int b = 0; b < 3; b++) add(bar.x[b] + bar.w[b] / 2, bar.y + bar.h / 2);
        StickPt c;
        if (stickSweep(clearClockAt, c)) add(c.x, c.y);
        add(13, 11);                    // the menu
        if (stickSweep(clearWatchAt, c)) add(c.x, c.y);
        add(w / 2, (bar.y + 28) / 2);
        return n;
    }
    if (state == AppState::ALERT) {
        // No stop on the plate: a box over it hid the text, and BACK
        // dismisses it already. HUNT, SNOOZE, INFO, IGNORE.
        int16_t xs[6], ys[6];
        const uint8_t k = uiAlertStops(w, h, xs, ys, 6);
        if (k >= 3) { add(xs[2], ys[2]); add(xs[1], ys[1]); }
        for (uint8_t i = 3; i < k; i++) add(xs[i], ys[i]);
        return n;
    }
    if (state == AppState::SYS_PROPS) {
        int16_t xs[6], ys[6];
        const uint8_t k = uiSysPropsStops(*canvas, xs, ys, 6);
        for (uint8_t i = 0; i < k; i++) add(xs[i], ys[i]);
        return n;
    }
    if (state == AppState::DEX) {
        int16_t xs[STICK_STOPS_MAX], ys[STICK_STOPS_MAX];
        const uint8_t k = uiDexStops(w, h, xs, ys, cap);
        for (uint8_t i = 0; i < k; i++) add(xs[i], ys[i]);
        return n;
    }
    if (state == AppState::OUTFIT_UNLOCK) return 0;   // a card: SELECT or BACK closes it
#if defined(SENSE_CYD_APP)
    if (state == AppState::SENSING) {
        // The page dots (SCOPE, SNEAK, VITALS: the T-Deck) and the three
        // buttons; on the StickS3 and the Cardputer, with no button bar, the
        // banner, where a press re-calibrates (or, with no network saved,
        // opens WIFI NETWORKS). HOME leaves, as from anywhere, and hands the
        // radio back to the detector. (Upstream v1.42/v1.49.)
        int16_t xs[4], ys[4];
        const uint8_t k = uiSensingStops(w, h, xs, ys, 4);
        for (uint8_t i = 0; i < k; i++) add(xs[i], ys[i]);
        return n;
    }
#endif
#if defined(CARDPUTER_ADV)
    if (cardTyping()) return 0;   // the keyboard types; ESC is BACK; nothing to point at
#endif
    if (stickIsSweep()) {
        // Every id the sweep finds is a stop, at the middle of where it
        // answered (checked, as below), in the order first met: reading order.
        struct Acc { int id; int32_t sx, sy; uint16_t c; } acc[STICK_STOPS_MAX];
        uint8_t an = 0;
        for (int y = 2; y < h; y += 4)
            for (int x = 4; x < w; x += 8) {
                const int id = stickSweepId(x, y);
                if (id < 0) continue;
                uint8_t a = 0;
                while (a < an && acc[a].id != id) a++;
                if (a == an) { if (an == STICK_STOPS_MAX) continue; acc[an++] = { id, 0, 0, 0 }; }
                acc[a].sx += x; acc[a].sy += y; acc[a].c++;
            }
        StickCand cand[STICK_STOPS_MAX];
        uint8_t cn = 0;
        for (uint8_t a = 0; a < an; a++) {
            int cx = acc[a].sx / acc[a].c, cy = acc[a].sy / acc[a].c;
            if (stickSweepId(cx, cy) != acc[a].id) {
                int32_t best = INT32_MAX;
                for (int y = 2; y < h; y += 4)
                    for (int x = 4; x < w; x += 8)
                        if (stickSweepId(x, y) == acc[a].id) {
                            const int32_t d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                            if (d < best) { best = d; cx = x; cy = y; }
                        }
            }
            cand[cn++] = { acc[a].id, (int16_t)cx, (int16_t)cy,
                           (int16_t)stickIdHeight(stickSweepId, acc[a].id, cx, cy), acc[a].id / 100 };
        }
        cn = stickDropPartial(cand, cn);
        // A row is an id whose kind holds more than one: a list.
        for (uint8_t i = 0; i < cn; i++) {
            uint8_t same = 0;
            for (uint8_t j = 0; j < cn; j++) if (cand[j].group == cand[i].group) same++;
            if (same >= 2 || (state == AppState::WIFI_ADD && cand[i].group == (int)WifiAddHit::ROW)) s_stickHasRows = true;
            add(cand[i].x, cand[i].y);
        }
        return n;
    }
    if (stickIsList()) {
        // Each thing the hit test names gets one stop, at the middle of
        // everywhere it answered. In reading order, which for rows is down.
        struct Acc { int id; int32_t sx, sy; uint16_t c; } acc[12];
        uint8_t an = 0;
        for (int y = 2; y < h; y += 4)
            for (int x = 6; x < w; x += 12) {
                const int id = stickListId(x, y);
                if (id < 0) continue;
                uint8_t a = 0;
                while (a < an && acc[a].id != id) a++;
                if (a == an) { if (an == 12) continue; acc[an++] = { id, 0, 0, 0 }; }
                acc[a].sx += x; acc[a].sy += y; acc[a].c++;
            }
        // LOG's rows open their WATCH/HUNT panel on a long press.
        const bool logRows = state == AppState::LOG && !s_confirmPending;
        StickCand cand[12];
        uint8_t cn = 0;
        for (uint8_t a = 0; a < an; a++) {
            int cx = acc[a].sx / acc[a].c, cy = acc[a].sy / acc[a].c;
            if (stickListId(cx, cy) != acc[a].id) {
                // The middle missed (an L-shaped or split region): the nearest
                // point that is really this thing.
                int32_t best = INT32_MAX;
                for (int y = 2; y < h; y += 4)
                    for (int x = 6; x < w; x += 12)
                        if (stickListId(x, y) == acc[a].id) {
                            const int32_t d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                            if (d < best) { best = d; cx = x; cy = y; }
                        }
            }
            // Ids from 1000 up are panels' buttons and the stepper's halves,
            // not rows: never dropped.
            cand[cn++] = { acc[a].id, (int16_t)cx, (int16_t)cy,
                           (int16_t)stickIdHeight(stickListId, acc[a].id, cx, cy), acc[a].id < 1000 ? 0 : -1 };
        }
        cn = stickDropPartial(cand, cn);
        for (uint8_t i = 0; i < cn; i++) {
            if (cand[i].group == 0) s_stickHasRows = true;
            add(cand[i].x, cand[i].y, logRows ? 3 : 0);
        }
        const bool panel = (state == AppState::SETTINGS && uiSettingsConfirmRow() != SettingsRow::NONE) ||
                           (state == AppState::LOG && s_confirmPending);
        // No scroll arrows: stepping past the last row scrolls the list
        // (see stickListStep), so the rows are all the cursor needs.
        if (!panel) s_stickRowsN = n;
        if (logRows) {
            // < LOG > (home) and [ CLR ]: LOG's bar has nothing in the first slot.
            const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
            for (int b = 1; b < 3; b++) add(bar.x[b] + bar.w[b] / 2, bar.y + bar.h / 2);
        } else if (!panel) {
            add(w / 2, h - Theme::pinnedBackH(w) / 2);                      // BACK
        }
        return n;
    }
    if (state == AppState::DIAGNOSTICS) {
        if (Theme::compact()) add(w / 2, h / 2 - 10);   // the next page, a page at a time at 135 rows
        StickPt c;
        if (stickSweep([&](int x, int y) { return uiDiagnosticsHitBack(x, y, w, h); }, c)) add(c.x, c.y);
        return n;
    }
    if (state == AppState::WATCH_ALERT) {
        add(w / 2, h / 3);              // anywhere dismisses
        StickPt c;
        if (stickSweep([&](int x, int y) { return uiWatchAlertHitRemove(*canvas, x, y); }, c)) add(c.x, c.y);
        else add(w / 2, h - 14);        // REMOVE FROM WATCH LIST
        return n;
    }
    if (state == AppState::DIARY) {
        add(w / 2, h / 2);              // anywhere goes back
        return n;
    }
    if (state == AppState::OUTFIT) {
        add(w - 17, h - 22);            // the next outfit
        add(17, h - 22);                // the one before
        add(w / 2, h / 2);              // anywhere else goes back
        return n;
    }
#if MULESKIN_LORA
    if (state == AppState::LORA_CHAT) {
        // The two tabs, then the chats themselves, scrolled by a scripted
        // drag (no row is a target), then BACK.
        StickPt c;
        if (stickSweep([&](int x, int y) { return uiLoraChatHit(*canvas, x, y) == LoraChatHit::TAB_MESHTASTIC; }, c)) add(c.x, c.y);
        if (stickSweep([&](int x, int y) { return uiLoraChatHit(*canvas, x, y) == LoraChatHit::TAB_MESHCORE; }, c))   add(c.x, c.y);
        const int backH = Theme::pinnedBackH(w);
        add(w - 9, h - backH - 9, 1);                                   // older
        add(w - 9, Theme::LIST_TOP + 30, 2);                            // newer
        add(w / 2, h - backH / 2);                                      // BACK
        return n;
    }
#endif
    add(w / 2, h - 12);                 // BACK, pinned to the bottom edge
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) add(w * (2 * c + 1) / 8, h * (2 * r + 1) / 6);
    return n;
}
// The sweep is a few hundred hit tests, so it is not run twice a frame: the
// answer is kept for a moment, and thrown away when the screen or a key moves.
static StickPt  s_stickPts[STICK_STOPS_MAX];
static uint8_t  s_stickPtsN = 0;
static uint32_t s_stickPtsAt = 0;
static AppState s_stickPtsState = AppState::BOOT;
static uint8_t stickStops(StickPt* out, bool fresh = false) {
    const uint32_t now = millis();
    if (fresh || !s_stickPtsAt || state != s_stickPtsState || now - s_stickPtsAt > 200) {
        s_stickPtsN = stickStopsBuild(s_stickPts, STICK_STOPS_MAX);
        s_stickPtsAt = now ? now : 1;
        s_stickPtsState = state;
    }
    memcpy(out, s_stickPts, sizeof(StickPt) * s_stickPtsN);
    return s_stickPtsN;
}

#if defined(ESP32) && !defined(TWATCH_S3)
static bool chargeModeOn();                 // below, with CHARGE MODE
#else
static bool chargeModeOn() { return false; }   // no CHARGE MODE (the watch)
#endif

// Where BACK taps on this screen: its own BACK / CANCEL, found with the
// screen's own hit test where that test only answers and never acts, and at
// a fixed point where the screen closes on a tap anywhere. False when the
// screen has no way back of its own; the caller goes home instead. Never a
// guessed spot: upstream had one land on a confirm button.
static bool stickBackPoint(StickPt& at) {
    const int w = tft.width(), h = tft.height();
    auto put = [&](int x, int y) { at = { (int16_t)x, (int16_t)y, 0 }; return true; };
#define sweep(fn) stickSweep(fn, at)
    if (s_infoPending && (state == AppState::LOG || state == AppState::ALERT)) return put(w / 2, h - 17);   // GOT IT
    switch (state) {
        case AppState::CLEAR:
#if defined(SQW_SMALL)
            // Home has nowhere to go back to: BACK opens the menu instead
            // (upstream v1.43 does it for the Cardputer's ESC).
            if (!uiZoneCardWanted()) return put(13, 11);
#endif
            return false;
        case AppState::LOG:
            if (s_confirmPending)
                return sweep([&](int x, int y) { return uiLogHitConfirm(x, y, w, h) == LogConfirmTap::CANCEL; });
            return false;   // < LOG > goes home, which is where false goes
        case AppState::SETTINGS:
            if (uiSettingsConfirmRow() != SettingsRow::NONE)
                return sweep([&](int x, int y) { return uiSettingsConfirmButtonAt(x, y, w, h) == SettingsConfirmTap::CANCEL; });
            return sweep([&](int x, int y) { return uiSettingsTapPinnedBack(*canvas, x, y, w, h); });
        case AppState::POWER_SAVER:
        case AppState::SECURITY:
        case AppState::DETECTION_FILTER:
        case AppState::STATUS_LIGHT:
        case AppState::IGNORE_LIST:
#if MULESKIN_LORA
        case AppState::LORA_CHAT:
#endif
            return put(w / 2, h - Theme::pinnedBackH(w) / 2);
        case AppState::DEX:
            if (uiDexCardOpen()) {
                const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
                return put(bar.x[1] + bar.w[1] / 2, bar.y + bar.h / 2);   // DEX: back to the index
            }
            return put(w / 2, h - Theme::pinnedBackH(w) / 2);
        case AppState::HUNT:        return sweep([&](int x, int y) { return uiHuntHitBack(x, y, w, h); });
        case AppState::DIAGNOSTICS: return sweep([&](int x, int y) { return uiDiagnosticsHitBack(x, y, w, h); });
        case AppState::MEETING:     return sweep([&](int x, int y) { return uiMeetingHitBack(x, y, w, h); });
        case AppState::TODAY:       return sweep([&](int x, int y) { return uiTodayHitBack(x, y, w, h); });
        case AppState::TIME_SYNC:
            if (OtaWifi::timeSyncBusy()) return false;
            return sweep([&](int x, int y) { return uiTimeSyncHitBack(x, y, w, h); });
        case AppState::ALERT:       return put(w / 3, h / 2);   // the plate: a tap anywhere dismisses
        case AppState::WATCH_ALERT: return put(w / 2, h / 3);
        case AppState::DIARY:
        case AppState::OUTFIT:
        case AppState::OUTFIT_UNLOCK:
            return put(w / 2, h / 2);   // a tap away from the arrows goes back; the card closes on any
        // Screens whose BACK is only found by a test that answers without
        // acting. The choice on a question screen is the safe one.
        case AppState::WIFI_NETS:
            return sweep([&](int x, int y) { return uiWifiNetsHit(*canvas, x, y, nullptr) == WifiNetsHit::BACK; });
        case AppState::WIFI_ADD:
            return sweep([&](int x, int y) { return uiWifiAddHit(*canvas, x, y, engine, nullptr) == WifiAddHit::BACK; });
        case AppState::MY_ALERTS:
            return sweep([&](int x, int y) { return uiMyAlertsHit(*canvas, x, y, nullptr) == MyAlertsHit::BACK; });
        case AppState::UPDATE: {
            // BACK on the menu; CANCEL (or OK) while something runs.
            if (sweep([&](int x, int y) { return uiUpdateHitTest(*canvas, x, y, nullptr) == UpdateHit::BACK; })) return true;
            if (sweep([&](int x, int y) { return uiUpdateHitTest(*canvas, x, y, nullptr) == UpdateHit::CANCEL; })) return true;
            return sweep([&](int x, int y) { return uiUpdateHitTest(*canvas, x, y, nullptr) == UpdateHit::SWITCH_CANCEL; });
        }
        case AppState::AUTO_UPDATE:
            return sweep([&](int x, int y) { return uiAutoUpdateHit(*canvas, x, y) == AutoUpdateHit::SKIP; });
        case AppState::BEACON_WARN:
            return sweep([&](int x, int y) { return uiBeaconWarnHitTest(*canvas, x, y) == BeaconWarnHit::KEEP_OFF; });
        case AppState::COLOR_CHECK:
            return sweep([&](int x, int y) { return uiColorCheckHitTest(x, y, w, h) == ColorCheckTap::DONE; });
        case AppState::WIFI_PASS: {
            int16_t x, y, bw, bh;
            if (!uiWifiPassBackRect(x, y, bw, bh)) return false;
            return put(x + bw / 2, y + bh / 2);
        }
        case AppState::PIN_ENTRY: {
            int16_t x, y, bw, bh;
            if (!uiPhoneBackRect(x, y, bw, bh)) return false;
            return put(x + bw / 2, y + bh / 2);
        }
        case AppState::LOCKED: return false;   // no way back but the PIN; HOME will not leave it either
#if defined(SENSE_CYD_APP)
        case AppState::SENSING: {   // its BACK button; none on the small boards, where BACK goes home
            int16_t x, y;
            if (!uiSensingBackStop(w, h, x, y)) return false;
            return put(x, y);
        }
#endif
        // Everything else has no BACK that can be found without pressing
        // something (SYS_PROPS closes to the main screen, which is home).
        default:                    return false;
    }
#undef sweep
}

// ---- lists that scroll under the cursor -------------------------------------
// The rows on screen are the stops. A step off the last row scrolls the list
// one item and keeps the cursor on the bottom row; a step off the first row
// does the same upwards. At the true end the cursor goes on to BACK (or LOG's
// bar), and a step past that jumps to the other end of the list: the whole
// list loops. Whether the list moved is only known after it redraws, so a
// step is left pending and settled a few frames later. (Upstream v1.42.)
static int8_t   s_listPend = 0;          // +1/-1 a step, +2 jump to the top, -2 to the bottom
static int      s_listPendId = -1;       // the edge row's id before the step
static int16_t  s_listPendY = 0;         // ...and where it was
static uint32_t s_listPendAt = 0;        // when the scroll was asked for
static uint8_t  s_listPendTries = 0;
static const uint32_t LIST_SETTLE_MS = 150;   // several redraws: the hit tests read the last draw
#if defined(SQW_TRACKBALL)
static bool stickListBusy() { return s_listPend != 0; }
#endif

static bool stickListScroll(int delta) {
    switch (state) {
        case AppState::SETTINGS:         uiSettingsScroll(delta);   return true;
        case AppState::POWER_SAVER:      uiPowerScroll(delta);      return true;
        case AppState::SECURITY:         uiSecurityScroll(delta);   return true;
        case AppState::DETECTION_FILTER: uiDetFilterScroll(delta);  return true;
        case AppState::STATUS_LIGHT:     uiLightScroll(delta);      return true;
        case AppState::LOG:              uiLogScroll(delta);        return true;
        case AppState::IGNORE_LIST:      uiIgnoreListScroll(delta); return true;
        default: return false;
    }
}

// Starts a list step if this press is one. False if it is an ordinary move.
static bool stickListStep(bool down, const StickPt* pts, uint8_t n) {
    const uint8_t rows = s_stickRowsN;
    if (!stickIsList() || !rows || rows > n) return false;
    const uint8_t s = s_stickStop;
    int8_t pend = 0;
    if (down && s == rows - 1)      pend = 1;    // off the bottom row
    else if (!down && s == 0)       pend = -1;   // off the top row
    else if (down && s == n - 1)    pend = 2;    // past the last stop: to the top
    else if (!down && s == rows)    pend = -2;   // back from BACK: to the bottom
    if (!pend) return false;
    // The edge row as it is now: the hit tests follow the scroll at once,
    // so this has to be read before the list is moved.
    const StickPt& e = pts[pend > 0 ? rows - 1 : 0];
    s_listPendId = stickListId(e.x, e.y);
    s_listPendY = e.y;
    if (!stickListScroll(pend == 1 ? 1 : pend == -1 ? -1 : pend == 2 ? -10000 : 10000)) return false;
    s_listPend = pend;
    s_listPendAt = millis();
    s_listPendTries = 0;
    s_stickPtsAt = 0;
    return true;
}

// After the redraw: where the cursor lands. True once it has.
static bool stickListSettle(StickPt* pts, uint8_t& n) {
    if (!stickIsList()) { s_listPend = 0; return false; }
    if (millis() - s_listPendAt < LIST_SETTLE_MS) return false;
    n = stickStops(pts, true);
    const uint8_t rows = s_stickRowsN;
    if (!n) { s_listPend = 0; return false; }
    if (s_listPend == 2)  { s_stickStop = 0; s_listPend = 0; return true; }
    if (s_listPend == -2) { s_stickStop = rows ? rows - 1 : 0; s_listPend = 0; return true; }
    const bool down = s_listPend > 0;
    bool moved = true;
    for (uint8_t i = 0; i < rows; i++)
        if (stickListId(pts[i].x, pts[i].y) == s_listPendId) { moved = pts[i].y != s_listPendY; break; }
    if (moved && rows) {
        const uint8_t edge = down ? rows - 1 : 0;
        // Only a heading came into view: the same row is still at the edge.
        // One more item, up to a few times.
        if (stickListId(pts[edge].x, pts[edge].y) == s_listPendId && s_listPendTries < 4) {
            s_listPendY = pts[edge].y;
            stickListScroll(down ? 1 : -1);
            s_listPendAt = millis();
            s_listPendTries++;
            s_stickPtsAt = 0;
            return false;
        }
        s_stickStop = edge;
    } else {
        // The list was already at its end: on to BACK, or round to it.
        s_stickStop = down ? (rows < n ? rows : 0) : (uint8_t)(n - 1);
    }
    s_listPend = 0;
    return true;
}
// The highlight is always up on every screen but the main one, where it
// shows after a key and fades after STICK_SHOW_MS. On the T-Deck, a touch
// board, it shows only while the ball is in use.
static bool stickCursorShown(uint32_t now) {
#if defined(SQW_TRACKBALL)
    return s_stickShownAt && now - s_stickShownAt < TDECK_CURSOR_MS;
#endif
    // A scan with nothing found yet has nothing worth lighting up: no
    // highlight until a row arrives, unless a key was just pressed.
    if (state == AppState::WIFI_ADD && !s_stickHasRows)
        return s_stickShownAt && now - s_stickShownAt < STICK_SHOW_MS;
    if (state != AppState::CLEAR) return true;
    return s_stickShownAt && now - s_stickShownAt < STICK_SHOW_MS;
}

static TouchPoint stickPoll() {
    TouchPoint tp = { false, 0, 0 };
    const uint32_t now = millis();
    const int w = tft.width(), h = tft.height();
    // A scripted drag: down in the middle, then a row's worth a frame, then up.
    if (s_stickDrag) {
        if (s_stickDragStep > 3) { s_stickDrag = 0; s_stickPtsAt = 0; return tp; }
        tp.valid = true;
        tp.x = w / 2;
        tp.y = h / 2 - s_stickDrag * 12 * s_stickDragStep;
        s_stickDragStep++;
        return tp;
    }
    // A new screen starts with its first stop and no "key just pressed": the
    // key that opened it does not count.
    if (state != s_stickStopState) {
        s_stickStopState = state; s_stickStop = 0; s_listPend = 0;
#if defined(SQW_TRACKBALL)
        // The ball that opened this screen goes on through it: the
        // highlight stays up, on the first stop.
        if (s_stickShownAt) s_stickShownAt = now ? now : 1;
#else
        s_stickShownAt = 0;
#endif
    }
    StickPt pts[STICK_STOPS_MAX];
    uint8_t n = stickStops(pts);
    if (s_stickStop >= n) s_stickStop = 0;
#if SQW_DEVTOOLS
    SmallKeys keys = smallKeysRead();
    devKeysMerge(keys);
#else
    const SmallKeys keys = smallKeysRead();
#endif
#if defined(CARDPUTER_ADV)
    // The keyboard: a character goes to the text screen that is up.
    if (keys.ch) {
        lastTouch = now;
        if (state == AppState::WIFI_PASS) uiWifiPassKey(keys.ch, now);
        else                              uiPhoneKey(keys.ch);
    }
    // A letter on a button of this screen presses it. Two buttons with one
    // letter: the key steps between them and ENTER presses. No button: a
    // global hotkey, done by loop(). (Upstream keeps B for the background
    // everywhere; MuleSkin has one background, RADAR, so B is BACK's.)
    // LOG's CLR wipes the log with no confirm: never one stray key away.
    if (keys.hot == 'c' && state == AppState::LOG && !s_confirmPending) return tp;
    if (keys.hot && n && !s_stickHoldUntil && !s_stickDrag &&
        state != AppState::BOOT && state != AppState::LOCKED) {
        const Theme::HotButton* hb = nullptr;
        const uint8_t hn = Theme::hotButtons(&hb);
        uint8_t hit[STICK_STOPS_MAX]; uint8_t hits = 0;
        for (uint8_t i = 0; i < n; i++) {
            if (pts[i].kind == 1 || pts[i].kind == 2) continue;
            const Theme::HotButton* best = nullptr;
            for (uint8_t j = 0; j < hn; j++) {
                const Theme::HotButton& b = hb[j];
                if (pts[i].x < b.x || pts[i].x >= b.x + b.w || pts[i].y < b.y || pts[i].y >= b.y + b.h) continue;
                if (!best || b.seq > best->seq) best = &b;
            }
            if (best && best->c == keys.hot) hit[hits++] = i;
        }
        if (hits) {
            // Not lastTouch: the screens debounce the tap against it.
            s_stickShownAt = now ? now : 1;
            uint8_t pick = hit[0];
            for (uint8_t k = 0; k < hits; k++) if (hit[k] == s_stickStop) { pick = hit[(k + 1) % hits]; break; }
            s_stickStop = pick;
            if (hits > 1) {
                lastTouch = now;
                Serial.printf("[keys] hotkey %c: stop %u, one of %u\n", keys.hot, (unsigned)pick + 1, (unsigned)hits);
                return tp;
            }
            const StickPt at = pts[pick];
            Serial.printf("[keys] hotkey %c presses stop %u at %d,%d (screen %u)\n", keys.hot, (unsigned)pick + 1, (int)at.x, (int)at.y, (unsigned)state);
            s_stickHoldPt = at;
            s_stickHoldUntil = now + (at.kind == 3 ? 650 : 120);
            tp.valid = true; tp.x = at.x; tp.y = at.y;
            return tp;
        }
    }
    if (keys.hot) s_cardHot = keys.hot;
#endif
    const bool k1 = keys.press;
    if (keys.home) { s_stickHome = true; Serial.println("[keys] home"); }
    // CHARGE MODE wakes on a tap: on these boards, any key is that tap.
    if (chargeModeOn() && (keys.next || keys.prev || keys.select || keys.back || keys.press)) {
        s_stickHoldPt = { (int16_t)(w / 2), (int16_t)(h / 2), 0 };
        s_stickHoldUntil = now + 120;
        tp.valid = true; tp.x = w / 2; tp.y = h / 2;
        return tp;
    }
    // A card that closes on a tap anywhere: SELECT is that tap. Before the
    // wake below, which sets lastTouch: the card debounces against it and
    // would throw its own tap away.
    if (!n && keys.select && state == AppState::OUTFIT_UNLOCK && !s_stickHoldUntil) {
        s_stickHoldPt = { (int16_t)(w / 2), (int16_t)(h / 2), 0 };
        s_stickHoldUntil = now + 120;
        tp.valid = true; tp.x = w / 2; tp.y = h / 2;
        return tp;
    }
    // A screen with nothing to point at still wakes on a key.
    if (!n && (keys.next || keys.prev || keys.select)) lastTouch = now;
    // A list step waiting on the redraw: see stickListStep().
    if (s_listPend && stickListSettle(pts, n)) {
        Serial.printf("[keys] stop %u of %u at %d,%d (screen %u)\n", (unsigned)s_stickStop + 1, (unsigned)n,
                      (int)pts[s_stickStop].x, (int)pts[s_stickStop].y, (unsigned)state);
    }
    if ((keys.next || keys.prev) && n && !s_listPend) {
        // A hidden cursor (the main screen, or the T-Deck's anywhere) just
        // reappears. Everywhere else every press moves.
        if (!stickCursorShown(now)) {
        } else if (!stickListStep(keys.next, pts, n)) {
            s_stickStop = (uint8_t)((s_stickStop + (keys.next ? 1 : n - 1)) % n);
        }
        s_stickShownAt = now ? now : 1;
        lastTouch = now;
        if (!s_listPend)
            Serial.printf("[keys] stop %u of %u at %d,%d (screen %u)\n", (unsigned)s_stickStop + 1, (unsigned)n,
                          (int)pts[s_stickStop].x, (int)pts[s_stickStop].y, (unsigned)state);
    }
    // BACK: a tap on the screen's own way back. A screen with none goes home.
    if (keys.back && !s_stickHoldUntil && !s_stickDrag) {
        // Not lastTouch: the screens debounce a tap against it, and this IS
        // the tap. It sets lastTouch itself when the screen takes it.
        StickPt at = { 0, 0, 0 };
        if (stickBackPoint(at)) {
            s_stickHoldPt = at;
            s_stickHoldUntil = now + 120;
            Serial.printf("[keys] back at %d,%d (screen %u)\n", (int)at.x, (int)at.y, (unsigned)state);
            tp.valid = true; tp.x = at.x; tp.y = at.y;
            return tp;
        }
        s_stickHome = true;
        Serial.println("[keys] back: no way back here, going home");
    }
    // SELECT: a tap at the cursor (a hold on a long-press stop, a scroll on an
    // arrow). With the cursor hidden it only shows where it is.
    if (keys.select && n && !s_stickHoldUntil && !s_stickDrag) {
        if (!stickCursorShown(now)) {
            s_stickShownAt = now ? now : 1;
            lastTouch = now;   // the key pressed to find the cursor also wakes a dimmed screen
        } else {
            const StickPt at = pts[s_stickStop];
            s_stickShownAt = now ? now : 1;
            Serial.printf("[keys] select %u at %d,%d (screen %u)\n", (unsigned)s_stickStop + 1, (int)at.x, (int)at.y, (unsigned)state);
            if (at.kind == 1 || at.kind == 2) {
                s_stickDrag = at.kind == 1 ? 1 : -1;
                s_stickDragStep = 0;
                return tp;
            }
            s_stickHoldPt = at;
            s_stickHoldUntil = now + (at.kind == 3 ? 650 : 120);
            tp.valid = true; tp.x = at.x; tp.y = at.y;
            return tp;
        }
    }
    if (s_stickHoldUntil) {
        if ((int32_t)(now - s_stickHoldUntil) < 0) {
            tp.valid = true; tp.x = s_stickHoldPt.x; tp.y = s_stickHoldPt.y;
            s_stickKey1Was = k1;
            return tp;
        }
        s_stickHoldUntil = 0;
        s_stickPtsAt = 0;
    }
    // `press`, a level: the T-Deck's ball held down (and a console PRESS).
    if (!n) { s_stickKey1Was = k1; return tp; }
    if (k1 && !s_stickKey1Was && pts[s_stickStop].kind == 3) {
        s_stickHoldPt = pts[s_stickStop];
        s_stickHoldUntil = now + 650;
        s_stickShownAt = now ? now : 1;
        s_stickKey1Was = k1;
        tp.valid = true; tp.x = s_stickHoldPt.x; tp.y = s_stickHoldPt.y;
        return tp;
    }
    if (k1 && (pts[s_stickStop].kind == 1 || pts[s_stickStop].kind == 2)) {
        if (!s_stickKey1Was) {
            lastTouch = now;
            s_stickDrag = pts[s_stickStop].kind == 1 ? 1 : -1;
            s_stickDragStep = 0;
        }
    } else if (k1) {
        tp.valid = true;
        tp.x = pts[s_stickStop].x;
        tp.y = pts[s_stickStop].y;
    }
    if (k1) s_stickShownAt = now ? now : 1;
    s_stickKey1Was = k1;
    return tp;
}

#if defined(SQW_TRACKBALL)
// The T-Deck's two pointers. A finger on the glass wins, puts the highlight
// out and throws away what the ball was doing. Off the glass, the ball's
// cursor runs, but only while there is something for it to do: the stops'
// hit-test sweep is not worth a frame's time while nobody is rolling.
static bool tdeckBallIdle() {
    const uint32_t now = millis();
    return !tdeckBallPending() && !s_stickHoldUntil && !s_stickDrag && !s_listPend && !stickCursorShown(now);
}
static void tdeckBallTouched() {
    s_stickShownAt = 0;
    s_stickHoldUntil = 0;
    s_stickDrag = 0;
    s_listPend = 0;
    tdeckBallForget();
}
#endif

// ---- the focus highlight ---------------------------------------------------
// The whole button under the cursor lights up: a purple > cyan > pink
// gradient sliding through its face, its text and edges knocked to black on
// top, a border flashing white and pink, and a stutter every few frames.
// (Upstream SquachWatch v1.42, in place of the amber brackets.)
//
// What counts as "the button" is asked of the screen itself: whatever its own
// hit test names at the cursor, grown outwards until the answer changes.

// What is at (x,y) on this screen, as an id: the same id across one button,
// a different one on the next, -1 where nothing answers. Only hit tests that
// answer without acting.
static int stickRegionId(int x, int y) {
    const int w = tft.width(), h = tft.height();
    const bool barScreen = state == AppState::CLEAR || (state == AppState::LOG && !s_confirmPending);
    if (barScreen && !(state == AppState::CLEAR && uiZoneCardWanted())) {
        const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
        for (int b = 0; b < 3; b++)
            if (x >= bar.x[b] && x < bar.x[b] + bar.w[b] && y >= bar.y && y < bar.y + bar.h) return 9100 + b;
    }
    if (stickIsSweep()) return stickSweepId(x, y);
    if (stickIsList()) {
        const int id = stickListId(x, y);
        if (id >= 0) return id;
        if (state != AppState::LOG && Theme::pinnedBackHit(x, y, w, h)) return 9000;
        return -1;
    }
    switch (state) {
        case AppState::CLEAR:
            if (uiZoneCardWanted()) return -1;
            if (uiClearClockHit(x, y))     return 9600;
            if (uiClearWatchPillHit(x, y)) return 9602;
            return -1;
        case AppState::ALERT:
            if (s_infoPending) return -1;
            if (uiAlertHitSnooze(x, y, w, h))   return 9400;
            if (uiAlertHitHunt(x, y, w, h))     return 9401;
            if (uiAlertHitMoreInfo(x, y, w, h)) return 9402;
            if (uiAlertHitIgnore(x, y, w, h))   return 9403;
            return -1;
        case AppState::DIAGNOSTICS:
            return uiDiagnosticsHitBack(x, y, w, h) ? 9700 : -1;
        case AppState::WATCH_ALERT:
            return uiWatchAlertHitRemove(*canvas, x, y) ? 9800 : -1;
        default: return -1;
    }
}

// RGB332 from 8-bit channels: the frame sprite's own pixel format.
static inline uint8_t rgb332(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6));
}

static void stickDrawCursor(TFT_eSPI& t, uint32_t now) {
    if (!stickCursorShown(now)) return;
    StickPt pts[STICK_STOPS_MAX];
    const uint8_t n = stickStops(pts);
    if (s_stickStop >= n) return;
    const StickPt p = pts[s_stickStop];
    const int w = tft.width(), h = tft.height();

    // The button's rectangle: grown from the cursor while the id holds.
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    bool region = false;
    const int id = (p.kind == 1 || p.kind == 2 || s_stickRectOn) ? -1 : stickRegionId(p.x, p.y);
    if (s_stickRectOn) {
        // The screen named the rectangle itself (TERMINAL MODE).
        const StickRect& r = s_stickRect[s_stickStop];
        x0 = r.x; y0 = r.y; x1 = r.x + r.w - 1; y1 = r.y + r.h - 1;
        region = r.w >= 6 && r.h >= 6;
    } else if (id >= 0) {
        x0 = x1 = p.x; y0 = y1 = p.y;
        while (x0 > 0     && stickRegionId(x0 - 1, p.y) == id) x0--;
        while (x1 < w - 1 && stickRegionId(x1 + 1, p.y) == id) x1++;
        while (y0 > 0     && stickRegionId(p.x, y0 - 1) == id) y0--;
        while (y1 < h - 1 && stickRegionId(p.x, y1 + 1) == id) y1++;
        region = (x1 - x0) >= 6 && (y1 - y0) >= 6;
    }
    if (!region) {
        // No button to ask about (an icon, a tile, a scroll arrow): a box
        // round the point, frame only, the thing inside left alone.
        const int rx = (p.kind == 1 || p.kind == 2) ? 9 : 14, ry = (p.kind == 1 || p.kind == 2) ? 8 : 11;
        x0 = p.x - rx; x1 = p.x + rx; y0 = p.y - ry; y1 = p.y + ry;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;

    // The gradient, built once: purple > cyan > pink > purple, 24 steps.
    static uint8_t grad[24];
    static bool gradOk = false;
    if (!gradOk) {
        static const uint8_t K[4][3] = { { 0xB4, 0x00, 0xFF }, { 0x00, 0xFF, 0xF5 }, { 0xFF, 0x2E, 0xC8 }, { 0xB4, 0x00, 0xFF } };
        for (int i = 0; i < 24; i++) {
            const int seg = i / 8, f = i % 8;
            uint8_t c[3];
            for (int k = 0; k < 3; k++) c[k] = (uint8_t)(K[seg][k] + (K[seg + 1][k] - K[seg][k]) * f / 8);
            grad[i] = rgb332(c[0], c[1], c[2]);
        }
        gradOk = true;
    }

    // The flicker: a new beat every 70 ms. Most beats slide the gradient,
    // about one in six flashes it white-hot, one in seven runs it backwards.
    const uint32_t beat = now / 70;
    const uint32_t mix = beat * 2654435761u;
    const bool flash = (mix >> 26) % 6 == 0;
    const bool back = (mix >> 24) % 7 == 0;
    const int slide = (int)((now / 35) % 24);

    uint8_t* buf = (frame.getColorDepth() == 8) ? (uint8_t*)frame.buf() : nullptr;
    const int bw = frame.bufW();
    if (region && buf && &t == (TFT_eSPI*)&frame && y1 < frame.bufH()) {
        const int rw = x1 - x0 + 1;
        // The button's own face is its most common colour, dark or light:
        // everything else in it is text or edge. Brightness alone took the
        // Win95 buttons' grey face for ink and blacked them out whole.
        uint8_t face = 0;
        {
            static uint16_t hist[256];
            memset(hist, 0, sizeof hist);
            for (int y = y0; y <= y1; y++) {
                const uint8_t* row = buf + y * bw;
                for (int x = x0; x <= x1; x++) hist[row[x]]++;
            }
            for (int c = 1; c < 256; c++) if (hist[c] > hist[face]) face = (uint8_t)c;
        }
        for (int y = y0; y <= y1; y++) {
            uint8_t* row = buf + y * bw;
            for (int x = x0; x <= x1; x++) {
                const uint8_t v = row[x];
                if (v != face) { row[x] = 0x00; continue; }
                if (flash) { row[x] = 0xFF; continue; }
                int gi = (x - x0) * 24 / rw + (back ? -slide : slide) + (y - y0) / 3;
                gi %= 24; if (gi < 0) gi += 24;
                row[x] = grad[gi];
            }
        }
    }
    // The border: a cyan/purple ring, then 2 px white and pink by turns,
    // all INSIDE the button. A screen repaints its own buttons and often
    // nothing around them, so a ring drawn outside stayed behind as a ghost.
    const uint16_t edge = (beat & 1) ? TFT_WHITE : Theme::PINK;
    t.drawRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, (beat % 3 == 0) ? Theme::CYAN : Theme::PURPLE);
    if (x1 - x0 > 6 && y1 - y0 > 6) {
        t.drawRect(x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, edge);
        t.drawRect(x0 + 2, y0 + 2, x1 - x0 - 3, y1 - y0 - 3, edge);
    }
    if (p.kind == 1 || p.kind == 2) {
        // The scroll arrow, drawn bold over its box.
        const bool down = p.kind == 1;
        t.fillRect(x0 + 2, y0 + 2, x1 - x0 - 3, y1 - y0 - 3, (beat & 2) ? Theme::CYAN : Theme::PINK);
        for (int i = 0; i < 7; i++) {
            const int half = down ? 6 - i : i;
            t.drawFastHLine(p.x - half, p.y - 3 + i, 2 * half + 1, TFT_BLACK);
        }
    }
}
#endif
