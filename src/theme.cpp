// MuleSkin-CYD — theme implementation
#include "theme.h"
#include "draw_band.h"
#include "fast_sprite.h"
#include "muleskin_art.h"
#include "muleskin_art_row.h"
#include "radar_art.h"
#include "radar_blip.h"
#include "frame_prof.h"
#include "caustic_tile.h"
#include "lil_guy.h"
#include "detection.h"
#include "bangers_font.h"
#include "muleskin.h"
#include "settings.h"
#include "security.h"

namespace Theme {

// Usable bottom of the background band, published by whichever screen
// owns the layout. -1 means nobody said, so backgrounds fall back to
// their own yEnd. (The retired backgrounds stood their scenery on it.)
static int s_bgFloor   = -1;

// How much of a "frame" has elapsed since the last one, for the backgrounds
// that animate by stepping once per call. Computed once per frame at the top
// of drawActiveBackground() and read by every stepper below.
//
// Those steppers were written as `x += 0.06f` per call and nothing tied them
// to the clock, so they ran at whatever the board's frame rate was: ~54 ms a
// frame on cyd-fast, ~65 on the 40 MHz boards, 33 in the emulator -- three
// different speeds for the same animation, and nobody could tell because no
// two were ever side by side. Then the wide-line fix took cyd-fast from 18 to
// 25 fps (2026-09-11) and every one of them visibly sped up, which is how
// it was noticed at all.
//
// ANIM_REF_MS is the cadence they were tuned at -- the cyd-fast frame before
// that fix, which is the speed the owner was used to. A stepper multiplies
// its increment by s_animK, so it moves the same distance per second on any
// board, at any frame rate. Capped so a stall (a message screen, a scan)
// does not deliver ten frames' worth of motion in one jump when CLEAR
// comes back; a frame skipped is a frame skipped.
//
// The ski hill and the fireflies already did this their own way, with a
// `ds = dt / 16` per function; this is the same idea for the rest, at the
// cadence the rest were tuned at rather than retuning every constant.
static const uint32_t ANIM_REF_MS = 50;
static float          s_animK     = 1.0f;
// Where the text starts, as opposed to where the ground is. See
// setBackgroundFloor's comment in theme.h for why these are two values.
static int s_bgTextTop = -1;

// Default-initialized to the original MuleSkinWare vaporwave values —
// applyPalette(0) (VAPRW4VE) reproduces these exactly.
uint16_t BG           = 0x0801;
uint16_t TASKBAR      = 0x0803;
uint16_t PURPLE       = 0xAC1F;
uint16_t CYAN         = 0x07FF;
uint16_t PINK         = 0xF96F;
uint16_t VAPOR_PINK   = 0xFB99;
uint16_t VAPOR_PURPLE = 0xBB5F;
uint16_t VAPOR_BLUE   = 0x067F;
uint16_t VAPOR_YELLOW = 0xFFD2;
uint16_t GREEN        = 0x07E0;
uint16_t AMBER        = 0xFD20;
uint16_t RED          = 0xF800;

// Six presets in the spirit of skizzophrenic/M5PORKCHOP_DualScreen's
// theme table (same leetspeak naming style). BG/TASKBAR stay dark
// across all of them (everything in the UI assumes a dark backdrop
// with light/colored text on top of it) — only the accent hues shift
// per theme. RED is kept literal "red" in most presets since it also
// reads as an alert-severity color, not just decoration.
const Palette kPalettes[PALETTE_COUNT] = {
    { "VAPRW4VE",   0x0801, 0x0803, 0xAC1F, 0x07FF, 0xF96F, 0xFB99, 0xBB5F, 0x067F, 0xFFD2, 0x07E0, 0xFD20, 0xF800 },
    { "CYB3RGR33N", 0x0000, 0x0120, 0x07E0, 0x2FE6, 0x8FE8, 0xAFEA, 0x5FE9, 0x07E8, 0xCFEA, 0x07E0, 0xFFE0, 0xF800 },
    { "AMB3RTERM",  0x0800, 0x1000, 0xFD20, 0xFEA0, 0xFCC0, 0xFDE0, 0xFB80, 0xFC40, 0xFFE0, 0xFEA0, 0xFD20, 0xF800 },
    { "BUBBL3GUM",  0x1002, 0x2004, 0xF81F, 0xFB9D, 0xF96F, 0xFB99, 0xE01F, 0xFA1F, 0xFFF0, 0xFB56, 0xFD20, 0xF800 },
    { "GH0ST",      0x0000, 0x2104, 0xFFFF, 0xF79E, 0xC638, 0xEF7D, 0xB5B6, 0xDEFB, 0xFFFF, 0xFFFF, 0xFFFF, 0xF800 },
    { "BL00D",      0x1000, 0x2000, 0xF800, 0xFB2C, 0xFAEB, 0xF9AB, 0xC0C4, 0xF9CB, 0xFC60, 0xF800, 0xFD20, 0xF800 },
};

void applyPalette(uint8_t idx) {
    if (idx >= PALETTE_COUNT) idx = 0;
    const Palette& p = kPalettes[idx];
    BG = p.bg; TASKBAR = p.taskbar; PURPLE = p.purple; CYAN = p.cyan;
    PINK = p.pink; VAPOR_PINK = p.vaporPink; VAPOR_PURPLE = p.vaporPurple;
    VAPOR_BLUE = p.vaporBlue; VAPOR_YELLOW = p.vaporYellow; GREEN = p.green;
    AMBER = p.amber; RED = p.red;
}

Palette dimPaletteForOverlay(uint16_t t) {
    Palette saved = { "", BG, TASKBAR, PURPLE, CYAN, PINK, VAPOR_PINK,
                      VAPOR_PURPLE, VAPOR_BLUE, VAPOR_YELLOW, GREEN, AMBER, RED };
    PURPLE       = blend(PURPLE, BG, t);
    CYAN         = blend(CYAN, BG, t);
    PINK         = blend(PINK, BG, t);
    VAPOR_PINK   = blend(VAPOR_PINK, BG, t);
    VAPOR_PURPLE = blend(VAPOR_PURPLE, BG, t);
    VAPOR_BLUE   = blend(VAPOR_BLUE, BG, t);
    VAPOR_YELLOW = blend(VAPOR_YELLOW, BG, t);
    GREEN        = blend(GREEN, BG, t);
    AMBER        = blend(AMBER, BG, t);
    RED          = blend(RED, BG, t);
    return saved;
}

void restorePalette(const Palette& saved) {
    BG = saved.bg; TASKBAR = saved.taskbar; PURPLE = saved.purple; CYAN = saved.cyan;
    PINK = saved.pink; VAPOR_PINK = saved.vaporPink; VAPOR_PURPLE = saved.vaporPurple;
    VAPOR_BLUE = saved.vaporBlue; VAPOR_YELLOW = saved.vaporYellow; GREEN = saved.green;
    AMBER = saved.amber; RED = saved.red;
}

uint16_t colorFor(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::META:
            return PINK;
        case DetectionType::SKIMMER:
            return VAPOR_YELLOW;
        case DetectionType::RAVEN:
        case DetectionType::ALPR:
            return AMBER;
        case DetectionType::AIRTAG:
        case DetectionType::DRONE:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG:
        case DetectionType::TILE:
        // Filed with the trackers rather than left on the default. It is not
        // following YOU the way a tag in your coat is, but it exists to know
        // when you walk past, which is the same colour of problem.
        case DetectionType::IBEACON:
            return VAPOR_PURPLE;
        case DetectionType::CAMERA:
        case DetectionType::RING:
            return CYAN;
        case DetectionType::DEAUTH:
        case DetectionType::EVILTWIN:
        // Filed with the two attack detections rather than with the
        // cameras. Everything else on this screen is equipment that
        // watches; this is equipment that reaches out and does something
        // to a radio, which is the same colour of problem as a deauth
        // flood or a rogue AP -- and often literally the box producing one.
        case DetectionType::HACKER:
            return RED;
        default:
            return GREEN;
    }
}

uint16_t labelOn(uint16_t fill) {
    const int r = (fill >> 11) & 0x1F, g = (fill >> 5) & 0x3F, b = fill & 0x1F;
    // Perceived brightness, 0..~255, with the 5/6/5 channels scaled to 8 bits.
    const int luma = (r * 8 * 54 + g * 4 * 183 + b * 8 * 19) >> 8;
    return luma > 120 ? BLACK : WHITE;
}

uint16_t blend(uint16_t a, uint16_t b, uint16_t t) {
    // 8.8 fixed-point t, 0..256
    uint8_t ar = (a >> 8) & 0xF8;
    uint8_t ag = (a >> 3) & 0xFC;
    uint8_t ab = (a << 3) & 0xF8;
    uint8_t br = (b >> 8) & 0xF8;
    uint8_t bg = (b >> 3) & 0xFC;
    uint8_t bb = (b << 3) & 0xF8;
    uint8_t rr = (uint8_t)(((uint16_t)ar * (256 - t) + (uint16_t)br * t) >> 8) & 0xF8;
    uint8_t rg = (uint8_t)(((uint16_t)ag * (256 - t) + (uint16_t)bg * t) >> 8) & 0xFC;
    uint8_t rb = (uint8_t)(((uint16_t)ab * (256 - t) + (uint16_t)bb * t) >> 8) & 0xF8;
    return (uint16_t)((rr << 8) | (rg << 3) | (rb >> 3));
}

uint16_t titlebarColor(int x, int w) {
    if (w <= 1) return CYAN;
    // Clean two-stop cyan -> magenta fade across the full bar.
    return blend(CYAN, VAPOR_PINK, (uint16_t)(((uint32_t)x * 256) / w));
}

// Rotate button drawn in the top-right corner of the title bar: a
// circular arrow (a ~300 degree ring, cyan into magenta, with an
// arrowhead at the open end) instead of the previous "flip" glyph
// (vertical divider + two triangles) — reads as an actual rotate/
// refresh icon at a glance instead of an abstract shape. The tap
// target (ROTATE_HIT_*) is bigger than the visual icon and extends
// below the title bar into the content area — a finger needs a much
// bigger target than a stylus would.
static const int ROTATE_ICON_W = Theme::TITLE_ICON_W;
// A quarter bigger than they were, and the ICON grew as well as the target.
// The targets were already 44x40, far larger than the 22px glyph inside them,
// so what made these awkward to hit was never the hit box -- it was that they
// looked tiny and people aimed at the drawing rather than at the button.
static const int ROTATE_HIT_W  = 55;
static const int ROTATE_HIT_H  = 50;

static void drawRotateIcon(TFT_eSPI& t, int w, int barH) {
    int x0 = w - ROTATE_ICON_W;
    t.fillRect(x0, 0, ROTATE_ICON_W, barH, BG);
    int cx = x0 + ROTATE_ICON_W / 2;
    int cy = barH / 2;
    int r  = 6;
    // Ring sweeps clockwise from 30 to 330 degrees (drawArc's 0 is 12
    // o'clock), leaving a 60 degree gap centered at the top for the
    // arrowhead to sit in.
    t.drawArc(cx, cy, r, r - 2, 30, 180, CYAN, BG, true);
    t.drawArc(cx, cy, r, r - 2, 180, 330, VAPOR_PINK, BG, true);
    // Arrowhead at the ring's clockwise end (330 degrees), pointing
    // further clockwise (i.e. back up towards the gap) to read as
    // motion, not just a stray triangle.
    t.fillTriangle(cx + 1, cy - r,
                    cx - 4, cy - 3,
                    cx - 2, cy - 1,
                    VAPOR_PINK);
}

bool rotateButtonHit(int x, int y, int w) {
    return x >= w - ROTATE_HIT_W && x < w && y >= 0 && y < ROTATE_HIT_H;
}

// Settings button, mirrored into the top-left corner of the title bar:
// a 3-bar "hamburger" glyph, same oversized tap target treatment as the
// rotate icon on the other side.
static const int SETTINGS_ICON_W = Theme::TITLE_ICON_W;
static const int SETTINGS_HIT_W  = 55;
static const int SETTINGS_HIT_H  = 50;
// The two icons float over live background now that the bar behind them is
// gone, so each keeps a small opaque box of its own -- without it a thin
// cyan glyph disappears against the synthwave sun.
static const int ICON_BOX_H      = Theme::TITLE_ICON_BAND_H;

static void drawSettingsIcon(TFT_eSPI& t, int barH) {
    t.fillRect(0, 0, SETTINGS_ICON_W, barH, BG);
    int cx = SETTINGS_ICON_W / 2;
    int y0 = barH / 2 - 5;
    t.drawFastHLine(cx - 9, y0,      18, CYAN);
    t.drawFastHLine(cx - 9, y0 + 5,  18, VAPOR_PINK);
    t.drawFastHLine(cx - 9, y0 + 10, 18, CYAN);
}

void drawBevel(TFT_eSPI& t, int x, int y, int w, int h, uint16_t face,
               uint16_t lit, uint16_t litSoft, uint16_t shd, uint16_t shdSoft,
               bool sunk) {
    t.fillRect(x + 2, y + 2, w - 4, h - 4, face);
    const uint16_t oTL = sunk ? shd : lit,         oBR = sunk ? lit : shd;
    const uint16_t iTL = sunk ? shdSoft : litSoft, iBR = sunk ? litSoft : shdSoft;
    t.drawFastHLine(x, y, w, oTL);         t.drawFastVLine(x, y, h, oTL);
    t.drawFastHLine(x, y + h - 1, w, oBR); t.drawFastVLine(x + w - 1, y, h, oBR);
    t.drawFastHLine(x + 1, y + 1, w - 2, iTL);            t.drawFastVLine(x + 1, y + 1, h - 2, iTL);
    t.drawFastHLine(x + 1, y + h - 2, w - 2, iBR);        t.drawFastVLine(x + w - 2, y + 1, h - 2, iBR);
}

void drawSteelPanel(TFT_eSPI& t, int x, int y, int w, int h, bool sunk) {
    drawBevel(t, x, y, w, h, W95_FACE, W95_HILITE, W95_LIGHT, W95_SHADOW, W95_DKSHADOW, sunk);
}

// The key is the same raised edge over a coloured face, so it is drawn by
// the same code -- it used to be a second copy of those eight lines, and
// the two had already drifted by one shade.
void drawSteelKey(TFT_eSPI& t, int x, int y, int w, int h, bool lit) {
    drawBevel(t, x, y, w, h, lit ? PURPLE : TASKBAR,
              W95_LIGHT, W95_FACE, W95_DKSHADOW, W95_SHADOW, false);
}

bool settingsButtonHit(int x, int y) {
    return x >= 0 && x < SETTINGS_HIT_W && y >= 0 && y < SETTINGS_HIT_H;
}

static bool s_rotateIconVisible = true;
// Whether the icon is currently ON the glass, so hiding it can erase what
// it left behind once rather than every frame. See drawTitleBar().
static bool s_rotateIconDrawn = false;

void setRotateIconVisible(bool visible) {
    s_rotateIconVisible = visible;
}

// The padlock. Shown only while a PIN is set (Security::enabled()), so its hit
// box does not exist otherwise. It shares the rotate icon's box when rotation
// is hidden, else sits one icon-width to its left.
static const int LOCK_ICON_W = 26;
static const int LOCK_HIT_W   = 44;

// Same condition drawTitleBar uses to decide whether the rotate icon is up.
static bool rotateShown() {
    return s_rotateIconVisible && !Settings::rotationLocked();
}
// Left edge of the padlock's own icon box.
static int lockIconX(int w) {
    return rotateShown() ? (w - ROTATE_ICON_W - LOCK_ICON_W) : (w - LOCK_ICON_W);
}

int titleBarRightIconsX(int w) {
    if (Security::enabled()) return lockIconX(w);
    if (rotateShown()) return w - ROTATE_ICON_W;
    return w;
}

static void drawLockIcon(TFT_eSPI& t, int w, int barH) {
    const int x0 = lockIconX(w);
    t.fillRect(x0, 0, LOCK_ICON_W, barH, BG);
    const int cx = x0 + LOCK_ICON_W / 2;
    const int cy = barH / 2;
    // A little shackle over a body.
    t.drawFastHLine(cx - 3, cy - 4, 6, AMBER);
    t.drawFastVLine(cx - 3, cy - 4, 3, AMBER);
    t.drawFastVLine(cx + 2, cy - 4, 3, AMBER);
    t.fillRect(cx - 5, cy - 1, 10, 7, AMBER);
    t.drawPixel(cx, cy + 2, BG);            // keyhole
}

bool lockButtonHit(int x, int y, int w) {
    if (!Security::enabled()) return false;
    const int x0 = lockIconX(w);
    return x >= x0 - (LOCK_HIT_W - LOCK_ICON_W) && x < x0 + LOCK_ICON_W &&
           y >= 0 && y < ROTATE_HIT_H;
}


// The bar is gone; the two buttons that lived on it are not.
//
// It used to paint a full-width gradient, a rule, and a centred title across
// the top sixteen rows of every screen. What it actually CARRIED was the only
// route into Settings, the only route back out of four screens, and the
// rotation control -- so those stay, as floating corner buttons, and the
// decoration goes.
//
// The `title` argument is deliberately kept and deliberately ignored. Twelve
// screens call this, each passing its own name; keeping the signature means
// none of them changed, and putting a title back later is a one-line edit
// here rather than twelve.
//
// Note what did NOT change: every screen still starts its body at y=16 and
// still tells MuleSkin his band begins there. He sizes himself from the space
// he is given -- scale = charAvail / BASE_HEIGHT -- so handing him the
// sixteen freed rows would have made him a tenth bigger and moved everything
// hanging off him. The rows are freed on screen without being offered to the
// layout, which is the whole trick.
// TFT_eSPI's font 2: the built-in 16-row proportional face. A dozen fonts
// with more personality were tried in its place (tools/ttf2gfx.py converts
// any TrueType face; the emulator's shim renders the result) and every one
// of them looked converted rather than native at this size. The plain one
// reads best. A build can still try another with -DBUBBLE_FONT=3
// -DBUBBLE_GFX_FONT=<name> and that face's header on the include path.
#ifndef BUBBLE_FONT
#define BUBBLE_FONT 2
#endif
#if BUBBLE_FONT == 2
void bubbleFontOn(TFT_eSPI& t)  { t.setTextFont(2); }
void bubbleFontOff(TFT_eSPI& t) { t.setTextFont(1); }
int  bubbleTextH()  { return 16; }
int  bubbleAscent() { return 0; }
#else
static int s_bubAb = -1, s_bubBb = 0;
static void bubbleMeasure() {
    if (s_bubAb >= 0) return;
    s_bubAb = 0; s_bubBb = 0;
    const GFXfont& f = BUBBLE_GFX_FONT;
    for (int c = 0; c <= (int)(f.last - f.first); c++) {
        const int ab = -f.glyph[c].yOffset;
        const int bb = f.glyph[c].height - ab;
        if (ab > s_bubAb) s_bubAb = ab;
        if (bb > s_bubBb) s_bubBb = bb;
    }
}
void bubbleFontOn(TFT_eSPI& t)  { t.setFreeFont(&BUBBLE_GFX_FONT); }
void bubbleFontOff(TFT_eSPI& t) { t.setTextFont(1); }
int  bubbleTextH()  { bubbleMeasure(); return s_bubAb + s_bubBb; }
int  bubbleAscent() { bubbleMeasure(); return s_bubAb; }
#endif

void drawListHeading(TFT_eSPI& t, const char* text, uint16_t color) {
    t.setTextSize(1);
    t.setTextColor(color, BG);
    t.setCursor(8, LIST_TOP + (LIST_HEADING_H - t.fontHeight()) / 2);
    t.print(text);
}

void drawListRowPanel(TFT_eSPI& t, int w, int y, int hgt) {
    // Four rows of backdrop between tiles, not two, so they read as tiles
    // on the scene rather than as a ruled list. Inset a row from the top
    // as well so the text, centred on the row, stays centred on the tile.
    const int x0 = 3, ww = w - 10, hh = hgt - 4;
    if (ww <= 0 || hh <= 0) return;
    t.fillRect(x0, y + 1, ww, hh, BG);
    t.drawRect(x0, y + 1, ww, hh, PURPLE);
}

int pinnedBackH(int panelW) { return panelW >= 400 ? 36 : PINNED_BACK_H; }

void drawPinnedBack(TFT_eSPI& t, const char* label) {
    const int w = t.width(), h = pinnedBackH(w), y = t.height() - h;
    t.fillRect(0, y, w, h, BG);
    t.drawFastHLine(0, y, w, PURPLE);
    // The font, not just the size. textfont is sticky state on the sprite and
    // this helper inherits whatever drew last -- and FONT2 renders through
    // setWindow(), which does not apply the viewport datum, so on the 3.5"'s
    // second band it lands outside the buffer and draws nothing at all. A
    // strip with no label on it. Shared helpers say what they want.
    t.setTextFont(1);
    t.setTextSize(uiMenuTextSize(t));
    t.setTextColor(CYAN, BG);
    t.setCursor((w - t.textWidth(label)) / 2, y + (h - t.fontHeight()) / 2);
    t.print(label);
}

bool pinnedBackHit(int x, int y, int screenW, int screenH) {
    return x >= 0 && x < screenW && y >= screenH - pinnedBackH(screenW) && y < screenH;
}

void drawTitleBar(TFT_eSPI& t, const char* title) {
    (void)title;
    int w = t.width();
    drawSettingsIcon(t, ICON_BOX_H);
    // Gone when rotation is locked, not just on AWOK. It used to keep
    // drawing while locked, on the reasoning that leaving the control
    // visible shows the switch exists -- but the tap handler has always
    // been gated on the same flag, so what it actually showed was a button
    // that does nothing. A visible inert control reads as a bug, not as a
    // setting; the switch is in SETTINGS > ROTATION LOCK, which is where it
    // was turned on in the first place.
    const bool show = s_rotateIconVisible && !Settings::rotationLocked();
    if (show) {
        drawRotateIcon(t, w, ICON_BOX_H);
        s_rotateIconDrawn = true;
    } else if (s_rotateIconDrawn) {
        // Clear exactly once, on the transition. Doing it unconditionally
        // would stamp a BG rectangle into the top-right corner on AWOK,
        // where the icon has never been drawn and the background currently
        // shows through -- a regression on the one board that already had
        // this right.
        t.fillRect(w - ROTATE_ICON_W, 0, ROTATE_ICON_W, ICON_BOX_H, BG);
        s_rotateIconDrawn = false;
    }
    // The padlock, only while a PIN is set. Every screen that draws this bar
    // repaints its whole top band from the background first, so a lock that
    // was there last frame and is not now leaves nothing behind -- no erase
    // needed, unlike the rotate icon above, which predates that repaint.
    if (Security::enabled()) drawLockIcon(t, w, ICON_BOX_H);
}

uint8_t uiTextSize(TFT_eSPI& t, uint8_t base) {
    return (base == 1 && t.width() >= 400) ? 2 : base;
}

uint8_t uiMenuTextSize(TFT_eSPI& t) {
    return t.width() >= 400 ? 3 : 2;
}

void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                const char* label, bool pressed, uint8_t textSize) {
    uint16_t fill = pressed ? PURPLE : BG;
    uint16_t fg   = pressed ? labelOn(PURPLE) : CYAN;
    t.fillRect(x, y, w, h, fill);
    t.drawRect(x, y, w, h, PURPLE);
    // Button labels are short and their boxes grew with the panel, so this is
    // the safest place for the step-up. But not every button is a third of the
    // screen: the phone screen's QWERTY and BACK sit in boxes sized for size-1
    // text, and the bigger label ran straight out of them with its brackets
    // cut off. So the step-up has to earn its place -- measure it, and keep
    // the smaller size if it does not fit. That makes this self-limiting for
    // every button on every screen instead of a list of exceptions.
    uint8_t ts = uiTextSize(t, textSize);
    if (ts != textSize) {
        t.setTextSize(ts);
        if (t.textWidth(label) > w - 6) ts = textSize;
    }
    t.setTextSize(ts);
    t.setTextColor(fg, fill);
    int tw = t.textWidth(label);
    int th = t.fontHeight();
    t.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    t.print(label);
}

void drawWin95Button(TFT_eSPI& t, int x, int y, int w, int h,
                     const char* label, bool sunken) {
    // Face first, inset by the two bevel rings so the edges below draw over
    // nothing they need to keep.
    t.fillRect(x + 2, y + 2, w - 4, h - 4, W95_FACE);

    // Raised: outer ring lit from the top-left, shaded at the bottom-right,
    // and the inner ring the same way one step softer. Sunken swaps which
    // side is lit -- that swap IS the press, so the two branches are the
    // same four calls with the colour pairs exchanged rather than a
    // separate drawing routine that could drift out of step with this one.
    const uint16_t outerTL = sunken ? W95_DKSHADOW : W95_HILITE;
    const uint16_t outerBR = sunken ? W95_HILITE   : W95_DKSHADOW;
    const uint16_t innerTL = sunken ? W95_SHADOW   : W95_LIGHT;
    const uint16_t innerBR = sunken ? W95_LIGHT    : W95_SHADOW;

    t.drawFastHLine(x, y, w, outerTL);
    t.drawFastVLine(x, y, h, outerTL);
    t.drawFastHLine(x, y + h - 1, w, outerBR);
    t.drawFastVLine(x + w - 1, y, h, outerBR);

    t.drawFastHLine(x + 1, y + 1, w - 2, innerTL);
    t.drawFastVLine(x + 1, y + 1, h - 2, innerTL);
    t.drawFastHLine(x + 1, y + h - 2, w - 2, innerBR);
    t.drawFastVLine(x + w - 2, y + 1, h - 2, innerBR);

    if (!label || !*label) return;

    // Black on silver, no exceptions: a coloured label on a system button is
    // the tell that it is a costume. Centred on the FACE rather than on the
    // whole rect, so the bevel does not pull the text off-centre, then the
    // one-pixel press offset on top.
    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(BLACK, W95_FACE);
    const int tw = t.textWidth(label);
    const int th = t.fontHeight();
    const int ox = sunken ? 1 : 0;
    t.setCursor(x + 2 + (w - 4 - tw) / 2 + ox,
                y + 2 + (h - 4 - th) / 2 + ox);
    t.print(label);
}

ButtonBarGeom computeButtonBar(int screenW, int screenH) {
    ButtonBarGeom g;
    // Half of the original 40px (which was sized to comfortably clear
    // ~9mm finger-touch-target guidance) — explicitly requested smaller
    // to free up more room above for content. Still tappable, just a
    // tighter target than the original guidance-driven size.
    g.h = 20;
    const int margin = 8, gap = 8;
    g.y = screenH - g.h - 6;
    int bw = (screenW - 2 * margin - 2 * gap) / 3;
    g.w[0] = g.w[1] = g.w[2] = bw;
    g.x[0] = margin;
    g.x[1] = g.x[0] + bw + gap;
    g.x[2] = g.x[1] + bw + gap;
    return g;
}

void drawButtonBar(TFT_eSPI& t, ButtonId highlighted, ButtonBarMode mode) {
    ButtonBarGeom g = computeButtonBar(t.width(), t.height());
    drawButton(t, g.x[1], g.y, g.w[1], g.h, "< LOG >",  highlighted == ButtonId::LOG);
    if (mode == ButtonBarMode::MAIN) {
        // Left of LOG: join a saved WiFi network and set the clock.
        t.setTextSize(1);
        const char* wt = (t.textWidth("< WIFI TIME >") + 6 <= g.w[0]) ? "< WIFI TIME >" : "WIFI TIME";
        drawButton(t, g.x[0], g.y, g.w[0], g.h, wt, highlighted == ButtonId::SCAN);
    }
    if (mode == ButtonBarMode::LOG) {
        drawButton(t, g.x[2], g.y, g.w[2], g.h, "[ CLR ]", highlighted == ButtonId::CLR);
    } else {
        // The bracketed label is 84 px; the 240-wide rotation's slots are
        // 69, so there it goes without the < >.
        t.setTextSize(1);
        const char* m = (t.textWidth("< IN MEETING >") + 6 <= g.w[2]) ? "< IN MEETING >" : "IN MEETING";
        drawButton(t, g.x[2], g.y, g.w[2], g.h, m, highlighted == ButtonId::CLR);
    }
}

ButtonId hitTestButtonBar(int x, int y, int screenW, int screenH) {
    ButtonBarGeom g = computeButtonBar(screenW, screenH);
    if (y < g.y || y > g.y + g.h) return ButtonId::NONE;
    if (x >= g.x[0] && x <= g.x[0] + g.w[0]) return ButtonId::SCAN;
    if (x >= g.x[1] && x <= g.x[1] + g.w[1]) return ButtonId::LOG;
    if (x >= g.x[2] && x <= g.x[2] + g.w[2]) return ButtonId::CLR;
    return ButtonId::NONE;
}

void drawScanline(TFT_eSPI& t, int y, uint16_t color) {
    t.drawFastHLine(0, y, t.width(), color);
}

void drawScrollbar(TFT_eSPI& t, int x, int y, int h,
                   int totalItems, int visibleItems, int scrollOffset) {
    if (totalItems <= visibleItems || visibleItems <= 0) return;
    t.drawFastVLine(x, y, h, PURPLE);
    int thumbH = h * visibleItems / totalItems;
    if (thumbH < 6) thumbH = 6;
    int maxScroll = totalItems - visibleItems;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (scrollOffset < 0) scrollOffset = 0;
    int travel = h - thumbH;
    int thumbY = y + (maxScroll > 0 ? (travel * scrollOffset / maxScroll) : 0);
    t.fillRect(x - 1, thumbY, 3, thumbH, CYAN);
}

// The per-type artwork. Positionable, because the ALERT screen now draws it
// inside a gauge rather than at a fixed anchor near the bottom of the panel.
//
// One rule runs through all of it, and it is the one the redesign paid for:
// nothing is drawn in a value close to the ground. BG is (10,0,15), so
// anything below roughly (70,70,80) disappears into it -- which is how a set
// of sunglasses, a raven and a camera dome all came out as holes the first
// time. A black object at forty pixels is a MID tone with dark accents and a
// lit edge, the same way film lights a black cat.
//
// The two helpers below are what make the five cameras read as a family of
// related products rather than five unrelated drawings, while staying
// individually tellable -- which the old art was not: one camera glyph
// served FLOCK, AXON, ALPR, CAMERA and RING, and one pebble served the three
// trackers.
static uint16_t SHELL, SHELL_HI, SHELL_LO, ICO_INK, ICO_GLASS, ICO_LENS, ICO_METAL,
                ICO_METAL2, ICO_WARN, ICO_LED, ICO_APPLE, ICO_APPLE_HI,
                ICO_APPLE_LO, ICO_STEM, ICO_LEAF, ICO_SHEEN, ICO_PLASTIC;
static bool s_icoPalReady = false;

static void icoPalette(TFT_eSPI& t) {
    if (s_icoPalReady) return;
    SHELL       = t.color565(104,110,132); SHELL_HI    = t.color565(158,166,192);
    SHELL_LO    = t.color565(58,62,80);    ICO_INK     = t.color565(26,26,38);
    ICO_GLASS   = t.color565(36,104,132); ICO_LENS    = t.color565(0,210,220);
    ICO_METAL   = t.color565(150,150,160); ICO_METAL2  = t.color565(214,214,224);
    ICO_WARN    = t.color565(255,60,40);   ICO_LED     = t.color565(255,220,60);
    ICO_APPLE   = t.color565(226,44,40);   ICO_APPLE_HI= t.color565(255,124,98);
    ICO_APPLE_LO= t.color565(148,20,24);   ICO_STEM    = t.color565(126,84,42);
    ICO_LEAF    = t.color565(60,192,80);   ICO_SHEEN   = 0xFFFF;
    ICO_PLASTIC = t.color565(232,228,214);
    s_icoPalReady = true;
}

// A housing: mid shell, lit top edge, dark underside. Used by everything
// that is a box, so they all catch the light from the same direction.
static void housing(TFT_eSPI& t,int x,int y,int w,int h){
    t.fillRect(x,y,w,h,SHELL);
    t.fillRect(x,y,w,(h/6)?h/6:1,SHELL_HI);
    t.fillRect(x,y+h-((h/8)?h/8:1),w,(h/8)?h/8:1,SHELL_LO);
}
// A lens: dark socket, glass, catchlight. Every camera in the set uses it,
// which is what makes them a family rather than five unrelated drawings.
static void lens(TFT_eSPI& t,int cx,int cy,int r){
    t.fillCircle(cx,cy,r,ICO_INK);
    t.fillCircle(cx,cy,(r*2)/3,ICO_GLASS);
    t.fillCircle(cx-r/3,cy-r/3,(r/4)?r/4:1,ICO_SHEEN);
}

void drawTypeIcon(TFT_eSPI& t, DetectionType type, int cx, int cy, int s) {
    icoPalette(t);
    const DetectionType tt = type;

    switch (tt) {
    case DetectionType::FLOCK:
        t.fillRect(cx-3, cy-s/4, 6, s*5/4, SHELL_LO);                 // pole
        t.fillRect(cx-3, cy-s/4, 2, s*5/4, SHELL);
        housing(t, cx-s, cy-s*3/4, s*2, s);
        lens(t, cx-s/2, cy-s/4, s/3);
        for(int i=0;i<3;i++) t.fillCircle(cx+s/4+i*s/4, cy-s/4, s/10, ICO_WARN);
        t.fillRect(cx-s-2, cy-s*3/4-s/3, s*2+4, s/4, ICO_METAL);          // solar
        t.fillRect(cx-s-2, cy-s*3/4-s/3, s*2+4, s/12, ICO_METAL2);
        break;
    case DetectionType::AXON:
        housing(t, cx-s*2/3, cy-s*3/4, s*4/3, s*3/2);
        lens(t, cx, cy-s/4, s/2);
        t.fillRect(cx-s/3, cy+s/3, s*2/3, s/6, ICO_INK);                  // speaker
        t.fillCircle(cx+s/3, cy+s*2/3, s/8, ICO_WARN);                    // REC
        t.fillRect(cx-s/2, cy-s*3/4-s/4, s, s/4, ICO_METAL);              // clip
        t.fillRect(cx-s/2, cy-s*3/4-s/4, s, s/12, ICO_METAL2);
        break;
    case DetectionType::META:
        t.fillRect(cx-s*3/2, cy-s/2, s*3, s/4, SHELL_HI);             // brow
        t.fillRect(cx-s*3/2, cy-s/2, s*3, s/12, ICO_SHEEN);
        t.fillRect(cx-s*3/2, cy-s/4, s*5/4, s*5/9, SHELL);
        t.fillRect(cx+s/4,   cy-s/4, s*5/4, s*5/9, SHELL);
        t.fillRect(cx-s*3/2+2, cy-s/4+2, s*5/4-4, s*5/9-4, ICO_INK);
        t.fillRect(cx+s/4+2,   cy-s/4+2, s*5/4-4, s*5/9-4, ICO_INK);
        t.drawLine(cx-s*5/4, cy+s/6, cx-s*3/4, cy-s/8, SHELL_HI);
        t.drawLine(cx-s,     cy+s/6, cx-s*2/3, cy,     SHELL_HI);
        t.drawLine(cx+s/2,   cy+s/6, cx+s,     cy-s/8, SHELL_HI);
        t.drawLine(cx+s*3/4, cy+s/6, cx+s*13/12, cy,   SHELL_HI);
        t.fillRect(cx-s/4, cy-s/4, s/2, s/5, SHELL_HI);               // bridge
        t.fillRect(cx-s*7/4, cy-s/2, s/3, s/5, SHELL);                // temples
        t.fillRect(cx+s*3/2-2, cy-s/2, s/3, s/5, SHELL);
        t.fillCircle(cx-s*3/2+s/5, cy, s/7, ICO_WARN);
        t.fillCircle(cx-s*3/2+s/5, cy, s/14, ICO_LED);
        break;
    case DetectionType::SKIMMER:
        t.fillRect(cx-s/2, cy-s*5/4, s*3/2, s*2/3, ICO_LED);              // card
        t.fillRect(cx-s/2, cy-s*5/4, s*3/2, s/8, ICO_SHEEN);
        t.fillRect(cx-s/2, cy-s*5/4+s/3, s*3/2, s/6, ICO_INK);            // magstripe
        housing(t, cx-s, cy-s/2, s*2, s);
        t.fillRect(cx-s+4, cy-s/4, s*2-8, s/4, ICO_INK);                  // the slot
        break;
    case DetectionType::RAVEN: {
        // Heavier head, shorter bill, hunched. The wading bird pass 6 drew
        // came from a long neck and a small head -- a raven is mostly head
        // and shoulders with the bill buried in the profile, not held out.
        t.fillTriangle(cx+s/2, cy+s/4, cx+s*7/5, cy+s, cx+s/3, cy+s*4/5, SHELL_LO);
        t.fillEllipse(cx+s/6, cy+s/4, s*7/10, s*3/5, SHELL);          // body
        t.fillEllipse(cx+s/6, cy+s/8, s*7/10, s*2/5, SHELL_HI);       // lit back
        t.fillEllipse(cx+s/4, cy+s/3, s*2/5, s*2/5, SHELL_LO);        // wing
        t.fillCircle(cx-s/2, cy-s/3, s/2, SHELL);                     // big head
        t.fillCircle(cx-s/2, cy-s/2, s/3, SHELL_HI);                  // lit crown
        t.fillTriangle(cx-s*9/10, cy-s*2/5, cx-s*8/5, cy-s/5,
                       cx-s*9/10, cy,       SHELL_LO);                // short bill
        t.fillTriangle(cx-s*9/10, cy-s*2/5, cx-s*8/5, cy-s/5,
                       cx-s*9/10, cy-s/5,   SHELL);                   // lit edge
        t.fillTriangle(cx-s/2, cy, cx+s/8, cy+s/3, cx-s*3/5, cy+s/3, SHELL_LO); // hackle
        t.fillCircle(cx-s*3/5, cy-s*2/5, s/8, ICO_SHEEN);
        t.fillCircle(cx-s*3/5, cy-s*2/5, s/16, ICO_INK);
        t.fillRect(cx,      cy+s*3/4, 3, s/3, SHELL_LO);
        t.fillRect(cx+s/3,  cy+s*3/4, 3, s/3, SHELL_LO);
        break;
    }
    case DetectionType::AIRTAG:
        t.fillCircle(cx-s/3, cy+s/6, s, ICO_APPLE);
        t.fillCircle(cx+s/3, cy+s/6, s, ICO_APPLE);
        t.fillRect(cx-s/3, cy-s*2/3, s*2/3, s, ICO_APPLE);
        t.fillCircle(cx+s/2, cy+s/2, s/2, ICO_APPLE_LO);
        t.fillCircle(cx-s/2, cy-s/6, s/3, ICO_APPLE_HI);
        t.fillCircle(cx-s*7/12, cy-s/4, s/8, ICO_SHEEN);
        t.fillCircle(cx+s*11/12, cy-s/3, s/2, BG);             // bite
        t.fillCircle(cx+s/2,  cy-s*5/6, s/6, BG);
        t.fillCircle(cx+s*7/6, cy+s/12, s/6, BG);
        t.fillRect(cx-2, cy-s-s/3, 4, s/2, ICO_STEM);
        t.fillTriangle(cx+2, cy-s-s/6, cx+s, cy-s-s/2, cx+s/2, cy-s+2, ICO_LEAF);
        break;
    case DetectionType::DRONE:
        for(int k=0;k<4;k++){
            const int dx=(k&1)?s:-s, dy=(k&2)?s:-s;
            t.drawLine(cx,cy,cx+dx,cy+dy,SHELL_LO);
            t.fillEllipse(cx+dx,cy+dy,s/2,s/6,ICO_METAL2);
            t.fillCircle(cx+dx,cy+dy,s/8,SHELL);
        }
        t.fillEllipse(cx,cy,s*2/3,s/2,SHELL);
        t.fillEllipse(cx-s/5,cy-s/6,s/4,s/6,SHELL_HI);
        lens(t,cx,cy+s/3,s/4);
        break;
    case DetectionType::ALPR:
        housing(t, cx-s, cy-s, s*2, s*3/4);
        lens(t, cx-s/2, cy-s*5/8, s/4);
        for(int i=0;i<3;i++) t.fillCircle(cx+s/4+i*s/4, cy-s*5/8, s/12, ICO_WARN);
        t.fillRect(cx-s, cy+s/6, s*2, s*3/4, ICO_PLASTIC);                // the plate
        t.fillRect(cx-s, cy+s/6, s*2, s/12, ICO_SHEEN);
        t.drawRect(cx-s, cy+s/6, s*2, s*3/4, ICO_INK);
        for(int i=0;i<5;i++) t.fillRect(cx-s+5+i*(s*2-10)/5, cy+s/3, 3, s*2/5, ICO_INK);
        break;
    case DetectionType::CAMERA: {
        const int py=cy-s*2/3;
        t.fillCircle(cx,cy,s,SHELL);
        t.fillRect(cx-s-1,cy-s-1,s*2+2,(cy-py),BG);            // top half off
        t.fillCircle(cx-s/2,cy+s/6,s/3,SHELL_HI);                     // glass sheen
        lens(t,cx+s/5,cy+s/12,s*2/5);
        t.fillRect(cx-s*5/4,py,s*5/2,s/4,ICO_METAL);                      // ceiling plate
        t.fillRect(cx-s*5/4,py,s*5/2,s/12,ICO_METAL2);
        break;
    }
    case DetectionType::SAMSUNG_TAG:
        t.fillEllipse(cx,cy,s*3/4,s,ICO_PLASTIC);
        t.fillEllipse(cx-s/4,cy-s/3,s/4,s/3,ICO_SHEEN);
        t.fillCircle(cx,cy-s*2/3,s/5,BG);                      // keyring hole
        t.fillRect(cx-s/3,cy+s/6,s*2/3,s/4,ICO_GLASS);
        break;
    case DetectionType::GOOGLE_TAG:
        t.fillCircle(cx,cy-s/4,s*3/4,ICO_LEAF);
        t.fillTriangle(cx-s*5/8,cy+s/8,cx+s*5/8,cy+s/8,cx,cy+s,ICO_LEAF);
        t.fillCircle(cx-s/4,cy-s/2,s/5,tt == DetectionType::GOOGLE_TAG?ICO_SHEEN:ICO_LEAF);
        t.fillCircle(cx,cy-s/4,s/3,BG);
        break;
    case DetectionType::TILE:
        t.fillRect(cx-s*3/4,cy-s*3/4,s*3/2,s*3/2,ICO_PLASTIC);
        t.fillRect(cx-s*3/4,cy-s*3/4,s*3/2,s/6,ICO_SHEEN);
        t.fillCircle(cx+s/2,cy-s/2,s/5,BG);
        t.fillRect(cx-s/4,cy-s/8,s/2,s/4,ICO_GLASS);
        t.drawRect(cx-s*3/4,cy-s*3/4,s*3/2,s*3/2,SHELL_LO);
        break;
    case DetectionType::RING:
        housing(t, cx-s*2/3, cy-s, s*4/3, s*2);
        lens(t, cx, cy-s/2, s/2);
        t.fillCircle(cx,cy+s/2,s/2,ICO_INK);
        t.fillCircle(cx,cy+s/2,s/3,ICO_LENS);
        t.fillCircle(cx,cy+s/2,s/5,ICO_INK);                              // lit ring
        break;
    case DetectionType::DEAUTH:
        t.fillRect(cx-s/6,cy-s/4,s/3,s*5/4,ICO_METAL);
        t.fillRect(cx-s/6,cy-s/4,s/8,s*5/4,ICO_METAL2);
        t.fillTriangle(cx+s/4,cy-s,cx+s*3/4,cy-s/2,cx+s/2,cy-s/4,ICO_METAL); // snapped top
        for(int i=1;i<=3;i++) t.drawCircle(cx-s/12,cy-s/3,i*s/3,ICO_WARN);
        t.fillTriangle(cx-s/2,cy-s/2,cx-s/6,cy-s,cx-s/8,cy-s/3,ICO_LED);
        t.fillTriangle(cx-s/3,cy-s/3,cx,cy-s*3/4,cx+s/12,cy-s/6,ICO_LED);
        break;
    case DetectionType::EVILTWIN:
        // Simplified from the crowded pass 2: one solid box, one hollow
        // copy, and a single shared nameplate under both.
        housing(t, cx-s, cy-s/2, s*5/6, s/2);
        t.drawRect(cx+s/6, cy-s/2, s*5/6, s/2, ICO_WARN);
        t.drawRect(cx+s/6+2, cy-s/2+2, s*5/6-4, s/2-4, ICO_WARN);
        for(int i=1;i<=2;i++){
            t.drawCircle(cx-s*7/12, cy-s/2, i*s/3, ICO_LENS);
            t.drawCircle(cx+s*7/12, cy-s/2, i*s/3, ICO_WARN);
        }
        t.fillRect(cx-s, cy+s/2, s*2, s/3, ICO_PLASTIC);                  // one SSID
        t.fillRect(cx-s+3, cy+s/2+3, s*2-6, s/8, SHELL_LO);
        break;
    case DetectionType::IBEACON:
        t.fillRect(cx-s,cy+s/2,s*2,s/3,ICO_METAL);                        // shelf
        t.fillRect(cx-s,cy+s/2,s*2,s/12,ICO_METAL2);
        t.fillEllipse(cx,cy+s/4,s*2/3,s/3,SHELL);
        t.fillEllipse(cx,cy+s/6,s*2/3,s/3,ICO_PLASTIC);                   // puck
        t.fillEllipse(cx,cy+s/6,s/3,s/6,ICO_GLASS);
        for(int i=1;i<=3;i++) t.drawCircle(cx,cy+s/6,s/2+i*s/3,ICO_LENS);
        break;
    case DetectionType::HACKER:
        // Untouched. It was right.
        t.fillRect(cx-s,cy-s*2/3,s*2,s*4/3,ICO_LED);
        t.fillRect(cx-s,cy-s*2/3,s*2,s/6,tt == DetectionType::HACKER?ICO_SHEEN:ICO_LED);
        t.fillRect(cx-s+3,cy-s/2,s+4,s*3/4,ICO_INK);
        t.fillRect(cx-s+5,cy-s/2+2,s,s/4,ICO_LEAF);
        t.fillCircle(cx+s/2,cy+s/4,s/3,ICO_STEM);
        t.fillRect(cx+s/2-s/5,cy+s/4-3,s*2/5,6,ICO_INK);
        t.fillRect(cx+s/2-3,cy+s/4-s/5,6,s*2/5,ICO_INK);
        break;
    default:
        t.drawCircle(cx,cy,s,ICO_METAL);
        t.drawCircle(cx,cy,s-1,ICO_METAL);
        t.setTextSize(3); t.setTextColor(ICO_METAL2,BG);
        t.setCursor(cx-8,cy-12); t.print("?");
        break;
    }
}

void drawPulsingBorder(TFT_eSPI& t, uint32_t now, uint16_t a, uint16_t b,
                       uint8_t thick) {
    // 1.5 s sine pulse, fade between a and b
    float phase = (float)((now / 10) % 1500) / 1500.0f * 6.2831853f;
    float s = 0.5f + 0.5f * sinf(phase);
    uint16_t col = blend(a, b, (uint16_t)(s * 256.0f));
    int w = t.width();
    int h = t.height();
    for (int i = 0; i < thick; i++) {
        t.drawFastHLine(0, i, w, col);
        t.drawFastHLine(0, h - 1 - i, w, col);
        t.drawFastVLine(i, 0, h, col);
        t.drawFastVLine(w - 1 - i, 0, h, col);
    }
}

// TFT_eSPI has fillTriangle but no polygon fill, and a wing lobe is an
// 18-vertex shape. Fanning it into triangles costs ~16 fillTriangle calls
// per wing, and each of those runs its own scanline pass over a bounding
// box that overlaps its neighbours'. One scanline pass over the whole
// polygon is a single drawFastHLine per row instead -- about 30 row fills
// for a wing, and a row fill is the cheapest thing the sprite can do.
//
// Even-odd rule, so a wing that curls back over itself still fills
// correctly. MAXHIT is generous: the curled variants cross a scanline
// four times at most.
static void fillPoly(TFT_eSPI& t, const int16_t* xs, const int16_t* ys,
                     uint8_t n, uint16_t col) {
    if (n < 3) return;
    int16_t ymin = ys[0], ymax = ys[0];
    for (uint8_t i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    static const uint8_t MAXHIT = 12;
    for (int16_t y = ymin; y <= ymax; y++) {
        int16_t xh[MAXHIT];
        uint8_t hits = 0;
        for (uint8_t i = 0; i < n && hits < MAXHIT; i++) {
            const uint8_t j = (uint8_t)((i + 1) % n);
            const int16_t y1 = ys[i], y2 = ys[j];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                xh[hits++] = (int16_t)(xs[i] +
                    (int32_t)(y - y1) * (xs[j] - xs[i]) / (y2 - y1));
            }
        }
        for (uint8_t a = 1; a < hits; a++) {          // tiny insertion sort
            const int16_t v = xh[a];
            int8_t b = (int8_t)a - 1;
            while (b >= 0 && xh[b] > v) { xh[b + 1] = xh[b]; b--; }
            xh[b + 1] = v;
        }
        for (uint8_t a = 0; (uint8_t)(a + 1) < hits; a += 2)
            t.drawFastHLine(xh[a], y, xh[a + 1] - xh[a] + 1, col);
    }
}

// Wing width along its length, sampled at the 9 points the wing is built
// from. Baked rather than evaluated because the real curve is
// (1-t^5)^0.45 * (0.4 + 0.6*min(1, t/0.3)), and two powf() calls per point
// per wing per sprite per frame is a lot of transcendental for a shape
// that never changes. The profile holds its width through the middle and
// drops only at the very end -- a width that falls off linearly gives a
// spike, and the tip has to read as rounded.
static const float WING_PROFILE[9] = {
    0.400f, 0.650f, 0.900f, 0.997f, 0.986f, 0.956f, 0.885f, 0.724f, 0.0f
};

// One rounded bird wing: a curved lobe with a few feather divisions drawn
// back onto it. It beats by swinging about the shoulder, which is what a
// bird does -- a wing that only slides up and down reads as being dragged.
void drawWing(TFT_eSPI& t, float sx, float sy, float len, float angDeg,
              float flap, float width, uint8_t ndiv,
              uint16_t body, uint16_t edge, float curl, float lift,
              uint16_t shade) {
    static const uint8_t N = 8;
    const float step = len / (float)N;
    const float da   = curl * 60.0f * 0.017453293f / (float)N;
    float a  = (angDeg + flap * lift) * 0.017453293f;
    float px = sx, py = sy;

    int16_t tx[N + 1], ty[N + 1], bx[N + 1], by[N + 1];
    int16_t cx[N + 1], cy[N + 1];
    for (uint8_t i = 0; i <= N; i++) {
        const float wgt = WING_PROFILE[i] * width * len;
        const float nx = -sinf(a), ny = cosf(a);
        cx[i] = (int16_t)px;              cy[i] = (int16_t)py;
        tx[i] = (int16_t)(px + nx * wgt * 0.58f);
        ty[i] = (int16_t)(py + ny * wgt * 0.58f);
        bx[i] = (int16_t)(px - nx * wgt * 0.44f);
        by[i] = (int16_t)(py - ny * wgt * 0.44f);
        px += cosf(a) * step;
        py += sinf(a) * step;
        a  += da;
    }

    int16_t hx[2 * (N + 1)], hy[2 * (N + 1)];
    uint8_t n = 0;
    for (uint8_t i = 0; i <= N; i++)      { hx[n] = tx[i]; hy[n] = ty[i]; n++; }
    for (int8_t i = (int8_t)N; i >= 0; i--) { hx[n] = bx[i]; hy[n] = by[i]; n++; }
    fillPoly(t, hx, hy, n, body);

    for (uint8_t i = 0; i < n; i++) {
        const uint8_t j = (uint8_t)((i + 1) % n);
        t.drawLine(hx[i], hy[i], hx[j], hy[j], edge);
    }
    // Shade whichever edge actually ends up LOWER on screen, rather than
    // always the same array. The flock only ever wears wings on one flank
    // so this never showed there, but MuleSkin wears a mirrored pair -- and
    // mirroring flips the sign of the normal, which swaps which of top/bot
    // is the lower edge. A fixed choice therefore put the shadow under one
    // wing and over the other, which reads as one wing being upside down.
    const bool shadeTop = ty[N / 2] > by[N / 2];
    const int16_t* sxArr = shadeTop ? tx : bx;
    const int16_t* syArr = shadeTop ? ty : by;

    // 2*(N+1), not N+3. The shade polygon walks the centreline out and the
    // shaded edge back, so it holds two runs of (N-1) points -- 14 at N=8,
    // where N+3 is 11. The three-entry overflow ran off the end of shx into
    // shy, so three coordinates became garbage and fillPoly drew spans to
    // wherever they landed: stray white lines trailing off the wings.
    int16_t shx[2 * (N + 1)], shy[2 * (N + 1)];
    uint8_t sn = 0;
    for (uint8_t i = 2; i <= N; i++) { shx[sn] = cx[i]; shy[sn] = cy[i]; sn++; }
    for (int8_t i = (int8_t)N; i >= 2; i--) { shx[sn] = sxArr[i]; shy[sn] = syArr[i]; sn++; }
    fillPoly(t, shx, shy, sn, shade);

    for (uint8_t d = 1; d <= ndiv; d++) {
        uint8_t i0 = (uint8_t)((float)N * (0.26f + 0.16f * (float)d));
        uint8_t i1 = (uint8_t)(i0 + 2);
        if (i1 > N) i1 = N;
        if (i1 <= i0) break;
        t.drawLine(cx[i0], cy[i0], sxArr[i1], syArr[i1], edge);
    }
}

// The lil guy, walking the same line the Mowin' Man does. He was drawn for a
// background that got shelved, and this is the whole of him that survived --
// eight frames of a walk cycle in flash (see lil_guy.h) and a cameo once every
// five minutes.
//
// Two device pixels per art pixel, drawn as fillRect: at 10x10 art that is at
// most a hundred small fills, and only while he is actually on screen. The
// frame comes off the clock rather than off a frame counter, because 80 ms is
// slower than this board's own frame time and a counter would run him at
// whatever speed the rest of the scene happened to be managing.
void drawLilGuy(TFT_eSPI& t, int x, int baseY, uint32_t now, uint8_t scale, bool flip) {
    static const uint16_t PAL[4] = { 0, 0, 0, 0 };
    (void)PAL;
    const uint16_t hair = t.color565(0, 255, 245);
    const uint16_t skin = t.color565(255, 208, 240);
    const uint16_t body = t.color565(185, 103, 255);
    const uint8_t  f    = (uint8_t)((now / 80u) % LILGUY_FRAMES);
    const int      s    = scale ? scale : 2;
    const int      top  = baseY - LILGUY_H * s;
    for (uint8_t y = 0; y < LILGUY_H; y++) {
        const uint32_t row = LILGUY[f * LILGUY_H + y];
        for (uint8_t xx = 0; xx < LILGUY_W; xx++) {
            const uint8_t c = (uint8_t)((row >> (xx * 2)) & 3u);
            if (!c) continue;
            // The art faces right, so travelling left is the mirrored column.
            const uint8_t dx = flip ? (uint8_t)(LILGUY_W - 1 - xx) : xx;
            t.fillRect(x + dx * s, top + y * s, s, s,
                       (c == 1) ? hair : (c == 2) ? skin : body);
        }
    }
}

// Defined further down, next to backgroundTap() which consumes it.


// ---- the Aquarium shark --------------------------------------------------
// TWO touches, not one, and the first one does not catch anything -- it only
// makes him turn round and come back at you.
//
// That is a fix as much as it is a flourish. The shark is deliberately the
// biggest thing in the tank, and between an eight second crossing and a ten
// to twenty-five second nap he is on screen about a THIRD of the time --
// which is the same shape of problem the gold toaster's hit box had, where a
// generous target that is up a lot handed the costume to people who were
// only poking at the background. Splitting it in two means a stray tap costs
// nothing but a turn, and the costume has to be meant twice.
//
// Where he was last drawn, for backgroundTap(). Stale the same way the
// toaster's is: not refreshed in the last few frames means not on screen.
static int      s_sharkX = -1, s_sharkY = 0, s_sharkHW = 0, s_sharkHH = 0;
static uint32_t s_sharkAt = 0;
static bool     s_sharkTurnWanted = false;   // touch one; drawAquarium owns his heading
static bool     s_sharkHunting    = false;   // he has noticed you
static uint32_t s_sharkHuntAt     = 0;       // ...since when
static bool     s_sharkCaught     = false;   // touch two; pending for muleskin.cpp
static int      s_sharkFxX = 0, s_sharkFxY = 0;
static uint32_t s_sharkBoltAt = 0;           // ...and the flourish when he goes


// ---- XYZZY ----------------------------------------------------------------
// The magic word from the first adventure game. Every so often the terminal
// types it, large, either side of MuleSkin, and holds it for a few seconds.
// Tap it three times (three appearances) and consumeXyzzy() reports each
// one; the third is the YZZERD unlock.
//
// Either side of him, not centred like the banner below: he stands in the
// middle of this band and a centred word is behind him.
static const uint8_t  XYZZY_NEEDED  = 3;
static uint32_t s_xyzzyNextAt = 0, s_xyzzyStart = 0, s_xyzzyHitAt = 0;
// Where the word is RIGHT NOW, so a tap can find it -- same shape as the
// lodge and the eye: published every frame it is up, stale after 250 ms.
static int      s_xyzzyX[2] = { 0, 0 }, s_xyzzyY = 0, s_xyzzyHW = 0, s_xyzzyHH = 0;
static uint32_t s_xyzzyAt = 0;
static uint8_t  s_xyzzyTaps = 0, s_xyzzyPending = 0;

uint8_t consumeXyzzy() {
    const uint8_t n = s_xyzzyPending;
    s_xyzzyPending = 0;
    return n;
}

// Bring the word up now rather than on its own clock: the console's XYZZY
// command and the emulator, so the unlock can be tested without waiting.
void summonXyzzy() { if (!s_xyzzyStart) s_xyzzyNextAt = 1; }

// ---- tappable background bits -----------------------------------------
// The werewolf's speech bubble is PUBLISHED rather than drawn where it
// is computed. drawFire is the background: ui_clear paints MuleSkin over
// it, then the idle-event flourishes, then the ALL CLEAR headline -- so
// a bubble drawn inside drawFire ends up underneath the mascot, and for
// text that does not read as depth, it reads as a rendering fault. It is
// the same reason ALL CLEAR is drawn after MuleSkin rather than before.
//
// Stale for the same reason the moon below is: if the background is
// switched away from FIRE mid-howl, nothing clears these, so the
// overlay checks the timestamp before it trusts them.
static int      s_wolfSayX = -1, s_wolfSayY = -1;
static int      s_wolfSayKind = 0;
static float    s_wolfHowlK = 0.0f;
static int      s_wolfSayW = 0, s_wolfSayTop = 0;
static uint32_t s_wolfSayAt = 0;

// drawFire publishes its moon here every frame it draws one, and
// backgroundTap() below tests against that. The timestamp matters: a
// moon position left over from a background that is no longer on screen
// must not stay tappable, and drawFire is simply not called once the
// user cycles away.
static int      s_moonX = -1, s_moonY = -1, s_moonR = 0;
static uint32_t s_moonAt = 0;

// Five taps, each within MOON_TAP_WINDOW of the one before, summon the
// werewolf. The window is what makes it a deliberate act rather than an
// accumulation: a tap now and a tap five minutes from now should not
// count toward the same thing. It was ten; five is still a drum roll and
// nobody hits it by accident, because the window does that work, not the
// count.
static const uint32_t MOON_TAP_WINDOW = 2500;
static const uint8_t  MOON_TAPS_NEEDED = 5;
static uint8_t  s_moonTaps  = 0;
static uint32_t s_moonTapAt = 0;
static uint32_t s_wolfAt    = 0;      // 0 = no werewolf on stage
static bool     s_wolfSummonPending = false;   // consumed by main.cpp

// Stage timings, all eased into each other. Nothing here pops: that is
// the whole lesson of the tree that used to strobe in this same scene.
// The howl is now a shaped move rather than a linear ramp: COIL is the
// crouch he gathers on, RISE is the snap up into the note, and whatever
// is left of WOLF_HOWL is the note held. Longer than it was because the
// SKID line no longer overlaps it -- the howl finally has the stage to
// itself and needs room to land, plus its echoes.

bool consumeWerewolfSummon() {
    if (!s_wolfSummonPending) return false;
    s_wolfSummonPending = false;
    return true;
}

void dimRegion(TFT_eSPI& t, int x, int y, int w, int h, uint8_t amount) {
    if (amount == 0) return;
    if (amount >= 250) { t.fillRect(x, y, w, h, BG); return; }
    // amount -> row spacing: 128 blanks every other row, 85 every third,
    // 64 every fourth, and so on. Below ~50 the effect stops being worth
    // the pass at all.
    const int step = 255 / (int)amount + 1;
    if (step < 2) { t.fillRect(x, y, w, h, BG); return; }
    for (int yy = y + (step - 1); yy < y + h; yy += step)
        t.drawFastHLine(x, yy, w, BG);
}

static char     s_toastHead[18] = {0};
static char     s_toastSub[22]  = {0};
static uint16_t s_toastAccent   = 0;
static uint32_t s_toastUntil    = 0;

void showToast(const char* head, const char* sub, uint16_t accent, uint32_t ms) {
    strncpy(s_toastHead, head ? head : "", sizeof(s_toastHead) - 1);
    s_toastHead[sizeof(s_toastHead) - 1] = 0;
    strncpy(s_toastSub, sub ? sub : "", sizeof(s_toastSub) - 1);
    s_toastSub[sizeof(s_toastSub) - 1] = 0;
    s_toastAccent = accent;
    s_toastUntil  = millis() + ms;
}

void drawToast(TFT_eSPI& t, uint32_t now) {
    if (!s_toastUntil) return;
    if ((int32_t)(now - s_toastUntil) >= 0) { s_toastUntil = 0; return; }

    const int w = t.width(), h = t.height();
    t.setTextSize(2);
    int bw = t.textWidth(s_toastHead) + 30;
    if (s_toastSub[0]) {
        t.setTextSize(1);
        const int sw = t.textWidth(s_toastSub) + 30;
        if (sw > bw) bw = sw;
    }
    if (bw > w - 20) bw = w - 20;
    const int bh = s_toastSub[0] ? 48 : 34;
    const int bx = (w - bw) / 2, by = (h - bh) / 2;

    t.fillRect(bx, by, bw, bh, BG);
    t.drawRect(bx, by, bw, bh, s_toastAccent);
    t.drawRect(bx + 1, by + 1, bw - 2, bh - 2, blend(s_toastAccent, BG, 160));

    t.setTextSize(2);
    t.setTextColor(s_toastAccent, BG);
    t.setCursor(bx + (bw - t.textWidth(s_toastHead)) / 2, by + 8);
    t.print(s_toastHead);
    if (s_toastSub[0]) {
        t.setTextSize(1);
        t.setTextColor(WHITE, BG);
        t.setCursor(bx + (bw - t.textWidth(s_toastSub)) / 2, by + 31);
        t.print(s_toastSub);
    }
}

void setBackgroundFloor(int y, int textTop) {
    s_bgFloor   = y;
    s_bgTextTop = (textTop >= 0) ? textTop : y;
}
void clearBackgroundFloor()     { s_bgFloor = -1; s_bgTextTop = -1; }

// Cost of the last background draw, exponentially smoothed. Measured
// HERE rather than in loop(), because loop()'s own frame average is
// whatever screen you are currently looking at -- and DIAGNOSTICS,
// which is where you read the number, draws no background at all. Its
// FRAME figure was therefore timing the diagnostics screen and saying
// nothing about the animation it was being consulted about. This one
// holds the last value from a screen that actually drew a backdrop, so
// it survives the walk over to go and read it.
static uint32_t s_bgUsAvg = 0;
uint32_t backgroundUs() { return s_bgUsAvg; }

// Single place that maps the Settings background choice onto a
// renderer. Lifted out of uiClearTick(), which owned it while CLEAR was
// the only screen with a live backdrop.
// The fire background's heat grid: 6.4 KB on a 2.8" board, and for years a
// fixed cost whether or not anyone had ever picked FIRE. It is taken from
// the heap the first time fire draws and given back the moment another
// background is chosen, so a board that never shows fire never pays for it.

// ---- the MuleSkin artwork -----------------------------------------------------
// One 320 px RGB332 square (include/muleskin_art.h, from tools/make_boot_art.py)
// serves the boot splash and the MULESKIN background. Rows are sampled into a
// line buffer and handed to the frame sprite whole -- 77k pixels a frame for
// the background, which pixel by pixel would cost more than the rest of the
// frame put together.
static FastSprite* s_frameSprite = nullptr;
void setFrameSprite(FastSprite* f) { s_frameSprite = f; }

// The library's own RGB332 -> RGB565 expansion (TFT_eSPI::color8to16), so a
// row drawn as lines lands on exactly the bytes a copied one would.
static inline uint16_t art565(uint8_t c) {
    static const uint8_t B2TO5[4] = { 0, 10, 21, 31 };
    return (uint16_t)(((c & 0xE0) << 8) | ((c & 0xC0) << 5) | ((c & 0x1C) << 6) |
                      ((c & 0x1C) << 3) | B2TO5[c & 0x03]);
}

static inline int artWrap(int v, int n) { v %= n; return v < 0 ? v + n : v; }

void drawArtwork(TFT_eSPI& t, int x, int y, int w, int h, int sx, int sy, int sw, int sh,
                 int clipY0, int clipY1, uint32_t el) {
    using MuleSkinArt::SIZE;
    static const int MAXW = 1024;
    if (w <= 0 || h <= 0 || w > MAXW || sw <= 0 || sh <= 0) return;
    static uint8_t line[MAXW];
    static int16_t colOf[MAXW];
    for (int c = 0; c < w; c++) colOf[c] = (int16_t)(sx + c * sw / w);

    // docs/twich.py's ear twitch: a 24-step cycle at 41 ms -- the left ear
    // flicks out and back, then the right ear in, back and a smaller second
    // flick. Each ear is a vertical slice of the picture rolled sideways,
    // fully at the tips and fading to nothing 55% of the way down, so the hood
    // and shoulders stay put. Shifts are twich.py's, in its 460 px source,
    // scaled to this 320 px one and rounded rather than truncated, so the
    // right ear's smaller flicks survive a small picture.
    const int step = (int)((el / 41) % 24);
    int left = 0, right = 0;
    if      (step >= 5  && step <= 7)  left  = -7;
    else if (step >= 8  && step <= 9)  left  =  3;
    if      (step >= 14 && step <= 15) right = -5;
    else if (step >= 16 && step <= 17) right =  5;
    else if (step >= 18 && step <= 19) right = -3;
    const int earRows = SIZE * 55 / 100;
    const int lx0 = SIZE * 20 / 100, mid = SIZE / 2, rx1 = SIZE * 80 / 100;
    const float k = (float)SIZE / 460.0f;

    FastSprite* const fs = (s_frameSprite && (TFT_eSPI*)s_frameSprite == &t) ? s_frameSprite : nullptr;
    for (int r = 0; r < h; r++) {
        const int dy = y + r;
        if (dy < clipY0 || dy >= clipY1 || !DrawBand::has(dy, dy + 1)) continue;
        int srow = sy + r * sh / h;
        if (srow >= SIZE) srow = SIZE - 1;
        int amtL = 0, amtR = 0;
        if ((left || right) && srow < earRows) {
            const float f = (float)(earRows - srow) / (float)earRows;
            amtL = (int)lroundf((float)left  * k * f * f);
            amtR = (int)lroundf((float)right * k * f * f);
        }
        // The picture is stored compressed (tools/make_boot_art.py): decode
        // the row, once -- scaled up, the same row is drawn several times.
        static uint8_t artRow[SIZE];
        static int     artRowAt = -1;
        if (srow != artRowAt) { MuleSkinArt::row(srow, artRow); artRowAt = srow; }
        const uint8_t* src = artRow;
        // 1:1 across with nothing rolled -- every background row on a
        // 320-wide panel outside a twitch -- is a straight copy out of flash.
        // The sampling loop below cost the background 24 ms a frame on the
        // CYD; this is the case that needs to be cheap.
        if (fs && sw == w && amtL == 0 && amtR == 0 && sx >= 0 && sx + w <= SIZE &&
            fs->pushRow332(x, dy, w, src + sx)) continue;
        for (int c = 0; c < w; c++) {
            int sc = colOf[c];
            // np.roll: what lands here came from `amt` back, wrapped in its slice.
            if (amtL && sc >= lx0 && sc < mid)      sc = lx0 + artWrap(sc - lx0 - amtL, mid - lx0);
            else if (amtR && sc >= mid && sc < rx1) sc = mid + artWrap(sc - mid - amtR, rx1 - mid);
            line[c] = src[sc < 0 ? 0 : sc >= SIZE ? SIZE - 1 : sc];
        }
        if (fs && fs->pushRow332(x, dy, w, line)) continue;
        // Anywhere else (the emulator, a board drawing straight to the panel):
        // one line per run of a colour.
        int run = 0;
        for (int c = 1; c <= w; c++) {
            if (c == w || line[c] != line[run]) {
                t.drawFastHLine(x + run, dy, c - run, art565(line[run]));
                run = c;
            }
        }
    }
}

void drawArtworkSquare(TFT_eSPI& t, int x, int y, int size, uint32_t el) {
    drawArtwork(t, x, y, size, size, 0, 0, MuleSkinArt::SIZE, MuleSkinArt::SIZE, y, y + size, el);
}

void drawArtworkBackground(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    using MuleSkinArt::SIZE;
    const int W = t.width(), H = t.height();
    // Cover the screen, keeping the aspect: the long side takes the square's
    // full width, the short side a crop. Cropped from the top down rather
    // than the middle -- the ears are the top of the picture, and the desk is
    // what can go.
    int sw = SIZE, sh = SIZE;
    if (W >= H) sh = SIZE * H / W;
    else        sw = SIZE * W / H;
    drawArtwork(t, 0, 0, W, H, (SIZE - sw) / 2, 0, sw, sh, yStart, yEnd, now);
}

// ---- the RADAR background ------------------------------------------------------
// docs/radar.gif as a model (include/radar_art.h, tools/make_radar.py): each
// pixel of the scope is RING (never changes) + LEVEL[BLOB][d], where d is how
// far behind the sweep arm it lies -- the arm's brightness and the weather's
// afterglow, both measured from the GIF. Only the scope's box is computed; the
// rest of the screen is black. Laid out in 320x240 and scaled to cover any
// other panel, as the GIF itself would be.
void drawRadarBackground(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    using namespace RadarArt;
    static const int MAXW = 1024;
    const int W = t.width(), H = t.height();
    if (W <= 0 || W > MAXW) return;
    static uint8_t line[MAXW];
    static int16_t colRx[MAXW];
    static uint8_t colAx[MAXW];
    static bool    colRight[MAXW];

    // Cover: the larger scale wins, the excess is cropped equally.
    const int coverW = (W * REF_H >= H * REF_W) ? W : H * REF_W / REF_H;
    const int coverH = (W * REF_H >= H * REF_W) ? W * REF_H / REF_W : H;
    const int ox = (W - coverW) / 2, oy = (H - coverH) / 2;
    int dx0 = W, dx1 = 0;
    for (int x = 0; x < W; x++) {
        const int rx = (x - ox) * REF_W / coverW;
        colRx[x] = (int16_t)rx;
        const float fx = (float)rx - CX;
        int ax = (int)(fx < 0 ? -fx : fx);
        colAx[x] = (uint8_t)(ax < QUAD_N ? ax : QUAD_N - 1);
        colRight[x] = fx >= 0;
        if (rx >= BOX_X && rx < BOX_X + BOX_W) { if (x < dx0) dx0 = x; dx1 = x + 1; }
    }
    const uint8_t sweep = (uint8_t)(PHASE + (uint32_t)((uint64_t)(now % PERIOD_MS) * 256u / PERIOD_MS));

    FastSprite* const fs = (s_frameSprite && (TFT_eSPI*)s_frameSprite == &t) ? s_frameSprite : nullptr;
    for (int y = yStart; y < yEnd; y++) {
        if (!DrawBand::has(y, y + 1)) continue;
        const int ry = (y - oy) * REF_H / coverH;
        if (ry < BOX_Y || ry >= BOX_Y + BOX_H || dx0 >= dx1) {
            t.drawFastHLine(0, y, W, BLACK);
            continue;
        }
        if (dx0 > 0) t.drawFastHLine(0, y, dx0, BLACK);
        if (dx1 < W) t.drawFastHLine(dx1, y, W - dx1, BLACK);
        const float fy = (float)ry - CY;
        int ay = (int)(fy < 0 ? -fy : fy);
        if (ay >= QUAD_N) ay = QUAD_N - 1;
        const bool up = fy < 0;
        const uint8_t* qrow = QUAD + ay * QUAD_N;
        const uint8_t* prow = PIXELS + (ry - BOX_Y) * BOX_W - BOX_X;
        for (int x = dx0; x < dx1; x++) {
            // atan2 by the quadrant table, mirrored into the other three.
            const uint8_t q = qrow[colAx[x]];
            uint8_t a;
            if (colRight[x]) a = up ? q : (uint8_t)(128 - q);
            else             a = up ? (uint8_t)(256 - q) : (uint8_t)(128 + q);
            const uint8_t px = prow[colRx[x]];
            int lv = (px >> 4) + LEVEL[(px & 0x0F) * 256 + (uint8_t)(sweep - a)];
            line[x - dx0] = PALETTE[lv > 15 ? 15 : lv];
        }
        const int n = dx1 - dx0;
        if (fs && fs->pushRow332(dx0, y, n, line)) continue;
        int run = 0;
        for (int c = 1; c <= n; c++) {
            if (c == n || line[c] != line[run]) {
                t.drawFastHLine(dx0 + run, y, c - run, art565(line[run]));
                run = c;
            }
        }
    }
}

// What the radar is actually seeing: every device heard in the last minute as
// a blip on the main screen's scope, in its type's colour. The bearing is a
// hash of the address -- the board has no direction finding, so it only has
// to stay put -- and the distance from the centre is the signal: strong near
// the middle, faint at the rim. Each blip lights up as the arm passes over it
// and dims behind it, like a real radar's paint, and fades out over the minute
// after the device was last heard.
static uint16_t scale565(uint16_t c, uint8_t k) {   // k/255 of c
    const uint16_t r = ((c >> 11) & 0x1F) * k / 255;
    const uint16_t g = ((c >> 5) & 0x3F) * k / 255;
    const uint16_t b = (c & 0x1F) * k / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Where each blip was last drawn, for radarBlipAt(): the main screen's tap
// handler asks which device a finger landed on. Stale after 250 ms, like the
// other tap targets -- a blip not redrawn lately is not on screen.
static const int MAX_BLIPS = 24;
static int16_t  s_blipX[MAX_BLIPS], s_blipY[MAX_BLIPS];
static uint8_t  s_blipMac[MAX_BLIPS][6];
static uint8_t  s_blipN = 0, s_blipR = 3;
static uint32_t s_blipAt = 0;

// A blip's colour is its COUNTER's colour, so a dot and the number under the
// radar that counts it match: the main screen's row folds the BLE tags into
// TRACKER (AIRTAG's colour), RING into CAM and EVIL TWIN into HACK -- see
// counterCount() in ui_clear.cpp. Everywhere else a type keeps its own colour.
static uint16_t blipColorFor(DetectionType t) {
    switch (t) {
        case DetectionType::GOOGLE_TAG: case DetectionType::TILE:
        case DetectionType::SAMSUNG_TAG: return colorFor(DetectionType::AIRTAG);
        case DetectionType::RING:        return colorFor(DetectionType::CAMERA);
        case DetectionType::EVILTWIN:    return colorFor(DetectionType::HACKER);
        default:                         return colorFor(t);
    }
}

static void drawRadarBlips(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                           const DetectionEngine& eng) {
    using namespace RadarArt;
    static const uint32_t SHOW_MS = 60000;
    const int W = t.width(), H = t.height();
    // The same cover transform drawRadarBackground() uses.
    const int coverW = (W * REF_H >= H * REF_W) ? W : H * REF_W / REF_H;
    const int coverH = (W * REF_H >= H * REF_W) ? W * REF_H / REF_W : H;
    const int ox = (W - coverW) / 2, oy = (H - coverH) / 2;
    const uint8_t sweep = (uint8_t)(PHASE + (uint32_t)((uint64_t)(now % PERIOD_MS) * 256u / PERIOD_MS));
    // The scope in screen pixels, centre and rim.
    const int cx = ox + (int)(CX * coverW / REF_W), cy = oy + (int)(CY * coverH / REF_H);
    const int rim = ((BOX_W < BOX_H ? BOX_W : BOX_H) / 2 - 6) * coverW / REF_W;
    int rad = 4 * coverW / REF_W;
    if (rad < 3) rad = 3;

    // A band pass that starts at the top rebuilds the tap list; the second
    // band (cyd35) adds to it.
    if (yStart == 0) s_blipN = 0;
    const uint8_t n = eng.logCount();
    for (uint8_t i = 0; i < n && s_blipN < MAX_BLIPS; i++) {
        const Detection* d = eng.logAt(i);
        if (!d || d->restored || !d->lastSeen) continue;
        const uint32_t age = now - d->lastSeen;
        if (age >= SHOW_MS) continue;
        const uint8_t a = RadarBlip::bearing(d->mac);
        int x, y;
        RadarBlip::point(a, rim * RadarBlip::radius256(d->rssi) / 256, cx, cy, x, y);
        if (y - rad - 2 < yStart || y + rad + 2 >= yEnd) continue;
        // Paint, then the minute's fade on top.
        const uint8_t behind = (uint8_t)(sweep - a);
        int k = RadarBlip::paint(behind) * (int)(SHOW_MS - age) / (int)SHOW_MS;
        if (k < 90) k = 90;
        const uint16_t base = blipColorFor(d->type);   // its counter's colour
        t.fillCircle(x, y, rad + 1, BLACK);   // lifts it off the weather behind
        t.fillCircle(x, y, rad, scale565(base, (uint8_t)k));
        if (behind < 16) t.drawCircle(x, y, rad + 2, scale565(base, 200));
        s_blipX[s_blipN] = (int16_t)x;
        s_blipY[s_blipN] = (int16_t)y;
        memcpy(s_blipMac[s_blipN], d->mac, 6);
        s_blipN++;
    }
    s_blipR = (uint8_t)rad;
    s_blipAt = now;
}

bool radarBlipAt(int x, int y, uint32_t now, uint8_t macOut[6]) {
    if (!s_blipN || now - s_blipAt > 250) return false;
    // Nearest within a finger's reach: blips are small and can sit close.
    const int reach = s_blipR + 10;
    int best = -1;
    int32_t bestD = (int32_t)reach * reach + 1;
    for (int i = 0; i < s_blipN; i++) {
        const int32_t dx = x - s_blipX[i], dy = y - s_blipY[i];
        const int32_t d2 = dx * dx + dy * dy;
        if (d2 < bestD) { bestD = d2; best = i; }
    }
    if (best < 0) return false;
    memcpy(macOut, s_blipMac[best], 6);
    return true;
}

void drawActiveBackground(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                          const DetectionEngine& eng, bool advance) {
    const uint32_t bgT0 = micros();
    // The frame's worth of motion for every per-call stepper -- see s_animK.
    // Zero on a non-advancing call (cyd35's second band), which is also
    // what stops those steppers running twice per logical frame there.
    {
        static uint32_t last = 0;
        if (advance) {
            const uint32_t dt = last ? now - last : ANIM_REF_MS;
            last = now;
            float k = (float)dt / (float)ANIM_REF_MS;
            if (k > 3.0f) k = 3.0f;
            s_animK = k;
        } else {
            s_animK = 0.0f;
        }
    }
    // RADAR is the only background left, with its live blips; BLACK is
    // boring mode's. Any other value saved on a board loads as RADAR (see
    // Settings::load()). BLACK is still a fill, not a skip: every screen that
    // draws a backdrop relies on it to erase the previous frame, so drawing
    // nothing would smear rather than go black.
    (void)advance;
    if (Settings::background() == Settings::Background::BLACK) {
        t.fillRect(0, yStart, t.width(), yEnd - yStart, BG);
    } else {
        drawRadarBackground(t, now, yStart, yEnd);
        drawRadarBlips(t, now, yStart, yEnd, eng);
    }
    const uint32_t bgDt = micros() - bgT0;
    s_bgUsAvg = s_bgUsAvg ? s_bgUsAvg + ((int32_t)bgDt - (int32_t)s_bgUsAvg) / 8 : bgDt;
}

// Where the gold toaster was last drawn, and how big. Published by
// drawFlyingToasters() every frame it is on screen so backgroundTap() has
// something to hit-test against -- the same shape as the moon's
// s_moonX/s_moonY, and stale for the same reason: if it has not been
// refreshed in the last few frames the toaster is gone.
static int      s_goldX = -1, s_goldY = -1, s_goldHW = 0, s_goldHH = 0;
static uint32_t s_goldAt = 0;
static bool     s_goldCaught = false;

bool consumeToasterCatch() {
    if (!s_goldCaught) return false;
    s_goldCaught = false;
    return true;
}

bool consumeSharkCatch() {
    if (!s_sharkCaught) return false;
    s_sharkCaught = false;
    return true;
}

// ---- the Starfield eye ---------------------------------------------------
// Catch two in a row and the VOID EYE costume is yours. "In a row" is the
// whole mechanic: without a reset it quietly degrades into "two eyes ever",
// which is not the same game at all. An eye only counts -- as a catch or as
// a miss -- once it has grown past EYE_CATCH_MIN_R, because below that it was
// never a target anyone could have hit.
//
// One eye is tracked for hit-testing but the bookkeeping is per slot: the
// junk field can hold two pieces at once, and a second eyeball arriving must
// not cancel the first one's miss.
static const uint8_t EYE_PAIR_NEEDED = 2;

static int      s_eyeX = -1, s_eyeY = -1, s_eyeR = 0;
static uint32_t s_eyeAt = 0;
static int8_t   s_eyeSlot = -1;
static bool     s_eyeGot[2] = { false, false };   // and was caught before it left
// Caught big eyes in a row. Read back from the settings store at first use:
// two eyes can be minutes apart, so a restart in between used to throw the
// first one away. See Settings::Hunt.
static uint8_t  s_eyeStreak = 0;
static bool     s_eyeStreakRead = false;

static uint8_t eyeStreak() {
    if (!s_eyeStreakRead) {
        s_eyeStreak = Settings::huntProgress(Settings::Hunt::EYE_STREAK);
        if (s_eyeStreak >= EYE_PAIR_NEEDED) s_eyeStreak = 0;   // stale, from a finished hunt
        s_eyeStreakRead = true;
    }
    return s_eyeStreak;
}

static void setEyeStreak(uint8_t v) {
    s_eyeStreak = v;
    s_eyeStreakRead = true;
    Settings::setHuntProgress(Settings::Hunt::EYE_STREAK, v);
}
static bool     s_eyePairPending = false;         // consumed by main.cpp

// The tell. Catching the first of the pair has to be visible or the second
// one is a coin flip: a ring snaps out from where it was, which costs three
// circle outlines for a third of a second.
static int      s_eyeFxX = 0, s_eyeFxY = 0;
static uint32_t s_eyeFxAt = 0;

// ---- the lodge -----------------------------------------------------------
// The lodge is 40 wide and 27 tall, sitting ON the ridge line, and it drifts
// across at about 4.7 px a second on the far plane -- roughly a minute and a
// half on screen, then off for anywhere from ten seconds to three minutes.
// That rhythm is the whole reason it works as a target: generous while it is
// there, and impossible to stumble into while it is not.
static const uint8_t LODGE_KNOCKS_NEEDED = 5;
static int      s_lodgeHitX = -1, s_lodgeHitY = 0;
static uint32_t s_lodgeAt = 0;
static uint8_t  s_lodgeKnocks = 0;
static uint32_t s_lodgeKnockAt = 0;
static bool     s_lodgePending = false;

// Where the toasters cameo is RIGHT NOW, so a tap can find him. Same shape
// as the lodge and the starfield eye: the drawing code publishes a box each
// frame, backgroundTap() tests it, and the staleness check means a tap can
// only land while he is actually on screen.
static int      s_lilX = 0, s_lilY = 0, s_lilW = 0, s_lilH = 0;
static uint32_t s_lilAt = 0;
static bool     s_lilPending = false;

bool consumePetUnlock() {
    if (!s_lilPending) return false;
    s_lilPending = false;
    return true;
}

bool consumeLodgeKnock() {
    if (!s_lodgePending) return false;
    s_lodgePending = false;
    return true;
}

bool consumeEyeCatch() {
    if (!s_eyePairPending) return false;
    s_eyePairPending = false;
    return true;
}

bool backgroundTap(int x, int y, uint32_t now) {
    // The lil guy, while he is crossing. Generous by a few pixels each way
    // on purpose -- he is twenty pixels square and moving, which is a much
    // harder target than a lodge that stays still, and this is the EARNED
    // route to the pet rather than a secret meant to resist being found.
    if (s_lilAt && (now - s_lilAt) <= 250) {
        const int m = 6;
        if (x >= s_lilX - m && x <= s_lilX + s_lilW + m &&
            y >= s_lilY - m && y <= s_lilY + s_lilH + m) {
            s_lilPending = true;
            s_lilAt = 0;                          // caught: one tap is enough
            return true;
        }
    }
    // The Starfield eye, while it is close. Circle hit test on the sphere it
    // actually draws -- no generous margin, the way the gold toaster's box
    // was tightened: this one is a big target already, and the whole point of
    // the egg is that it cannot be stumbled into.
    if (s_eyeSlot >= 0 && (now - s_eyeAt) <= 250) {
        const int edx = x - s_eyeX, edy = y - s_eyeY;
        if (edx * edx + edy * edy <= s_eyeR * s_eyeR) {
            s_eyeGot[s_eyeSlot] = true;
            s_eyeFxX = s_eyeX; s_eyeFxY = s_eyeY;
            s_eyeFxAt = now ? now : 1;
            s_eyeSlot = -1;                      // caught: stop accepting taps
            const uint8_t run = (uint8_t)(eyeStreak() + 1);
            if (run >= EYE_PAIR_NEEDED) {
                setEyeStreak(0);               // hunt over; do not keep a stale count
                s_eyePairPending = true;
            } else {
                setEyeStreak(run);
            }
            return true;
        }
    }

    // XYZZY, while the terminal has it up. Either copy of the word counts.
    if (s_xyzzyAt && (now - s_xyzzyAt) <= 250) {
        const int xdy = y - s_xyzzyY;
        for (uint8_t k = 0; k < 2; k++) {
            const int xdx = x - s_xyzzyX[k];
            if (xdx < -s_xyzzyHW || xdx > s_xyzzyHW || xdy < -s_xyzzyHH || xdy > s_xyzzyHH) continue;
            s_xyzzyAt    = 0;                    // taken: one tap an appearance
            s_xyzzyHitAt = now ? now : 1;
            if (++s_xyzzyTaps >= XYZZY_NEEDED) s_xyzzyTaps = 0;
            s_xyzzyPending = s_xyzzyTaps ? s_xyzzyTaps : XYZZY_NEEDED;
            return true;
        }
    }

    // Five knocks on the lodge door. Box, not circle: it is a building.
    // The window between taps is the moon's, because it is the same idea and
    // it should feel the same in the hand.
    if (s_lodgeHitX >= 0 && (now - s_lodgeAt) <= 250) {
        const int ldx = x - s_lodgeHitX, ldy = y - s_lodgeHitY;
        if (ldx >= -22 && ldx <= 22 && ldy >= -30 && ldy <= 4) {
            if ((now - s_lodgeKnockAt) > MOON_TAP_WINDOW) s_lodgeKnocks = 0;
            s_lodgeKnockAt = now;
            if (++s_lodgeKnocks >= LODGE_KNOCKS_NEEDED) {
                s_lodgeKnocks = 0;
                s_lodgePending = true;
            }
            return true;
        }
    }

    // The Aquarium shark. The FIRST touch never catches him -- it turns him
    // round -- and the second one, on the pass he makes coming back, does.
    if (s_sharkX >= 0 && (now - s_sharkAt) <= 250) {
        const int sdx = x - s_sharkX, sdy = y - s_sharkY;
        if (sdx <= s_sharkHW && sdx >= -s_sharkHW &&
            sdy <= s_sharkHH && sdy >= -s_sharkHH) {
            if (!s_sharkHunting) {
                s_sharkHunting    = true;
                s_sharkHuntAt     = now;
                s_sharkTurnWanted = true;
            } else {
                s_sharkCaught = true;
                s_sharkFxX = s_sharkX; s_sharkFxY = s_sharkY;
                s_sharkBoltAt = now ? now : 1;
                s_sharkHunting = false;
                s_sharkX = -1;             // taken: stop accepting taps on him
            }
            return true;
        }
    }

    // The rare gold toaster is catchable. One tap, unlike the moon's three:
    // it is only on screen for a few seconds at a time and moving, which is
    // difficulty enough without also demanding a triple-tap on a target
    // that will not still be there.
    if (s_goldX >= 0 && (now - s_goldAt) <= 250) {
        const int gdx = x - s_goldX, gdy = y - s_goldY;
        if (gdx <= s_goldHW && gdx >= -s_goldHW &&
            gdy <= s_goldHH && gdy >= -s_goldHH) {
            s_goldCaught = true;
            s_goldX = -1;               // caught: stop accepting taps on it
            return true;
        }
    }

    // Not drawn recently means not on screen.
    if (s_moonX < 0 || (now - s_moonAt) > 250) return false;
    const int dx = x - s_moonX, dy = y - s_moonY;
    const int r  = s_moonR + 7;            // generous: it is a small target
    if (dx * dx + dy * dy > r * r) return false;
    if (s_wolfAt) return true;             // already out; eat the tap, do nothing
    if ((now - s_moonTapAt) > MOON_TAP_WINDOW) s_moonTaps = 0;
    s_moonTapAt = now;
    if (++s_moonTaps >= MOON_TAPS_NEEDED) {
        s_moonTaps = 0;
        s_wolfAt   = now ? now : 1;        // never 0, that means "none"
        s_wolfSummonPending = true;
    }
    return true;
}

// ---- the yeti ----------------------------------------------------------
// He came from the retired SNOWFALL background; the pet (pet.cpp) and the
// outfit-unlock card still draw him, through drawYeti().
enum class YPose : uint8_t { RUN, EAT, WINDED, DOWN, RECOIL };

// holding: put the arms in the reaching pose regardless of what the
// face is doing. The inspection needs his hands out in front while his
// mouth is still SHUT -- the jaw only opens once he has decided -- and
// the arm branch used to key entirely off the pose, so there was no way
// to have one without the other.
static void snowYeti(TFT_eSPI& t, int x, int y, uint32_t now, YPose pose, int bulge = 0,
                     bool holding = false, int holdDX = 0, int holdDY = 0) {
    // Mid grey, not off-white. At (201,206,214) he quantised to
    // (219,219,255) -- within one palette step of the snow he stands on,
    // so his whole body vanished and he read as a floating head. The
    // reference sprite is plainly grey against white for the same reason.
    const uint16_t fur = t.color565(150, 155, 168), furHi = t.color565(205, 210, 222);
    const uint16_t furS = t.color565(100, 106, 128), mane = t.color565(120, 126, 140);
    const uint16_t head = t.color565(49, 54, 63),   limb = t.color565(43, 48, 56);
    const uint16_t eye = t.color565(255, 36, 20),   tooth = t.color565(255, 210, 26);
    const uint16_t claw = t.color565(232, 237, 243);

    if (pose == YPose::DOWN) {
        // Face down in the snow, legs still going. The funniest of the
        // five and the cheapest -- it is the same parts, on their side.
        const int kick = (int)(sinf((float)now / 90.0f) * 4.0f);
        t.fillRect(x - 18, y - 12, 34, 10, fur);
        t.fillRect(x - 18, y - 12, 34, 3,  furHi);
        t.fillRect(x + 14, y - 14, 14, 9,  head);
        t.fillRect(x + 12, y - 16, 6, 4,   mane);
        t.fillRect(x - 24, y - 10, 8, 4,   limb);
        t.fillRect(x - 22, y - 20 + kick, 4, 10, limb);
        t.fillRect(x - 14, y - 22 - kick, 4, 11, limb);
        t.fillRect(x + 18, y - 6, 6, 3, tooth);
        return;
    }
    const int bob = (int)(sinf((float)now / (pose == YPose::WINDED ? 260.0f : 90.0f))
                          * (pose == YPose::WINDED ? 2.5f : 1.5f));
    const int B = y - bob;
    const int lean = (pose == YPose::WINDED) ? 4 : ((pose == YPose::RECOIL) ? -6 : 0);
    // The legs were detached, twice over. The body's six rows start at
    // B-38 and are 22 tall, so the belly ends at B-16 -- and the legs
    // started at B-13, leaving three pixels of night between his hips
    // and his torso. Worse, `lean` moved the body and not the legs, so
    // WINDED and RECOIL slid the whole torso six pixels off them.
    //
    // Legs now start at B-17, one pixel INTO the bottom body row so
    // there is no seam at any bob offset, and they carry half the lean:
    // his hips move with him, just less than his shoulders do.
    const int hip = lean / 2;
    // A gait, in opposition. Free -- it moves rectangles that were being
    // drawn anyway -- and a running character with rigid legs was the
    // other half of why he read as a cardboard cutout.
    const int gait = (pose == YPose::RUN) ? (int)(sinf((float)now / 90.0f) * 4.0f) : 0;
    t.fillRect(x - 8 + hip, B - 17, 4, 15, limb);
    t.fillRect(x + 4 + hip, B - 17, 4, 15, limb);
    // Feet swing fore and aft, and the trailing one lifts off the snow.
    t.fillRect(x - 12 + hip + gait, B - 3 - ((gait > 2) ? 1 : 0), 8, 3, limb);
    t.fillRect(x +  4 + hip - gait, B - 3 - ((gait < -2) ? 1 : 0), 8, 3, limb);
    static const int8_t  gw[6] = { 8, 10, 11, 11, 10, 8 };
    static const uint8_t gh[6] = { 3, 3, 4, 5, 4, 3 };
    int yy = B - 38;
    for (int i = 0; i < 6; i++) {
        const int extra = (bulge && i >= 1 && i <= 4) ? bulge : 0;
        t.fillRect(x - gw[i] - extra + lean, yy, (gw[i] + extra) * 2, gh[i], fur);
        yy += gh[i];
    }
    t.fillRect(x - 11 + lean, B - 33, 3, 14, furS);
    t.fillRect(x + 6 + lean,  B - 35, 4, 12, furHi);
    // ---- arms: shoulder, elbow, hand ---------------------------------
    // What was here before was two dark bars floating beside him. The arm
    // rect ran y B-40..B-28 at x-20..x-16, while the torso's widest row
    // only reaches x-11 -- a five to six pixel channel of background down
    // the whole arm, with the top two rows above the torso entirely,
    // since the body does not start until B-38. Against white snow, with
    // the arms in near-black limb and the body in pale fur, they read as
    // two objects rather than as one animal.
    //
    // The joint is now at x +/- 7, B-34, which is INSIDE the second body
    // row (that one runs -10..10 across B-35..B-32), and the upper arm is
    // drawn in FUR rather than limb, so it emerges from the creature
    // instead of being bolted to the side of it. Only the forearm goes
    // dark. The colour change does as much work here as the geometry.
    const int jy = B - 34;
    for (int8_t sd = -1; sd <= 1; sd += 2) {
        int ex, ey, hx, hy;
        if (holding || pose == YPose::EAT || pose == YPose::RECOIL) {
            // Reaching. Held rather than cycling -- this is a gesture with
            // a victim on the end of it, not a loop. holdDX/holdDY let the
            // eat sequence walk the hands from the snow up to eye level
            // and back in to the mouth.
            ex = sd * 13;          ey = -46 + holdDY / 2;
            hx = sd * 11 + 4 + holdDX; hy = -56 + holdDY;
        } else if (pose == YPose::WINDED) {
            // Hands down on his knees, barely moving.
            ex = sd * 13; ey = -30;
            hx = sd * 11; hy = -22 + (int)(sinf((float)now / 260.0f) * 1.5f);
        } else {
            // TUBE MAN. The whole arm ripples: the elbow leads and the
            // hand follows about a quarter cycle behind it. That LAG is
            // the entire reason it reads as loose rather than as merely
            // fast -- an arm whose parts all move in phase is a rigid arm
            // being waved, which is what the old swing looked like.
            //
            // The two sides run on opposite phases so he is never
            // symmetrical, which is the other half of it.
            const float ph = (float)now / 294.0f + (float)sd * 2.0f;
            const float a  = sinf(ph);
            const float b  = sinf(ph - 1.5f);
            ex = (int)((float)sd * (15.0f + a * 6.0f));
            ey = (int)(-42.0f + a * 7.0f);
            hx = (int)((float)sd * (17.0f + b * 10.0f));
            hy = (int)(-55.0f + b * 9.0f);
        }
        const int sx  = x + sd * 7 + lean, sy = jy;
        const int aex = x + ex + lean,     aey = B + ey;
        const int ahx = x + hx + lean,     ahy = B + hy;
        // Upper arm: fur, with a shaded core down the middle so it has
        // the same two-tone the rest of him does.
        t.drawWideLine(sx, sy, aex, aey, 7, fur);
        t.drawWideLine(sx, sy, aex, aey, 4, furS);
        // Forearm, dark, and a patch over the elbow so the corner between
        // the two segments does not notch.
        t.drawWideLine(aex, aey, ahx, ahy, 5, limb);
        t.fillRect(aex - 3, aey - 3, 6, 6, fur);
        t.fillRect(ahx - 3, ahy - 2, 7, 5, limb);
        for (int c = 0; c < 3; c++) {
            const int e = (c == 1) ? 1 : 0;
            t.fillRect(ahx - 4 + c * 3, ahy + 3 - e, 2, 4 + e, claw);
        }
    }
    t.fillRect(x - 11 + lean, B - 52, 22, 4, mane);
    t.fillRect(x - 13 + lean, B - 50, 3, 7, mane);
    t.fillRect(x + 10 + lean, B - 50, 3, 7, mane);
    t.fillRect(x - 10 + lean, B - 49, 20, 13, head);
    t.fillRect(x - 7 + lean, B - 45, 6, 4, eye);
    t.fillRect(x + 1 + lean, B - 45, 6, 4, eye);
    t.fillRect(x - 7 + lean, B - 46, 3, 2, eye);
    t.fillRect(x + 4 + lean, B - 46, 3, 2, eye);
    const int th = (pose == YPose::EAT) ? 7 : ((pose == YPose::WINDED) ? 6 : 4);
    t.fillRect(x - 7 + lean, B - 40, 14, th, tooth);
    for (int i = 0; i < 4; i++) t.fillRect(x - 4 + i * 3 + lean, B - 40, 1, th, head);
    if (pose == YPose::EAT) t.fillRect(x - 7 + lean, B - 40 + th / 2, 14, 1, head);
}

// He is not only the hill's any more: the pet draws him too. Two of his five
// poses are all a visit needs -- the gait he chases with, and standing about.
void drawYeti(TFT_eSPI& t, int x, int baseY, uint32_t now, YetiPose pose) {
    YPose p = YPose::WINDED;
    switch (pose) {
    case YetiPose::WALK:   p = YPose::RUN;    break;
    case YetiPose::NAP:    p = YPose::DOWN;   break;
    case YetiPose::FLINCH: p = YPose::RECOIL; break;
    case YetiPose::STAND:  break;
    }
    snowYeti(t, x, baseY, now, p);
}

// Anything the background needs drawn ON TOP of the mascot. Called by
// ui_clear after MuleSkin and the idle-event flourishes, before the title
// bar (which owns its own row and repaints it whole).
//
// Only the werewolf's bubble uses this so far. It is here rather than in
// drawFire because drawFire runs first by definition -- it is the
// background -- and a speech bubble is the one thing in a scene that
// cannot afford to be half-covered. Everything else the fire draws is
// happy to be occluded by the mascot; text is not.
void drawBackgroundOverlay(TFT_eSPI& t, uint32_t now) {
    // Stale guard, same shape as the moon's: a background switch does
    // not clear these, so an old position must not paint a ghost.
    if (s_wolfSayY < 0 || (now - s_wolfSayAt) > 250u) return;
    const int w = s_wolfSayW;
    const int yStart = s_wolfSayTop;
        auto wolfBubble = [&](const char* txt, int by, uint8_t dim, bool big) {
            t.setTextSize(big ? 2 : 1);
            const int bw = t.textWidth(txt) + (big ? 11 : 7);
            const int bh = big ? 20 : 11;
            int bx = w - 2 - bw;
            if (bx < 1)          bx = 1;
            if (by < yStart + 1) by = yStart + 1;
            const uint16_t paper = blend(BG, t.color565(236, 232, 218), dim);
            const uint16_t ink   = blend(BG, t.color565(16, 12, 10), dim);
            t.fillRect(bx, by, bw, bh, paper);
            t.drawRect(bx, by, bw, bh, ink);
            // Tail under the wolf, not under the corner of the bubble.
            int tailX = s_wolfSayX - 3;
            if (tailX < bx + 2)      tailX = bx + 2;
            if (tailX > bx + bw - 6) tailX = bx + bw - 6;
            t.drawFastHLine(tailX, by + bh,     4, paper);
            t.drawFastHLine(tailX, by + bh + 1, 2, paper);
            t.drawPixel(tailX - 1, by + bh, ink);
            t.setTextColor(ink, paper);
            t.setCursor(bx + 4, by + (big ? 4 : 2));
            t.print(txt);
            t.setTextSize(1);
        };

        if (s_wolfSayKind == 1) {
            wolfBubble("Don't Be a SKID!", s_wolfSayY, 255, false);
        } else if (s_wolfSayKind == 2) {
            // Echoes come in behind the note and step up and back as the
            // sound rolls off the valley -- drawn first so the loud one
            // sits in front of them.
            const float k = s_wolfHowlK;
            // One echo, not two. The howl puts s_wolfSayY near the top of
            // the band already, so a second one stacked above it just
            // clamped to the same line and the pair drew on top of each
            // other -- a bubble jammed under the title bar reads as a
            // fault, not as distance.
            if (k > 0.20f) wolfBubble("awooo...", s_wolfSayY - 22, 110, false);
            wolfBubble("AWOOOO!", s_wolfSayY, 255, true);
        }
    if (s_wolfSayKind == 1) {
        wolfBubble("Don't Be a SKID!", s_wolfSayY, 255, false);
    } else if (s_wolfSayKind == 2) {
        // Echoes come in behind the note and step up and back as the
        // sound rolls off the valley -- drawn first so the loud one sits
        // in front of them.
        const float k = s_wolfHowlK;
        if (k > 0.20f) wolfBubble("awooo...", s_wolfSayY - 22, 110, false);
        wolfBubble("AWOOOO!", s_wolfSayY, 255, true);
    }
}

void drawGlitchText(TFT_eSPI& t, int y, const char* text,
                    uint16_t color, uint32_t now) {
    int8_t jitter = (int8_t)((now / 150) % 3) - 1;  // -1, 0, +1
    int w = t.width();
    t.setTextSize(1);
    t.setTextColor(color, BG);
    int tw = t.textWidth(text);
    t.setCursor((w - tw) / 2 + jitter * 2, y);
    t.print(text);
}

void drawTransitionGlitch(TFT_eSPI& t, uint32_t elapsedMs, uint32_t totalMs) {
    if (elapsedMs >= totalMs) return;
    int w = t.width();
    int h = t.height();
    float fade = 1.0f - (float)elapsedMs / (float)totalMs;
    int bands = 2 + (int)(fade * 5);

    static const int MAX_W = 400;
    static uint16_t rowBuf[MAX_W];
    int useW = (w < MAX_W) ? w : MAX_W;

    for (int i = 0; i < bands; i++) {
        int maxY = (h > 3) ? h - 3 : 1;
        int by   = random(0, maxY);
        int bh   = 1 + random(0, 2);
        int xoff = random(-10, 11);
        if (xoff == 0) xoff = 4;
        for (int row = 0; row < bh && (by + row) < h; row++) {
            int y = by + row;
            for (int x = 0; x < useW; x++) rowBuf[x] = t.readPixel(x, y);
            for (int x = 0; x < useW; x++) {
                int sx = x - xoff;
                if (sx < 0) sx = 0;
                if (sx >= useW) sx = useW - 1;
                t.drawPixel(x, y, rowBuf[sx]);
            }
        }
    }
    if (random(0, 3) == 0) {
        t.drawFastHLine(0, random(0, h), w, blend(BG, WHITE, (uint16_t)(fade * 200)));
    }
}

static const BangersFont::Glyph* bangersFind(char c, BangersSize size) {
    const BangersFont::Glyph* table = (size == BangersSize::LG) ? BangersFont::LG_GLYPHS
                                    : (size == BangersSize::XL) ? BangersFont::XL_GLYPHS : BangersFont::MD_GLYPHS;
    uint8_t count = (size == BangersSize::LG) ? BangersFont::LG_GLYPH_COUNT
                  : (size == BangersSize::XL) ? BangersFont::XL_GLYPH_COUNT : BangersFont::MD_GLYPH_COUNT;
    for (uint8_t i = 0; i < count; i++) {
        if (table[i].ch == c) return &table[i];
    }
    return nullptr;
}

int bangersTextWidth(const char* s, BangersSize size) {
    int w = 0;
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (g) w += g->advance;
    }
    return w;
}

// Random brief "signal corruption" glitch on Bangers headline text --
// a whole-text x-jitter plus scattered horizontal scanline dropouts,
// refreshed every ~40ms during a short burst that fires every 5-10s.
// Deterministic from `now` (a cheap multiplicative hash, not repeated
// random() calls) rather than rolling fresh dice per row: several
// callers redraw the same string many times per frame at tiny offsets
// to backfill a solid-color outline behind the real fill color (see
// ui_alert.cpp/ui_watchalert.cpp) -- if each of those passes rolled
// its own random jitter they'd all land differently and the outline
// would smear apart from the fill instead of tearing together like
// one corrupted signal.
static uint32_t s_glitchNextAt   = 0;
static uint32_t s_glitchUntil    = 0;
static uint8_t  s_glitchLevel    = 1;   // 0..4, see triggerGlitchBurst()'s comment

// Per-level tuning -- index is the clamped 0..4 intensity. Deliberately
// NOT a smooth curve: level 3 is where a full-screen tear (see
// drawGlitchStatic()) joins in on top of everything else, so levels 3
// and 4 jump harder than the 0->1->2 ramp does.
static const uint32_t BURST_MS_BY_LEVEL[]    = { 150, 200, 260, 320, 420 };
static const int      SPECKLE_N_BY_LEVEL[]   = {  60, 110, 170, 240, 320 };
static const int      JITTER_MAX_BY_LEVEL[]  = {   1,   2,   3,   4,   5 };
static const int      DROPOUT_MOD_BY_LEVEL[] = {  10,   6,   4,   3,   2 };  // 1-in-N rows drop

static uint32_t glitchHash(uint32_t x) {
    x *= 2654435761u;
    x ^= x >> 15;
    return x;
}

// Advances the shared burst timer and reports whether `now` falls
// inside one. Safe to call more than once for the same `now` (e.g.
// once from drawGlitchStatic() and again from several drawBangersText()
// calls within one frame) -- once the first call arms a burst,
// `now < s_glitchUntil` makes every later call in that same instant
// see it's already armed instead of re-rolling.
static bool updateGlitchState(uint32_t now) {
    if (s_glitchNextAt == 0) s_glitchNextAt = now + (uint32_t)random(5000, 10001);
    if (now >= s_glitchNextAt && now >= s_glitchUntil) {
        // Ambient, nobody-asked-for-it bursts always stay mild (level
        // 1) so idle screens read as consistent flavor, not a ramping
        // spectacle -- only an explicit triggerGlitchBurst() call asks
        // for something louder.
        s_glitchLevel  = 1;
        s_glitchUntil  = now + BURST_MS_BY_LEVEL[s_glitchLevel];
        s_glitchNextAt = s_glitchUntil + (uint32_t)random(5000, 10001);
    }
    return now < s_glitchUntil;
}

bool glitchActive() {
    return updateGlitchState(millis());
}

void triggerGlitchBurst(uint8_t intensity) {
    if (intensity > 4) intensity = 4;
    uint32_t now = millis();
    s_glitchLevel  = intensity;
    s_glitchUntil  = now + BURST_MS_BY_LEVEL[intensity];
    s_glitchNextAt = s_glitchUntil + (uint32_t)random(5000, 10001);
}

// Real per-frame TV-static snow, not the deterministic per-bucket
// jitter drawBangersText() uses -- this has no multi-pass outline to
// stay in sync with, so a fresh random() scatter every call (i.e.
// every frame it's active) gives the authentic flickering-snow look
// instead of a held static pattern.
void drawGlitchStatic(TFT_eSPI& t, int x0, int y0, int x1, int y1) {
    if (!glitchActive()) return;
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    static const uint16_t SPECKLE_COLORS[] = {
        WHITE, CYAN, VAPOR_PINK, VAPOR_PURPLE, VAPOR_BLUE, PURPLE,
    };
    int speckleN = SPECKLE_N_BY_LEVEL[s_glitchLevel];
    for (int i = 0; i < speckleN; i++) {
        int sx = x0 + random(0, rw);
        int sy = y0 + random(0, rh);
        uint16_t col = SPECKLE_COLORS[random(0, 6)];
        if (random(0, 3) == 0) t.drawFastHLine(sx, sy, 2, col);
        else                   t.drawPixel(sx, sy, col);
    }
    // The big payoff at the top two levels -- a genuine pixel-shifted
    // screen tear layered on top of the speckle/text glitch already
    // drawn this frame, reusing the same tear drawTransitionGlitch()
    // already does for screen-change transitions (fade=1.0 at level 4,
    // a smaller half-strength tear at level 3) rather than a second
    // bespoke tear implementation.
    if (s_glitchLevel >= 3) {
        drawTransitionGlitch(t, (s_glitchLevel >= 4) ? 0 : 50, 100);
    }
}

// One actual render pass -- shared by the real draw and the ghost copy
// below so both respect the exact same per-row dropout decisions
// (same bucket/row inputs) and tear together instead of independently.
static void drawBangersPass(TFT_eSPI& t, const char* s, int x, int y, uint16_t color,
                             BangersSize size, bool glitching, uint32_t bucket, uint8_t level) {
    int cursorX = x;
    int dropoutMod = DROPOUT_MOD_BY_LEVEL[level];
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (!g) continue;
        if (g->bitmap) {
            int rowBytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
                // Same dropout decision for a given (bucket, row) no
                // matter which glyph or which pass is drawing it, so a
                // dropped scanline tears across the whole word at once
                // instead of a random per-letter speckle.
                if (glitching && (glitchHash(bucket * 131u + row) % dropoutMod) == 0) continue;
                const uint8_t* rowPtr = g->bitmap + row * rowBytes;
                int runStart = -1;
                for (int col = 0; col <= g->w; col++) {
                    bool bit = false;
                    if (col < g->w) {
                        uint8_t byte = rowPtr[col / 8];
                        bit = (byte >> (7 - (col % 8))) & 1;
                    }
                    if (bit && runStart < 0) runStart = col;
                    if (!bit && runStart >= 0) {
                        t.drawFastHLine(cursorX + g->xoff + runStart, y + g->yoff + row, col - runStart, color);
                        runStart = -1;
                    }
                }
            }
        }
        cursorX += g->advance;
    }
}

void drawBangersSteady(TFT_eSPI& t, int x, int y, const char* s, uint16_t color, BangersSize size) {
    drawBangersPass(t, s, x, y, color, size, false, 0, 0);
}

void drawBangersText(TFT_eSPI& t, int x, int y, const char* s, uint16_t color, BangersSize size) {
    uint32_t now = millis();
    bool glitching = updateGlitchState(now);
    uint8_t level = s_glitchLevel;
    uint32_t bucket = now / 40;
    int jitterMax = JITTER_MAX_BY_LEVEL[level];
    int jitterX = glitching ? (int)(glitchHash(bucket) % (2 * jitterMax + 1)) - jitterMax : 0;

    // Chromatic-split ghost -- a faint offset copy in a contrasting
    // color, drawn first so the real pass paints over/beside it.
    // Skipped for BLACK: that's the color the outline trick
    // (ui_clear.cpp/ui_watchalert.cpp) uses for its 24-pass solid
    // backing behind the one real colored pass that follows -- ghosting
    // each of those 24 would be wasted work and visual mud, not fringe.
    // Offset grows with level too, so the fringe visibly widens along
    // with everything else instead of staying a fixed 2px at any
    // intensity.
    if (glitching && color != BLACK) {
        uint16_t ghostColor = blend(CYAN, VAPOR_PINK, (uint16_t)(glitchHash(bucket + 7) % 256));
        int ghostOfs = 2 + level;
        drawBangersPass(t, s, x + jitterX + ghostOfs, y - 1, ghostColor, size, glitching, bucket, level);
    }

    drawBangersPass(t, s, x + jitterX, y, color, size, glitching, bucket, level);
}

// The outline behind a headline, in one pass instead of twenty-four.
//
// The 24-pass trick draws the same text at every offset in a 5x5 square but
// the centre, each pass scanning every glyph bitmap bit by bit and drawing
// every ink run again. NEARBY at LG that way was 14.8 ms of every frame on
// the main screen -- measured -- which is as much as the whole animated
// background and nearly as much as pushing the frame to the panel.
//
// Same picture, one scan: every ink run becomes one block, the run widened
// by `radius` each side and `radius` rows above and below. The union of
// those blocks IS the union of the 24 offsets (the one pixel the offsets
// miss, the run's own, the colour pass then paints over in both versions),
// so the result is identical to the pixel -- checked against the emulator's
// renders, not asserted.
//
// The glitch is reproduced, not skipped: same jitter, same dropped rows,
// from the same hash of the same bucket the colour pass will use, so a
// burst tears the outline and the fill together the way it did before.
void drawBangersOutline(TFT_eSPI& t, int x, int y, const char* s, uint16_t color,
                        BangersSize size, uint8_t radius) {
    uint32_t now = millis();
    bool glitching = updateGlitchState(now);
    uint8_t level = s_glitchLevel;
    uint32_t bucket = now / 40;
    int jitterMax = JITTER_MAX_BY_LEVEL[level];
    int jitterX = glitching ? (int)(glitchHash(bucket) % (2 * jitterMax + 1)) - jitterMax : 0;
    int dropoutMod = DROPOUT_MOD_BY_LEVEL[level];
    const int r = radius;
    const int tall = 2 * r + 1;

    int cursorX = x + jitterX;
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (!g) continue;
        if (g->bitmap) {
            int rowBytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
                if (glitching && (glitchHash(bucket * 131u + row) % dropoutMod) == 0) continue;
                const uint8_t* rowPtr = g->bitmap + row * rowBytes;
                int runStart = -1;
                for (int col = 0; col <= g->w; col++) {
                    bool bit = false;
                    if (col < g->w) {
                        uint8_t byte = rowPtr[col / 8];
                        bit = (byte >> (7 - (col % 8))) & 1;
                    }
                    if (bit && runStart < 0) runStart = col;
                    if (!bit && runStart >= 0) {
                        t.fillRect(cursorX + g->xoff + runStart - r, y + g->yoff + row - r,
                                   (col - runStart) + 2 * r, tall, color);
                        runStart = -1;
                    }
                }
            }
        }
        cursorX += g->advance;
    }
}

// Ported from muleskin.cpp verbatim (was a private static there,
// duplicated for LOG's MORE INFO panel until this promotion) -- see
// its declaration in theme.h for the full behavior notes.
//
// lines[][48], not [40]: confirmed on real hardware that a wide-enough
// maxW (a caller with real screen width to spare) let a line accumulate
// several words -- each individually well under maxW in pixels -- past
// the buffer's own char capacity before the width check ever tripped,
// silently truncating mid-word ("...Amazon's vid" instead of "video").
// The strlen() check added below is the actual fix (forces a break the
// moment the buffer itself would fill, independent of maxW); the wider
// buffer just means that happens less often in the first place.
uint8_t wrapText(TFT_eSPI& t, const char* text, int maxW,
                 char lines[][48], uint8_t maxLines) {
    // 320, not the original 160 -- fine for every short quip/bubble
    // this ran on originally, but LOG's MORE INFO panel passes real
    // paragraph-length explanations (the RSSI/confidence primer alone
    // is ~290 chars), which strncpy was silently truncating before a
    // single word ever got wrapped.
    char buf[320];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    uint8_t n = 0;
    char lineBuf[48] = "";
    char* word = strtok(buf, " ");
    while (word) {
        char trial[48];
        if (lineBuf[0]) snprintf(trial, sizeof(trial), "%s %s", lineBuf, word);
        else            snprintf(trial, sizeof(trial), "%s", word);
        bool tooWide = lineBuf[0] &&
                       (t.textWidth(trial) > maxW || strlen(trial) >= sizeof(lineBuf) - 1);
        if (tooWide) {
            if (n >= maxLines - 1) break; // out of lines -- let the rest go rather than drop it silently
            strncpy(lines[n], lineBuf, sizeof(lines[n]) - 1); lines[n][sizeof(lines[n]) - 1] = 0; n++;
            strncpy(lineBuf, word, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        } else {
            strncpy(lineBuf, trial, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        }
        word = strtok(nullptr, " ");
    }
    if (lineBuf[0] && n < maxLines) {
        strncpy(lines[n], lineBuf, sizeof(lines[n]) - 1); lines[n][sizeof(lines[n]) - 1] = 0; n++;
    }
    return n;
}

// Info panel geometry -- see theme.h's comment on drawInfoPanel() for
// what this is/who uses it.
static const uint8_t INFO_MAX_LINES = 7;

static void infoRects(int screenW, int screenH,
                       int& px, int& py, int& pw, int& ph,
                       int& headingY,
                       int& muleskinCx, int& muleskinBaseY, float& muleskinScale, int& muleskinWander,
                       int& textTop, int& textMaxW,
                       int& btnX, int& btnY, int& btnW, int& btnH) {
    // As much of the screen as the panel can reasonably use -- the
    // description text (size 1, see drawInfoPanel()) still needs real
    // room, and a small-margin modal reads fine here since it's the
    // only thing on screen while it's up.
    pw = screenW - 16;
    if (pw > 300) pw = 300;
    ph = screenH - 8;
    if (ph > 260) ph = 260;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;

    headingY = py + 6;
    muleskinCx = px + pw / 2;
    muleskinScale = 0.78f;
    muleskinBaseY = py + 88;
    muleskinWander = pw / 2 - 40;
    if (muleskinWander < 0) muleskinWander = 0;
    textTop = py + 104;
    textMaxW = pw - 16;

    btnH = 26;
    btnW = pw - 20;
    btnX = px + 10;
    btnY = py + ph - btnH - 8;
}

bool infoPanelHitDismiss(int x, int y, int screenW, int screenH) {
    int px, py, pw, ph, headingY, muleskinCx, muleskinBaseY, muleskinWander, textTop, textMaxW, btnX, btnY, btnW, btnH;
    float muleskinScale;
    infoRects(screenW, screenH, px, py, pw, ph, headingY, muleskinCx, muleskinBaseY, muleskinScale, muleskinWander,
              textTop, textMaxW, btnX, btnY, btnW, btnH);
    return x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH;
}

void drawInfoPanel(TFT_eSPI& t, int w, int h, uint32_t now,
                   const char* typeName, const char* text) {
    int px, py, pw, ph, headingY, muleskinCx, muleskinBaseY, muleskinWander, textTop, textMaxW, btnX, btnY, btnW, btnH;
    float muleskinScale;
    infoRects(w, h, px, py, pw, ph, headingY, muleskinCx, muleskinBaseY, muleskinScale, muleskinWander,
              textTop, textMaxW, btnX, btnY, btnW, btnH);

    t.fillRoundRect(px, py, pw, ph, 6, BG);
    t.drawRoundRect(px, py, pw, ph, 6, PURPLE);

    // No heading during the one-time RSSI/confidence primer page --
    // typeName is null then since that page isn't about any one type.
    if (typeName) {
        int tw = bangersTextWidth(typeName, BangersSize::MD);
        int maxTw = pw - 16;
        if (tw > maxTw) tw = maxTw; // clipped, not shrunk -- every real type name fits comfortably as-is
        drawBangersText(t, px + (pw - tw) / 2, headingY, typeName, VAPOR_PINK, BangersSize::MD);
    }

    MuleSkin::drawWaving(t, muleskinCx, muleskinBaseY, now, muleskinScale, nullptr, true, muleskinWander);

    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(WHITE, BG);
    char lines[INFO_MAX_LINES][48];
    uint8_t n = wrapText(t, text, textMaxW, lines, INFO_MAX_LINES);
    int ly = textTop;
    for (uint8_t i = 0; i < n; i++) {
        int lw = t.textWidth(lines[i]);
        t.setCursor(px + (pw - lw) / 2, ly);
        t.print(lines[i]);
        ly += 12;
    }

    drawButton(t, btnX, btnY, btnW, btnH, "[ GOT IT ]", false, 2);
}

}  // namespace Theme
