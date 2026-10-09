// MuleSkin-CYD — IN A MEETING: a red do-not-disturb sign, from the main
// screen's < IN MEETING > button. BACK at the bottom returns to it.
#pragma once
#include <TFT_eSPI.h>

void uiMeetingInit(TFT_eSPI& t);
void uiMeetingTick(TFT_eSPI& t, uint32_t now);
// True when (x, y) is on the BACK button.
bool uiMeetingHitBack(int x, int y, int screenW, int screenH);
// The sign itself, above the button: a tap there steps the timer.
bool uiMeetingHitSign(int x, int y, int screenW, int screenH);
// No timer -> 15 -> 30 -> 60 minutes -> no timer. "BACK IN N MIN" shows
// under the words while one runs.
void uiMeetingCycleTimer(uint32_t now);
// True once a running timer reaches zero: the sign takes itself down.
bool uiMeetingTimerDone(uint32_t now);
