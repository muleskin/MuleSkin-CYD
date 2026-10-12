// MuleSkin-CYD — an access point that answers to every network name.
//
// A "karma" attack AP (a WiFi Pineapple's PineAP, and the open tools that
// copy it) listens to the names nearby phones ask for and answers each one,
// beaconing or probe-responding as that network -- so a phone that has ever
// joined "Starbucks WiFi" connects to it without being asked. Its tell: one
// hardware address (BSSID) under many different network names. A real
// access point offering several networks gives each its own BSSID.
//
// Pure logic, host-tested (test/karma_watch_test.cpp): the caller hands in
// every beacon and probe response's BSSID and SSID with the time, and note()
// says, once, when a BSSID has shown THRESHOLD different names inside
// WINDOW_MS. Fed from DetectionEngine::processWiFiQ(); reported as HACKER.
#pragma once
#include <stdint.h>

namespace KarmaWatch {

const uint8_t  SLOTS     = 16;                 // BSSIDs followed at once; the stalest goes
const uint8_t  THRESHOLD = 5;                  // names from one BSSID that make it a karma AP
const uint32_t WINDOW_MS = 5u * 60u * 1000u;   // ...inside this long

// True the first time `bssid` reaches THRESHOLD distinct names inside a
// window -- and not again for that BSSID until its slot is reused. An empty
// SSID (a hidden network) is no name and counts for nothing.
bool note(const uint8_t bssid[6], const char* ssid, uint32_t now);
void clear();

}  // namespace KarmaWatch
