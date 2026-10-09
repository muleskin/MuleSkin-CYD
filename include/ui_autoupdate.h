// MuleSkin-CYD — the AUTO UPDATE countdown: what a board shows before it
// installs a newer release on its own, at night (main.cpp serviceAutoUpdate).
//
// A countdown, not a question. The owner turned AUTO UPDATE on to get
// exactly this, so the board proceeds unless somebody taps SKIP; the count
// is there so a board in a hand has a moment to say no, and NOW is there
// for the person who is watching and does not want to wait.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class AutoUpdateHit : uint8_t { NONE, NOW, SKIP };

// `ver` is the release to install, `seconds` the count.
void     uiAutoUpdateInit(TFT_eSPI& t, const uint8_t ver[3], uint16_t seconds, uint32_t now);
void     uiAutoUpdateTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
AutoUpdateHit uiAutoUpdateHit(TFT_eSPI& t, int x, int y);
int      uiAutoUpdateSecondsLeft(uint32_t now);
