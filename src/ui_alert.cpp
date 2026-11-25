// MuleSkin-CYD — ALERT screen implementation
#include "ui_alert.h"
#include "theme.h"
#include "privacy.h"
#include "signatures.h"
#include "detection.h"
#include "ignore_list.h"
#include "tag_ring.h"
#include "trail.h"      // the trail: stops, the signal ring, FOLLOWING
#include "regulars.h"   // MET BEFORE
#include <Arduino.h>
#if defined(CYD_GPS)
#include "sd_row.h"   // degreesText() for the place line
#endif
#include <string.h>
// The QR code to the alerts guide (below) uses ESP-IDF's encoder; the
// desktop simulator has none and keeps the gauge.
#if defined(ARDUINO_ARCH_ESP32) && __has_include(<qrcode.h>)
#include <qrcode.h>
#define ALERT_QR 1
#endif

static const char* targetLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:   return "FLOCK CAM";
        case DetectionType::AXON:    return "AXON BODY";
        case DetectionType::META:    return "META GLASSES";
        case DetectionType::SKIMMER: return "CARD SKIMMER";
        case DetectionType::RAVEN:   return "RAVEN";
        case DetectionType::AIRTAG:  return "AIRTAG";
        case DetectionType::DRONE:   return "DRONE";
        case DetectionType::ALPR:    return "ALPR";
        case DetectionType::CAMERA:  return "CAMERA";
        case DetectionType::SAMSUNG_TAG: return "SAMSUNG TAG";
        case DetectionType::GOOGLE_TAG:  return "GOOGLE TAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING CAM";
        case DetectionType::EVILTWIN:    return "EVIL TWIN AP";
        case DetectionType::IBEACON:     return "PROXIMITY BEACON";
        case DetectionType::HACKER:      return "HACKER HARDWARE";
        case DetectionType::LORAWAN:     return "LORAWAN NETWORK";
        case DetectionType::MINE:        return "MY ALERT";
        case DetectionType::MOTION:      return "MOTION IN ROOM";
        case DetectionType::LISTENER:    return "SMART SPEAKER";
        default:                     return "UNKNOWN";
    }
}

static Detection s_last;

// ---- the alerts guide, one scan away ------------------------------------
// The three tracker alerts that can be about YOU -- WITH YOU, NEAR YOU ON N
// DAYS, AWAY FROM OWNER -- draw a QR code to their own section of
// flasher.oillie.cloud/alerts/ in place of the gauge: what it means and what
// to do, on the phone in your hand. Encoded once per section and kept as a
// bitmap (version 3, 29x29: ~110 bytes).
#ifdef ALERT_QR
namespace {
const uint8_t QR_MAX = 33;                 // version 4 at most
uint8_t     s_qrBits[(QR_MAX * QR_MAX + 7) / 8];
uint8_t     s_qrSize = 0;
const char* s_qrFor = nullptr;             // the anchor it encodes
void qrTake(esp_qrcode_handle_t q) {
    const int n = esp_qrcode_get_size(q);
    if (n <= 0 || n > QR_MAX) { s_qrSize = 0; return; }
    memset(s_qrBits, 0, sizeof s_qrBits);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (esp_qrcode_get_module(q, x, y)) s_qrBits[(y * n + x) >> 3] |= (uint8_t)(1u << ((y * n + x) & 7));
    s_qrSize = (uint8_t)n;
}
bool qrFor(const char* anchor) {
    if (s_qrFor == anchor) return s_qrSize != 0;
    s_qrFor = anchor;
    s_qrSize = 0;
    char url[64];
    snprintf(url, sizeof url, "https://flasher.oillie.cloud/alerts/#%s", anchor);
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qrTake;
    cfg.max_qrcode_version = 4;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    if (esp_qrcode_generate(&cfg, url) != ESP_OK) s_qrSize = 0;
#ifdef BENCH_TOOLS
    // The bitmap as hex, for the bench to decode (it can't see the screen).
    Serial.printf("[qr] %s %u ", anchor, (unsigned)s_qrSize);
    for (size_t i = 0; i < ((size_t)s_qrSize * s_qrSize + 7) / 8; i++) Serial.printf("%02x", s_qrBits[i]);
    Serial.println();
#endif
    return s_qrSize != 0;
}
// The code, black on white with its quiet zone, centred in a box of side
// `side`, and "SCAN: WHAT TO DO" under it when there is room. False when it
// does not fit (the caller draws the gauge instead).
bool drawQr(TFT_eSPI& t, int cx, int cy, int side) {
    const int n = s_qrSize;
    if (!n) return false;
    const int capH = 10;
    int px = (side - capH) / (n + 4);
    bool cap = true;
    if (px < 2) { px = side / (n + 4); cap = false; }
    if (px < 2) return false;
    const int qs = (n + 4) * px;
    const int x0 = cx - qs / 2, y0 = cy - (qs + (cap ? capH : 0)) / 2;
    t.fillRect(x0, y0, qs, qs, TFT_WHITE);
    for (int y = 0; y < n; y++) {
        int run = -1;   // horizontal runs of dark modules: one fillRect each
        for (int x = 0; x <= n; x++) {
            const bool on = x < n && (s_qrBits[(y * n + x) >> 3] >> ((y * n + x) & 7)) & 1;
            if (on && run < 0) run = x;
            if (!on && run >= 0) {
                t.fillRect(x0 + (2 + run) * px, y0 + (2 + y) * px, (x - run) * px, px, TFT_BLACK);
                run = -1;
            }
        }
    }
    if (cap) {
        t.setTextSize(1);
        t.setTextColor(Theme::VAPOR_YELLOW, Theme::BG);
        const char* c = "SCAN: WHAT TO DO";
        t.setCursor(cx - t.textWidth(c) / 2, y0 + qs + 2);
        t.print(c);
    }
    return true;
}
}  // namespace
#endif
static bool s_touched = false;
static bool s_ringing = false;   // RING held down while the board talks to the tracker
static int  s_ringW = 0, s_ringH = 0;   // the panel the card was last drawn on
void uiAlertSetRinging(bool on) { s_ringing = on; }
static bool s_redacted = false;
void uiAlertSetRedacted(bool r) { s_redacted = r; }

// Six saturated stops blended pairwise. The same construction the CLEAR
// headline's rainbow uses, and for the same reason: RGB332 has 8 levels of
// red and green but only FOUR of blue, so a computed HSV sweep bands badly
// while a walk between colours known to survive the downconvert does not.
static uint16_t hue6(float pos) {
    static const uint16_t STOPS[6] = {
        Theme::RED, Theme::AMBER, Theme::GREEN,
        Theme::CYAN, Theme::VAPOR_PURPLE, Theme::PINK
    };
    pos = fmodf(pos, 6.0f);
    if (pos < 0) pos += 6.0f;
    const int i0 = (int)pos, i1 = (i0 + 1) % 6;
    return Theme::blend(STOPS[i0], STOPS[i1], (uint16_t)((pos - i0) * 255));
}

static uint16_t lighten(uint16_t c, uint16_t amt) { return Theme::blend(c, Theme::WHITE, amt); }
static uint16_t darken (uint16_t c, uint16_t amt) { return Theme::blend(c, Theme::BLACK, amt); }

// The RGB frame. One full spectrum wrapped exactly once around the
// perimeter, rotating -- so every hue is on screen at all times and the
// border never goes dark or reads as switched off.
//
// The walk is what makes that possible: d is the distance travelled around
// the outer rectangle, continuous from 0 back to P, so the hue at the last
// pixel of the left edge meets the hue at the first pixel of the top. Each
// step paints BORDER_W pixels inward, which means the four corners are
// painted twice -- they agree, because both visits carry the same d.
static const int   BORDER_W  = 2;
static const float BORDER_MS = 400.0f;  // ms per sixth of the spectrum

static void drawRgbBorder(TFT_eSPI& t, int w, int h, uint32_t now) {
    const float P = (float)(2 * (w + h));
    const float phase = (float)now / BORDER_MS;
    for (int x = 0; x < w; x++) {
        t.drawFastVLine(x, 0, BORDER_W, hue6((float)x / P * 6.0f + phase));
        t.drawFastVLine(x, h - BORDER_W, BORDER_W,
                        hue6((float)(w + h + (w - 1 - x)) / P * 6.0f + phase));
    }
    for (int y = 0; y < h; y++) {
        t.drawFastHLine(w - BORDER_W, y, BORDER_W,
                        hue6((float)(w + y) / P * 6.0f + phase));
        t.drawFastHLine(0, y, BORDER_W,
                        hue6((float)(2 * w + h + (h - 1 - y)) / P * 6.0f + phase));
    }
}

// The header strip's bevel, built from the strip's OWN colour rather than
// Win95 grey: the strip is a coloured title bar, and grey chrome around it
// would read as a second, unrelated widget sitting behind the type name.
//
// Pushed harder than the IGNORE button's bevel. That one has five distinct
// greys to build from; a bevel made out of a single colour has only what
// lightening and darkening can reach from that colour, and on a mid purple
// through RGB332 the gentle version came back as almost nothing.
static void stripBevel(TFT_eSPI& t, int x, int y, int w, int stripH, uint16_t c) {
    const uint16_t lit = lighten(c, 170), litSoft = lighten(c, 75);
    const uint16_t shd = darken(c, 170),  shdSoft = darken(c, 75);
    const int R = x + w - 1, Bm = y + stripH - 1;
    t.drawFastHLine(x, y, w, lit);
    t.drawFastHLine(x, y + 1, w, litSoft);
    t.drawFastHLine(x, Bm - 1, w, shdSoft);
    t.drawFastHLine(x, Bm, w, shd);
    t.drawFastVLine(x, y, stripH, lit);
    t.drawFastVLine(x + 1, y, stripH, litSoft);
    t.drawFastVLine(R - 1, y, stripH, shdSoft);
    t.drawFastVLine(R, y, stripH, shd);
}

// Whether the player's selected background animates behind the alert.
// Kept as a named constant rather than inlined so it is one edit to take
// back out, and 0..255 of dim so it can be tuned without touching the
// draw order.
static const bool     ALERT_SHOW_BACKGROUND = true;
static const uint16_t ALERT_PALETTE_DIM     = 150;  // palette knock-back
static const uint8_t  ALERT_BACKGROUND_DIM  = 128;  // every other row to BG

// ALERT gets its own deliberate glitch cadence instead of waiting on
// the shared ambient 5-10s roll -- one right away (so the screen reads
// as glitchy from the first frame, not just eventually), then again at
// 5s/8s/10s, each one louder than the last (see
// Theme::triggerGlitchBurst()'s intensity param), then holding at the
// loudest level every 2.5s for as long as the alert stays up. All of
// it fires through that same shared burst drawBangersText()/
// drawGlitchStatic() everywhere else already read from, so this is
// just a schedule + escalation curve, not a second rendering path.
static uint32_t s_alertStart = 0;
static uint8_t  s_glitchStep = 0;

// step 0 (immediate) -> level 1, step 1 (2.5s) -> level 2, step 2 (4s)
// -> level 3 (screen tear joins in), step 3 (5s) -> level 4 (loudest),
// step 4+ (every 1.25s after) stays pinned at 4. Twice the cadence of
// the original 0/5/8/10s + 2.5s schedule -- same shape, half the wait.
static uint32_t glitchStepOffsetMs(uint8_t step) {
    switch (step) {
        case 0: return 0;
        case 1: return 2500;
        case 2: return 4000;
        case 3: return 5000;
        default: return 5000 + (uint32_t)(step - 3) * 1250;
    }
}
static uint8_t glitchStepLevel(uint8_t step) {
    uint16_t lv = (uint16_t)step + 1;
    return (uint8_t)(lv > 4 ? 4 : lv);
}

static bool s_first = false;
static bool s_night = false;
static bool s_lastFree = false;
void uiAlertSetFirst(bool first) { s_first = first; }
void uiAlertSetNight(bool night) { s_night = night; }
void uiAlertSetLastFree(bool lastFree) { s_lastFree = lastFree; }
static bool     s_spam      = false;
static uint16_t s_spamFakes = 0;
void uiAlertSetSpam(bool spam, uint16_t fakes) { s_spam = spam; s_spamFakes = fakes; }
static const char* s_snzLabel = nullptr;
static uint16_t    s_snzLeft  = 0;
void uiAlertSetSnoozePick(const char* label, uint16_t left) {
    s_snzLabel = label;
    s_snzLeft  = left > 1000 ? 1000 : left;
}
#if defined(CYD_GPS)
static bool    s_place = false, s_placeFake = false;
static int32_t s_placeLat7 = 0, s_placeLon7 = 0;
void uiAlertSetPlace(bool has, int32_t lat7, int32_t lon7, bool fake) {
    s_place = has; s_placeLat7 = lat7; s_placeLon7 = lon7; s_placeFake = fake;
}
#endif
static uint16_t s_followMin = 0;
void uiAlertSetFollow(uint16_t minutes) { s_followMin = minutes; }
static uint8_t s_days = 0;
void uiAlertSetDays(uint8_t days) { s_days = days; }
static uint8_t s_queue = 0;
void uiAlertSetQueue(uint8_t n) { s_queue = n; }
static bool s_byHand = false, s_watching = false;
void uiAlertSetByHand(bool on)   { s_byHand = on; }
void uiAlertSetWatching(bool on) { s_watching = on; }

// ---- the trail (upstream SquachWatch v1.50.0, drawn MuleSkin's way) -------
// Upstream rebuilt its card around one question -- has this thing been
// following you? -- and replaced the card to ask it. Here the card keeps its
// shape and the answer goes where the card already says things: the verdict
// in the banner line, the stops on a line under the plate, the last minute of
// signal as a ladder in the SIGNAL bar, and a tracker's owner in the readout.
//
// The verdict, strongest first. WITH YOU and NEAR YOU ON N DAYS (the
// tracker-only checks) still outrank it, and FIRST / AT NIGHT take the line
// when the verdict is just NEW.
enum class Verdict : uint8_t { NEW, MET, FOLLOWING, SPAM };
static Verdict verdictOf(const Detection& d) {
    if (s_spam) return Verdict::SPAM;
    if (Trail::following(d)) return Verdict::FOLLOWING;
    if (Regulars::nameFor(d.mac)) return Verdict::MET;
    return Verdict::NEW;
}

// The banner line's text and colour, or nullptr for none. Shared by the big
// card and the 240x135 one so the two never disagree about what to say.
static const char* bannerText(char* buf, size_t n, uint16_t& col, Verdict v) {
    col = Theme::RED;
    if (s_followMin) { snprintf(buf, n, "* WITH YOU %u MIN *", (unsigned)s_followMin); return buf; }
    if (s_days)      { snprintf(buf, n, "* NEAR YOU ON %u DAYS *", (unsigned)s_days); return buf; }
    if (v == Verdict::FOLLOWING) {
        snprintf(buf, n, "* FOLLOWING: %u STOPS, %u MIN *", (unsigned)s_last.stops, (unsigned)Trail::spanMin(s_last));
        return buf;
    }
    if (v == Verdict::SPAM) { snprintf(buf, n, "* SPAM: %u FAKE TAGS *", (unsigned)s_spamFakes); return buf; }
    if (v == Verdict::MET && !s_redacted) {
        col = Theme::VAPOR_BLUE;
        snprintf(buf, n, "* MET BEFORE: %s, %u DAYS *", Regulars::nameFor(s_last.mac), (unsigned)Regulars::daysFor(s_last.mac));
        return buf;
    }
    if (s_first || s_night) {
        col = Theme::VAPOR_YELLOW;
        return (s_first && s_night) ? "* FIRST, AND AT NIGHT *" : s_first ? "* FIRST OF ITS KIND *" : "* AT NIGHT *";
    }
    if (s_lastFree) { col = Theme::CYAN; return "* QUIET UNLESS IT NEARS *"; }
    return nullptr;
}

// A tracker's owner, in the words the card uses; nullptr when the advert
// does not say. AWAY also shows as the vendor line ("AWAY FROM OWNER").
static const char* ownerWord(uint8_t owner) {
    switch (owner) {
        case OWNER_WITH:  return "WITH OWNER";
        case OWNER_AWAY:  return "AWAY";
        case OWNER_UNSET: return "NOT SET UP";
        default:          return nullptr;
    }
}

// "41m", "3h", "2d": how long ago a minute of uptime was.
static void agoText(char* b, size_t n, uint16_t nowMin, uint16_t thenMin) {
    const unsigned m = (uint16_t)(nowMin - thenMin);
    if (m < 60)        snprintf(b, n, "%um", m);
    else if (m < 1440) snprintf(b, n, "%uh", m / 60);
    else               snprintf(b, n, "%ud", m / 1440);
}

// The stops line: "TRAIL HOME 41m > 18m > HERE  1.2KM". Only once there are
// two stops -- one stop is just "here", which says nothing.
static bool trailText(char* b, size_t n, uint32_t now) {
    if (s_last.stops < 2 || s_redacted) return false;
    const uint16_t nowMin = (uint16_t)(now / 60000u);
    size_t k = (size_t)snprintf(b, n, "TRAIL");
    for (uint8_t i = 0; i < s_last.stops && k < n; i++) {
        const uint8_t pl = s_last.stopPlace[i];
        char ago[8];
        const bool here = (i == s_last.stops - 1) && pl == Places::current();
        if (here) snprintf(ago, sizeof ago, "HERE");
        else      agoText(ago, sizeof ago, nowMin, s_last.stopLastMin[i]);
        k += (size_t)snprintf(b + k, n - k, "%s%s%s", i ? " > " : " ",
                              Places::isHome(pl) ? "HOME " : "", ago);
    }
    const uint16_t m = Trail::spanM(s_last);
    if (m && k < n) {
        if (m >= 1000) k += (size_t)snprintf(b + k, n - k, "  %u.%uKM", m / 1000, (m % 1000) / 100);
        else           k += (size_t)snprintf(b + k, n - k, "  %uM", (unsigned)m);
    }
    return true;
}

// The card keeps the row it was opened with, but its trail moves while it is
// up: the ladder, the stops and the owner are read off the live row.
static void refreshTrail(const DetectionEngine& eng) {
    for (uint8_t i = 0; i < eng.logCount(); i++) {
        const Detection* d = eng.logAt(i);
        if (!d || d->type != s_last.type || memcmp(d->mac, s_last.mac, 6) != 0) continue;
        memcpy(s_last.spark, d->spark, sizeof s_last.spark);
        s_last.sparkHead  = d->sparkHead;
        s_last.sparkCount = d->sparkCount;
        s_last.sparkAt    = d->sparkAt;
        s_last.stops      = d->stops;
        memcpy(s_last.stopPlace, d->stopPlace, sizeof s_last.stopPlace);
        memcpy(s_last.stopFirstMin, d->stopFirstMin, sizeof s_last.stopFirstMin);
        memcpy(s_last.stopLastMin, d->stopLastMin, sizeof s_last.stopLastMin);
        if (d->owner) s_last.owner = d->owner;
        return;
    }
}

// The last minute of signal as a ladder of bars, oldest on the left, in the
// SIGNAL bar's box: -90 dBm empty, -40 full, the newest bar brightest.
static void drawLadder(TFT_eSPI& t, int x, int y, int w, int h, uint16_t col) {
    t.drawRect(x, y, w, h, col);
    const int n = Detection::SPARK_N;
    const int slot = (w - 2) / n;
    if (slot < 2) return;
    const int x0 = x + 1 + ((w - 2) - slot * n) / 2;
    for (int i = 0; i < s_last.sparkCount && i < n; i++) {
        int v = Trail::sparkAt(s_last, (uint8_t)i);
        if (v < -90) v = -90;
        if (v > -40) v = -40;
        const int bh = (v + 90) * (h - 2) / 50;
        if (bh <= 0) continue;
        const int bx = x0 + (n - 1 - i) * slot;
        t.fillRect(bx, y + h - 1 - bh, slot - 1, bh, i == 0 ? Theme::WHITE : col);
    }
}

void uiAlertInit(TFT_eSPI& t, const Detection& d) {
    s_ringW = t.width(); s_ringH = t.height();
    s_last = d;
    s_first = false;
    s_night = false;
    s_lastFree = false;
    s_spam     = false;
    s_snzLabel = nullptr;
    s_followMin = 0;
    s_days = 0;
    s_queue = 0;
    s_byHand = false;
    s_watching = false;
#if defined(CYD_GPS)
    s_place    = false;   // a card never shows an earlier alert's place
#endif
    s_touched = false;
    s_alertStart = millis();
    s_glitchStep = 0;
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

// MORE INFO button, bottom-right corner -- every other element on this
// screen (title, target label, confidence, vendor, MAC, RSSI, radar,
// wordmark) stays horizontally centered regardless of rotation, so a
// corner is the one spot guaranteed clear on all of them.
// Every number on this screen was measured against a 320x240 panel and then
// fixed there, so on the 3.5" the card, the radar and the three buttons all
// kept their old sizes and drifted apart around a hole in the middle. These
// scale them together; the narrow case returns exactly what it always did.
static inline bool wideAlert(int w) { return w >= 400; }
static inline int  stripHOf(int w)  { return wideAlert(w) ? 56 : 42; }
// A screen under 200 rows (the M5Stack StickS3's and Cardputer's 240x135)
// gets a card of its own -- see drawTinyCard(). Its four buttons: IGNORE in
// a slim header, HUNT / SNOOZE / INFO in a row along the bottom.
static inline bool tinyAlert(int w, int h) { return h < 200 && w < 300; }
static const int TINY_STRIP_H = 24, TINY_BTN_H = 20;
static void tinyBtnRect(int i, int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    bw = (screenW - 16) / 3;
    bh = TINY_BTN_H;
    bx = 4 + i * (bw + 4);
    by = screenH - bh - 4;
}

static void moreInfoBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    if (tinyAlert(screenW, screenH)) { tinyBtnRect(2, screenW, screenH, bx, by, bw, bh); return; }
    // Stacked "MORE"/"INFO" on two lines (see the draw call below)
    // instead of one wide "MORE INFO" row -- a size-2 attempt at a
    // bigger tap target that widened the button instead ate into the
    // running critter animation's clear margin on the narrowest 240px
    // landscape rotation. Going taller instead of wider keeps the
    // footprint narrow enough to actually clear the radar/MAC/wordmark
    // over there while still being a generously padded, easy target.
    // Sized off this font's known 12x16px-per-glyph metrics at size 2
    // (both words are 4 characters, so one width covers both) rather
    // than a live measurement, since these are fixed literal strings,
    // not runtime data -- the actual draw call still centers each line
    // with a live t.textWidth() regardless, so a few px of slop here
    // just shows up as slightly uneven padding, never a layout break.
    bw = wideAlert(screenW) ? 104 : 70;
    bh = wideAlert(screenW) ?  64 : 48;
    bx = screenW - bw - 4;
    by = screenH - bh - 4;
}

// HUNT button, bottom-LEFT corner -- the mirror of moreInfoBtnRect()
// above, and for the same reason: the corners are the only spots
// guaranteed clear of the centered column of text on every rotation.
// Same 70x48 footprint so the two read as a matched pair, even though
// "HUNT" is one short line and doesn't need the height -- matching the
// neighbour matters more here than shrinking to fit the label.
static void huntBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    if (tinyAlert(screenW, screenH)) { tinyBtnRect(0, screenW, screenH, bx, by, bw, bh); return; }
    bw = wideAlert(screenW) ? 104 : 70;
    bh = wideAlert(screenW) ?  64 : 48;
    bx = 4;
    by = screenH - bh - 4;
}

// Height of the coloured header strip the detection type is named in. File
// scope rather than a local in the draw function because the IGNORE button
// sits inside this strip and is centred in it -- a number two things have to
// agree on is not one either of them should own privately.


// IGNORE sits in the header strip, at its right-hand end. The bottom two
// corners are taken by HUNT and MORE INFO, and the strip's left-hand end is
// where the detection type is named, so this is the remaining spot that
// stays clear on the narrow rotations as well as the wide ones.
static void ignoreBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    if (tinyAlert(screenW, screenH)) {
        bw = 54; bh = TINY_STRIP_H - 6;
        bx = screenW - bw - 6; by = 3;
        return;
    }
    // Deliberately smaller than HUNT and MORE INFO, which are 70x48 down
    // in the corners where nothing else goes. Up here it shares the header
    // strip with the target label, and the label's budget is measured off
    // this rect (see the stripHOf(w) block), so every pixel taken here is a
    // pixel taken from the longest type names.
    //
    // 62x28. Thicker than the 54x20 it replaced, and WIDER in step with it
    // rather than just taller: "IGNORE" is 36px at size 1, so inside the 2px
    // bevel 62x28 leaves 11px of air each side against 8 above and below.
    // Horizontal padding exceeding vertical is the proportion that makes a
    // push button look like one, and growing the height alone would have
    // inverted it -- a Win95 button is a wide, shallow thing (75x23 dialog
    // units, about 3.3:1), so getting chunkier has to mean getting bigger,
    // not getting squarer.
    //
    // The width is capped by the header label beside it, whose budget is
    // measured off this rect: on the 240px rotation that budget is 218-bw,
    // and every type name was measured in Bangers MD to find the real
    // ceiling. The longest that still fits the face is "CARD SKIMMER" at
    // 151px, so bw can reach 67 before it would push that one into the
    // built-in font. 62 leaves 5px of margin. The only two names over the
    // line -- "PROXIMITY BEACON" at 188 and "HACKER HARDWARE" at 187 --
    // were already stepping down at any width.
    bw = wideAlert(screenW) ? 92 : 62;
    bh = wideAlert(screenW) ? 38 : 28;
    // 8 from the edge rather than 4. The margin has to clear the BORDER, not
    // the screen: with a 3px frame drawn over the outermost columns, a 4px
    // margin left the button one pixel off it, which reads as a collision
    // rather than a gap. 8 leaves 5 clear of the widest frame.
    bx = screenW - bw - 8;
    // Centred in the strip rather than pinned 4px below its top edge, which
    // left 18 rows of dead colour underneath and made the button read as
    // stuck to the ceiling instead of sitting in a title bar. The strip is
    // the only thing behind it, so centring on the strip IS centring on
    // everything it overlaps -- and it is derived, so if stripHOf(w) ever moves
    // the button moves with it.
    by = (stripHOf(screenW) - bh) / 2;
}

bool uiAlertHitIgnore(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    ignoreBtnRect(screenW, screenH, bx, by, bw, bh);
    // Padded outward. The button lost 4 rows becoming the right shape, and
    // a correctly proportioned button is no use if a resistive panel and a
    // fingertip cannot land on it -- so the drawn rect shrank and the
    // target grew. Nothing else is up here to steal a near miss from: the
    // slop runs into the screen edge on two sides and into the header
    // strip's own dead space on the others.
    const int SLOP = 6;
    return x >= bx - SLOP && x <= bx + bw + SLOP &&
           y >= by - SLOP && y <= by + bh + SLOP;
}

// RING, on the trackers only (and only on the boards built with it): the same
// system button as IGNORE, just left of it in the header strip. The label's
// budget is measured off this one when it is there.
// Not on the 240x135 card: its row of three has no room, and a button that
// is not drawn must not take taps either.
static bool ringShown() { return TagRing::canRing(s_last.type) && !tinyAlert(s_ringW, s_ringH); }
static void ringBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    int ix, iy, iw, ih;
    ignoreBtnRect(screenW, screenH, ix, iy, iw, ih);
    bw = wideAlert(screenW) ? 76 : 52;
    bh = ih;
    bx = ix - 10 - bw;
    by = iy;
}

bool uiAlertRingShown() { return ringShown(); }

bool uiAlertHitRing(int x, int y, int screenW, int screenH) {
    s_ringW = screenW; s_ringH = screenH;
    if (!ringShown()) return false;
    int bx, by, bw, bh;
    ringBtnRect(screenW, screenH, bx, by, bw, bh);
    // Less sideways slop than IGNORE's: the two sit 10 px apart.
    return x >= bx - 3 && x <= bx + bw + 3 && y >= by - 6 && y <= by + bh + 6;
}

bool uiAlertRingCentre(int screenW, int screenH, int& cx, int& cy) {
    if (!ringShown()) return false;
    int bx, by, bw, bh;
    ringBtnRect(screenW, screenH, bx, by, bw, bh);
    cx = bx + bw / 2;
    cy = by + bh / 2;
    return true;
}

// SNOOZE, bottom centre. The corners are taken and the plate ends well
// above the bottom row, so between HUNT and MORE INFO is the one strip
// left clear on every rotation: 92 px on the narrow one, 84 used.
static void snoozeBtnRect(int screenW, int screenH, int& bx, int& by, int& bw, int& bh) {
    if (tinyAlert(screenW, screenH)) { tinyBtnRect(1, screenW, screenH, bx, by, bw, bh); return; }
    bw = wideAlert(screenW) ? 124 : 84;
    bh = wideAlert(screenW) ?  40 : 28;
    bx = (screenW - bw) / 2;
    by = screenH - bh - 4;
}

uint8_t uiAlertStops(int screenW, int screenH, int16_t* xs, int16_t* ys, uint8_t cap) {
    uint8_t n = 0;
    auto add = [&](int x, int y) { if (n < cap) { xs[n] = (int16_t)x; ys[n] = (int16_t)y; n++; } };
    add(screenW / 3, screenH / 2);   // the plate: anywhere dismisses
    int bx, by, bw, bh;
    snoozeBtnRect(screenW, screenH, bx, by, bw, bh);   add(bx + bw / 2, by + bh / 2);
    huntBtnRect(screenW, screenH, bx, by, bw, bh);     add(bx + bw / 2, by + bh / 2);
    moreInfoBtnRect(screenW, screenH, bx, by, bw, bh); add(bx + bw / 2, by + bh / 2);
    ignoreBtnRect(screenW, screenH, bx, by, bw, bh);   add(bx + bw / 2, by + bh / 2);
    return n;
}

bool uiAlertHitSnooze(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    snoozeBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

bool uiAlertHitHunt(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    huntBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

bool uiAlertHitMoreInfo(int x, int y, int screenW, int screenH) {
    int bx, by, bw, bh;
    moreInfoBtnRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

// The card on a 240x135 screen: everything the big one says, in the order
// it matters, and nothing that needs room -- no gauge, no QR code. The type
// in a slim strip in its colour with IGNORE at its end; a banner line (WITH
// YOU, NEAR YOU ON N DAYS, SPAM, FIRST...) when there is one; vendor, the
// device's own name, its address; signal as a bar with the grade; the three
// buttons along the bottom.
static void drawTinyCard(TFT_eSPI& t, int w, int h) {
    const uint16_t typeCol = Theme::colorFor(s_last.type);
    t.fillRect(0, 0, w, TINY_STRIP_H, typeCol);
    t.setTextWrap(false);
    {
        int ibx, iby, ibw, ibh;
        ignoreBtnRect(w, h, ibx, iby, ibw, ibh);
        const char* tgt = targetLabel(s_last.type);
        t.setTextSize(2);
        if (t.textWidth(tgt) > ibx - 10) t.setTextSize(1);
        t.setTextColor(Theme::BG, typeCol);
        t.setCursor(6, (TINY_STRIP_H - t.fontHeight()) / 2);
        t.print(tgt);
        Theme::drawWin95Button(t, ibx, iby, ibw, ibh, IgnoreList::contains(s_last.mac) ? "MUTED" : "IGNORE",
                               IgnoreList::contains(s_last.mac));
    }
    // The plate: between the strip and the buttons.
    const int py = TINY_STRIP_H + 3, ph = h - TINY_BTN_H - 8 - py;
    t.fillRect(4, py, w - 8, ph, Theme::BG);
    t.drawRect(4, py, w - 8, ph, typeCol);
    int y = py + 4;
    t.setTextSize(1);
    char line[48];
    uint16_t bannerCol = Theme::RED;
    const char* banner = bannerText(line, sizeof line, bannerCol, verdictOf(s_last));
    // QUIET UNLESS IT NEARS is housekeeping; this card has no line to spare.
    if (banner && !strcmp(banner, "* QUIET UNLESS IT NEARS *")) banner = nullptr;
    if (banner) {
        t.setTextColor(bannerCol, Theme::BG);
        t.setCursor((w - t.textWidth(banner)) / 2, y);
        t.print(banner);
        y += 10;
    }
    // Vendor, upper-cased, the way the big card says it.
    {
        const char* vend = vendorText(s_last);
        const char* src = s_redacted ? "LOCKED" : strcmp(vend, "Away-Owner") == 0 ? "AWAY FROM OWNER" : vend;
        char up[24];
        size_t i = 0;
        for (; src[i] && i + 1 < sizeof up; i++) up[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i];
        up[i] = '\0';
        t.setTextSize(up[0] && t.textWidth(up) * 2 <= w - 16 ? 2 : 1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor((w - t.textWidth(up)) / 2, y);
        t.print(up);
        y += t.fontHeight() + 2;
    }
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    if (s_last.name[0] && !s_redacted) {
        char pv[40];
        const char* nm = Privacy::name(s_last.name, pv, sizeof pv);
        t.setCursor((w - t.textWidth(nm)) / 2, y);
        t.print(nm);
        y += 10;
    }
    char mac[24];
    if (s_redacted) snprintf(mac, sizeof mac, "%s", "-- LOCKED --");
    else            Privacy::mac(mac, sizeof mac, s_last.mac);
    t.setCursor((w - t.textWidth(mac)) / 2, y);
    t.print(mac);
    // Signal along the plate's floor: the bar, the number, the grade.
    {
        const Confidence conf = s_last.conf;
        const uint16_t confColor = (conf == Confidence::HIGH_CONF) ? Theme::GREEN
                                 : (conf == Confidence::MED_CONF) ? Theme::AMBER : Theme::RED;
        const int by = py + ph - 12;
        const char* ow = ownerWord(s_last.owner);
        if (ow && s_last.owner != OWNER_AWAY && !s_redacted) {
            // WITH OWNER / NOT SET UP over the bar (AWAY is the vendor line).
            t.setTextColor(Theme::VAPOR_BLUE, Theme::BG);
            t.setCursor((w - t.textWidth(ow)) / 2, by - 10);
            t.print(ow);
        }
        snprintf(line, sizeof line, "%d dBm", (int)s_last.rssi);
        t.setTextColor(confColor, Theme::BG);
        t.setCursor(10, by + 1);
        t.print(line);
        const char* cl = confidenceLabel(conf);
        t.setCursor(w - 10 - t.textWidth(cl), by + 1);
        t.print(cl);
        const int bx = 10 + t.textWidth("-100 dBm") + 6, bw = (w - 10 - t.textWidth(cl) - 6) - bx;
        int v = s_last.rssi;
        if (v < -90) v = -90;
        if (v > -40) v = -40;
        if (bw > 8) {
            t.drawRect(bx, by, bw, 9, Theme::CYAN);
            const int fill = (v + 90) * (bw - 2) / 50;
            if (fill > 0) t.fillRect(bx + 1, by + 1, fill, 7, Theme::CYAN);
        }
    }
    // HUNT / SNOOZE / INFO.
    static const char* const LABELS[3] = { "HUNT", "SNOOZE", "INFO" };
    if (s_queue && !banner) {
        char q[12];
        snprintf(q, sizeof q, "+%u MORE", (unsigned)s_queue);
        t.setTextSize(1);
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(w - 8 - t.textWidth(q), py + 4);
        t.print(q);
    }
    for (int i = 0; i < 3; i++) {
        int bx, by, bw, bh;
        tinyBtnRect(i, w, h, bx, by, bw, bh);
        const bool picking = (i == 1 && s_snzLabel);
        const bool watch   = (i == 1 && s_byHand);
        const char* label = picking ? s_snzLabel : watch ? ("WATCH") : LABELS[i];
        t.fillRect(bx, by, bw, bh, watch && s_watching ? Theme::PURPLE : Theme::BG);
        t.drawRect(bx, by, bw, bh, picking ? Theme::AMBER : Theme::PURPLE);
        t.setTextSize(1);
        t.setTextColor(i == 0 || picking ? Theme::AMBER : Theme::CYAN, Theme::BG);
        t.setCursor(bx + (bw - t.textWidth(label)) / 2, by + (bh - t.fontHeight()) / 2);
        t.print(label);
        if (picking) {
            const int barW = (int)((int32_t)(bw - 4) * s_snzLeft / 1000);
            if (barW > 0) t.fillRect(bx + 2, by + bh - 3, barW, 1, Theme::AMBER);
        }
    }
}

void uiAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                 bool infoPending, const char* infoTypeName, const char* infoText) {
    int w = t.width();
    int h = t.height();
    s_ringW = w; s_ringH = h;
    refreshTrail(eng);
    const Verdict verdict = verdictOf(s_last);

    uint32_t elapsed = now - s_alertStart;
    // Capped well short of wrapping the uint8_t step counter -- an
    // alert would need to sit on screen for ~10+ minutes to get here,
    // at which point it's already been holding at the loudest level
    // for a long time and one more (or zero more) re-trigger makes no
    // visible difference.
    while (s_glitchStep < 200 && elapsed >= glitchStepOffsetMs(s_glitchStep)) {
        Theme::triggerGlitchBurst(glitchStepLevel(s_glitchStep));
        s_glitchStep++;
    }

    // The player's chosen background runs behind the alert, then the
    // themed detection FX layer on top of it without their own erase.
    // Dimmed on the way past: at full brightness a starfield or a fire
    // competes with the headline type for attention, and this screen has
    // to stay readable first. ALERT_SHOW_BACKGROUND is the single switch
    // if that turns out to be the wrong call -- false restores the flat
    // Theme::BG this screen had before.
    if (ALERT_SHOW_BACKGROUND) {
        // Two-stage knock-back, both cheap. The palette dim costs
        // nothing at all -- the background renderers read these globals,
        // so they simply draw darker -- but it only reaches colours that
        // come from the palette, and several backgrounds (the starfield's
        // tunnel rings and planets especially) pack their own literals.
        // The scanline pass catches whatever the palette could not.
        Theme::Palette saved = Theme::dimPaletteForOverlay(ALERT_PALETTE_DIM);
        Theme::drawActiveBackground(t, now, 0, h, eng);
        Theme::restorePalette(saved);
        Theme::dimRegion(t, 0, 0, w, h, ALERT_BACKGROUND_DIM);
    } else {
        t.fillRect(0, 0, w, h, Theme::BG);
    }

    if (tinyAlert(w, h)) {
        drawTinyCard(t, w, h);
        Theme::drawGlitchStatic(t, 0, 0, w, h);
        drawRgbBorder(t, w, h, now);
        if (infoPending) Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
        return;
    }

    // ---- header strip ------------------------------------------------
    // Replaces the pulsing border and the "!! DETECTION !!" headline with
    // one solid bar in the detection's own colour. Three things fall out of
    // that. The category is readable before any word is: HACKER and the
    // attack types come up red, cameras cyan, trackers purple. The border's
    // six pixels come back on all four sides. And "!! DETECTION !!" stops
    // spending the best row on the screen restating what the screen is.
    //
    // 42 rows because Bangers MD is a 33px cell and it has to sit inside.
    const uint16_t typeCol = Theme::colorFor(s_last.type);
    t.fillRect(0, 0, w, stripHOf(w), typeCol);

    // Inset by the border width. The frame draws last and would otherwise
    // paint straight over the strip's lit top edge and both its side edges,
    // leaving only the bottom shadow -- which is not a bevel. The strip sits
    // INSIDE the frame, so its bevel has to as well.
    stripBevel(t, BORDER_W, BORDER_W, w - 2 * BORDER_W, stripHOf(w) - BORDER_W, typeCol);

    // The label in Bangers MD rather than LG: MD is narrower per glyph, and
    // the longest labels here ("PROXIMITY BEACON", "HACKER HARDWARE") are
    // longer than the "CARD SKIMMER" the LG sizing note was written for.
    // Measured anyway, with a fallback, because a label that runs off the
    // side of a 240px rotation is not something to find out on hardware.
    {
        const char* tgt = targetLabel(s_last.type);
        // The budget is the space left of the IGNORE button, NOT the screen
        // width. The button is drawn over this strip's right-hand end, so
        // measuring against w put "PROXIMITY BEACON" straight through it on
        // the 240px rotation -- which is exactly the sort of thing that only
        // shows up on the narrow board. Read from ignoreBtnRect rather than
        // restated, so the two cannot drift when the button is resized.
        int ibx, iby, ibw, ibh;
        if (ringShown()) ringBtnRect(w, h, ibx, iby, ibw, ibh);
        else             ignoreBtnRect(w, h, ibx, iby, ibw, ibh);
        // The 6 is the gap between this label and the button, down from 8 to
        // pay for the button's new edge margin without shrinking the button
        // or pushing "CARD SKIMMER" -- the longest name that still fits the
        // Bangers face, at 151px -- into the built-in fallback. On the 240px
        // rotation the budget is now 154, so it clears by 3.
        const int avail = ibx - 10 - 6;
        const int tw = Theme::bangersTextWidth(tgt, Theme::BangersSize::MD);
        if (tw <= avail) {
            Theme::drawBangersText(t, 10, 4, tgt, Theme::BG, Theme::BangersSize::MD);
        } else {
            // No smaller Bangers exists, so step down through the built-in
            // font rather than clip.
            t.setTextSize(wideAlert(w) ? 3 : 2);
            if (t.textWidth(tgt) > avail) t.setTextSize(wideAlert(w) ? 2 : 1);
            t.setTextColor(Theme::BG, typeCol);
            t.setCursor(10, (stripHOf(w) - t.fontHeight()) / 2);
            t.print(tgt);
        }
    }

    // Just the grade, not the old "~60%": that was a number invented to
    // sound precise, and the grade is what the ALERT FILTER gates on.
    Confidence conf = s_last.conf;
    uint16_t confColor = (conf == Confidence::HIGH_CONF) ? Theme::GREEN
                        : (conf == Confidence::MED_CONF) ? Theme::AMBER
                        : Theme::RED;

    // ---- data plate ----------------------------------------------------
    // A solid ground with a hairline in the type's colour. The background
    // still runs behind and around it, so the screen keeps its character,
    // but six-pixel text stops competing with a sunset gradient -- which
    // was the single worst thing about the old layout.
    // Two columns when there is width for them: identity on the left,
    // the gauge on the right. The 240px rotation cannot hold both, so it
    // keeps the full-width plate and puts a smaller gauge underneath.
    // Two columns need a LANDSCAPE shape, not just 300 pixels of width. The
    // 3.5" in portrait is 320x480: wide enough by the old test, so it took the
    // side-by-side layout meant for 320x240 and left the bottom half of the
    // screen empty under it. Asking whether the panel is wider than it is tall
    // sorts that out and cannot change any existing board -- 320x240 is still
    // wide, 240x320 was already narrow.
    const bool wide = (w >= 300 && w > h);
    const int PLATE_X = wide ? 12 : 14;
    const int PLATE_Y = stripHOf(w) + 12;
    const int PLATE_W = wideAlert(w) ? 250 : (wide ? 168 : (w - 28));
    const int PLATE_H = wideAlert(w) ? 152 : 98;

    // Where each line sits inside the plate. The old numbers were measured
    // against a 98px card and a size-1 readout; a wide panel gets a 138px card
    // and a size-2 one, so they are re-measured rather than stretched.
    const bool  bigPlate = wideAlert(w);
    const uint8_t readSz = bigPlate ? 2 : 1;
    const int NAME_DY  = bigPlate ?  46 : 38;
    const int MAC_DY   = bigPlate ?  66 : 50;
    const int BAR_DY   = bigPlate ? 112 : 72;
    const int LABEL_DY = bigPlate ?  22 : 12;   // above the bar
    const int INFO_DY  = bigPlate ?  22 : 11;   // up from the plate's bottom
    const int BAR_HH   = bigPlate ?  14 : 12;

    t.fillRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, Theme::BG);
    t.drawRect(PLATE_X, PLATE_Y, PLATE_W, PLATE_H, typeCol);

    // Vendor in Bangers, upper-cased. Bangers carries A-Z, 0-9, space, '!'
    // and (since the hyphen was added for exactly this) '-'. Anything else
    // is SKIPPED by drawBangersText with no advance, so "DroneID" would
    // come out "DID" -- upper-casing is what makes the vendor strings in
    // signatures.cpp renderable at all.
    {
        // "Away-Owner" (signatures.h, "Trackers away from their owner") said
        // in words: the tracker case that matters is one away from its owner.
        const char* vend = vendorText(s_last);
        const char* src = s_redacted ? "LOCKED"
                        : strcmp(vend, "Away-Owner") == 0 ? "AWAY FROM OWNER"
                        : vend;
        char up[16];
        size_t i = 0;
        for (; src[i] && i + 1 < sizeof(up); i++) {
            const char c = src[i];
            up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        up[i] = '\0';
        const int vw = Theme::bangersTextWidth(up, Theme::BangersSize::MD);
        if (vw > 0 && vw <= PLATE_W - 8) {
            Theme::drawBangersText(t, PLATE_X + (PLATE_W - vw) / 2, PLATE_Y + 2, up,
                                   Theme::CYAN, Theme::BangersSize::MD);
        } else {
            t.setTextSize(2);
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(PLATE_X + (PLATE_W - t.textWidth(src)) / 2, PLATE_Y + (bigPlate ? 14 : 10));
            t.print(src);
        }
    }

    // The device's own name, where it has one -- a Flipper's nickname, a
    // Pwnagotchi's, a drone's serial, an iBeacon's deployment. Carried in
    // Detection all along and never drawn on this screen.
    t.setTextSize(readSz);
    t.setTextColor(Theme::WHITE, Theme::BG);
    if (s_last.name[0] && !s_redacted) {
        char pv[40];
        const char* nm = Privacy::name(s_last.name, pv, sizeof pv);
        t.setCursor(PLATE_X + (PLATE_W - t.textWidth(nm)) / 2, PLATE_Y + NAME_DY);
        t.print(nm);
    }

    char mac[24];
    if (s_redacted)
        snprintf(mac, sizeof(mac), "%s", "-- LOCKED --");
    else
        Privacy::mac(mac, sizeof mac, s_last.mac);
    t.setCursor(PLATE_X + (PLATE_W - t.textWidth(mac)) / 2, PLATE_Y + MAC_DY);
    t.print(mac);

    // Signal as a bar as well as a number: -90 dBm empty, -40 full. The
    // number is for the log; the bar is what reads from across a room.
    {
        const int BAR_X = PLATE_X + 8, BAR_Y = PLATE_Y + BAR_DY;
        const int BAR_W = PLATE_W - 16, BAR_H = BAR_HH;
        t.setTextColor(Theme::CYAN, Theme::BG);
        // Label above the bar rather than beside it: the two-column layout
        // leaves the plate 168 wide, and a label plus a usable meter do not
        // both fit on one line at that width. The grade shares that row,
        // right-aligned -- which is also what takes it out of the readout
        // line below, where the four fields together ran 174px into a 168px
        // plate and pushed the sighting count off the edge.
        t.setCursor(PLATE_X + 8, BAR_Y - LABEL_DY);
        // Which way it is going over the last minute, once there is a minute.
        const int8_t tr = Trail::trend(s_last);
        t.print(tr > 0 ? "CLOSER" : tr < 0 ? "FURTHER" : "SIGNAL");
        t.setTextColor(confColor, Theme::BG);
        const char* cl = confidenceLabel(conf);
        t.setCursor(PLATE_X + PLATE_W - 8 - t.textWidth(cl), BAR_Y - LABEL_DY);
        t.print(cl);
        int v = s_last.rssi;
        if (v < -90) v = -90;
        if (v > -40) v = -40;
        // Two readings or more: the ladder of the last minute. One: the bar.
        if (s_last.sparkCount >= 2) {
            drawLadder(t, BAR_X, BAR_Y, BAR_W, BAR_H, Theme::CYAN);
        } else {
            const int fill = (v + 90) * (BAR_W - 2) / 50;
            t.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, Theme::CYAN);
            if (fill > 0) t.fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, Theme::CYAN);
        }
    }

    // The numbers, without the grade -- that moved up to the SIGNAL row so
    // this line fits the narrower plate.
    char info[40];
#if SQW_WIFI_5G
    // A 5 GHz catch says so, in the same width: two spaces where there
    // were three, so "-72 dBm  5G CH 149  x12" is no longer than before.
    if (s_last.channel > 14)
        snprintf(info, sizeof(info), "%d dBm  5G CH %u  x%u",
                 s_last.rssi, s_last.channel, (unsigned)s_last.hits);
    else
#endif
    // A LoRaWAN operator has no WiFi channel and is not Bluetooth: "CH 0"
    // would be a number that means nothing.
    if (s_last.type == DetectionType::LORAWAN)
        snprintf(info, sizeof(info), "%d dBm   LORA   x%u", s_last.rssi, (unsigned)s_last.hits);
    // HUMAN SENSING's movement: the dBm is the router's and says nothing
    // about a person. How sure the model was, and how many times this room
    // has started moving again.
    else if (s_last.type == DetectionType::MOTION)
        snprintf(info, sizeof(info), "SENSED   %s   x%u",
                 s_last.conf == Confidence::HIGH_CONF ? "SURE" : s_last.conf == Confidence::MED_CONF ? "LIKELY" : "MAYBE",
                 (unsigned)s_last.hits);
    // A tracker that says where its owner is: that, in place of "CH 0",
    // which is all a Bluetooth catch had there.
    else if (ownerWord(s_last.owner) && !s_redacted)
        snprintf(info, sizeof(info), "%d dBm   %s   x%u",
                 s_last.rssi, ownerWord(s_last.owner), (unsigned)s_last.hits);
    else
    snprintf(info, sizeof(info), "%d dBm   CH %u   x%u",
             s_last.rssi, s_last.channel, (unsigned)s_last.hits);
    t.setTextColor(confColor, Theme::BG);
    t.setCursor(PLATE_X + (PLATE_W - t.textWidth(info)) / 2, PLATE_Y + PLATE_H - INFO_DY);
    t.print(info);
    // The banner line, in the gap between the strip and the plate: the
    // verdict and the rest, strongest first (bannerText()). WITH YOU and
    // NEAR YOU ON N DAYS are about you rather than the device and lead;
    // FOLLOWING is the trail's answer; a spam flood is about the room; MET
    // BEFORE, FIRST and AT NIGHT are about this catch; QUIET UNLESS IT
    // NEARS is housekeeping and takes the line only when nothing else does.
    char fl[48];
    uint16_t flCol = Theme::RED;
    const char* banner = bannerText(fl, sizeof fl, flCol, verdict);
    if (banner) {
        t.setTextSize(1);
        t.setTextColor(flCol, Theme::BG);
        t.setCursor(PLATE_X + (PLATE_W - t.textWidth(banner)) / 2, stripHOf(w) + 2);
        t.print(banner);
    }
    // The stops line, under the plate when the gauge is beside it, under the
    // gauge when it is below. Red while it is FOLLOWING.
    int underY = wide ? PLATE_Y + PLATE_H + 3 : PLATE_Y + PLATE_H + 62;
    {
        char tl[64];
        if (trailText(tl, sizeof tl, now)) {
            t.setTextSize(1);
            if (underY + t.fontHeight() <= h - 56) {
                const int tw = t.textWidth(tl);
                const int tx = tw <= PLATE_W ? PLATE_X + (PLATE_W - tw) / 2 : PLATE_X;
                t.fillRect(tx - 2, underY - 1, tw + 4, t.fontHeight() + 2, Theme::BG);
                t.setTextColor(verdict == Verdict::FOLLOWING ? Theme::RED : Theme::CYAN, Theme::BG);
                t.setCursor(tx, underY);
                t.print(tl);
                underY += t.fontHeight() + 3;
            }
        }
    }
#if defined(CYD_GPS)
    // Where it was caught, from a fresh GPS fix (upstream SquachWatch
    // v1.36.0). In the banner's line when there is no banner; else under the
    // plate when the gauge is beside it, or under the gauge when it is below.
    // Never on a locked card: it would show where the owner was.
    if (s_place && !s_redacted) {
        char la[16], lo[16], pl[40];
        SdRow::degreesText(la, sizeof la, s_placeLat7, 5);
        SdRow::degreesText(lo, sizeof lo, s_placeLon7, 5);
        snprintf(pl, sizeof pl, "%s, %s%s", la, lo, s_placeFake ? " FAKE" : "");
        const int py = !banner ? stripHOf(w) + 2 : underY;
        t.setTextSize(1);
        if (py + t.fontHeight() <= h - 56) {
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(PLATE_X + (PLATE_W - t.textWidth(pl)) / 2, py);
            t.print(pl);
        }
    }
#endif

    // ---- the gauge -----------------------------------------------------
    // The detected thing, drawn large with the instrument grid over the top
    // of it. The old screen had a 22px radar tucked under the readout and a
    // separate icon at the bottom of the panel, and the two never met -- one
    // said "a contact is out there", the other said "this is what it is".
    // One object, one gauge.
    //
    // Deliberately NOT viewer-centred with the icon riding a bearing: the
    // bearing is derived from the MAC so it holds still during an alert, and
    // it is otherwise arbitrary. One antenna cannot do direction finding, and
    // putting a recognisable object at a compass position would claim it can.
    // Distance is honest -- RSSI really does map to a ring -- so that is what
    // the rings show, with the object at the centre of its own field.
    {
        // The band the gauge actually owns: below the header strip, above
        // the buttons. Centring it on the PLATE instead put its well three
        // pixels into the strip -- the plate starts 12 rows below the strip,
        // so a circle centred on the plate is not centred on the gap.
        const int ceilY  = stripHOf(w) + 4;
        const int floorY = h - 56;
        const int gx = wide ? (PLATE_X + PLATE_W + (w - PLATE_X - PLATE_W) / 2)
                            : (w / 2);
        const int gy = wide ? ((ceilY + floorY) / 2)
                            : (PLATE_Y + PLATE_H + 30);
        // Bounded by whichever runs out first: the width beside the plate,
        // or the height of the band -- and the well is 4px larger than the
        // radius on every side, so that is what has to fit, not the circle.
        int gr = wide ? ((w - PLATE_X - PLATE_W) / 2 - 6) : 26;
        if (gy - gr - 4 < ceilY)  gr = gy - ceilY - 4;
        if (gy + gr + 4 > floorY) gr = floorY - gy - 4;
        const int grMax = wideAlert(w) ? 92 : 60;
        if (gr > grMax) gr = grMax;
        // A tracker alert about you: the guide's QR code in the gauge's box.
        const char* qrAnchor = nullptr;
#ifdef ALERT_QR
        if (!s_redacted) {
            if (s_followMin)    qrAnchor = "withyou";
            else if (s_days)    qrAnchor = "days";
            else if (strcmp(vendorText(s_last), "Away-Owner") == 0) qrAnchor = "away";
        }
#endif
        bool qrDrawn = false;
        if (qrAnchor && gr > 8) {
            t.fillRect(gx - gr - 4, gy - gr - 4, (gr + 4) * 2, (gr + 4) * 2, Theme::BG);
            t.drawRect(gx - gr - 4, gy - gr - 4, (gr + 4) * 2, (gr + 4) * 2, typeCol);
#ifdef ALERT_QR
            qrDrawn = qrFor(qrAnchor) && drawQr(t, gx, gy, (gr + 4) * 2 - 4);
#endif
        }
        if (gr > 8 && !qrDrawn) {
            t.fillRect(gx - gr - 4, gy - gr - 4, (gr + 4) * 2, (gr + 4) * 2, Theme::BG);
            t.drawRect(gx - gr - 4, gy - gr - 4, (gr + 4) * 2, (gr + 4) * 2, typeCol);
            Theme::drawTypeIcon(t, s_last.type, gx, gy, gr / 2);
            // Grid over the object, not under it.
            t.drawCircle(gx, gy, gr,         (uint16_t)0x02CA);
            t.drawCircle(gx, gy, gr * 2 / 3, (uint16_t)0x0207);
            t.drawCircle(gx, gy, gr / 3,     (uint16_t)0x0185);
            t.drawFastHLine(gx - gr, gy, 2 * gr, (uint16_t)0x0144);
            t.drawFastVLine(gx, gy - gr, 2 * gr, (uint16_t)0x0144);
            const float sweep = (float)(now % 2000) / 2000.0f * 6.2831853f;
            t.drawLine(gx, gy, gx + (int)(sinf(sweep) * gr),
                       gy - (int)(cosf(sweep) * gr), Theme::GREEN);
            t.drawCircle(gx, gy, gr, Theme::VAPOR_PINK);
        }
        // Catches waiting behind this card, in the box's top corner: any tap
        // that closes the card brings the next one up.
        if (s_queue && gr > 8) {
            char q[12];
            snprintf(q, sizeof q, "+%u MORE", (unsigned)s_queue);
            t.setTextSize(1);
            const int qw = t.textWidth(q);
            const int qx = gx + gr + 4 - qw - 3, qy = gy - gr - 4 + 3;
            t.fillRect(qx - 2, qy - 1, qw + 4, t.fontHeight() + 2, Theme::BG);
            t.setTextColor(Theme::AMBER, Theme::BG);
            t.setCursor(qx, qy);
            t.print(q);
        }
    }

    // MORE INFO -- opens the same explanation panel LOG's long-press
    // menu does (see uiAlertHitMoreInfo()), so a fresh detection can be
    // looked up without having to remember to go find it in LOG after.
    // "MORE"/"INFO" stacked on two lines (see moreInfoBtnRect()'s
    // comment) rather than Theme::drawButton()'s usual single-line
    // label, so this button doesn't need to go wide to stay legible.
    // IGNORE: stops this exact device raising the alert again. Labelled on
    // two lines like its neighbours so all three buttons match.
    {
        int bx, by, bw, bh;
        ignoreBtnRect(w, h, bx, by, bw, bh);
        const bool already = IgnoreList::contains(s_last.mac);
        // A real system button rather than the vaporwave chrome the rest of
        // the device wears. It is the one control on this screen that is not
        // about the detection -- it changes what the firmware will do from
        // now on -- and looking like it was lifted out of a system dialog is
        // how that reads at a glance.
        //
        // MUTED is the SAME button held down, not a differently coloured
        // one. Win95 had a word for a button that stays in after you let go
        // and this is it, so the state costs no new colour, no legend and
        // nothing to learn: the thing is pushed in, and pushed in means done.
        Theme::drawWin95Button(t, bx, by, bw, bh,
                               already ? "MUTED" : "IGNORE", already);
    }
    // RING: held down while the board is connected to the tracker.
    if (ringShown()) {
        int bx, by, bw, bh;
        ringBtnRect(w, h, bx, by, bw, bh);
        Theme::drawWin95Button(t, bx, by, bw, bh, "RING", s_ringing);
    }

    {
        int bx, by, bw, bh;
        moreInfoBtnRect(w, h, bx, by, bw, bh);
        Theme::hotButton(bx, by, bw, bh, "INFO");   // the Cardputer's I, shared with IGNORE
        t.fillRect(bx, by, bw, bh, Theme::BG);
        t.drawRect(bx, by, bw, bh, Theme::PURPLE);
        t.setTextSize(2);
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setTextWrap(false);
        int lineH = t.fontHeight();
        const int lineGap = 3;
        int ty = by + (bh - (lineH * 2 + lineGap)) / 2;
        int mw = t.textWidth("MORE");
        t.setCursor(bx + (bw - mw) / 2, ty);
        t.print("MORE");
        int iw = t.textWidth("INFO");
        t.setCursor(bx + (bw - iw) / 2, ty + lineH + lineGap);
        t.print("INFO");
    }

    // HUNT -- starts tracking this exact device straight from the alert,
    // the same engine.huntBle()/huntWifi() call LOG's long-press confirm
    // panel makes. Without it, chasing a device you were just warned
    // about meant dismissing the alert, opening LOG, finding the row
    // again and long-pressing it -- by which point a moving target may
    // already be out of range. Drawn in AMBER rather than the CYAN
    // MORE INFO uses: this one changes what the device is doing, the
    // other only opens a text panel.
    {
        int bx, by, bw, bh;
        huntBtnRect(w, h, bx, by, bw, bh);
        Theme::hotButton(bx, by, bw, bh, "HUNT");
        t.fillRect(bx, by, bw, bh, Theme::BG);
        t.drawRect(bx, by, bw, bh, Theme::PURPLE);
        t.setTextSize(2);
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setTextWrap(false);
        int hw = t.textWidth("HUNT");
        t.setCursor(bx + (bw - hw) / 2, by + (bh - t.fontHeight()) / 2);
        t.print("HUNT");
    }
    // SNOOZE between the two: this device, quiet for a while. Each tap
    // steps the time up (main.cpp owns the steps); while one is being
    // picked the button stays pushed in and wears it, and a line along its
    // foot runs out as the pick locks in. (Upstream SquachWatch v1.40.0.)
    {
        int bx, by, bw, bh;
        snoozeBtnRect(w, h, bx, by, bw, bh);
        // A card opened from LOG: WATCH, pushed in while it is watched (the way
        // IGNORE reads MUTED by staying in).
        if (s_byHand)
            Theme::drawButton(t, bx, by, bw, bh, "WATCH", s_watching);
        else
            Theme::drawButton(t, bx, by, bw, bh, s_snzLabel ? s_snzLabel : "SNOOZE",
                              s_snzLabel != nullptr);
        Theme::hotButton(bx, by, bw, bh, "SNOOZE");   // its name, whatever it says
        if (s_snzLabel) {
            const int barW = (int)((int32_t)(bw - 6) * s_snzLeft / 1000);
            t.fillRect(bx + 3, by + bh - 4, bw - 6, 2, Theme::BG);
            if (barW > 0) t.fillRect(bx + 3, by + bh - 4, barW, 2, Theme::AMBER);
            // Nothing else on the card says the button counts taps, so once
            // the first one has landed it says so itself, just above.
#if defined(SQW_SMALL)
            static const char* HINT = "PRESS AGAIN: LONGER";
#else
            static const char* HINT = "TAP AGAIN: LONGER";
#endif
            t.setTextFont(wideAlert(w) ? 2 : 1);
            t.setTextSize(1);
            t.setTextWrap(false);
            const int hw = t.textWidth(HINT);
            const int fh = t.fontHeight();
            const int hx = (w - hw) / 2;
            const int hy = by - fh - (wideAlert(w) ? 1 : 5);
            t.fillRect(hx - 3, hy - 1, hw + 6, fh + 1, Theme::BG);
            t.setTextColor(Theme::AMBER, Theme::BG);
            t.setCursor(hx, hy);
            t.print(HINT);
            t.setTextFont(1);
        }
    }

    // TV-static snow over the whole screen during the same random burst
    // the Bangers headline text above already glitches on -- drawn
    // last so it overlays everything, including the MAC/RSSI readout;
    // briefly obscuring the readout mid-burst reads as "interference"
    // rather than a bug, which fits an alert about surveillance gear.
    Theme::drawGlitchStatic(t, 0, 0, w, h);

    // The RGB frame, after the static so the chrome stays clean: the static
    // is meant to read as interference in the CONTENT, and a frame that
    // breaks up along with it stops reading as a frame at all.
    drawRgbBorder(t, w, h, now);

    // Info panel drawn last, opaquely on top of everything above
    // (including the static) -- same "modal drawn every tick on top of
    // a screen that keeps rendering underneath" pattern LOG's confirm/
    // info panels use.
    if (infoPending) Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
}

bool uiAlertTouched() {
    return s_touched;
}

// Hook called by main when it polls touch during ALERT.
void uiAlertNoteTouch() { s_touched = true; }
