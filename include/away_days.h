// MuleSkin-CYD — trackers that turn up near you on several days, away from home.
//
// A tracker away from its owner seen on three different days is the "near you
// on N days" alert. Counted plainly, a board that never leaves home raises it
// for a neighbour's tag in a drawer next door (seen 2026-10-13: one AirTag,
// every hour of four days, never near the board's owner). So a day counts
// only if the tracker was heard while the board was AWAY from home -- home
// being the WiFi network marked USE, heard in the last few minutes
// (DetectionEngine::homeInView). A tag following you is heard in different
// places on different days; one beside your house is heard only at home.
// With no home network set, every day counts, as before.
//
// Pure logic, host-tested (test/away_days_test.cpp). main.cpp feeds it once a
// minute from the log, asks due(), and keeps the table in NVS (save()/load()).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace AwayDays {

const uint8_t SLOTS      = 16;   // trackers followed; the one heard longest ago makes room
const uint8_t DAYS_ALERT = 3;    // away-from-home days that make the alert

struct Slot { uint8_t mac[6]; uint32_t lastDay; uint8_t days; uint32_t toldDay; };

class Table {
public:
    // A tracker away from its owner heard today. `away` = the board is away
    // from home (or has no home set). Counts at most once a day.
    void    note(const uint8_t mac[6], uint32_t day, bool away);
    uint8_t days(const uint8_t mac[6]) const;
    // True once a day for a tracker on DAYS_ALERT days or more: the alert.
    bool    due(const uint8_t mac[6], uint32_t day);
    // Changed since the last take: time to save.
    bool    takeDirty() { const bool d = _dirty; _dirty = false; return d; }
    // The table as bytes, for NVS, and back. load() ignores a blob of the
    // wrong size (a different version's).
    size_t  save(uint8_t* out, size_t cap) const;
    void    load(const uint8_t* in, size_t n);
    void    clear() { _n = 0; _dirty = true; }
    // For AWAYDAYS LIST / DEL (the console and the web flasher).
    uint8_t     count() const { return _n; }
    const Slot* at(uint8_t i) const { return i < _n ? &_s[i] : nullptr; }
    bool        remove(const uint8_t mac[6]);
private:
    int  find(const uint8_t mac[6]) const;
    Slot _s[SLOTS];
    uint8_t _n = 0;
    bool _dirty = false;
};

}  // namespace AwayDays
