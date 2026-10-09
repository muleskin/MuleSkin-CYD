// MuleSkin-CYD — the payphone's PIN pad: the lock screen's keypad, and where a
// PIN is set. Digits only.
#pragma once
#include <TFT_eSPI.h>
#include "detection.h"

// `advance` is false on the second of the 3.5"'s two band passes -- the
// same frame drawn again -- so anything that steps by the call rather
// than by the clock must sit still for it. Other boards draw once.
void uiPhoneTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);

// The edges of a touch; the pad acts on the press and ignores the rest.
enum class PhoneTouch : uint8_t { DOWN, MOVE, UP };
void uiPhoneTouch(int x, int y, uint32_t now, PhoneTouch phase);

bool uiPhoneDone();

// ---- PIN entry ----
// The same payphone, digits only, for the lock. `len` dots to fill; `prompt`
// is the short line above them ("ENTER PIN", "SET A PIN", "AGAIN"...). When
// allowBack is false there is no way out but the right digits -- the lock
// screen. It reports through uiPhoneDone(): uiPhonePinReady() true means `len`
// digits were entered (read them with uiPhonePinDigits()), false means BACK.
void        uiPhoneInitPin(TFT_eSPI& t, uint8_t len, const char* prompt, bool allowBack);
bool        uiPhonePinReady();
const char* uiPhonePinDigits();
// A wrong-PIN shake and clear, driven by the caller; and the wait banner shown
// during lockout instead of the dots.
void        uiPhonePinReject();
void        uiPhonePinWait(const char* msg);   // nullptr clears it
// The line above the dots, changeable while the pad is up.
void        uiPhonePinPrompt(const char* prompt);
// The lock screen's way out for a forgotten PIN: a FORGOT button where BACK
// would be. Two taps within five seconds -- the first says what it will do --
// and uiPhoneDone() comes back with uiPhonePinForgot() true.
void        uiPhonePinAllowForgot(bool allow);
bool        uiPhonePinForgot();
