// MuleSkin-CYD — "clear" (idle) screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiClearInit(TFT_eSPI& t);
// advance: see MuleSkin::tick()'s header comment -- gates state
// mutation for boards that render in multiple physical bands per
// logical frame. Defaults to true (unchanged behavior for single-pass
// boards).
// The mascot's own clock. MuleSkin, the pet, a visitor, the crowd and the
// idle events step once per call with no notion of elapsed time -- they were
// tuned by eye on a board that drew sixteen frames a second, and the day the
// frame rate doubled they ran at double speed. The backgrounds scale their
// motion by elapsed time and did not. So the screen draws every frame, and
// the mascot STEPS on this clock: true when at least MASCOT_STEP_MS has
// passed since the last step, and the caller hands it to every tick() as
// `advance` -- which already means "draw, but do not move" when false,
// because the 3.5" board draws twice per frame and needed exactly that.
//
// 62 ms was the pace the 80 MHz boards had before they got fast; 120 is
// what two boards side by side said was right once they had. Live on the
// console as PACE N (milliseconds a step) and kept in settings, so the
// number is chosen by eye on a real board rather than argued about.
bool     uiMascotStep(uint32_t now, bool advance);
void     uiMascotStepSet(uint32_t ms);
uint32_t uiMascotStepMs();

// Whether MuleSkin -- and his pet, his bubble and any visiting MuleSkins --
// is on the main screen. Never while MuleSkin::MASCOT_SHOWN is off; otherwise
// not in boring mode, and not over the MULESKIN background, which is a
// picture of him already, except while the first-boot walkthrough runs.
bool uiClearMascotShown();

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                  bool advance = true);

// The watch/hunt indicator, bottom left of CLEAR. True when a tap landed on
// it; main.cpp opens the watch-alert screen, which is where the target is
// named and where REMOVE FROM WATCH LIST lives. Only ever true while a watch
// or a hunt is actually set -- the pill is not drawn otherwise.
bool uiClearWatchPillHit(int x, int y);
// The clock at the top centre (once it is set): a tap opens TODAY.
bool uiClearClockHit(int x, int y);
// The NEARBY headline, which only exists while something is live. Long-pressing
// it opens the closest device -- see main.cpp. False whenever it is not drawn.
bool uiClearNearbyHit(int x, int y);
// PUSH ALERTS waiting for a WiFi network: a "PUSH n" pill in the title bar,
// 0 for none. Set by main.cpp every pass.
void uiClearSetPushWaiting(uint8_t n);

