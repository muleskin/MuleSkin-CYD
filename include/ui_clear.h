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

// The battery, for TERMINAL MODE's LOG title bar. A board with a battery
// hands over a function that fills in the percent and whether it is on the
// cable, and returns false while no cell is plugged in. Boards without a
// battery never set one, and the title bar shows none. (Upstream v1.43.)
typedef bool (*BatterySource)(uint8_t& pct, bool& charging);
void uiClearSetBatterySource(BatterySource fn);
bool uiClearHasBattery();   // a source is set: this board can show a battery

// TERMINAL MODE (Settings::boringMode(): the BORING MODE switch, renamed):
// the windows on the home screen. (Upstream SquachWatch v1.43-v1.49.)
// True while a gesture that started on a window is theirs.
bool uiTerminalTouch(int x, int y, bool valid, bool justDown, uint32_t now);
// The device the terminal asked to open (the LOG's WATCH/HUNT/INFO/IGNORE
// panel), once.
bool uiTerminalTakeOpen(uint8_t mac[6]);
// The LOG's order without a tap (emulator shots): 0 TIME, 1 KIND, 2 DEVICE, 3 dBm;
// dir 1 is the first tap's order, -1 the flipped one.
void uiTerminalSetSort(uint8_t col, int8_t dir);
// The terminal's cursor stops for the boards that drive a cursor (StickS3,
// Cardputer, the T-Deck's trackball). Boxes, so the cursor can light each
// one whole.
uint8_t uiTerminalStops(int16_t* rx, int16_t* ry, int16_t* rw, int16_t* rh, uint8_t cap);

