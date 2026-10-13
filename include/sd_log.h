// MuleSkin-CYD — optional SD card event log
// If the SD card is mounted at boot, each Detection is appended to
// /MuleSkin-YYYYMMDD.log as one CSV line, one file per local day. Until the
// clock is trusted the rows go to /MuleSkin-nodate.log, because a guessed
// date can be days behind. (From upstream SquachWatch v1.36.0, PR #46 by
// jotra7; the file used to be /MuleSkin-0.log on every board, forever.)
// Row columns: time (UTC "YYYY-MM-DDTHH:MM:SSZ", or millis() since boot when
// the clock is not trusted), type, rssi, mac, channel, vendor, name.
// If the card is absent, every call is a silent no-op.
#pragma once
#include <Arduino.h>
#include "state.h"

class SdLog {
public:
    bool begin();              // returns true if card mounted
    bool ready() const { return _ready; }
    void logEvent(const Detection& d);
    void tick();               // flush / housekeeping (called from loop)
    // Deletes every MuleSkin log file on the card. For the security wipe --
    // the phrase and the ignore list live in NVS, but the detection history a
    // wipe must also erase is here. A no-op when no card is mounted.
    void wipe();
private:
    bool     _ready = false;
    uint32_t _lastFlush = 0;
    uint32_t _day = 0;           // the local day _filename is for; 0 is nodate
    char     _filename[32] = {0};
    void     openDaily();
};
