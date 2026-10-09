// MuleSkin-CYD — boot splash implementation
#include "ui_boot.h"
#include "theme.h"
#include "settings.h"
#include <cstdio>
#include "version_cmp.h"

// Stamped in by extra_script.py from `git describe` at build time --
// same macro the Diary screen already reads (see its own guard
// comment). Falls back to "unknown" so this still compiles standalone
// (the PC emulator, an IDE's syntax pass) without the build flag.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

// When this splash started, and how far through its scripted glitch
// bursts we are. The subtitle's chromatic split rides the shared burst
// (Theme::glitchActive()), but that rolls on its own every 5-10s while
// the splash only lasts 3000ms -- so left to the ambient schedule the
// effect would usually never fire during a boot at all. Driving the
// same shared burst deliberately is what triggerGlitchBurst() is for.
static uint32_t s_bootAt      = 0;
static uint8_t  s_glitchStage = 0;

void uiBootInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants when t is a sprite. fillRect() with the (virtual,
    // correctly-overridden) width()/height() clears the whole thing.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_bootAt      = 0;
    s_glitchStage = 0;
}

void uiBootTick(TFT_eSPI& t, uint32_t now) {
    if (s_bootAt == 0) s_bootAt = now ? now : 1;
    const uint32_t bootEl = now - s_bootAt;
    // Two bursts inside the 3s window: one early enough to be seen, one
    // late enough to feel like the thing is still settling. Intensities
    // stay mild -- 3 and up add a full-screen pixel-shift tear, which
    // on a splash reads as a fault rather than as style.
    if (s_glitchStage == 0 && bootEl > 500)  { Theme::triggerGlitchBurst(2); s_glitchStage = 1; }
    if (s_glitchStage == 1 && bootEl > 1750) { Theme::triggerGlitchBurst(1); s_glitchStage = 2; }

    int w = t.width();
    int h = t.height();

    // The MULESKIN background -- the hooded MuleSkin at his computer, ears
    // twitching -- behind the whole splash, the same picture the main screen
    // opens onto. Redrawn every frame (the twitch moves); the text below is
    // drawn with a transparent background so the picture shows around every
    // glyph.
    Theme::drawArtworkBackground(t, now, 0, h);

    // Big title, Bangers comic-impact font — landscape-only screen
    // (boot always starts at rotation 1) so the width is never tight.
    // Capitals: the Bangers subset is A-Z, 0-9 and a few marks, and
    // drawBangersText skips any glyph it lacks -- "MuleSkin" drew as "MS".
    const char* title = "MULESKIN";
    int tw = Theme::bangersTextWidth(title, Theme::BangersSize::LG);
    int tx = (w - tw) / 2;
    // 3px black outline. The title sits over a picture, and pink needs
    // separating from whatever is behind it. Drawn as a dilation -- the same glyphs at every offset
    // inside a radius-3 disc -- rather than a rectangular halo, so the
    // stroke follows the letterforms instead of boxing them.
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx * dx + dy * dy > 9 || (dx == 0 && dy == 0)) continue;
            Theme::drawBangersText(t, tx + dx, 8 + dy, title, Theme::BLACK, Theme::BangersSize::LG);
        }
    }
    Theme::drawBangersText(t, tx, 8, title, Theme::VAPOR_PINK, Theme::BangersSize::LG);

    // gradient line — moved up right under the title (the v1.0 line
    // used to sit here; removed so MuleSkin below gets that room too).
    for (int x = 0; x < w; x++) {
        t.drawFastHLine(x, 46, 1, Theme::titlebarColor(x, w));
    }

    // The release under the MULESKIN wordmark, as "V.3.1.1": taken from
    // FIRMWARE_VERSION (the git tag, stamped at build time) so it can never
    // drift from the version the board reports. Anything past the x.y.z --
    // "-3-g554330d" after a tag, "-dirty" for local edits -- is left off;
    // a build with no tag at all shows what git describe gave it.
    //
    // Chromatic: a white core with cyan and magenta copies split either
    // side, over a hard black drop shadow. The shadow replaces the old
    // 8-way outline and does the same job -- purple over the sun's
    // oranges had almost no separation -- but reads as depth rather
    // than as a sticker.
    //
    // The split rides Theme::glitchActive(), the shared burst the
    // Bangers headings already roll every 5-10s, rather than running a
    // timer of its own. That is the whole point of that flag being
    // exposed: when the wordmark above corrupts, this corrupts with it
    // and the two read as one event instead of two things that happen
    // to twitch near each other.
    //
    // Deliberately NOT built with setViewport, which is the obvious way
    // to slice a proper tear: real TFT_eSPI moves the drawing origin
    // when a viewport is set (vpDatum defaults to true) while the
    // emulator shim ignores that argument and treats it as a pure clip
    // rectangle. Anything built on it would look correct in one and
    // wrong on the other, and the emulator is where this gets checked.
    t.setTextSize(2);
    char sub[24];
    Version::label(sub, sizeof sub, FIRMWARE_VERSION);   // version_cmp.cpp, tested
    const int sw = t.textWidth(sub);
    const int sx = (w - sw) / 2;
    const int sy = 54;

    const bool glitch = Theme::glitchActive();
    const int  split  = glitch ? 3 + (int)random(0, 3) : 1;
    const int  jitter = glitch ? (int)random(-2, 3) : 0;

    // Shadow first, so everything else sits on top of it. Two pixels
    // down and right: enough to lift the word off the sunset without
    // the gap reading as a second, blurrier copy of the text.
    t.setTextColor(Theme::BLACK);
    t.setCursor(sx + 2, sy + 2);
    t.print(sub);

    // Magenta trails right, cyan leads left -- the direction a
    // mistracked CRT actually smears.
    t.setTextColor(Theme::PINK);
    t.setCursor(sx + split + jitter, sy);
    t.print(sub);
    t.setTextColor(Theme::CYAN);
    t.setCursor(sx - split + jitter, sy);
    t.print(sub);

    // Burst only: one more cyan copy dropped a row or two, like a scan
    // that failed to land. Kept to a couple of rows so it reads as a
    // fault and not as a second line of text.
    if (glitch) {
        t.setTextColor(Theme::CYAN);
        t.setCursor(sx + jitter * 2, sy + 1 + (int)random(0, 2));
        t.print(sub);
    }

    // White core last, so the word stays legible whatever the copies
    // are doing around it.
    t.setTextColor(Theme::WHITE);
    t.setCursor(sx + jitter, sy);
    t.print(sub);

    // Snow over the subtitle's own box. A no-op while glitchActive() is
    // false, so this is safe to call every frame.
    Theme::drawGlitchStatic(t, sx - 4, sy - 2, sx + sw + 4, sy + 18);

    // INITIALIZING...  vX.Y.Z -- version tacked onto this line rather
    // than given its own row. Everything from the subtitle down to
    // here is already tightly packed around MuleSkin's cameo (see its
    // own comment above about the room dropping the old standalone
    // version line bought him), and this is the one line on the splash
    // that already reads as status text, not brand/character content,
    // so it's the natural place for a version stamp without touching
    // his space.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN);
    char init[56];   // room for a full "git describe --dirty" string, not just a bare tag
#if defined(CYD35)
    // The 3.5" ships as a beta and says so on every boot. That board is newer
    // than the rest of the port by years, it is the only one that draws in two
    // bands, and it is the one most likely to still surprise somebody -- so
    // the word goes where nobody can miss it rather than in a release note
    // they may never have read.
    snprintf(init, sizeof(init), "3.5in BETA   %s", FIRMWARE_VERSION);
#else
    snprintf(init, sizeof(init), "INITIALIZING...  %s", FIRMWARE_VERSION);
#endif
    int iw = t.textWidth(init);
    t.setCursor((w - iw) / 2, h - 16);
#if defined(CYD35)
    t.setTextColor(Theme::AMBER);   // not the cyan the rest of the line uses
#endif
    t.print(init);

    // animated scanline sweeping top to bottom every 600 ms
    int phase = (int)((now / 600) % (uint32_t)h);
    Theme::drawScanline(t, phase, Theme::VAPOR_PURPLE);
}

bool uiBootDone(uint32_t startMs, uint32_t holdMs) {
    return (millis() - startMs) >= holdMs;
}
