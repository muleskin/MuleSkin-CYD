// MuleSkin-CYD — clear (idle) screen implementation
#include "ui_clear.h"
#include "clock.h"      // the watch's corner clock
#include "draw_band.h"
#include "frame_prof.h"
// Needed this early: the title-bar pills come before the includes below.
#include "theme.h"
#include "settings.h"
#include "muleskin.h"

// ---- the watch/hunt indicator ------------------------------------------------
// Until this existed, a watch was invisible: its label was drawn in exactly one
// place, the alert screen, which only appears when the target comes back into
// range AND the 30s cooldown has passed. Set one and walk away and there was no
// way to learn it was still running -- or that you had set one at all.
//
// In the TITLE BAR's empty middle, not down by the counters. The bottom-left
// corner looked free and is not: the landscape counter row is 312px wide on a
// 320px screen, so it starts at x=4 -- exactly where this sat -- and
// drawCounterLine() runs later in the same tick and painted over it. Portrait
// is worse: that row overflows the screen. The renders showed no pill at all.
//
// drawTitleBar() paints the gear (x 0..28), the rotate icon (right 28px) and
// the lock (26px inside that right group) and throws its title argument away,
// so the span between them is genuinely empty -- and it is drawn BEFORE this,
// which is what makes the placement safe rather than merely available.
//
// This draws in boring mode too: it is not about
// MuleSkin, it is the state of a detection feature, and boring mode keeps all
// of those.
static bool    s_watchPillOn = false;

// The watch's corner clock: 12-hour, cyan on a black tile like the icons, just
// left of whichever right-hand icons are showing (the corner itself when
// rotation is locked, the default). Only once the time is real -- a guessed or
// unset clock draws nothing. Drawn straight after the background, BEFORE
// MuleSkin, the visitor and every speech bubble, so a bubble that reaches the
// top band covers the clock rather than the clock cutting a hole in the
// bubble. Returns where the WATCH pill's free span must end, or -1.
static int16_t s_cornerClockPillR = -1;
#if defined(TWATCH_S3)
void twatchGpsBadge(TFT_eSPI& t);   // main.cpp: the GPS counter, top centre, while the GPS is on
#if MULESKIN_LORA
void twatchLoraBadge(TFT_eSPI& t);  // main.cpp: LORA n NEW, under it, while chats are unread
#endif
static int16_t drawCornerClock(TFT_eSPI& t, int w) {
    if (!Clock::trusted()) return -1;
    char tm[8];
    Clock::formatTime(tm, sizeof tm, true);
    t.setTextSize(2);
    const int icons = Theme::titleBarRightIconsX(w);
    const int right = icons - (icons < w ? 2 : 4);
    const int tw = t.textWidth(tm) - 2;   // no spacing column after the last glyph
    const int x = right - tw;
    t.fillRect(x - 3, 0, tw + 6, 20, TFT_BLACK);
    t.setTextColor(Theme::CYAN, TFT_BLACK);
    t.setCursor(x, 3);
    t.print(tm);
    return (int16_t)(x - 3 - 4);
}
#endif
#if !defined(TWATCH_S3)
// Every other board: the same clock, at the top centre, once the time is real
// (WIFI TIME on the bar sets it). Returns where the WATCH pill's free span
// must end, so a pill sits left of the clock rather than under it. A tap on
// it opens TODAY (uiClearClockHit()).
static int16_t s_clkX = 0, s_clkW = 0;
static bool    s_clkOn = false;
static int16_t drawCornerClock(TFT_eSPI& t, int w) {
    s_clkOn = false;
    if (!Clock::trusted()) return -1;
    char hm[8], tm[12];
    bool pm = false;
    Clock::formatTime(hm, sizeof hm, true, &pm);
    snprintf(tm, sizeof tm, "%s %s", hm, pm ? "PM" : "AM");
    t.setTextSize(2);
    const int tw = t.textWidth(tm) - 2;   // no spacing column after the last glyph
    const int x = (w - tw) / 2;
    t.fillRect(x - 3, 0, tw + 6, 20, TFT_BLACK);
    t.setTextColor(Theme::CYAN, TFT_BLACK);
    t.setCursor(x, 3);
    t.print(tm);
    s_clkX = (int16_t)(x - 8); s_clkW = (int16_t)(tw + 16); s_clkOn = true;
    return (int16_t)(x - 3 - 4);
}
bool uiClearClockHit(int x, int y) {
    return s_clkOn && x >= s_clkX && x < s_clkX + s_clkW && y >= 0 && y < 28;
}
#else
bool uiClearClockHit(int, int) { return false; }
#endif
static int16_t s_wpX = 0, s_wpY = 0, s_wpW = 0, s_wpH = 0;

// spanR: the right end of the free span. -1 keeps the old fixed reserve for
// the rotate button and the padlock; the watch passes the corner clock's
// left edge instead, which already sits left of both.
static void drawWatchPill(TFT_eSPI& t, int screenW, bool watching, bool hunting, int spanR = -1) {
    // HUNT wins the label when both are set: it is the active, look-at-me mode.
    // The two are independent slots (see DetectionEngine), so both can be on.
    const char* txt = hunting ? "HUNT" : "WATCH";
    const uint16_t accent = hunting ? Theme::AMBER : Theme::CYAN;
    t.setTextSize(1);
    // 16 in a 20px bar: two rows of clearance top and bottom.
    const int bh = 16;
    const int bw = 16 + t.textWidth(txt) + 7;
    // Left edge of the free span, past the gear. The right limit is the rotate
    // icon (28) plus the lock (26) -- reserve both whether or not either is
    // showing, so the pill cannot move when a PIN is set or rotation locked.
    const int spanL = 32;
    if (spanR < 0) spanR = screenW - 54;
    int x = spanL + ((spanR - spanL) - bw) / 2;
    if (x < spanL) x = spanL;
    const int y = (20 - bh) / 2;
    t.fillRoundRect(x, y, bw, bh, 4, Theme::BG);
    t.drawRoundRect(x, y, bw, bh, 4, accent);
    // An eye: open for a passive watch, with a line through it for a hunt.
    t.drawCircle(x + 9, y + bh / 2, 4, accent);
    t.fillCircle(x + 9, y + bh / 2, 1, accent);
    if (hunting) t.drawFastHLine(x + 3, y + bh / 2, 12, accent);
    t.setTextColor(accent, Theme::BG);
    t.setCursor(x + 16, y + (bh - 8) / 2);
    t.print(txt);
    // A finger-sized target: the bar is only 20px tall, so grow downward.
    s_wpX = (int16_t)(x - 4); s_wpY = (int16_t)0;
    s_wpW = (int16_t)(bw + 8); s_wpH = (int16_t)(bh + 14);
    s_watchPillOn = true;
}

// Detection is off for a moment -- the radio is on WiFi (AUTO TIME, an update
// check) or resting -- so the counters below are standing still. Same slot and
// shape as the WATCH pill, which it stands in for while it lasts: a paused
// scan is the more important thing to know. Not a tap target.
static void drawPausedPill(TFT_eSPI& t, int screenW, const char* txt, int spanR = -1) {
    const uint16_t accent = Theme::AMBER;
    t.setTextSize(1);
    const int bh = 16;
    const int bw = 16 + t.textWidth(txt) + 7;
    const int spanL = 32;
    if (spanR < 0) spanR = screenW - 54;
    int x = spanL + ((spanR - spanL) - bw) / 2;
    if (x < spanL) x = spanL;
    const int y = (20 - bh) / 2;
    t.fillRoundRect(x, y, bw, bh, 4, Theme::BG);
    t.drawRoundRect(x, y, bw, bh, 4, accent);
    // A pause sign: two short bars.
    t.fillRect(x + 6,  y + 4, 2, bh - 8, accent);
    t.fillRect(x + 10, y + 4, 2, bh - 8, accent);
    t.setTextColor(accent, Theme::BG);
    t.setCursor(x + 16, y + (bh - 8) / 2);
    t.print(txt);
}

// The NEARBY headline's last drawn rectangle, grown to a finger-sized target.
static bool    s_nearbyOn = false;
static int16_t s_nbX = 0, s_nbY = 0, s_nbW = 0, s_nbH = 0;

bool uiClearNearbyHit(int x, int y) {
    return s_nearbyOn &&
           x >= s_nbX && x < s_nbX + s_nbW && y >= s_nbY && y < s_nbY + s_nbH;
}

bool uiClearWatchPillHit(int x, int y) {
    return s_watchPillOn &&
           x >= s_wpX && x < s_wpX + s_wpW && y >= s_wpY && y < s_wpY + s_wpH;
}

bool uiClearMascotShown() {
    if (!MuleSkin::MASCOT_SHOWN || Settings::boringMode()) return false;
    return Settings::background() != Settings::Background::MULESKIN || MuleSkin::onboardingActive();
}

#include "theme.h"
#include "muleskin.h"
#include "pet.h"
#include "settings.h"
#include "idle_events.h"
#include <Arduino.h>

void uiClearInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static const char* counterLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        // Not just Meta any more -- Snap Spectacles and Luxottica land here too.
        case DetectionType::META:        return "GLASS";
        case DetectionType::SKIMMER:     return "SKIM";
        // AIRTAG doubles as the combined "TRACKER" bucket here -- see
        // counterCount() below. GOOGLE_TAG/TILE/SAMSUNG_TAG keep their
        // own labels everywhere else (LOG screen, colors, vendor
        // names); this is just the compact main-screen row folding all
        // four BLE-tracker types into one column to save space. RING
        // folds into CAM the same way.
        case DetectionType::AIRTAG:      return "TRACKER";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::RAVEN:       return "RAV";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        // Four letters, like HACK: this row is packed six-across.
        case DetectionType::IBEACON:     return "BCON";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL";
        // Four letters, not "HACKER": this row is packed six-across in
        // landscape and four-across on a 240px portrait panel. EVILTWIN
        // keeps its own label everywhere else -- it just does not get its
        // own column here, because its count is inside this one.
        case DetectionType::HACKER:      return "HACK";
        default:                         return "?";
    }
}

// GOOGLE_TAG, TILE and SAMSUNG_TAG's counts fold into AIRTAG's here
// (see counterLabel's "TRACKER" case above) -- they're still tracked
// and displayed as their own distinct types everywhere else (LOG
// screen, colors, vendor labels), just combined into one number/column
// on this compact row to free up space, especially under the 4-per-row
// portrait cap.
//
// SAMSUNG_TAG joined the fold when EVILTWIN was added: they're all
// "something is quietly tracking you" and read fine as one number,
// whereas a rogue AP is a different kind of problem and had nowhere to
// go. It has somewhere to go now -- HACKER, below. Row stays at 12
// columns either way, so the layout has never moved.
static uint16_t counterCount(const DetectionEngine& eng, DetectionType t) {
    uint16_t n = eng.countByType(t);
    if (t == DetectionType::AIRTAG) {
        n += eng.countByType(DetectionType::GOOGLE_TAG)
           + eng.countByType(DetectionType::TILE)
           + eng.countByType(DetectionType::SAMSUNG_TAG);
    }
    // A Ring doorbell is a camera, and on a row this tight that is the
    // level the column needs to work at. It keeps its own type, colour,
    // icon and name everywhere else -- the LOG still says RING -- and the
    // column it gives up is what makes room for beacons below.
    if (t == DetectionType::CAMERA) {
        n += eng.countByType(DetectionType::RING);
    }
    // EVILTWIN folds into HACKER the same way, and for a better reason than
    // saving a column: a rogue AP is not a category of hardware, it is a
    // thing pentest hardware DOES. A Pineapple running PineAP karma is an
    // evil twin -- the same box, seen by its behaviour instead of by its
    // signature. Counting them apart would split one device across two
    // columns and read as two problems.
    if (t == DetectionType::HACKER) {
        n += eng.countByType(DetectionType::EVILTWIN);
    }
    return n;
}

// All types in one place, chunked into rows of at most MAX_PER_ROW at
// draw time (see uiClearTick()) instead of two hand-split arrays --
// the old 7-and-6 split ran wide enough on a narrow 240px portrait
// screen that FLOCK (first on the line) got clipped off the left edge
// entirely. A hard per-row cap fixes that on both boards, not just
// AWOK's narrower panel.
//
// THE RULE THIS ROW KEEPS: every type that can be detected is counted in
// one of these columns, whether or not it has one of its own. Three folds
// do that (see counterLabel/counterCount above) -- GOOGLE_TAG, TILE and
// SAMSUNG_TAG into TRACKER, EVILTWIN into HACK, RING into CAM -- which is
// what keeps the row at eleven fixed columns on a screen that has room for
// twelve.
static const DetectionType FIXED_COUNTER_TYPES[] = {
    DetectionType::FLOCK,   DetectionType::AXON,     DetectionType::META,   DetectionType::SKIMMER,
    DetectionType::RAVEN,   DetectionType::AIRTAG,   DetectionType::DRONE,  DetectionType::ALPR,
    DetectionType::CAMERA,  DetectionType::HACKER,   DetectionType::DEAUTH,
};
static const uint8_t FIXED_COUNTER_TYPES_N =
    sizeof(FIXED_COUNTER_TYPES) / sizeof(FIXED_COUNTER_TYPES[0]);
// ...and the twelfth column goes to beacons, but only while the type is
// switched on. It ships off (DEFAULT_OFF in settings.cpp) because one shop
// can put dozens of them in range, so most boards never see this column;
// the one that asked for the type gets it, and the rule above stays true
// without anybody having to remember it. Twelve is what this row has
// always drawn, so the layout never grows.
static const uint8_t MAX_COUNTER_TYPES = FIXED_COUNTER_TYPES_N + 1;
// Alphabetical by the label the row shows (ALPR, AXON, ... TRACKER), sorted
// here rather than written in that order above so the beacon column, when
// it is on, lands in its place among them instead of tacked on the end.
static uint8_t activeCounterTypes(DetectionType* out) {
    uint8_t n = 0;
    for (; n < FIXED_COUNTER_TYPES_N; n++) out[n] = FIXED_COUNTER_TYPES[n];
    if (Settings::typeEnabled(DetectionType::IBEACON)) out[n++] = DetectionType::IBEACON;
    for (uint8_t i = 1; i < n; i++) {
        const DetectionType v = out[i];
        uint8_t j = i;
        for (; j > 0 && strcmp(counterLabel(out[j - 1]), counterLabel(v)) > 0; j--) out[j] = out[j - 1];
        out[j] = v;
    }
    return n;
}
// Portrait (narrow) caps at 4 per row -- see the comment above. Landscape
// has plenty of width for the original 7-and-6 two-row split (that's
// exactly what this produces: 13 types / 2 rows), so row count is
// picked dynamically off the live orientation in uiClearTick() rather
// than fixed at compile time -- it changes every time the screen
// rotates, not just once per board.
static const uint8_t MAX_PER_ROW_PORTRAIT   = 4;
static const uint8_t COUNTER_ROWS_LANDSCAPE = 2;
// Three rows in portrait at eleven columns and at twelve alike, so turning
// beacons on never moves the counters up into MuleSkin's band.
static const uint8_t COUNTER_ROWS_PORTRAIT  =
    (MAX_COUNTER_TYPES + MAX_PER_ROW_PORTRAIT - 1) / MAX_PER_ROW_PORTRAIT;  // ceil

static void drawCounterLine(TFT_eSPI& t, int w, int y, const DetectionEngine& eng,
                            const DetectionType* types, uint8_t n) {
    // 80, not 56: worst case is 7 entries x up to "XXXXX:999  " (11
    // chars) = 77 -- the old 56-byte buffer was already marginal for
    // 6 entries at high counts and would silently truncate (snprintf
    // is bounds-safe, just visually cuts off) once TILE/RING pushed a
    // line to 7.
    char buf[80] = "";
    int off = 0;
    for (uint8_t i = 0; i < n; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "%s:%u  ",
                        counterLabel(types[i]), counterCount(eng, types[i]));
    }
    // The trailing gap is spacing between entries, not part of the last one.
    while (off > 0 && buf[off - 1] == ' ') buf[--off] = '\0';
    int tw = t.textWidth(buf);
    const int x = (w - tw) / 2;
    // A dark plate a few pixels past the text, not just the character cells:
    // tight to the glyphs, the numbers read as cut out of whatever the
    // background is doing behind them.
    const int PAD_X = 4, PAD_Y = 2;
    t.fillRect(x - PAD_X, y - PAD_Y, tw + 2 * PAD_X, t.fontHeight() + 2 * PAD_Y, Theme::BG);
    // Entry by entry. A type that is around (count above zero) is a pill in
    // its type's colour -- the colour its blips have on the radar, so a dot
    // and its counter read as the same thing -- with black or white text,
    // whichever reads on that colour. A type at zero is plain light grey
    // text: dim colours on black were unreadable on the panel. The pill
    // stays inside the two-space gap either side, so the layout is the one
    // measured above.
    int cx = x;
    const int fh = t.fontHeight();
    const uint16_t ZERO_GREY = 0xA514;   // light grey
    for (uint8_t i = 0; i < n; i++) {
        char entry[20];
        const unsigned c = counterCount(eng, types[i]);
        snprintf(entry, sizeof entry, "%s:%u", counterLabel(types[i]), c);
        const int ew = t.textWidth(entry);
        if (c) {
            const uint16_t col = Theme::colorFor(types[i]);
            // Perceived brightness from the RGB565 channels (0..255 each).
            const int r = ((col >> 11) & 0x1F) * 255 / 31;
            const int g = ((col >> 5) & 0x3F) * 255 / 63;
            const int b = (col & 0x1F) * 255 / 31;
            const bool light = (r * 299 + g * 587 + b * 114) / 1000 > 140;
            t.fillRoundRect(cx - 3, y - 2, ew + 5, fh + 3, 2, col);
            t.setTextColor(light ? Theme::BLACK : Theme::WHITE, col);
        } else {
            t.setTextColor(ZERO_GREY, Theme::BG);
        }
        t.setCursor(cx, y);
        t.print(entry);
        cx += ew + (i + 1 < n ? t.textWidth("  ") : 0);
    }
}


static uint32_t s_mascotStepMs = 0;   // 0: not yet read from settings
void     uiMascotStepSet(uint32_t ms) { Settings::setMascotPaceMs((uint16_t)ms); s_mascotStepMs = Settings::mascotPaceMs(); }
uint32_t uiMascotStepMs()            { if (!s_mascotStepMs) s_mascotStepMs = Settings::mascotPaceMs(); return s_mascotStepMs; }

bool uiMascotStep(uint32_t now, bool advance) {
    static uint32_t s_lastStep = 0;
    if (!advance) return false;
    if ((uint32_t)(now - s_lastStep) < uiMascotStepMs()) return false;
    s_lastStep = now;
    return true;
}

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    int w = t.width();
    int h = t.height();
    // Recomputed every tick, not cached per-board: rotating the screen
    // changes w/h live, and the counter layout should follow it rather
    // than staying stuck at whatever orientation was active at boot.
    bool landscape = w > h;
    // Two rows in landscape on every board, the 3.5" included. Stepping the
    // counters up to size 2 was tried and taken back out: thirteen types is
    // about 77 characters, which is 924 px at size 2 against a 480 px row, so
    // bigger digits could only be paid for with four rows instead of two --
    // and four bars of counters under the mascot is not what that screen is
    // for. The counters stay small and stay two lines.
    const uint8_t counterRows = landscape ? COUNTER_ROWS_LANDSCAPE : COUNTER_ROWS_PORTRAIT;

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int lineH          = 14;
    const int countersTop    = bar.y - counterRows * lineH - 6;
    // The counter rows alone sit 9px lower than countersTop, and nothing
    // else does. Measured off a rendered landscape frame before any of
    // this: the headline's ink ended at row 172, the two counter rows ran
    // 180-186 and 194-200, and the button bar started at 214 -- so the
    // block sat 7px under the headline and 13px above the buttons, hugging
    // the text above it.
    //
    // Centring it in that slack was the first attempt and it was the wrong
    // one: 10px above and 10px below is balanced, and reads as belonging to
    // neither the headline nor the buttons. Low is better. The counters are
    // chrome, the same as the buttons are, so grouping the two into one
    // footer and leaving the headline up with MuleSkin says what goes with
    // what. 9 puts the last row's ink 4px off the button bar.
    //
    // Not further: 13 would touch the buttons. (The rows also have to land
    // inside the background's repaint or they smear; that runs to the bottom
    // of the screen now, so it no longer sets the limit.)
    //
    // Deliberately NOT folded into countersTop, which would look like the
    // tidier fix. That value is the floor of everything above it: the
    // headline is bottom-aligned to it, MuleSkin sizes himself against it,
    // the pet takes it as its band, and the background repaints to it.
    // Moving it would slide the headline down with the rows (leaving the
    // grouping exactly as muddled as before) and hand MuleSkin nine more
    // pixels of height -- which he cannot take: his waving arm already
    // reaches row 4, and growing him walks the hand off the top edge.
    //
    // Orientation-independent by construction. The last row's ink lands at
    // bar.y - 14 + this whatever counterRows is, so portrait's four rows
    // clear the buttons by the same 4px landscape's two do.
    const int counterTextTop = countersTop + 9;

    // The headline hangs off the counter block now, not off countersTop.
    //
    // Measured off a render: ty puts the Bangers ink 4 rows lower and it
    // runs 23 rows, with the 24-pass outline adding 2 more each way. So the
    // whole painted band is ty+2 .. ty+28, and sitting it HEADLINE_PAD above
    // the counters is that arithmetic run backwards.
    //
    // It used to bottom-align to countersTop, which put it at row 146 --
    // floating across his shins in the middle of otherwise empty space,
    // with 17 rows of nothing between it and the numbers it belongs to.
    // LG, not MD. The size is a consequence of the word: at 90px LG the
    // headline uses under a third of a 320px row, where the old 17-character
    // one needed MD just to fit and still ran 216px. Short text does not
    // want the same face at a smaller size, it wants the bigger face -- and
    // this row is a burst that cuts in for a moment, so it has to land.
    static const Theme::BangersSize HEADLINE_SIZE = Theme::BangersSize::LG;
    // Measured off a render at HEADLINE_SIZE by diffing a seeded frame
    // against a --noseed one, which isolates the headline from a background
    // that repaints every row of this band: the painted result runs ty+4 to
    // ty+32, so 29 rows of which the 24-pass outline is the outer 2 each
    // way. 33 is what puts that last painted row exactly HEADLINE_PAD above
    // the counter text.
    static const int HEADLINE_H   = 33;
    static const int HEADLINE_PAD = 5;
    const int headlineTop = counterTextTop - HEADLINE_PAD - HEADLINE_H;

    // ...which frees the rows it used to sit in, and MuleSkin takes them.
    //
    // His floor was countersTop, a number that stopped meaning anything to
    // him once the counters moved down into the footer: it is neither where
    // the text starts nor where the screen runs out. Two rows above the
    // counter ink is the real bottom of the space he has.
    //
    // He is sized from the band he is handed -- scale = charAvail /
    // BASE_HEIGHT -- so this is the whole of "make him bigger", and it is
    // bigger everywhere rather than per costume. His feet land on the new
    // floor for free; nothing else needs moving.
    //
    // The ceiling on this is his WAVING ARM, not his head. It reaches about
    // two rows above his crest scaled, and the crest is only 8 rows off the
    // top today, so there is far less room up there than the empty-looking
    // rows suggest. See the measurement in the commit that added this.
    const int muleskinBottom = counterTextTop - 2;

    const int titleBottom  = 16;

    // Background animation, the whole screen top to bottom -- style picked
    // from the settings menu. It used to stop just above the button bar and
    // leave a black strip under it; the buttons fill their own boxes, so the
    // animation runs on around and beneath them instead.
    // The counters get drawn over the band further down, so tell the
    // background where its usable floor really is before it places
    // anything that stands on the ground.
    // Two arguments now, because the counters no longer start where MuleSkin's
    // feet land. Cameos still stand level with him at countersTop; the
    // backgrounds that fill a bright ground band keep filling to where the
    // numbers actually begin, instead of stopping nine pixels short of them.
    Theme::setBackgroundFloor(muleskinBottom, counterTextTop);
    // From the very top of the screen, not from titleBottom. The title bar
    // used to own rows 0-15 and paint them every frame; with it gone they
    // belonged to nobody and kept whatever the previous frame left there.
    // Handing them to the background is also the point of removing the bar:
    // the animation now runs edge to edge behind the two corner buttons.
    //
    // MuleSkin is still told his band starts at titleBottom. He sizes himself
    // from the space he is given, so telling him about these rows would make
    // him a tenth bigger and move everything hanging off him.
    FrameProf::lap(FrameProf::CHROME);   // the counting and geometry above
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::clearBackgroundFloor();
    FrameProf::lap(FrameProf::BG);
    // Under everything that moves: see drawCornerClock().
    if (DrawBand::has(0, titleBottom)) s_cornerClockPillR = drawCornerClock(t, w);
#if defined(TWATCH_S3)
    twatchGpsBadge(t);   // under the bubbles too
#if MULESKIN_LORA
    twatchLoraBadge(t);
#endif
#endif
    // Everything from here that moves by the call, not by the clock, moves
    // on the mascot's clock. See uiMascotStep().
    const bool step = uiMascotStep(now, advance);

    // MuleSkin: main character, reacts to events, cracks jokes when idle.
    // His available region runs all the way to countersTop (not
    // statusTop) — past where the ALL CLEAR text sits — so he scales up
    // further and his feet land on top of its upper portion. The text
    // itself draws AFTER him (below) so it stays fully legible on top
    // of his body wherever they overlap, instead of being covered. A
    // big bounce can push his dirty-rect clear a few px above
    // titleBottom into the title bar's row, so the title bar is drawn
    // after him too — it fully repaints its own row every frame, so it
    // always ends up on top and never shows any bleed-over from his
    // clear box.
    //
    // "Boring mode" skips this call entirely — his internal state
    // (mood/quip timers, the stats MuleSkin::trigger() tracks for the
    // Diary screen) keeps updating regardless since that's driven from
    // main.cpp's trigger() calls, not from here; this only turns off
    // his actual on-screen presence. The background above already
    // fully repaints this whole region every frame, so skipping him
    // just leaves it as animated negative space — no layout changes
    // needed anywhere else on this screen.
    if (uiClearMascotShown()) {
        // The last argument is the SIZE row in Settings. CLEAR is the only
        // screen that passes it: everywhere else he is a cameo in a box
        // somebody sized deliberately, and shrinking him there would just
        // leave a hole.
        MuleSkin::tick(t, w / 2, titleBottom, muleskinBottom - titleBottom, now, step,
                      1.0f, false, -1, Settings::muleskinSizePct());
    }


    // Rare decorative flourishes (UFO/sparkle/critter/glitch-line) --
    // see idle_events.h. Skipped for the same reasons MuleSkin's own
    // presence is skipped above (boring mode) or would collide with
    // the walkthrough's own bubble (onboarding) -- a UFO flying past
    // mid-lesson would be more distraction than delight.
    FrameProf::lap(FrameProf::MULESKIN);
    if (!Settings::boringMode() && !MuleSkin::onboardingActive()) {
        IdleEvents::tick(t, now, 0, titleBottom, w, muleskinBottom, step);
    }
    FrameProf::lap(FrameProf::IDLE);

    // Whatever the background wants on top of the mascot. Right now
    // that is the werewolf's speech bubble: drawFire computes it but
    // deliberately does not paint it, because the background is drawn
    // before MuleSkin and the bubble was ending up behind him. Same
    // reasoning as ALL CLEAR below -- text is the one thing here that
    // cannot afford to be half-covered.
    Theme::drawBackgroundOverlay(t, now);

    // Title bar at the top
    if (DrawBand::has(0, titleBottom)) Theme::drawTitleBar(t, ">> MuleSkin <<  SCANNING");

    // The watch/hunt indicator, in the title bar's empty middle. AFTER the bar
    // itself, which repaints that whole band -- see drawWatchPill()'s comment
    // for why the first attempt at this was invisible. Drawn in every mode,
    // whenever either target is set.
    {
        const bool watching = eng.watchKind() != DetectionEngine::WatchKind::NONE;
        const bool hunting  = eng.huntKind()  != DetectionEngine::WatchKind::NONE;
        // The flag goes inside the guard with the drawing it describes. Left
        // outside it, the pass that cannot reach the title bar would clear a
        // pill the other pass had just drawn, and it would stop being tappable.
        if (DrawBand::has(0, titleBottom)) {
            s_watchPillOn = false;
            // Right of the pill: the watch's corner clock, drawn earlier (see
            // drawCornerClock()); -1 elsewhere, the old fixed reserve.
            if (eng.updateRadioOn())
                drawPausedPill(t, w, "PAUSED", s_cornerClockPillR);
            else if (eng.radiosResting())
                drawPausedPill(t, w, "RESTING", s_cornerClockPillR);
            else if (watching || hunting)
                drawWatchPill(t, w, watching, hunting, s_cornerClockPillR);
        }
    }

    // ALL CLEAR (only flash if there are NO active detections). Same
    // Bangers headline font as the ALERT screen's "!! DETECTION !!" —
    // now sitting directly on top of the counter block it labels, rather
    // than floating in the middle of the empty space above it. See
    // headlineTop.
    //
    // Still drawn AFTER MuleSkin, and that stays deliberate. Drawing it
    // first would let his shadow and feet fall across it, which sounds
    // better than it is: he is about 60px wide at the ankles against a
    // 185px headline, and a word with its middle punched out is not a
    // word. The 24-pass black outline is what keeps it legible over him.
    // Is anything actually live right now? Not lifetime -- a camera seen an
    // hour ago is not something happening, and the counters decay for the
    // same reason.
    FrameProf::lap(FrameProf::CHROME);
    bool anyActive = false;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        if (eng.countByType((DetectionType)i) > 0) { anyActive = true; break; }
    }
    s_nearbyOn = false;           // set again below only if it is drawn
    // Its ARRIVAL is the event, so the glitch fires on the edge rather than
    // on the state -- once, when the screen goes from nothing to something,
    // not again when a second detection joins the first. Level 3 is where
    // the shared burst adds a full-screen tear on top of the jitter and
    // dropout, which is the point: the headline does not fade in, it cuts
    // in badly, the way a signal does.
    //
    // triggerGlitchBurst drives the same burst drawBangersText already reads
    // from, so the headline glitches on its own with nothing else wired up.
    static bool s_wasActive = false;
    if (anyActive && !s_wasActive) Theme::triggerGlitchBurst(3);
    s_wasActive = anyActive;

    // Nothing on the row while nothing is happening. ALL CLEAR is gone and
    // the headline does not replace it: a permanent label asserting anything
    // over a screen of zeroes is just untrue, and the counters underneath
    // already say the same thing more precisely.
    //
    // Blank is not a compromise here, it is the better screen. MuleSkin's
    // geometry hangs off counterTextTop rather than this row, so he does not
    // move -- he is simply no longer painted over by a 25-pass headline that
    // exists to be legible ON TOP of him. The pet and the Mowin' Man stop
    // running in behind it. And the 24 outline passes plus the fill come off
    // the frame the device spends nearly all its time rendering.
    //
    // Nothing needs erasing: the background repaints this whole band every
    // frame, so a headline that stops being drawn is simply gone next frame.
    if (anyActive) {
        // The rainbow ALL CLEAR used to own. It was the good part and it was
        // wasted on the state you see least; now it runs on the state that
        // actually matters. Same hue-wash as MuleSkin's party-mode confetti.
        static const uint16_t RAINBOW[6] = {
            Theme::RED, Theme::AMBER, Theme::GREEN,
            Theme::CYAN, Theme::VAPOR_PURPLE, Theme::PINK
        };
        const float huePos = fmodf((float)now / 900.0f, 6.0f);
        const int i0 = (int)huePos % 6, i1 = (i0 + 1) % 6;
        const uint16_t col =
            Theme::blend(RAINBOW[i0], RAINBOW[i1], (uint16_t)((huePos - (int)huePos) * 255));
        // Not a status label any more. This row now appears BECAUSE an
        // event happened, so it reads as the event rather than describing
        // the screen's state -- the job a label like ACTIVE DETECTIONS was
        // doing twice, and less precisely than the counters underneath.
        //
        // One word because the subject was the vague half. SOMETHING'S
        // NEARBY said nothing the counters do not say better; NEARBY is the
        // half that carries the meaning, and dropping the other one is what
        // buys the bigger face above.
        const char* msg = "NEARBY";
        // 2px black outline: draw the same text at every offset in a
        // 5x5 grid around the real position (minus the center) in
        // black first, then the real color on top. A full grid, not
        // just a ring at radius 2, so there's no gap between the 1px
        // and 2px shells. The Bangers glyph renderer only paints ink
        // pixels (not a full opaque cell), so the offset passes land
        // as a clean outline rather than clobbering each other.
        static const int8_t OUTLINE_OFS[24][2] = {
            {-2,-2},{-1,-2},{0,-2},{1,-2},{2,-2},
            {-2,-1},{-1,-1},{0,-1},{1,-1},{2,-1},
            {-2, 0},{-1, 0},        {1, 0},{2, 0},
            {-2, 1},{-1, 1},{0, 1},{1, 1},{2, 1},
            {-2, 2},{-1, 2},{0, 2},{1, 2},{2, 2},
        };
        int tw = Theme::bangersTextWidth(msg, HEADLINE_SIZE);
        int ty = headlineTop;
        if (tw <= w - 8) {
            int tx = (w - tw) / 2;
            // One pass, not twenty-four: this was 14.8 ms of every frame.
            if (DrawBand::has(ty, ty + HEADLINE_H)) {
                Theme::drawBangersOutline(t, tx, ty, msg, Theme::BLACK, HEADLINE_SIZE, 2);
                Theme::drawBangersText(t, tx, ty, msg, col, HEADLINE_SIZE);
            }
            // Padded well past the ink: the word is only 33px tall and it is
            // pressed with a thumb, over a moving MuleSkin.
            s_nbX = (int16_t)(tx - 16); s_nbY = (int16_t)(ty - 10);
            s_nbW = (int16_t)(tw + 32); s_nbH = (int16_t)(HEADLINE_H + 20);
            s_nearbyOn = true;
        } else {
            // Kept as a guard, not because the current headline needs it:
            // NEARBY measures 90px in LG against the 232 a 240px portrait
            // screen leaves, so it clears by a factor of two and a half.
            // Bangers has no step below MD to fall back to the way the
            // built-in font does, so any future headline that outgrows the
            // narrow rotation drops to the built-in face rather than clip.
            t.setTextSize(2);
            int sw = t.textWidth(msg);
            int sx = (w - sw) / 2, sy = counterTextTop - HEADLINE_PAD - t.fontHeight(2);
            t.setTextColor(Theme::BLACK, Theme::BG);
            for (uint8_t i = 0; i < 24; i++) {
                t.setCursor(sx + OUTLINE_OFS[i][0], sy + OUTLINE_OFS[i][1]);
                t.print(msg);
            }
            t.setTextColor(col, Theme::BG);
            t.setCursor(sx, sy);
            t.print(msg);
            s_nbX = (int16_t)(sx - 16); s_nbY = (int16_t)(sy - 10);
            s_nbW = (int16_t)(sw + 32); s_nbH = (int16_t)(t.fontHeight() + 20);
            s_nearbyOn = true;
        }
    }
    FrameProf::lap(FrameProf::HEADLINE);

    // The pet, after MuleSkin AND after the headline.
    //
    // After MuleSkin because he perches on top of him. After the headline
    // because he spends most of his visits on the ground, and the ground on
    // this screen is the same rows the headline occupies -- drawn before it
    // he stood there for three seconds with his legs behind ACTIVE
    // DETECTIONS, which is a poor showing for the only other character on
    // the device.
    //
    // MuleSkin stays behind the headline on purpose and this does not change
    // that: he is 130px of opaque brown and the text has to survive him. The
    // pet is thirty pixels wide and moving, so passing in front reads as
    // depth rather than as an obstruction.
    //
    // Still before the counters, which he never reaches.
    if (uiClearMascotShown()) Pet::tick(t, now, w, titleBottom, muleskinBottom);

    // Counter lines above the buttons — all 13 detection types, split
    // across counterRows (2 in landscape, capped at 4/row in portrait
    // -- see the constants above) so a row never runs wide enough to
    // clip off a narrow portrait screen, while landscape still gets
    // the more compact two-row layout it has room for. Whole
    // label:count tokens only, so nothing ever breaks mid-word. No
    // flat clear here anymore — the background animation now fully
    // repaints this whole row every frame (it runs to the bottom of the
    // screen), the same "let the background do the erasing"
    // pattern already relied on for MuleSkin and the status line above.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setTextWrap(false);

    // Evenly balanced, not greedily packed (e.g. 4/4/4/1 in portrait)
    // -- a lone last row with a single item looked worse than several
    // similarly-sized rows does, and this still never exceeds the
    // per-orientation cap on any row.
    DetectionType counterTypes[MAX_COUNTER_TYPES];
    const uint8_t counterN = activeCounterTypes(counterTypes);
    uint8_t base      = counterN / counterRows;
    uint8_t remainder = counterN % counterRows;
    uint8_t start = 0;
    for (uint8_t row = 0; row < counterRows; row++) {
        uint8_t n = base + (row < remainder ? 1 : 0);
        const int rowY = counterTextTop + row * lineH;
        // `start` advances either way: the rows share one list of types, so a
        // row that is not painted still has to hand the next one its place.
        if (DrawBand::has(rowY, rowY + lineH))
            drawCounterLine(t, w, rowY, eng, counterTypes + start, n);
        start += n;
    }

    // Soft buttons, straight over the background: it repaints the whole
    // strip under them every frame, margins and gaps included. Each
    // button fills its own box, so the labels stay on a dark ground.
    if (DrawBand::has(bar.y, bar.y + bar.h))
        Theme::drawButtonBar(t, ButtonId::NONE, Theme::ButtonBarMode::MAIN);
}
