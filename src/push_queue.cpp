// MuleSkin-CYD — PUSH ALERTS' rules. See include/push_queue.h.
#include "push_queue.h"
#include <stdlib.h>
#include <string.h>

namespace PushQueue {

bool important(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::ALPR:
        case DetectionType::RAVEN:
        case DetectionType::CAMERA:
        case DetectionType::META:
        case DetectionType::SKIMMER:
        case DetectionType::DEAUTH:
        case DetectionType::EVILTWIN:
            return true;
        default:
            return false;
    }
}

bool wanted(What what, bool nightWithYouOnly, bool night, DetectionType t, bool withYou) {
    if (withYou) return true;                       // the one that is about you: always
    if (night && nightWithYouOnly) return false;
    switch (what) {
        case What::ALL:       return true;
        case What::IMPORTANT: return important(t);
        default:              return false;
    }
}

What atHome(What what, uint8_t homeMode, bool home) {
    if (!home || homeMode == 0) return what;
    const What cap = homeMode == 1 ? What::IMPORTANT : What::WITH_YOU;
    return (uint8_t)what < (uint8_t)cap ? what : cap;
}

CapSay DailyCap::take(uint32_t day) {
    if (day != _day) { _day = day; _sent = 0; _over = 0; _told = false; }
    if (_sent < DAILY_CAP) { _sent++; return CapSay::SEND; }
    if (_over < 0xFFFF) _over++;
    if (!_told) { _told = true; return CapSay::TELL; }
    return CapSay::HOLD;
}

bool Cooldown::allow(const uint8_t mac[6], uint32_t now) {
    for (uint8_t i = 0; i < _n; i++) {
        if (memcmp(_mac[i], mac, 6) != 0) continue;
        if ((int32_t)(now - _at[i]) < (int32_t)COOLDOWN_MS) return false;
        _at[i] = now;
        return true;
    }
    uint8_t slot = _n;
    if (_n < COOLDOWN_SLOTS) {
        _n++;
    } else {
        slot = 0;   // full: the one pushed longest ago makes room
        for (uint8_t i = 1; i < _n; i++)
            if ((int32_t)(_at[i] - _at[slot]) < 0) slot = i;
    }
    memcpy(_mac[slot], mac, 6);
    _at[slot] = now;
    return true;
}

bool Queue::add(const Msg& m, uint32_t now, bool urgent) {
    if (!_q) {
        _q  = (Msg*)calloc(CAP, sizeof(Msg));
        _at = (uint32_t*)calloc(CAP, sizeof(uint32_t));
        if (!_q || !_at) { free(_q); free(_at); _q = nullptr; _at = nullptr; return false; }
    }
    if (_n == CAP) {
        // Full: the oldest goes -- unless it is out on a join right now, in
        // which case the newest waits its turn rather than racing the send.
        if (_out) return false;
        dropFront(1);
    }
    _q[_n]  = m;
    _at[_n] = now;
    _n++;
    if (urgent) _urgent = true;
    return true;
}

void Queue::dropFront(uint8_t k) {
    if (k > _n) k = _n;
    memmove(&_q[0], &_q[k], sizeof(Msg) * (_n - k));
    memmove(&_at[0], &_at[k], sizeof(uint32_t) * (_n - k));
    _n = (uint8_t)(_n - k);
    if (!_n) _urgent = false;
}

// Ages are compared signed throughout: a message stamped with millis() a
// moment AFTER the caller read `now` -- queued earlier in the same pass -- is
// a few milliseconds in the future, and unsigned that is four billion
// milliseconds old: dropped as stale on the spot. (It was, on hardware: the
// first daily summary vanished that way.)
static inline int32_t age(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

bool Queue::due(uint32_t now) {
    if (_out || !_n) return false;
    while (_n && age(now, _at[0]) > (int32_t)STALE_MS) dropFront(1);
    if (!_n) return false;
    if (_retry && age(now, _retryAt) < 0) return false;
    if (_urgent) return true;
    if (age(now, _at[0]) < (int32_t)GATHER_MS) return false;               // gather a burst
    if (_last && age(now, _last) < (int32_t)GAP_MS) return false;          // not join after join
    return true;
}

void Queue::sending(uint32_t now) {
    _out   = _n;
    _last  = now;
    _retry = false;
}

void Queue::finished(uint8_t sent, bool ok, uint32_t now) {
    if (sent > _out) sent = _out;
    _out = 0;
    dropFront(sent);
    if (!ok) { _retry = true; _retryAt = now + RETRY_MS; }
    // What was urgent has gone, or failed and waits like the rest.
    if (sent || !ok) _urgent = false;
}

void Queue::clear() {
    _n = _out = 0;
    _urgent = _retry = false;
}

}  // namespace PushQueue
