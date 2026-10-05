// MuleSkin-CYD — WIFI TIME: the main screen's < WIFI TIME > button joins a
// saved WiFi network just long enough to set the clock (OtaWifi::timeSync*),
// and this screen says how that is going. BACK, once it is done, goes home.
#pragma once
#include <TFT_eSPI.h>

void uiTimeSyncInit(TFT_eSPI& t);
void uiTimeSyncTick(TFT_eSPI& t, uint32_t now);
// True when (x, y) is on the BACK button. Only drawn once the job is over.
bool uiTimeSyncHitBack(int x, int y, int screenW, int screenH);
