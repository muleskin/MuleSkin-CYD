// MuleSkin-CYD — signature data tables and lookup functions
// Sources per docs/DETECTIONS.md.
#include "signatures.h"
#include <string.h>
#include <strings.h>   // strncasecmp
#include <ctype.h>     // the extra rules' parsing
#include <stdlib.h>    // ...and their tables, sized to the set

// OUI table. Order MATTERS: lookupOui walks top-to-bottom and returns
// the first match. Per DESIGN.md §6.2: Axon > Flock > ALPR > cameras >
// ESP32 family > skimmer. Some Flock hardware uses Espressif modules,
// so Flock entries for the same prefix must appear before generic ESP32.
const OuiEntry kOuiTable[] = {
    // ---- Axon / Taser (priority: highest) ----
    {{0x00, 0x25, 0xDF}, "Axon",         DetectionType::AXON,       Confidence::HIGH_CONF},
    {{0xE4, 0x05, 0x40}, "Axon-Body",    DetectionType::AXON,       Confidence::LOW_CONF},
    {{0x28, 0x24, 0xFF}, "Axon-Signal",  DetectionType::AXON,       Confidence::LOW_CONF},

    // ---- Other body-camera makers (2026-10-14) ----
    // Registry-checked. Filed under AXON, which the screens call a body
    // camera; the label says whose. WatchGuard VIDEO (Motorola) -- not
    // WatchGuard Technologies, the firewall maker, whose 00:01:21 and
    // 00:90:7F would name every office firewall a police camera.
    {{0x00, 0x1D, 0x96}, "WatchGuard",   DetectionType::AXON,       Confidence::MED_CONF},
    {{0x00, 0x23, 0xBD}, "DigitalAlly",  DetectionType::AXON,       Confidence::MED_CONF},

    // ---- ShotSpotter / SoundThinking gunshot sensors ----
    // ShotSpotter, Inc.'s own block (IEEE MA-L). Filed with Raven, the other
    // gunshot sensor, because that is what it is.
    {{0xD4, 0x11, 0xD6}, "ShotSpotter",  DetectionType::RAVEN,      Confidence::HIGH_CONF},

    // ---- Flock Safety (ESP32 modules + LTE backhaul) ----
    // Source: colonelpanichacks/flock-you, @NitekryDPaul, DeFlockJoplin
    //
    // Seventeen Espressif blocks used to sit here as "Flock-ESP32". Espressif
    // makes the chip in every ESP32 -- this board, every other MuleSkin,
    // half the smart plugs in a house -- so they named all of those a Flock
    // camera, if only at LOW. Taken out 2026-10-14: the projects that test
    // at real Flock sites (all-cameras-are-beacons, Flock-You's own rewrite)
    // no longer trust an Espressif block, and Flock's newer cameras run a
    // Qualcomm radio (the QCA row below).
    {{0xB4, 0x1E, 0x52}, "Flock-MA-L",   DetectionType::FLOCK,      Confidence::HIGH_CONF},
    {{0x24, 0xB2, 0xB9}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xD0, 0x39, 0x57}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x00, 0xF4, 0x8D}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x14, 0x5A, 0xFC}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x80, 0x30, 0x49}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE0, 0x0A, 0xF6}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x70, 0xC9, 0x4E}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x3C, 0x91, 0x80}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xD8, 0xF3, 0xBC}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xB8, 0x35, 0x32}, "Flock",        DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x82, 0x6B, 0xF2}, "Flock-DeFlk",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    // From Flock-You (github.com/colonelpanichacks/flock-you, MIT), issue
    // #15: the community field list (@NitekryDPaul / DeFlockJoplin, 2026-07-16)
    // and a Flock camera firmware dump (2026-09-16). Every one is a module
    // vendor's block -- Liteon, USI, Silicon Labs, Atheros -- so LOW, like
    // the rows above. Left out: 48:27:EA, which the registry gives
    // to Samsung Electronics and would call every Samsung TV a Flock.
    {{0x74, 0x4C, 0xA1}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x9C, 0x2F, 0x9D}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xC0, 0x35, 0x32}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x94, 0x08, 0x53}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE4, 0xAA, 0xEA}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xF4, 0x6A, 0xDD}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE8, 0xD0, 0xFC}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xB8, 0x1E, 0xA4}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x70, 0x08, 0x94}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x58, 0x00, 0xE3}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x5C, 0x93, 0xA2}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x64, 0x6E, 0x69}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x14, 0xB5, 0xCD}, "Flock-Liteo", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x08, 0x3A, 0x88}, "Flock-UGSI",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xE0, 0x4F, 0x43}, "Flock-UGSI",  DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x58, 0x8E, 0x81}, "Flock-SiLab", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0xEC, 0x1B, 0xBD}, "Flock-SiLab", DetectionType::FLOCK,      Confidence::LOW_CONF},
    {{0x90, 0x35, 0xEA}, "Flock-SiLab", DetectionType::FLOCK,      Confidence::LOW_CONF},
    // Atheros's own default: the camera's QCA9377 ships 00:03:7F:50:00:01
    // in its calibration blobs, and a unit nobody reprogrammed keeps it.
    {{0x00, 0x03, 0x7F}, "Flock-QCA",     DetectionType::FLOCK,      Confidence::LOW_CONF},
    // Labelled as Sierra Wireless, the LTE modem in a Flock camera. The
    // registry says SPECTRA - TEK, which is neither Sierra nor Flock. The
    // prefix may still turn up on Flock hardware, so it stays -- as the
    // Low-confidence guess it always was.
    {{0x00, 0xA0, 0xD8}, "Flock-OEM",    DetectionType::FLOCK,      Confidence::LOW_CONF},

    // ---- ALPR and fixed surveillance camera vendors ----
    //
    // 00:0E:58 used to be the only entry here, labelled Vigilant. It is not
    // Vigilant. The IEEE registry gives that block to SONOS, INC. of Goleta
    // CA, assigned in February 2004, and it is still theirs -- so every
    // Sonos speaker in earshot was being logged as an ALPR camera. Removed
    // rather than corrected, because there is nothing to correct it to:
    // Motorola's own blocks are below.
    //
    // All seven of these were read out of the IEEE registry rather than
    // copied from another detector, after the one above turned out to be
    // wrong. Motorola Solutions absorbed Vigilant, so its blocks are the
    // nearest honest thing to the entry they replace.
    {{0x00, 0x04, 0x7D}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x00, 0x18, 0x85}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x00, 0x1F, 0x92}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x4C, 0xCC, 0x34}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Registered to Motorola Solutions Malaysia Sdn. Bhd. rather than to the
    // US parent, which is why it was missing from the sweep that found the
    // four above -- same company, different registry line.
    {{0xB8, 0xE2, 0x8C}, "ALPR-Mtrla",   DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Genetec's AutoVu is an LPR platform, so these sit with the ALPR set.
    {{0x00, 0xBF, 0x15}, "ALPR-Gentec",  DetectionType::ALPR,       Confidence::HIGH_CONF},
    {{0x0C, 0xBF, 0x15}, "ALPR-Gentec",  DetectionType::ALPR,       Confidence::HIGH_CONF},
    // Verkada sells LPR too but is mostly general-purpose surveillance, so
    // it is filed as a camera rather than overstated as a plate reader.

    // ---- Skimmer OUIs (BT Classic module prefixes) ----
    {{0x20, 0x13, 0x00}, "Skim-Linvor",  DetectionType::SKIMMER,    Confidence::LOW_CONF},
    // Was 98:D3:00, which nobody holds. The HC-05/HC-06 modules' maker,
    // Shenzhen Bolutek, holds 98:D3:31 (IEEE MA-L).
    {{0x98, 0xD3, 0x31}, "Skim-HC05",    DetectionType::SKIMMER,    Confidence::LOW_CONF},
    {{0x00, 0x1A, 0x7D}, "Skim-CSR",     DetectionType::SKIMMER,    Confidence::LOW_CONF},

    // ---- Specific camera vendors ----
    {{0x2C, 0xAA, 0x8E}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xD0, 0x3F, 0x27}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x7C, 0x78, 0xB2}, "Wyze",         DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0xB8, 0xD7, 0xAF}, "Wyze-Mod",     DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0x34, 0xD2, 0x70}, "Amazon",       DetectionType::CAMERA,     Confidence::MED_CONF},
    // Labelled Hikvision for a long time. The registry says AMAZON
    // TECHNOLOGIES, which is a different company entirely -- and a much
    // broader one, hence Medium rather than High.
    {{0xF0, 0x27, 0x2D}, "Amazon",       DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xC0, 0x56, 0xE3}, "Hikvision",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x44, 0x19, 0xB6}, "Hikvision",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    // Labelled Reolink; registered to Hikvision. Both make cameras, so the
    // detection stood up while the attribution did not.
    {{0x28, 0x57, 0xBE}, "Hikvision",    DetectionType::CAMERA,     Confidence::HIGH_CONF},
    {{0x00, 0xE0, 0x4C}, "Realtek",      DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0xBC, 0xDD, 0xC2}, "Arlo",         DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0x4C, 0x69, 0x05}, "Blink",        DetectionType::CAMERA,     Confidence::LOW_CONF},
    {{0xA4, 0xC1, 0x38}, "Tuya",         DetectionType::CAMERA,     Confidence::LOW_CONF},
    // Blink by Amazon's own blocks (IEEE MA-L, "Blink by Amazon").
    {{0x3C, 0xA0, 0x70}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x70, 0xAD, 0x43}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x74, 0x13, 0x48}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0x74, 0xAB, 0x93}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xC8, 0x19, 0xD8}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    {{0xF0, 0x74, 0xC1}, "Blink",        DetectionType::CAMERA,     Confidence::MED_CONF},
    // i-PRO (Panasonic's security cameras, body cams among them).
    {{0xD4, 0x2D, 0xC5}, "i-PRO",        DetectionType::CAMERA,     Confidence::HIGH_CONF},

    // ---- Ring (own type, not generic CAMERA) ----
    // FC:65:DE and 68:37:E9 were already here under CAMERA; the rest
    // are Ring LLC's full registered MA-L block. Source: IEEE MA-L
    // registry, cross-checked via netify.ai and maclookup.app (both
    // list the same 13 prefixes for "Ring LLC", registered 2019-03-01).
    {{0xFC, 0x65, 0xDE}, "Ring",         DetectionType::RING,       Confidence::MED_CONF},
    {{0x68, 0x37, 0xE9}, "Ring",         DetectionType::RING,       Confidence::MED_CONF},
    {{0xAC, 0x9F, 0xC3}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x18, 0x7F, 0x88}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x34, 0x3E, 0xA4}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x54, 0xE0, 0x19}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x5C, 0x47, 0x5E}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x64, 0x9A, 0x63}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x90, 0x48, 0x6C}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x9C, 0x76, 0x13}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0xCC, 0x3B, 0xFB}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0xC4, 0xDB, 0xAD}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x24, 0x2B, 0xD6}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x00, 0xB4, 0x63}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},
    {{0x50, 0xE4, 0x67}, "Ring",         DetectionType::RING,       Confidence::HIGH_CONF},

    // ---- Commercial / institutional camera vendors ----
    // Source: public IEEE MA-L registry (maclookup.app), cross-checked
    // per-vendor registration records.
    {{0xE0, 0xA7, 0x00}, "Verkada",      DetectionType::CAMERA,     Confidence::HIGH_CONF},  // registered 2016-09-22
    {{0x70, 0x1A, 0xD5}, "Avigilon",     DetectionType::CAMERA,     Confidence::HIGH_CONF},  // Avigilon Alta, registered 2021-04-27
    {{0x00, 0x40, 0x8C}, "Axis",         DetectionType::CAMERA,     Confidence::HIGH_CONF},  // Axis Communications, registered 1998
    {{0xB8, 0xA4, 0x4F}, "Axis",         DetectionType::CAMERA,     Confidence::HIGH_CONF},

    // ---- Camera glasses ----
    // Luxottica Group's own block (IEEE MA-L): Ray-Ban and Oakley Meta's
    // maker. Four other "Meta glasses" OUIs circulate (7C:2A:9E, CC:66:0A,
    // F4:03:43, 5C:E9:1E); the registry gives two to Apple, one to HPE and
    // one to nobody, so they are not here.
    {{0x98, 0x59, 0x49}, "Luxottica",    DetectionType::META,       Confidence::MED_CONF},

    // ---- Drones, by maker (2026-10-14) ----
    // For the drones that do not broadcast Remote ID, or not yet. All IEEE
    // MA-L. DJI's main blocks are its drones and controllers; the Baiwang
    // blocks are a DJI subsidiary with a wider range, so LOW. Parrot's two
    // oldest blocks are mostly its car kits, so LOW too. Left out: DJI's
    // Ronin and Osmo blocks (camera gimbals), and Autel's (an MA-M, finer
    // than this table matches).
    {{0x60, 0x60, 0x1F}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x34, 0xD2, 0x62}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x48, 0x1C, 0xB9}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0xE4, 0x7A, 0x2C}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x58, 0xB8, 0x58}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x04, 0xA8, 0x5A}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x8C, 0x58, 0x23}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x0C, 0x9A, 0xE6}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x88, 0x29, 0x85}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},   // registered 2025-10
    {{0x4C, 0x43, 0xF6}, "DJI",          DetectionType::DRONE,      Confidence::MED_CONF},   // registered 2025-12
    {{0x9C, 0x5A, 0x8A}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0xEC, 0x72, 0xF7}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0x34, 0x91, 0xF0}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0xC8, 0xA1, 0x62}, "DJI-BW",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0x38, 0x1D, 0x14}, "Skydio",       DetectionType::DRONE,      Confidence::HIGH_CONF},
    {{0x90, 0x03, 0xB7}, "Parrot",       DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x90, 0x3A, 0xE6}, "Parrot",       DetectionType::DRONE,      Confidence::MED_CONF},
    {{0xA0, 0x14, 0x3D}, "Parrot",       DetectionType::DRONE,      Confidence::MED_CONF},
    {{0x00, 0x12, 0x1C}, "Parrot",       DetectionType::DRONE,      Confidence::LOW_CONF},
    {{0x00, 0x26, 0x7E}, "Parrot",       DetectionType::DRONE,      Confidence::LOW_CONF},

    // ---- Pentest hardware ------------------------------------------
    // No Flipper prefix here. Flipper Devices' own IEEE block, 0C:FA:22,
    // sat here as HACKER until 2026-10-14, and this table is only ever
    // read for WiFi frames -- a radio a Flipper Zero does not have. What
    // it caught was the BUSY Bar, Flipper's desk sign, whose WiFi uses
    // that block (0c:fa:22:00:41:54 on the bench, beside three real
    // Zeros). The Zeros themselves advertise from 80:E1:26, which is in
    // nobody's IEEE registration, so it stays out as well; their three
    // service UUIDs below catch every one of them.

    // Hak5's WiFi Pineapple. Hak5 hold NO IEEE registration -- the whole
    // 40,000-row registry has no entry for them -- because their gear is
    // OpenWRT on ODM boards. What they do use is these two locally
    // administered addresses, which is a real habit and a real hint, but
    // bit 1 of the first octet is set on both: by construction they
    // identify no vendor and anybody can set them. LOW, and the test
    // suite enforces that no locally-administered row is ever graded
    // higher.
    {{0x02,0xC0,0xCA}, "Hak5-LA",    DetectionType::HACKER, Confidence::LOW_CONF},
    {{0x02,0x13,0x37}, "Hak5-LA",    DetectionType::HACKER, Confidence::LOW_CONF},
};
const uint16_t kOuiCount = sizeof(kOuiTable) / sizeof(kOuiTable[0]);

// 16-bit BLE service UUIDs. Note: SPP 0x1101 is the *16-bit* form of
// 00001101-0000-1000-8000-00805F9B34FB. Caller extracts the 16-bit
// UUID from the advertised service data.
const UuidEntry kUuidTable[] = {
    {0x1101, "Skim-SPP",   DetectionType::SKIMMER},   // Classic SPP
    {0xFEED, "Tile",       DetectionType::TILE},      // Tile, Inc. — Bluetooth SIG assigned
    {0xFEEC, "Tile",       DetectionType::TILE},      // Tile, Inc. — second SIG-assigned UUID
    {0xFD84, "Tile",       DetectionType::TILE},      // Tile, Inc. — third SIG-assigned UUID
    // Axon / TASER International's SIG-assigned UUIDs (OUI-SPY AXEOFF,
    // 2026-10-09; via upstream SquachWatch v1.38.0).
    {0xFC81, "Axon",       DetectionType::AXON},
    {0xFE6B, "Axon",       DetectionType::AXON},
    {0xFE6C, "Axon",       DetectionType::AXON},
    // More glasses: Meta Platforms' other two UUIDs, Snap's.
    {0xFEB7, "Meta",       DetectionType::META},
    {0xFEB8, "Meta",       DetectionType::META},
    {0xFE45, "Snap",       DetectionType::META},
    // DULT, the cross-maker "unwanted tracker" standard, for its label and
    // the extra rules; detection.cpp reads it from service data and only when
    // the tag is separated. Chipolo's own UUIDs, filed as trackers.
    {0xFCB2, "DULT",       DetectionType::GOOGLE_TAG},
    {0xFE33, "Chipolo",    DetectionType::GOOGLE_TAG},
    {0xFE65, "Chipolo",    DetectionType::GOOGLE_TAG},
    // Its own label, not "Meta": this UUID is what the Ray-Ban Meta glasses
    // advertise, and the company-ID rows below say Meta too -- which is any
    // Meta radio, Quest headsets included. Two different pages. The SIG
    // registers it to Meta Platforms Technologies, LLC and names no product
    // (checked 2026-10-09), so it is the glasses in practice, not by decree.
    {0xFD5F, "RayBanMeta", DetectionType::META},      // Ray-Ban Meta glasses
    {0x3100, "Raven",      DetectionType::RAVEN},     // Raven gunshot detector
    {0x3200, "Raven",      DetectionType::RAVEN},
    {0x3300, "Raven",      DetectionType::RAVEN},
    {0x3400, "Raven",      DetectionType::RAVEN},
    {0x3500, "Raven",      DetectionType::RAVEN},
    {0xFFFA, "DroneID",    DetectionType::DRONE},     // OpenDroneID
    {0xFD5A, "SmartTag",   DetectionType::SAMSUNG_TAG}, // Samsung's own SIG-assigned UUID for SmartTag discovery
    // Google LLC's Eddystone UUID. Every Eddystone retail beacon carries it
    // too, so the scanner matches it only as SERVICE DATA whose frame type is
    // 0x40 or 0x41 -- Google's Find My Device Network tracker frame (the FMDN
    // spec); 0x00/0x10/0x20/0x30 are the plain beacon frames. See
    // detection.cpp. The row is here for its label, and so DROP UUID 0xFEAA
    // can withdraw it.
    {0xFEAA, "FindMyDev",  DetectionType::GOOGLE_TAG},

    // Flipper Zero. It advertises 0x3080 ORed with its case colour (its own
    // firmware, serial_profile.c): 0x3081 black, 0x3082 white, 0x3083
    // transparent, and 0x3080 itself when the colour was never set. For our
    // purposes the colour does not matter; that there are exactly four fixed
    // values does, because it makes this an exact match rather than a guess.
    {0x3080, "Flipper",    DetectionType::HACKER},
    {0x3081, "Flipper",    DetectionType::HACKER},
    {0x3082, "Flipper",    DetectionType::HACKER},
    {0x3083, "Flipper",    DetectionType::HACKER},
};
const uint16_t kUuidCount = sizeof(kUuidTable) / sizeof(kUuidTable[0]);

// BT Classic device names (skimmers typically advertise as HC-05 etc).
const NameEntry kBtClassicNames[] = {
    {"HC-03",       DetectionType::SKIMMER},
    {"HC-05",       DetectionType::SKIMMER},
    {"HC-06",       DetectionType::SKIMMER},
    {"RN42",        DetectionType::SKIMMER},
    {"BT04-A",      DetectionType::SKIMMER},
    {"Flock_Setup", DetectionType::FLOCK},
    {"FS Ext Battery", DetectionType::FLOCK},
};
const uint16_t kBtClassicCount = sizeof(kBtClassicNames) / sizeof(kBtClassicNames[0]);

// WiFi SSID prefix matches (case-insensitive).
const SsidEntry kSsidPrefixes[] = {
    {"AB2-",    "Axon-Body2",   DetectionType::AXON},
    {"AB3-",    "Axon-Body3",   DetectionType::AXON},
    {"AB4-",    "Axon-Body4",   DetectionType::AXON},
    {"AXON-",   "Axon-Field",   DetectionType::AXON},
    {"flock-",  "Flock-Setup",  DetectionType::FLOCK},
    // A confirmed Flock hotspot's network, "Flock Camera net." (Flock-You
    // issue #43). The bare "Flock" and "flock-" above both miss it.
    {"Flock Camera", "Flock-Cam", DetectionType::FLOCK},

    // Hak5 WiFi Pineapple management AP. The documented default is
    // Pineapple_XXXX, the X being the last four of its MAC. It is the
    // setup network, so it is up on a fresh unit and on one somebody has
    // not renamed -- which is most of them, and none of the careful ones.
    {"Pineapple_", "Pineapple",  DetectionType::HACKER},

    // The ESP8266/ESP32 deauther family (DSTIKE and the many clones) puts
    // up its own control AP, default SSID "pwned", password "deauther".
    // A prefix rather than an exact match because the forks vary the
    // suffix; MED at best, since somebody can also just name their home
    // network this as a joke.
    {"pwned",   "Deauther",     DetectionType::HACKER},
};
const uint16_t kSsidCount = sizeof(kSsidPrefixes) / sizeof(kSsidPrefixes[0]);

// 16-bit BLE manufacturer IDs.
const MfgIdEntry kMfgIdTable[] = {
    {0x004C, "Apple",      DetectionType::AIRTAG},    // AirTag / FindMy
    // Registered to XUNTONG; seen in the adverts of Flock's "Penguin" battery
    // packs (Flock-You). A company ID on its own is weak evidence, so this
    // match is graded down unless the name agrees -- see detection.cpp.
    {0x09C8, "XUNTONG",    DetectionType::FLOCK},

    // ---- Camera glasses -------------------------------------------------
    // Service UUID 0xFD5F caught Ray-Ban Meta and nothing else. These are
    // the Bluetooth SIG company IDs the dedicated glasses-spotting apps
    // actually key on, which is what widens this from one product to the
    // category.
    //
    // The catch, and the reason META is no longer graded High: Meta uses
    // these same company IDs across their other Bluetooth products, Quest
    // headsets included. A hit here means a Meta radio nearby, not
    // necessarily a camera pointed at you. The 0xFD5F match remains the
    // specific one.
    {0x01AB, "Meta",       DetectionType::META},      // Meta Platforms
    // 0x058E (Meta Platforms Technologies) used to be here. It is what the
    // Quest headsets send, and no glasses capture has shown it; the real
    // Ray-Ban Meta sends 0x01AB with 0xFD5F (two independent captures,
    // 2026-07 and 2026-10). Taken out 2026-10-14.
    {0x0D53, "Luxottica",  DetectionType::META},      // Ray-Ban's manufacturer
    {0x03C2, "Snap",       DetectionType::META},      // Snap Spectacles
    {0x060C, "Vuzix",      DetectionType::META},      // Vuzix smart glasses

    // ---- Body cameras ----------------------------------------------------
    // TASER International, now Axon (SIG). OUI-SPY's AXEOFF keys on it.
    {0x034D, "Axon",       DetectionType::AXON},

    // ---- Trackers ----------------------------------------------------------
    {0x08C3, "Chipolo",    DetectionType::GOOGLE_TAG},

    // ---- Pentest hardware -----------------------------------------------
    // None. Flipper Devices' SIG company ID is 0x0E29 (not 0x0FBA, which
    // ESP32 Marauder comments it as: that is Cosonic, who make headsets),
    // and it sat here as HACKER until 2026-10-14. A Flipper Zero never sends
    // it -- its firmware's serial profile advertises only a service UUID and
    // a name, and three Zeros on the bench agreed -- while the BUSY Bar, a
    // desk sign, puts it in every advert until it is paired (its firmware's
    // ble_advertise.c). So the only thing the row ever caught was the sign.
};
const uint16_t kMfgIdCount = sizeof(kMfgIdTable) / sizeof(kMfgIdTable[0]);

// Apple's iBeacon, which is a fixed 25-byte manufacturer-data block:
//
//   4C 00   Apple's company ID, little endian
//   02      beacon type
//   15      remaining length, 21 bytes
//   ...     16-byte proximity UUID  (which deployment)
//   ...     2-byte major            (which site)
//   ...     2-byte minor            (which unit)
//   ...     1-byte measured power   (RSSI at one metre)
//
// Every one of those is fixed, so this is an exact match and not the kind
// of judgement call isAirTagPayload has to make. The length check is the
// whole test: 0x02 0x15 at that offset with 25 bytes behind it is an
// iBeacon, and an Apple device that is not one cannot accidentally look
// like one.
bool isIBeacon(const uint8_t* mfg, uint8_t len) {
    if (!mfg || len < 25) return false;
    if (mfg[0] != 0x4C || mfg[1] != 0x00) return false;   // Apple, little endian
    return mfg[2] == 0x02 && mfg[3] == 0x15;
}

// --- lookups ---


// ---- Extra rules, delivered without a firmware release ---------------------
// web-flasher/signatures.txt, signed into manifest-signatures.json by
// build_flasher_bins.sh and fetched by the boot check (ota_wifi.cpp). The
// rules are one line, entries separated by ';':
//
//   OUI aa:bb:cc TYPE [HIGH|MED|LOW] [Vendor]   an address prefix
//   SSID prefix TYPE [Vendor]                   a WiFi network-name prefix
//   UUID 0xFD5F TYPE [Vendor]                   a 16-bit BLE service UUID
//   CID 0x09C8 TYPE [Vendor]                    a BLE company ID (manufacturer data)
//   NAME prefix TYPE [Vendor]                   a BLE advertised-name prefix
//   DROP OUI|SSID|UUID|CID|NAME key             withdraw a rule, built in or not
//   GRADE aa:bb:cc HIGH|MED|LOW                 re-grade a known address prefix
//
// TYPE is a detectionTypeName() ("FLOCK", "AIRTAG", "EVIL_TWIN" for EVIL
// TWIN). The built-in tables are asked first, so an added rule can never
// change what this firmware already knows -- only DROP and GRADE can, and
// they say so in as many words. The Bluetooth kinds take their type's own
// grade, like the built-in Bluetooth tables. Anything that does not parse is
// skipped, not guessed at -- which is also what an older firmware does with
// a kind it has never heard of.
namespace {
enum XKind : uint8_t { XK_OUI, XK_SSID, XK_UUID, XK_CID, XK_NAME };
struct XOui   { uint8_t b[3]; DetectionType type; Confidence conf; char name[16]; };
struct XSsid  { char prefix[16]; DetectionType type; char name[16]; };
struct XBle   { uint8_t kind; uint16_t id; char prefix[16]; DetectionType type; char name[16]; };
struct XDrop  { uint8_t kind; uint8_t b[3]; uint16_t id; char key[16]; };
struct XGrade { uint8_t b[3]; Confidence conf; };
const uint8_t X_OUI_MAX = 64, X_SSID_MAX = 32, X_BLE_MAX = 32, X_DROP_MAX = 32, X_GRADE_MAX = 32;
// Allocated to the size of the set, not to the maximum: most boards carry few
// extra rules, and fixed tables would cost them kilobytes of RAM for nothing.
XOui*   g_xOui   = nullptr; uint8_t g_xOuiCap = 0,   g_xOuiN = 0;
XSsid*  g_xSsid  = nullptr; uint8_t g_xSsidCap = 0,  g_xSsidN = 0;
XBle*   g_xBle   = nullptr; uint8_t g_xBleCap = 0,   g_xBleN = 0;
XDrop*  g_xDrop  = nullptr; uint8_t g_xDropCap = 0,  g_xDropN = 0;
XGrade* g_xGrade = nullptr; uint8_t g_xGradeCap = 0, g_xGradeN = 0;

bool typeFromName(const char* s, DetectionType& out) {
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) {
        const char* n = detectionTypeName((DetectionType)t);
        bool same = true;
        size_t i = 0;
        for (; n[i] && s[i]; i++) {
            const char a = (char)toupper((unsigned char)s[i]);
            const char b = n[i] == ' ' ? '_' : n[i];
            if (a != b) { same = false; break; }
        }
        if (same && !n[i] && !s[i]) { out = (DetectionType)t; return true; }
    }
    return false;
}

int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

// "aa:bb:cc" into three bytes.
bool parseOui(const char* m, uint8_t out[3]) {
    if (strlen(m) != 8 || m[2] != ':' || m[5] != ':') return false;
    for (int b = 0; b < 3; b++) {
        const int hi = hexNib(m[b * 3]), lo = hexNib(m[b * 3 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[b] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

// "0xFD5F" (or "FD5F") into a 16-bit value.
bool parseHex16(const char* s, uint16_t& out) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    const size_t n = strlen(s);
    if (n < 1 || n > 4) return false;
    uint16_t v = 0;
    for (size_t i = 0; i < n; i++) {
        const int d = hexNib(s[i]);
        if (d < 0) return false;
        v = (uint16_t)(v * 16 + d);
    }
    out = v;
    return true;
}

bool parseConf(const char* s, Confidence& c) {
    if (strcasecmp(s, "HIGH") == 0) { c = Confidence::HIGH_CONF; return true; }
    if (strcasecmp(s, "MED")  == 0) { c = Confidence::MED_CONF;  return true; }
    if (strcasecmp(s, "LOW")  == 0) { c = Confidence::LOW_CONF;  return true; }
    return false;
}

bool kindFromName(const char* s, uint8_t& k) {
    if (strcasecmp(s, "OUI") == 0)  { k = XK_OUI;  return true; }
    if (strcasecmp(s, "SSID") == 0) { k = XK_SSID; return true; }
    if (strcasecmp(s, "UUID") == 0) { k = XK_UUID; return true; }
    if (strcasecmp(s, "CID") == 0)  { k = XK_CID;  return true; }
    if (strcasecmp(s, "NAME") == 0) { k = XK_NAME; return true; }
    return false;
}

// Up to `max` space-separated words of `s` into `w`.
uint8_t words(char* s, char** w, uint8_t max) {
    uint8_t n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        w[n++] = s;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = '\0';
    }
    return n;
}

void copyName(char* dst, const char* src) {
    size_t i = 0;
    for (; src && src[i] && i < 15; i++) dst[i] = src[i];
    dst[i] = '\0';
}

bool startsWithNoCase(const char* s, const char* prefix) {
    return s && prefix[0] && strncasecmp(s, prefix, strlen(prefix)) == 0;
}

template <typename T> void make(T*& p, uint8_t& cap, unsigned want, uint8_t max) {
    if (want > max) want = max;
    if (want && (p = (T*)calloc(want, sizeof(T))) != nullptr) cap = (uint8_t)want;
}
template <typename T> void drop(T*& p, uint8_t& cap, uint8_t& n) { free(p); p = nullptr; cap = 0; n = 0; }
}  // namespace

uint16_t setExtraRules(const char* body) {
    drop(g_xOui, g_xOuiCap, g_xOuiN);
    drop(g_xSsid, g_xSsidCap, g_xSsidN);
    drop(g_xBle, g_xBleCap, g_xBleN);
    drop(g_xDrop, g_xDropCap, g_xDropN);
    drop(g_xGrade, g_xGradeCap, g_xGradeN);
    if (!body || !*body) return 0;
    // Room for every entry of each kind the set could hold, capped.
    unsigned ouis = 0, ssids = 0, bles = 0, drops = 0, grades = 0;
    for (const char* q = body; *q; ) {
        while (*q == ' ' || *q == ';') q++;
        if      (strncasecmp(q, "OUI ", 4) == 0)   ouis++;
        else if (strncasecmp(q, "SSID ", 5) == 0)  ssids++;
        else if (strncasecmp(q, "UUID ", 5) == 0 || strncasecmp(q, "CID ", 4) == 0 ||
                 strncasecmp(q, "NAME ", 5) == 0)  bles++;
        else if (strncasecmp(q, "DROP ", 5) == 0)  drops++;
        else if (strncasecmp(q, "GRADE ", 6) == 0) grades++;
        while (*q && *q != ';') q++;
    }
    make(g_xOui, g_xOuiCap, ouis, X_OUI_MAX);
    make(g_xSsid, g_xSsidCap, ssids, X_SSID_MAX);
    make(g_xBle, g_xBleCap, bles, X_BLE_MAX);
    make(g_xDrop, g_xDropCap, drops, X_DROP_MAX);
    make(g_xGrade, g_xGradeCap, grades, X_GRADE_MAX);
    char entry[96];
    const char* p = body;
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != ';') n++;
        const size_t take = n < sizeof entry - 1 ? n : sizeof entry - 1;
        memcpy(entry, p, take);
        entry[take] = '\0';
        p += n + (p[n] == ';' ? 1 : 0);
        char* w[6];
        const uint8_t k = words(entry, w, 6);
        if (k < 3) continue;
        if (strcasecmp(w[0], "OUI") == 0 && g_xOuiN < g_xOuiCap) {
            XOui& x = g_xOui[g_xOuiN];
            if (!parseOui(w[1], x.b) || !typeFromName(w[2], x.type)) continue;
            x.conf = Confidence::MED_CONF;
            uint8_t nameAt = 3;
            if (k > 3 && parseConf(w[3], x.conf)) nameAt = 4;
            copyName(x.name, k > nameAt ? w[nameAt] : "");
            g_xOuiN++;
        } else if (strcasecmp(w[0], "SSID") == 0 && g_xSsidN < g_xSsidCap) {
            XSsid& x = g_xSsid[g_xSsidN];
            if (strlen(w[1]) > 15 || !typeFromName(w[2], x.type)) continue;
            copyName(x.prefix, w[1]);
            copyName(x.name, k > 3 ? w[3] : "");
            g_xSsidN++;
        } else if (g_xBleN < g_xBleCap &&
                   (strcasecmp(w[0], "UUID") == 0 || strcasecmp(w[0], "CID") == 0 ||
                    strcasecmp(w[0], "NAME") == 0)) {
            XBle& x = g_xBle[g_xBleN];
            kindFromName(w[0], x.kind);
            if (x.kind == XK_NAME) {
                if (strlen(w[1]) > 15) continue;
                copyName(x.prefix, w[1]);
            } else if (!parseHex16(w[1], x.id)) {
                continue;
            }
            if (!typeFromName(w[2], x.type)) continue;
            copyName(x.name, k > 3 ? w[3] : "");
            g_xBleN++;
        } else if (strcasecmp(w[0], "DROP") == 0 && g_xDropN < g_xDropCap) {
            XDrop& x = g_xDrop[g_xDropN];
            if (!kindFromName(w[1], x.kind)) continue;
            if (x.kind == XK_OUI) { if (!parseOui(w[2], x.b)) continue; }
            else if (x.kind == XK_UUID || x.kind == XK_CID) { if (!parseHex16(w[2], x.id)) continue; }
            else { if (strlen(w[2]) > 15) continue; copyName(x.key, w[2]); }
            g_xDropN++;
        } else if (strcasecmp(w[0], "GRADE") == 0 && g_xGradeN < g_xGradeCap) {
            XGrade& x = g_xGrade[g_xGradeN];
            if (!parseOui(w[1], x.b) || !parseConf(w[2], x.conf)) continue;
            g_xGradeN++;
        }
    }
    return (uint16_t)(g_xOuiN + g_xSsidN + g_xBleN + g_xDropN + g_xGradeN);
}

uint16_t extraRuleCount() { return (uint16_t)(g_xOuiN + g_xSsidN + g_xBleN + g_xDropN + g_xGradeN); }

static bool dropped(uint8_t kind, const uint8_t* b, uint16_t id, const char* s) {
    for (uint8_t i = 0; i < g_xDropN; i++) {
        const XDrop& d = g_xDrop[i];
        if (d.kind != kind) continue;
        if (kind == XK_OUI && b && b[0] == d.b[0] && b[1] == d.b[1] && b[2] == d.b[2]) return true;
        if ((kind == XK_UUID || kind == XK_CID) && id == d.id) return true;
        if ((kind == XK_SSID || kind == XK_NAME) && startsWithNoCase(s, d.key)) return true;
    }
    return false;
}

static const XOui* extraOui(const uint8_t* mac) {
    for (uint8_t i = 0; i < g_xOuiN; i++)
        if (mac[0] == g_xOui[i].b[0] && mac[1] == g_xOui[i].b[1] && mac[2] == g_xOui[i].b[2]) return &g_xOui[i];
    return nullptr;
}
static const XSsid* extraSsid(const char* ssid) {
    for (uint8_t i = 0; i < g_xSsidN; i++)
        if (strncasecmp(ssid, g_xSsid[i].prefix, strlen(g_xSsid[i].prefix)) == 0) return &g_xSsid[i];
    return nullptr;
}
static const XBle* extraBle(uint8_t kind, uint16_t id, const char* name) {
    for (uint8_t i = 0; i < g_xBleN; i++) {
        const XBle& x = g_xBle[i];
        if (x.kind != kind) continue;
        if (kind == XK_NAME ? startsWithNoCase(name, x.prefix) : x.id == id) return &x;
    }
    return nullptr;
}
static bool regraded(const uint8_t* mac, Confidence& c) {
    for (uint8_t i = 0; i < g_xGradeN; i++)
        if (mac[0] == g_xGrade[i].b[0] && mac[1] == g_xGrade[i].b[1] && mac[2] == g_xGrade[i].b[2]) {
            c = g_xGrade[i].conf;
            return true;
        }
    return false;
}

const char* ouiVendorName(const uint8_t* mac) {
    if (!mac || dropped(XK_OUI, mac, 0, nullptr)) return nullptr;
    for (uint16_t k = 0; k < kOuiCount; k++)
        if (mac[0] == kOuiTable[k].b[0] && mac[1] == kOuiTable[k].b[1] && mac[2] == kOuiTable[k].b[2])
            return kOuiTable[k].name;
    const XOui* x = extraOui(mac);
    return (x && x->name[0]) ? x->name : nullptr;
}
DetectionType lookupOui(const uint8_t* mac, Confidence* conf) {
    if (!mac || dropped(XK_OUI, mac, 0, nullptr)) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kOuiCount; i++) {
        if (mac[0] == kOuiTable[i].b[0] &&
            mac[1] == kOuiTable[i].b[1] &&
            mac[2] == kOuiTable[i].b[2]) {
            if (conf) {
                *conf = kOuiTable[i].conf;
                regraded(mac, *conf);             // a GRADE rule, since the build
            }
            return kOuiTable[i].type;
        }
    }
    if (const XOui* x = extraOui(mac)) {          // a rule added since the build
        if (conf) {
            *conf = x->conf;
            regraded(mac, *conf);
        }
        return x->type;
    }
    // Left alone rather than zeroed on a miss: the caller seeds it with the
    // type-level default before asking, so a non-OUI match keeps that.
    return DetectionType::UNKNOWN;
}

DetectionType lookupUuid(uint16_t uuid16) {
    if (dropped(XK_UUID, nullptr, uuid16, nullptr)) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kUuidCount; i++) {
        if (kUuidTable[i].uuid == uuid16) return kUuidTable[i].type;
    }
    if (const XBle* x = extraBle(XK_UUID, uuid16, nullptr)) return x->type;
    return DetectionType::UNKNOWN;
}

bool isBareSerialName(const char* name) {
    if (!name) return false;
    for (int i = 0; i < 10; i++) if (name[i] < '0' || name[i] > '9') return false;
    return name[10] == '\0';
}

// "FS-" and a hex serial, e.g. FS-BEC46A: a Flock accessory, seen at real
// Flock sites in 2026-06 (all-cameras-are-beacons). At least four hex digits
// and nothing else, so "FS-Living Room" is not one. (Upstream v1.38.0.)
bool isFlockFsName(const char* name) {
    if (!name || strncmp(name, "FS-", 3) != 0) return false;
    int n = 0;
    for (const char* p = name + 3; *p; p++, n++) {
        const char c = *p;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return n >= 4 && n <= 12;
}

DetectionType lookupBtName(const char* name) {
    if (!name || dropped(XK_NAME, nullptr, 0, name)) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kBtClassicCount; i++) {
        if (strcasecmp(name, kBtClassicNames[i].name) == 0) {
            return kBtClassicNames[i].type;
        }
    }
    // Substring matches for BLE advertised names
    if (strcasestr(name, "Flock"))    return DetectionType::FLOCK;
    if (strcasestr(name, "Penguin"))  return DetectionType::FLOCK;
    if (strcasestr(name, "Pigvision"))return DetectionType::FLOCK;
    // A Penguin battery pack on its other firmware names itself by its bare
    // ten-digit serial (Flock-You). Graded LOW where it is used.
    if (isBareSerialName(name))       return DetectionType::FLOCK;
    if (isFlockFsName(name))          return DetectionType::FLOCK;
    if (strcasestr(name, "Axon"))     return DetectionType::AXON;
    // Utility Inc.'s BodyWorn body cam: its activation remote advertises this
    // (nite-oui-collection field capture, via all-cameras-are-beacons).
    if (strcasestr(name, "BodyWorn")) return DetectionType::AXON;
    // A Flipper advertises "Flipper " followed by the unit's name. The
    // owner can change it, which is exactly why a name match is graded
    // down where it is used -- it is a string, not a signature. Left in
    // because the default is what most of them are still called, and it
    // costs nothing next to three exact signatures that cannot be typed.
    if (strcasestr(name, "Flipper"))  return DetectionType::HACKER;
    if (const XBle* x = extraBle(XK_NAME, 0, name)) return x->type;
    return DetectionType::UNKNOWN;
}

DetectionType lookupSsid(const char* ssid) {
    if (!ssid || dropped(XK_SSID, nullptr, 0, ssid)) return DetectionType::UNKNOWN;
    // A provisioned camera's bare "Flock" (Flock-You's firmware dump). Exact,
    // not a prefix: "Flock" as a prefix would take "Flockhart Family WiFi".
    if (strcasecmp(ssid, "Flock") == 0) return DetectionType::FLOCK;
    for (uint16_t i = 0; i < kSsidCount; i++) {
        size_t n = strlen(kSsidPrefixes[i].prefix);
        if (strncasecmp(ssid, kSsidPrefixes[i].prefix, n) == 0) {
            return kSsidPrefixes[i].type;
        }
    }
    if (const XSsid* x = extraSsid(ssid)) return x->type;
    return DetectionType::UNKNOWN;
}

const char* uuidName(uint16_t uuid16) {
    if (dropped(XK_UUID, nullptr, uuid16, nullptr)) return nullptr;
    for (uint16_t i = 0; i < kUuidCount; i++)
        if (kUuidTable[i].uuid == uuid16) return kUuidTable[i].name;
    const XBle* x = extraBle(XK_UUID, uuid16, nullptr);
    return (x && x->name[0]) ? x->name : nullptr;
}

const char* mfgIdName(uint16_t mfgId) {
    if (dropped(XK_CID, nullptr, mfgId, nullptr)) return nullptr;
    for (uint16_t i = 0; i < kMfgIdCount; i++)
        if (kMfgIdTable[i].mfgId == mfgId) return kMfgIdTable[i].name;
    const XBle* x = extraBle(XK_CID, mfgId, nullptr);
    return (x && x->name[0]) ? x->name : nullptr;
}

const char* btNameVendor(const char* name) {
    if (!name || dropped(XK_NAME, nullptr, 0, name)) return nullptr;
    const XBle* x = extraBle(XK_NAME, 0, name);
    return (x && x->name[0]) ? x->name : nullptr;
}

const char* ssidVendorName(const char* ssid) {
    if (!ssid || dropped(XK_SSID, nullptr, 0, ssid)) return nullptr;
    for (uint16_t i = 0; i < kSsidCount; i++) {
        size_t n = strlen(kSsidPrefixes[i].prefix);
        if (strncasecmp(ssid, kSsidPrefixes[i].prefix, n) == 0) {
            return kSsidPrefixes[i].name;
        }
    }
    if (const XSsid* x = extraSsid(ssid)) return x->name[0] ? x->name : nullptr;
    return nullptr;
}

DetectionType lookupMfgId(uint16_t mfgId) {
    if (dropped(XK_CID, nullptr, mfgId, nullptr)) return DetectionType::UNKNOWN;
    for (uint16_t i = 0; i < kMfgIdCount; i++) {
        if (kMfgIdTable[i].mfgId == mfgId) return kMfgIdTable[i].type;
    }
    if (const XBle* x = extraBle(XK_CID, mfgId, nullptr)) return x->type;
    return DetectionType::UNKNOWN;
}

// Ported from nyanBOX's airtag_detector.cpp (jbohack, MIT) -- known
// good against a real tag, which the previous subtype-only check was
// not. Scans the whole raw advertisement for either of two sequences.
//
// PATTERN 1: 1E FF 4C 00
//   0x1E is an AD structure LENGTH byte (30), 0xFF is the AD type
//   "manufacturer specific", 4C 00 is Apple. So this matches any Apple
//   manufacturer advert that is exactly 30 bytes -- NOT a subtype test,
//   which is what the old code here mistook it for. That is why this
//   file used to accept a subtype of 0x1E: there is no such Apple
//   subtype, and that branch never once fired.
//
// PATTERN 2: 4C 00 12 19
//   Apple, type 0x12 (Find My), payload length 0x19. The beacon a tag
//   sends once separated from its owner.
//
// KNOWN COST, accepted deliberately. Pattern 1 also matches Apple's
// Proximity Pairing advert (4C 00 07 19 ...), which is likewise 30
// bytes -- so AirPods and similar accessories will report as AIRTAG
// again, which is the false positive v1.5.0 removed by dropping 0x07.
// It is back on purpose: a freshly powered AirTag advertises 0x07, not
// 0x12, so rejecting it meant the detector could not see a tag at the
// exact moment someone is most likely to be testing it. Catching the
// tag matters more than the accessory noise.
//
// The way to get both is to look further into the Proximity Pairing
// payload, which carries a device model ID that separates a tag from
// headphones. That needs a capture of real bytes from both to get
// right, and guessing model IDs from memory is how this gets subtly
// wrong again.
bool isAirTagPayload(const uint8_t* payload, uint8_t len) {
    if (!payload || len < 4) return false;
    for (uint8_t i = 0; i + 3 < len; i++) {
        if (payload[i] == 0x1E && payload[i + 1] == 0xFF &&
            payload[i + 2] == 0x4C && payload[i + 3] == 0x00) return true;
        if (payload[i] == 0x4C && payload[i + 1] == 0x00 &&
            payload[i + 2] == 0x12 && payload[i + 3] == 0x19) return true;
    }
    return false;
}

// Where the Find My beacon starts (its 0x4C), or -1.
static int findMyAt(const uint8_t* payload, uint8_t len) {
    if (!payload || len < 4) return -1;
    for (uint8_t i = 0; i + 3 < len; i++)
        if (payload[i] == 0x4C && payload[i + 1] == 0x00 &&
            payload[i + 2] == 0x12 && payload[i + 3] == 0x19) return i;
    return -1;
}

bool isFindMyBeacon(const uint8_t* payload, uint8_t len) { return findMyAt(payload, len) >= 0; }

bool isFindMySeparated(const uint8_t* payload, uint8_t len) {
    const int i = findMyAt(payload, len);
    if (i < 0 || i + 4 >= len) return false;          // no status byte: not provably separated
    return (payload[i + 4] & 0x04) == 0;               // "maintained" clear
}

bool fmdnSeparated(const uint8_t* sd, size_t n) { return sd && n >= 1 && sd[0] == 0x41; }

int smartTagState(const uint8_t* sd, size_t n) { return (sd && n >= 1) ? (sd[0] >> 5) & 0x07 : -1; }

int dultNearOwner(const uint8_t* sd, size_t n) { return (sd && n >= 2) ? (sd[1] & 0x01) : -1; }

// The service data for a 16-bit UUID in a raw advert: AD type 0x16, its value
// the UUID (little-endian) then the data. Null when there is none.
static const uint8_t* serviceData16(const uint8_t* adv, uint8_t len, uint16_t uuid, size_t& n) {
    for (uint8_t i = 0; i + 1 < len; ) {
        const uint8_t l = adv[i];
        if (!l || i + 1 + l > len) break;
        if (adv[i + 1] == 0x16 && l >= 3 && (uint16_t)(adv[i + 2] | (adv[i + 3] << 8)) == uuid) {
            n = (size_t)(l - 3);
            return adv + i + 4;
        }
        i = (uint8_t)(i + 1 + l);
    }
    n = 0;
    return nullptr;
}

// A 16-bit UUID in the advert's service lists (AD types 0x02 / 0x03).
static bool listsUuid16(const uint8_t* adv, uint8_t len, uint16_t uuid) {
    for (uint8_t i = 0; i + 1 < len; ) {
        const uint8_t l = adv[i];
        if (!l || i + 1 + l > len) break;
        if (adv[i + 1] == 0x02 || adv[i + 1] == 0x03)
            for (uint8_t k = 2; k + 1 <= l; k += 2)
                if ((uint16_t)(adv[i + k] | (adv[i + k + 1] << 8)) == uuid) return true;
        i = (uint8_t)(i + 1 + l);
    }
    return false;
}

bool trackerVerdict(const uint8_t* adv, uint8_t len, TrackerVerdict& out) {
    if (!adv || !len) return false;
    size_t n = 0;
    const uint8_t* sd = nullptr;
    // Google Find Hub: its own frame types only (0x40 / 0x41), not Eddystone's.
    if ((sd = serviceData16(adv, len, 0xFEAA, n)) && n >= 1 && (sd[0] == 0x40 || sd[0] == 0x41)) {
        out.type = DetectionType::GOOGLE_TAG;
        out.away = fmdnSeparated(sd, n);
        out.conf = out.away ? Confidence::HIGH_CONF : Confidence::MED_CONF;
        return true;
    }
    if ((sd = serviceData16(adv, len, 0xFD5A, n)) || listsUuid16(adv, len, 0xFD5A)) {
        out.type = DetectionType::SAMSUNG_TAG;
        out.away = smartTagSeparated(smartTagState(sd, n));
        out.conf = out.away ? Confidence::HIGH_CONF : Confidence::MED_CONF;
        return true;
    }
    if ((sd = serviceData16(adv, len, 0xFCB2, n))) {
        if (dultNearOwner(sd, n) != 0) return false;   // beside its owner, or unreadable: not reported
        out.type = DetectionType::GOOGLE_TAG;
        out.away = true;
        out.conf = Confidence::HIGH_CONF;
        return true;
    }
    if (listsUuid16(adv, len, 0xFEED) || listsUuid16(adv, len, 0xFEEC) ||
        serviceData16(adv, len, 0xFEED, n) || serviceData16(adv, len, 0xFEEC, n)) {
        out.type = DetectionType::TILE;
        out.away = false;                              // Tile's advert says nothing either way
        out.conf = confidenceFor(DetectionType::TILE);
        return true;
    }
    if (isAirTagPayload(adv, len)) {
        out.type = DetectionType::AIRTAG;
        out.away = isFindMySeparated(adv, len);
        out.conf = out.away ? Confidence::HIGH_CONF
                 : isFindMyBeacon(adv, len) ? Confidence::MED_CONF : Confidence::LOW_CONF;
        return true;
    }
    return false;
}

// Walks the AD structures of a raw advertisement: length, type, data.
// Calls f(type, data, dataLen) for each; stops early if f returns true.
template <typename F>
static bool forEachAd(const uint8_t* p, uint8_t len, F f) {
    if (!p) return false;
    for (uint8_t i = 0; i + 1 < len; ) {
        const uint8_t l = p[i];
        if (l == 0 || i + 1 + l > len) break;
        if (f(p[i + 1], p + i + 2, (uint8_t)(l - 1))) return true;
        i = (uint8_t)(i + 1 + l);
    }
    return false;
}

bool isAxonBwcPayload(const uint8_t* payload, uint8_t len) {
    // "BWCDEVICE" forwards, and backwards: in the field it rides inside a
    // 128-bit UUID (AD 0x21), which is little-endian, so the bytes read
    // "...ECIVEDCWB" (all-cameras-are-beacons, owner-confirmed 2026-09-19).
    static const char F[] = "BWCDEVICE";
    const uint8_t n = 9;
    if (!payload || len < n) return false;
    for (uint8_t i = 0; i + n <= len; i++) {
        bool fw = true, bw = true;
        for (uint8_t k = 0; k < n && (fw || bw); k++) {
            if (payload[i + k] != (uint8_t)F[k]) fw = false;
            if (payload[i + k] != (uint8_t)F[n - 1 - k]) bw = false;
        }
        if (fw || bw) return true;
    }
    return false;
}

bool isFmdnPayload(const uint8_t* payload, uint8_t len) {
    return forEachAd(payload, len, [](uint8_t type, const uint8_t* d, uint8_t n) {
        return type == 0x16 && n >= 3 && d[0] == 0xAA && d[1] == 0xFE && (d[2] == 0x40 || d[2] == 0x41);
    });
}

uint16_t serviceDataUuid(const uint8_t* payload, uint8_t len) {
    uint16_t found = 0;
    forEachAd(payload, len, [&](uint8_t type, const uint8_t* d, uint8_t n) {
        if (type != 0x16 || n < 2) return false;
        const uint16_t u = (uint16_t)(d[0] | (d[1] << 8));
        // Their own checks in detection.cpp, which read the frame: Find Hub,
        // Remote ID, SmartTag, and DULT (separated only).
        if (u == 0xFEAA || u == 0xFFFA || u == 0xFD5A || u == 0xFCB2) return false;
        for (uint16_t i = 0; i < kUuidCount; i++)
            if (kUuidTable[i].uuid == u) { found = u; return true; }
        return false;
    });
    return found;
}

Confidence confidenceFor(DetectionType t) {
    // Per docs/DETECTIONS.md. FLOCK/AXON/META/SKIMMER/CAMERA are graded
    // High there for the signature path actually active in v1.0 (the
    // wildcard-probe and ESP32-generic-fallback ideas mentioned in that
    // doc as lower-confidence alternates aren't implemented — see the
    // note at the top of kOuiTable). RAVEN/AIRTAG/DRONE/ALPR are graded
    // Medium — unverified against real hardware, address rotation, or
    // thin OUI coverage, respectively. SAMSUNG_TAG is High: 0xFD5A is
    // Samsung's own dedicated SIG-assigned UUID, not shared with
    // anything else. GOOGLE_TAG is High since 2026-10-09: it used to match
    // 0xFEAA alone, Eddystone's UUID, which every retail beacon shares; it
    // now needs the Find My Device Network's own frame type in the service
    // data (0x40/0x41), which no plain beacon sends. TILE
    // is High: 0xFEED/0xFEEC are both Bluetooth SIG-assigned exclusively
    // to Tile, Inc. RING is High: real MA-L registry OUI matches, same
    // evidentiary basis as CAMERA.
    switch (t) {
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::SKIMMER:
        case DetectionType::CAMERA:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::TILE:
        case DetectionType::RING:
        case DetectionType::GOOGLE_TAG:
        // Pattern-based rather than a signature, but a specific and
        // hard-to-fake one: two BSSIDs claiming one SSID from different
        // vendors while disagreeing about encryption. A mesh network --
        // the obvious false positive, and the thing that sank the
        // earlier vendor-only test -- never disagrees with itself about
        // security. High.
        // Exact: the whole iBeacon header is fixed by Apple's format and the
        // block is a fixed length, so a match cannot be a coincidence. What
        // it is NOT is a claim about intent -- see the docs.
        case DetectionType::IBEACON:
        case DetectionType::EVILTWIN:
            return Confidence::HIGH_CONF;
        // Was High when it was only Ray-Ban Meta's 0xFD5F service UUID, which
        // is specific to the glasses. Broadening it to the Meta, Luxottica and
        // Snap company IDs catches the rest of the category and costs that
        // precision: Meta puts the same IDs on Quest headsets. Medium is the
        // conservative grade the rule at the top of this function asks for.
        case DetectionType::META:
        case DetectionType::RAVEN:
        case DetectionType::AIRTAG:
        case DetectionType::DRONE:
        case DetectionType::ALPR:
        // Rate-thresholded (see DetectionEngine's deauth-flood
        // tracking), not a single-frame guess -- a real burst pattern,
        // but the threshold/window are still heuristic, so Medium
        // rather than High.
        case DetectionType::DEAUTH:
        // The BASE grade only, and it is the one the weakest members of
        // this bucket deserve: an SSID beginning "pwned" or a BLE name
        // beginning "Flipper" is a string anybody can type. The exact
        // signatures -- the three Flipper Zero service UUIDs and a
        // Pwnagotchi announcing its own handshake count --
        // set HIGH explicitly at their match sites, and the OUI rows carry
        // their own. This is the value a caller gets when nothing more
        // specific was established.
        case DetectionType::HACKER:
            return Confidence::MED_CONF;
        default:
            return Confidence::LOW_CONF;
    }
}

const char* confidenceLabel(Confidence c) {
    switch (c) {
        case Confidence::HIGH_CONF:   return "HIGH CONF";
        case Confidence::MED_CONF: return "MED CONF";
        default:                 return "LOW CONF";
    }
}

uint8_t confidencePercent(Confidence c) {
    switch (c) {
        case Confidence::HIGH_CONF:   return 90;
        case Confidence::MED_CONF: return 60;
        default:                 return 30;
    }
}

// A pwnagotchi finds other pwnagotchis by stuffing a JSON blob into a
// vendor information element in its own beacon frames -- a private
// protocol riding inside a standard frame. It is not obfuscated in any
// way: the blob is plain ASCII and carries the unit's name, version,
// uptime, handshake count and whether deauth is switched on.
//
// So this does not parse JSON. Pulling in a parser to run inside the
// promiscuous callback would cost heap and time on every beacon in the
// air, and there are two things worth having: whether the blob is there,
// and the name. Both are byte scans.
//
// "pwnd_tot" is the key that makes this a pwnagotchi rather than any
// other JSON-in-a-beacon: it is the handshake counter, and nothing else
// advertises one. Requiring it means a beacon that merely contains a
// brace cannot match.
//
// Returns false and leaves `out` untouched unless both are found.
bool pwnagotchiName(const uint8_t* frame, uint32_t len,
                           char* out, size_t outSz) {
    if (!frame || !out || outSz < 2) return false;
    // 24-byte header plus the 12 fixed beacon parameters; the information
    // elements, and so anything a pwnagotchi added, start after that.
    const uint32_t start = 36;
    if (len <= start) return false;
    // Cap the scan. A beacon has no business being longer than this, and a
    // malformed sig_len must not walk this loop off the end of the buffer.
    if (len > 512) len = 512;
    const char* body = (const char*)frame;

    bool haveTot = false;
    static const char KEY_TOT[] = "pwnd_tot";
    const uint32_t totLen = sizeof(KEY_TOT) - 1;
    for (uint32_t i = start; i + totLen <= len; i++) {
        if (memcmp(body + i, KEY_TOT, totLen) == 0) { haveTot = true; break; }
    }
    if (!haveTot) return false;

    // Then the name, which is a quoted string value: "name":"foo". The
    // spacing varies between pwnagotchi versions, so this walks to the
    // colon and then to the opening quote rather than assuming a layout.
    static const char KEY_NAME[] = "\"name\"";
    const uint32_t nameLen = sizeof(KEY_NAME) - 1;
    for (uint32_t i = start; i + nameLen <= len; i++) {
        if (memcmp(body + i, KEY_NAME, nameLen) != 0) continue;
        uint32_t j = i + nameLen;
        while (j < len && (body[j] == ' ' || body[j] == ':')) j++;
        if (j >= len || body[j] != '"') return false;
        j++;
        size_t o = 0;
        while (j < len && body[j] != '"' && o + 1 < outSz) {
            // Printable ASCII only. This string is attacker-controlled and
            // lands in a field the log renders; a control character in it
            // would be somebody else deciding what our screen does.
            if (body[j] < 0x20 || body[j] > 0x7E) return false;
            out[o++] = body[j++];
        }
        out[o] = '\0';
        return o > 0;
    }
    return false;
}
