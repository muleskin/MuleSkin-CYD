// MuleSkin-CYD — IN A MEETING: a red do-not-disturb sign, from the main
// screen's [ IN MEETING ] button. BACK at the bottom returns to it.
#pragma once
#include <TFT_eSPI.h>

void uiMeetingInit(TFT_eSPI& t);
void uiMeetingTick(TFT_eSPI& t, uint32_t now);
// True when (x, y) is on the BACK button.
bool uiMeetingHitBack(int x, int y, int screenW, int screenH);
