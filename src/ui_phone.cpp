// The payphone's PIN pad -- the lock screen's keypad, and where a PIN is set.
//
// Twelve keys on a steel handset rather than a keyboard: on a 320px screen
// that means targets of 48x30, and fewer, larger targets on a resistive panel
// is the difference between typing and fighting. Digits only; the PIN submits
// itself the instant the last dot lands.
#include "ui_phone.h"

#include "theme.h"
#include "settings.h"
#include <Arduino.h>   // millis(), for the wrong-guess shake
#include <stdio.h>
#include <string.h>

namespace {

// The Win95 ramp, chosen for the IGNORE button because it survives RGB332
// where the authentic #c0c0c0 / #dfdfdf pair collapses into one colour. A
// steel payphone body inherits that solved problem for nothing.
const uint16_t STEEL_LT = Theme::W95_LIGHT;
const uint16_t STEEL    = Theme::W95_FACE;
const uint16_t STEEL_SH = Theme::W95_SHADOW;
const uint16_t STEEL_DK = Theme::W95_DKSHADOW;

const int UY = 8, UW = 164, UH = 224;
// The panel size the handset was last DRAWN at. The tap handler is given a
// point and nothing else, and the case is centred on the panel, so it learns
// the width from the drawing. Safe because a screen is always drawn before it
// can be touched.
int s_panelW = 320, s_panelH = 240;
// 78 was (320 - 164) / 2 -- centred by hand for a 320-wide panel. The 3.5"
// centres from its real width, by its long side, so it holds in both
// rotations; the 2.8" stays where it always was.
inline int caseX() {
    const bool bigPanel = (s_panelW >= 400 || s_panelH >= 400);
    return bigPanel ? (s_panelW - UW) / 2 : 78;
}
const int KW = 48, KH = 30, KGAP = 3;
inline int keysX() { return caseX() + (UW - (KW * 3 + KGAP * 2)) / 2; }
const int KY = UY + 60;

const char* const KEY_D[12] = { "1","2","3","4","5","6","7","8","9","*","0","#" };

// How many digits make a full PIN, whether BACK exists, the line above the
// dots, and whether a full PIN is sitting ready to be read.
uint8_t     s_pinLen      = 4;
bool        s_pinBack     = false;
const char* s_pinPrompt   = "";
bool        s_pinReady    = false;
const char* s_pinWaitMsg  = nullptr;   // lockout banner, shown instead of dots
uint32_t    s_pinShakeAt  = 0;         // a wrong-PIN shake
bool        s_pinForgot   = false;     // FORGOT offered (the lock screen)
uint8_t     s_forgotTaps  = 0;
uint32_t    s_forgotAt    = 0;
bool        s_pinForgotHit = false;
const uint32_t FORGOT_ARM_MS = 5000;

char     s_buf[16];
uint8_t  s_len   = 0;
int      s_backY = 0;     // filled by the draw, read by the hit test
bool     s_done  = false;

inline void bevel(TFT_eSPI& t, int x, int y, int w, int h, uint16_t face,
                  uint16_t lit, uint16_t litSoft, uint16_t shd, uint16_t shdSoft, bool sunk) {
    Theme::drawBevel(t, x, y, w, h, face, lit, litSoft, shd, shdSoft, sunk);
}

inline void steel(TFT_eSPI& t, int x, int y, int w, int h, bool sunk = false) {
    Theme::drawSteelPanel(t, x, y, w, h, sunk);
}

} // namespace

// The handset occupies x 78..242, so the strip down the left of the screen is
// free at any rotation. Bottom-left because that is where every other screen
// in this firmware puts BACK. Wider on a big panel, where the size-2 label
// needs the room.
static const int BX = 4;
static inline int BW_() { return s_panelW >= 400 ? 132 : 68; }
static const int BH = 26;
static int backY(int screenH) { return screenH - BH - 6; }

bool uiPhoneDone() { return s_done; }

void uiPhoneInitPin(TFT_eSPI& t, uint8_t len, const char* prompt, bool allowBack) {
    (void)t;
    if (len >= sizeof s_buf) len = sizeof s_buf - 1;
    s_pinLen     = len;
    s_pinBack    = allowBack;
    s_pinPrompt  = prompt ? prompt : "";
    s_pinReady   = false;
    s_pinWaitMsg = nullptr;
    s_pinShakeAt = 0;
    s_pinForgot  = false;
    s_forgotTaps = 0;
    s_pinForgotHit = false;
    s_len    = 0;
    s_buf[0] = '\0';
    s_done   = false;
}
bool        uiPhonePinReady()  { return s_pinReady; }
const char* uiPhonePinDigits() { return s_buf; }
void        uiPhonePinReject() { s_len = 0; s_buf[0] = '\0'; s_pinReady = false; s_done = false; s_pinShakeAt = millis(); }
void        uiPhonePinPrompt(const char* p) { s_pinPrompt = p ? p : ""; }
void        uiPhonePinAllowForgot(bool a) { s_pinForgot = a; s_forgotTaps = 0; s_pinForgotHit = false; }
bool        uiPhonePinForgot() { return s_pinForgotHit; }
static bool forgotArmed(uint32_t now) { return s_pinForgot && s_forgotTaps == 1 && now - s_forgotAt < FORGOT_ARM_MS; }
void        uiPhonePinWait(const char* m) { s_pinWaitMsg = m; if (m) { s_len = 0; s_buf[0] = '\0'; } }

// A tap on the PIN pad: digits fill the dots, DEL rubs one out, and the PIN
// submits itself the instant the last dot lands -- no OK to hunt for. During a
// lockout wait nothing is accepted.
static void pinTouch(int x, int y, uint32_t now) {
    // FORGOT works during a lockout wait too: that is exactly when somebody
    // who has forgotten the PIN is standing there.
    if (s_pinForgot && x >= BX && x <= BX + BW_() && y >= s_backY && y <= s_backY + BH) {
        if (forgotArmed(now)) { s_pinForgotHit = true; s_pinReady = false; s_done = true; }
        else                  { s_forgotTaps = 1; s_forgotAt = now; }
        return;
    }
    if (s_pinWaitMsg) return;
    if (s_pinBack && x >= BX && x <= BX + BW_() && y >= s_backY && y <= s_backY + BH) {
        s_pinReady = false;
        s_done = true;
        return;
    }
    for (int i = 0; i < 12; i++) {
        const int kx = keysX() + (i % 3) * (KW + KGAP);
        const int ky = KY + (i / 3) * (KH + KGAP);
        if (x < kx || x > kx + KW || y < ky || y > ky + KH) continue;
        if (i == 9) { if (s_len) s_buf[--s_len] = '\0'; return; }   // DEL
        char d = 0;
        if (i <= 8)       d = (char)('1' + i);      // 1..9
        else if (i == 10) d = '0';                  // 0
        else return;                                // # unused
        if (s_len < s_pinLen) { s_buf[s_len++] = d; s_buf[s_len] = '\0'; }
        if (s_len == s_pinLen) { s_pinReady = true; s_done = true; }
        return;
    }
}

void uiPhoneTouch(int x, int y, uint32_t now, PhoneTouch phase) {
    if (phase == PhoneTouch::DOWN) pinTouch(x, y, now);
}

// Dots where a readout would be, a keypad of bare digits.
void uiPhoneTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();
    s_panelW = w; s_panelH = h;
    Theme::Palette saved = Theme::dimPaletteForOverlay(120);
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 140);
    s_backY = backY(h);

    const int ux = caseX(), uy = UY;
    t.fillRect(ux + 4, uy + 5, UW, UH, Theme::BLACK);
    steel(t, ux, uy, UW, UH);

    // Prompt -- or, with FORGOT armed, what a second tap will do.
    const bool armed = forgotArmed(now);
    const char* pr = armed ? "TAP AGAIN: WIPE + UNLOCK" : s_pinPrompt;
    t.setTextSize(1);
    t.setTextColor(armed ? Theme::RED : STEEL_LT);
    t.setCursor(ux + (UW - t.textWidth(pr)) / 2, uy + 8);
    t.print(pr);

    // The dots, or the wait banner in their place. A shake nudges them for a
    // moment after a wrong PIN.
    const int dY = uy + 22, dH = 26;
    steel(t, ux + 9, dY - 3, UW - 18, dH + 6, true);
    t.fillRect(ux + 12, dY, UW - 24, dH, Theme::BLACK);
    if (s_pinWaitMsg) {
        t.setTextColor(Theme::RED);
        t.setTextSize(1);
        t.setCursor(ux + (UW - t.textWidth(s_pinWaitMsg)) / 2, dY + (dH - 8) / 2);
        t.print(s_pinWaitMsg);
    } else {
        int shake = 0;
        if (s_pinShakeAt && now - s_pinShakeAt < 300) shake = ((now / 40) % 2) ? 3 : -3;
        const int gap = 18, tot = (s_pinLen - 1) * gap;
        int cx = ux + UW / 2 - tot / 2 + shake, cy = dY + dH / 2;
        for (uint8_t i = 0; i < s_pinLen; i++) {
            const bool filled = i < s_len;
            if (filled) t.fillCircle(cx + i * gap, cy, 4, Theme::GREEN);
            else        t.drawCircle(cx + i * gap, cy, 4, STEEL_LT);
        }
    }

    for (int i = 0; i < 12; i++) {
        const int kx = keysX() + (i % 3) * (KW + KGAP);
        const int ky = KY + (i / 3) * (KH + KGAP);
        const char* lab = (i == 9) ? "DEL" : (i <= 8) ? KEY_D[i] : (i == 10) ? "0" : "";
        if (!lab[0]) continue;                       // # left blank
        bevel(t, kx, ky, KW, KH, Theme::TASKBAR, STEEL_LT, STEEL, STEEL_DK, STEEL_SH, false);
        t.setTextSize(2);
        if (t.textWidth(lab) > KW - 6) t.setTextSize(1);
        t.setTextColor(Theme::WHITE);
        t.setCursor(kx + (KW - t.textWidth(lab)) / 2, ky + (KH - t.fontHeight()) / 2);
        t.print(lab);
    }

    if (s_pinBack)
        Theme::drawButton(t, BX, s_backY, BW_(), BH, "[ BACK ]", false);
    else if (s_pinForgot)
        Theme::drawButton(t, BX, s_backY, BW_(), BH, armed ? "[ WIPE? ]" : "[ FORGOT ]", armed);
}
