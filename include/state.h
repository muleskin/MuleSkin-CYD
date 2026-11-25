// MuleSkin-CYD — shared state types
// Standalone header; only <stdint.h> dependency.
#pragma once
#include <stdint.h>

enum class DetectionType : uint8_t {
    UNKNOWN = 0,
    FLOCK   = 1,   // Flock Safety camera / sensor
    AXON    = 2,   // Axon body camera / LE equipment
    META    = 3,   // Ray-Ban Meta smart glasses
    SKIMMER = 4,   // HC-05/06/03 Bluetooth skimmer
    RAVEN   = 5,   // Raven gunshot detector
    AIRTAG  = 6,   // Apple AirTag / FindMy
    DRONE   = 7,   // OpenDroneID drone
    ALPR    = 8,   // Motorola / Vigilant ALPR
    CAMERA  = 9,   // Generic camera (existing OUI list)
    SAMSUNG_TAG = 10,  // Samsung Galaxy SmartTag / SmartTag+
    GOOGLE_TAG  = 11,  // Google Find My Device network tracker (Chipolo/Pebblebee/Moto Tag)
    TILE    = 12,  // Tile BLE tracker (was previously bucketed under AIRTAG)
    RING    = 13,  // Ring doorbell/camera (was previously bucketed under CAMERA)
    DEAUTH  = 14,  // WiFi deauth/disassoc flood -- rate-detected, not a signature match (see DetectionEngine)
    EVILTWIN = 15, // One SSID beaconing from a second BSSID whose OUI differs -- rogue/spoofed AP (see DetectionEngine)
    IBEACON = 16,  // Apple iBeacon proximity beacon -- retail/venue tracking, not police kit
    // Pentest and wireless-audit hardware: Flipper Zero, Pwnagotchi, WiFi
    // Pineapple, ESP deauthers. One bucket rather than four types because
    // what matters to somebody reading the screen is that a tool for
    // attacking radios is in the room, not which model it is -- the
    // specific device goes in the vendor label and, where it announces
    // one, its own name goes in the name field.
    //
    // Deliberately NOT in this bucket: bare Espressif and other generic
    // silicon. A nyanBOX, an ESP32 Marauder and a MuleSkin are the same
    // chip, and sixteen Espressif prefixes already sit under FLOCK. HACKER
    // takes exact signatures only, which is what keeps it worth alerting on.
    HACKER  = 17,
    // A LoRaWAN operator's network within earshot: a utility's meters, a
    // city's sensors, The Things Network. One per OPERATOR, not per device --
    // the DevAddr prefix names who runs it (lora_opalert.h), and a street of
    // meters is one fact, not three hundred. Only a board with a LoRa radio
    // can raise it, and only while that radio listens on LORAWAN. (Upstream
    // SquachWatch v1.45; numbered as upstream, ahead of MINE.)
    LORAWAN = 18,
    // MY ALERTS: a WiFi name matching one of the owner's own patterns (see
    // my_alerts.h). Kept LAST and outside everything that walks the catalogue
    // of known kit -- the DEX, BINGO and the TYPE FILTER stop short of it
    // (they loop to MINE, not COUNT): it is not a kind of device anybody
    // could collect, and its on/off switch is the list itself.
    MINE    = 19,
    // HUMAN SENSING: something moving in the room, on the radio path between
    // the router and the board (include/sense_cyd.h). One row per network
    // sensed through, under a made-up address (SenseMotion::macFor()), so
    // the LOG, IGNORE, SNOOZE and PUSH ALERTS treat it as they treat any other
    // catch. Only raised while sensing runs (its screen, or SENSE ARM), since
    // the detector has the radio the rest of the time. After MINE, not before:
    // MINE shipped as 19 and its number is in every log and ignore list since.
    // Outside the catalogue like MINE (no DEX card, no BINGO square), but it
    // has a TYPE FILTER row (Settings::filterTypeAt()).
    MOTION  = 20,
    // A smart speaker with microphones: Sonos, Apple's HomePod, and an Echo or
    // a Google speaker while it is in setup. The room check -- a hotel, a
    // rental -- next to the hidden cameras. Ships OFF, like IBEACON: a kitchen
    // counter is not a threat. (Upstream SquachWatch v1.50.0, where it is 20;
    // here MOTION already had 20, and both numbers are in logs and lists.)
    // In the catalogue (a DEX card, a TYPE FILTER row): catalogueAt() below
    // steps over MINE and MOTION to reach it.
    LISTENER = 21,
    COUNT   = 22
};

inline const char* detectionTypeName(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        case DetectionType::META:        return "META";
        case DetectionType::SKIMMER:     return "SKIMMER";
        case DetectionType::RAVEN:       return "RAVEN";
        case DetectionType::AIRTAG:      return "AIRTAG";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAMERA";
        case DetectionType::SAMSUNG_TAG: return "SAMSUNG_TAG";
        case DetectionType::GOOGLE_TAG:  return "GOOGLE_TAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL TWIN";
        case DetectionType::IBEACON:     return "IBEACON";
        case DetectionType::HACKER:      return "HACKER";
        case DetectionType::LORAWAN:     return "LORAWAN";
        case DetectionType::MINE:        return "MY ALERT";
        case DetectionType::MOTION:      return "MOTION";
        case DetectionType::LISTENER:    return "LISTENER";
        default:                         return "UNKNOWN";
    }
}

// The catalogue of kit: every kind of device there is a DEX card for, in
// order -- FLOCK..LORAWAN, then LISTENER. Not MY ALERTS (the owner's own list)
// and not MOTION (a room, not a device), which sit between them in the
// numbering; the walk steps over the two.
static const uint8_t CATALOGUE_N = (uint8_t)DetectionType::MINE;   // MINE-1 of them, and LISTENER
inline DetectionType catalogueAt(uint8_t i) {
    return (uint8_t)(i + 1) < (uint8_t)DetectionType::MINE ? (DetectionType)(i + 1) : DetectionType::LISTENER;
}
inline uint8_t catalogueIndex(DetectionType t) {
    const uint8_t v = (uint8_t)t;
    if (v >= 1 && v < (uint8_t)DetectionType::MINE) return (uint8_t)(v - 1);
    return t == DetectionType::LISTENER ? (uint8_t)(CATALOGUE_N - 1) : CATALOGUE_N;
}

// How sure we are that a match is what it claims to be.
//
// Moved here from signatures.h because it is now a property of the
// SIGHTING rather than of the type. A FLOCK hit off Flock Safety's own
// registered OUI and a FLOCK hit off a generic Espressif module block are
// the same DetectionType and are not remotely the same claim.
//
// Note: plain LOW/MEDIUM/HIGH collide with the Arduino core's pin-state
// macros through the preprocessor, which enum class scoping does not
// protect against -- hence the _CONF suffix.
enum class Confidence : uint8_t { LOW_CONF, MED_CONF, HIGH_CONF };

struct Detection {
    uint8_t        mac[6];
    int8_t         rssi;
    uint8_t        channel;        // 0 if N/A
    DetectionType  type;
    // Kept from before this boot (the black box): firstSeen then holds the
    // wall-clock second it was seen, not a millis() stamp, and lastSeen is 0.
    // Cleared the moment the device is seen again. Sits in what was padding,
    // so the log costs no more RAM than it did.
    uint8_t        restored;
    // The signal a couple of seconds ago, and when that was in units of two
    // seconds (wrapping every eight and a half minutes, which is far longer
    // than a row stays fresh). The LOG's closer/further arrow reads these:
    // comparing against the last ADVERT would only show the wobble a still
    // device makes, which is why the raw scan's arrow samples too.
    int8_t         prevRssi;
    uint8_t        prevAt;
    // Who made it, when the match knew: a pointer into the signature
    // tables or a literal, never a copy. Every vendor string in this
    // firmware is a compile-time constant, so a log of two hundred rows
    // was carrying two hundred copies of "Apple" -- eight bytes a row
    // against a pointer, and the log is the largest thing the board
    // allocates. nullptr when nothing named it: read it through
    // vendorText() rather than testing it at every use.
    const char*    vendor;
    char           name[20];
    uint32_t       firstSeen;
    uint32_t       lastSeen;
    uint16_t       hits;
    // AUTO SNOOZE (Settings). `alerts` counts the full-screen alerts this
    // device has actually raised; `quietBar` is the strongest RSSI it ever
    // alerted at. Past the threshold it has to beat that bar by a margin to
    // interrupt again -- so the board stops repeating itself about the
    // doorbell across the street without ever going deaf to it coming
    // closer, which a plain mute would not manage.
    //
    // NOT `hits`: that counts every sighting on the WiFi path and only the
    // returns on the Bluetooth one, so it means two different things
    // depending on which radio found the device.
    uint8_t        alerts;
    int8_t         quietBar;
    // When it last spent one. NOT lastSeen: by the time the gate runs, the
    // reactivation path has already moved lastSeen to now, so "how long has
    // it been gone" measured from it is always zero and the allowance would
    // never be handed back.
    uint32_t       lastAlertMs;
    // The grade of the signature that actually matched, not the grade of
    // the type. See lookupOui().
    Confidence     conf;
    bool           active;
    // When it last asked to interrupt, allowed or not, in minutes of uptime
    // (wrapping after 45 days). A device that keeps coming back keeps asking,
    // so the gap since the last ask is how long it was really gone. Sits in
    // what was padding.
    uint16_t       askedMin;
    // A tracker's owner, as its own advert says (OWNER_* below; trackers
    // only, 0 for everything else): the card's WITH OWNER, AWAY FROM OWNER
    // or NOT SET UP. Kept up to date while the tag is heard.
    uint8_t        owner;
};

// What a tracker's advert says about its owner (signatures.h, trackerVerdict).
enum : uint8_t {
    OWNER_UNKNOWN = 0,   // not a tracker, or one whose advert does not say (Tile)
    OWNER_WITH    = 1,   // near its owner's phone
    OWNER_AWAY    = 2,   // apart from its owner: the one worth an alert
    OWNER_UNSET   = 3,   // an AirTag nobody has set up: no owner to be with
};

// A detection's vendor, safe to print. A record is zeroed before it is
// filled, so "no vendor" arrives here as a null pointer.
inline const char* vendorText(const Detection& d) { return d.vendor ? d.vendor : ""; }

enum class AppState : uint8_t {
    BOOT     = 0,
    CLEAR    = 1,
    ALERT    = 2,
    LOG      = 3,
    SETTINGS = 4,
    DIARY    = 5,
    OUTFIT   = 6,
    // 7 was RAWSCAN, the manual BLE/WiFi scanner; removed, number kept free.
    WATCH_ALERT = 8,  // a watched target (see DetectionEngine::watchBle/watchWifi) came back in range
    DIAGNOSTICS = 9,  // on-device diagnostics screen, reached via Settings
    HUNT        = 10, // live signal-strength gauge for the watched target,
                       // reached via raw-scan's long-press confirm panel
                       // (the "HUNT" choice alongside WATCH/CANCEL)
    COLOR_CHECK = 11, // first-boot RED/GREEN/BLUE display sanity check,
                       // also reachable later via Settings' "CHECK COLORS" row
    DETECTION_FILTER = 12, // per-type detection on/off, reached via
                            // Settings' "DETECTION FILTER" row
    OUTFIT_UNLOCK    = 13, // "OUTFIT UNLOCKED" celebration, pushed
                            // automatically whenever MuleSkin earns a new
                            // costume; returns to CLEAR when dismissed
    IGNORE_LIST      = 14, // muted devices, reached via Settings'
                            // "IGNORED DEVICES" row
    POWER_SAVER      = 15, // battery settings, reached via Settings'
                            // "POWER SAVER" row
    // 16 to 20 were the payphone name screen and MuleSkinMesh's menu, consent
    // gate, phrase and compose screens; removed with the mesh, numbers kept free.
    BEACON_WARN      = 21, // what switching iBeacons on means, asked from
                            // DETECTION FILTER before it happens
    SECURITY         = 22, // the SECURITY submenu (PIN lock + the rest)
    LOCKED           = 23, // the lock screen: the payphone, digits only
    PIN_ENTRY        = 24, // setting, changing or checking a PIN, from SECURITY
    // 25 was SQUAD, removed with the mesh.
    UPDATE           = 26, // UPDATE FIRMWARE, from Settings' SYSTEM page:
                            // WiFi or Bluetooth update, or switch slots
    WIFI_PASS        = 27, // typing a WiFi password, from UPDATE's network list
    STATUS_LIGHT     = 28, // the RGB LED's settings, from the APPEARANCE page
    AUTO_UPDATE      = 29, // AUTO UPDATE's countdown, before it installs at night
    // 30 and 31 were SQUAD UPDATE and ADD TO SQUAD, removed with the mesh.
    // 32 was DESK, desk mode; removed, number kept free.
    WIFI_NETS        = 33, // WIFI NETWORKS, from the SYSTEM page: the saved list
    WIFI_ADD         = 34, // ...and the scan to add one from
    SYS_PROPS        = 35, // SYSTEM PROPERTIES: the window that says an update
                            // is out, in front of the main screen on the first
                            // frame after the intro. See ui_sysprops.h.
    // 36 was BINGO, the weekly detection card; removed in v3.2.5, number kept free.
    DEX              = 37, // the MULESKIN-DEX, from Settings' row. See ui_dex.h.
    LORA_CHAT        = 38, // LORA CHATS, from WATCH SETTINGS (MULESKIN_LORA builds). See ui_lorachat.h.
    MEETING          = 39, // IN A MEETING: the red sign, from the main screen's bar. See ui_meeting.h.
    TIME_SYNC        = 40, // WIFI TIME: join a saved network, set the clock. See ui_timesync.h.
    TODAY            = 41, // TODAY: today's sightings by hour and type. See ui_today.h.
    MY_ALERTS        = 42, // MY ALERTS, from Settings: the owner's own WiFi names. See ui_myalerts.h.
    SENSING          = 43  // HUMAN SENSING (SENSE_CYD_APP builds): WiFi motion sensing on the board's
                           // own radio, from Settings > HUMAN SENSING. See ui_sensing_cyd.h and sense_cyd.h.
};

enum class ButtonId : uint8_t {
    NONE  = 255,
    SCAN  = 0,
    LOG   = 1,
    CLR   = 2
};
