// MuleSkin-CYD — per-device alert suppression ("IGNORE")
#pragma once
#include <stdint.h>
#include "state.h"

// A short list of MACs whose detections should never raise the full-screen
// ALERT. The point is the devices you own: your own AirTag in your own
// pocket and your own doorbell camera are true positives every time, and a
// detector that shouts about them constantly is one you stop reading.
//
// Ignored devices are still scanned, still counted and still written to the
// LOG -- only the alert is suppressed. That way the list is recoverable
// (you can see the device and un-ignore it) and the counters stay honest.
//
// Storage is a single packed NVS blob of 7-byte records -- six bytes of
// MAC and one of DetectionType -- not one key per device. NVS allocates in
// 32-byte entries, so a key each would cost several times the space and
// make enumeration a series of lookups; the whole list at MAX entries is
// 448 bytes, which is nothing against the 20KB partition.
//
// The type is stored rather than looked up because the log it came from is
// a short ring: mute a device, leave it a while, and the detection that
// told you what it was has long since scrolled out. Without the type the
// list is a column of MACs, which tells you that you muted something but
// not what.
namespace IgnoreList {

    static const uint8_t MAX = 64;

    // Loads from NVS. Safe to call more than once.
    void begin();

    bool contains(const uint8_t* mac);

    // Returns false if the list is full or the MAC is already on it.
    // Persists immediately -- an ignore that did not survive a reboot
    // would be worse than no ignore at all.
    bool add(const uint8_t* mac, DetectionType type = DetectionType::UNKNOWN);

    bool remove(const uint8_t* mac);

    // Told of every add() and remove() that changed the list -- how main.cpp
    // passes a change made on this board to the others on its push topic.
    // nullptr: nobody (the default, and the host tests').
    void setChangeHook(void (*hook)(const uint8_t* mac, DetectionType type, bool added));

    uint8_t count();

    // nullptr when idx is out of range. Points into the module's own
    // storage; valid until the next add/remove/clear.
    const uint8_t* macAt(uint8_t idx);

    // UNKNOWN for out-of-range, and for anything muted before the type was
    // recorded or muted from the raw scanner, which classifies nothing.
    DetectionType typeAt(uint8_t idx);

    void clear();

    // SNOOZE: the same thing as IGNORE for one device -- no full-screen
    // alert, still scanned, counted and logged -- but in RAM only, so it
    // lasts until the board restarts and not a moment longer. For the
    // doorbell across the street that keeps coming and going on a walk
    // past, where IGNORE would be a decision about forever.
    //
    // It used to mute the whole TYPE for an hour, and more than the alert:
    // it switched detection of that type off, so nothing was logged either.
    // Snoozing a neighbour's AirTag silenced every AirTag, which is the one
    // thing a tracker detector must never quietly do.
    //
    // Thirty-two of them; when full, the oldest snooze makes room.
    void snooze(const uint8_t* mac);
    // The same, but over after `ms` as well as at a restart -- SNOOZE 1 DAY
    // from a notification. Snoozing a device already snoozed resets it.
    void snoozeFor(const uint8_t* mac, uint32_t ms);
    // A snooze over now: Undo on a notification's "snoozed".
    void unsnooze(const uint8_t* mac);
    bool snoozed(const uint8_t* mac);
    // What every alert gate asks: ignored for good, or snoozed for now.
    bool silenced(const uint8_t* mac);
    // A tag that changed its address (TrackerFollow linked the two): an
    // IGNORE moves to the new address (with its type; the old one comes off
    // the list, which is full at MAX), and a SNOOZE carries on under it with
    // the time it had left. True when there was either to carry.
    bool carryOver(const uint8_t* from, const uint8_t* to);
}
