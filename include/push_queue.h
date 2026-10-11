// MuleSkin-CYD — PUSH ALERTS' rules: which alerts go, and when a batch is sent.
//
// Pure logic, host-tested (test/push_queue_test.cpp); main.cpp feeds it and
// ota_wifi.cpp does the sending. Nothing here touches a radio or a clock: the
// caller passes `now` in milliseconds and the facts about the alert.
//
// WHICH ALERTS (PUSH WHAT, Settings): WITH YOU only; IMPORTANT -- WITH YOU plus
// the surveillance and attack types (Flock, Axon, plate readers, Raven,
// cameras, camera glasses, skimmers, deauth floods, evil twins); or ALL, every
// alert the screen raises. And PUSH AT NIGHT: through the night hours (NIGHT
// DIM's, or 11 PM-5 AM), everything as usual, or WITH YOU only.
//
// WHEN A BATCH GOES. As they happen, but not one join per alert: the first
// waits GATHER_MS for any that come with it, joins are GAP_MS apart, and a
// WITH YOU goes at once. A batch that did not get through waits RETRY_MS. The
// queue keeps the CAP newest, and drops any older than STALE_MS -- half an hour
// on, an alert is history, not news.
//
// ONE PER DEVICE AN HOUR (Cooldown). A camera on a street you sit beside, or a
// tracker that drops in and out of range, would otherwise push every time it
// comes back -- dozens a day, and through ntfy.sh's relay to an iPhone that
// ran into its daily limit (2026-10). An ordinary alert for a device pushed
// in the last COOLDOWN_MS is not pushed again; a WITH YOU always goes (it
// fires once a follow anyway). The table is small: when it is full, the
// oldest entry goes, which at worst lets one device through a little early.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "state.h"

namespace PushQueue {

// `seq`, when set, is ntfy's sequence ID: a later message with the same one
// REPLACES the earlier notification instead of adding another (ntfy server
// 2.16+, the Android and web apps; the iPhone app, as of 1.7, shows each).
// One per device, so a camera that keeps alerting is one notification.
// `act`, when set, is the body of the notification's IGNORE button: the phone
// posts it to the board's command topic (ota_wifi.cpp), and the board picks
// it up the next time it joins.
struct Msg { char title[40]; char body[100]; uint8_t prio; char tags[24]; char seq[24]; char act[24]; };

const uint8_t  CAP       = 8;
const uint32_t GATHER_MS = 3000;
const uint32_t GAP_MS    = 20000;
const uint32_t RETRY_MS  = 120000;
const uint32_t STALE_MS  = 30u * 60u * 1000u;

enum class What : uint8_t { WITH_YOU = 0, IMPORTANT = 1, ALL = 2 };

// The surveillance and attack types IMPORTANT sends.
bool important(DetectionType t);
// Whether this alert should be pushed at all. `withYou` for a WITH YOU;
// `night` is whether it is the night hours now, and `nightWithYouOnly`
// whether PUSH AT NIGHT is set to WITH YOU.
bool wanted(What what, bool nightWithYouOnly, bool night, DetectionType t, bool withYou);

// PUSH AT HOME (Settings::pushHome): 0 as set, 1 IMPORTANT at most, 2 WITH
// YOU only -- while the board can see the home network. What PUSH WHAT
// becomes there; never more than it is.
What atHome(What what, uint8_t homeMode, bool home);

// A DAY'S CAP. However the hour's cooldown spreads them, a busy day can still
// mean a push every few minutes. Past DAILY_CAP ordinary alerts in one local
// day the rest stay on the board: the first one over says so (TELL), the rest
// are held quietly, and the evening summary counts them. WITH YOU is never
// capped. The day is the caller's (Clock::localDay(), 0 before the clock is
// set); a new day starts the count again.
const uint16_t DAILY_CAP = 30;
enum class CapSay : uint8_t { SEND, TELL, HOLD };
class DailyCap {
public:
    CapSay   take(uint32_t day);
    uint16_t sent(uint32_t day) const { return day == _day ? _sent : 0; }
    uint16_t over(uint32_t day) const { return day == _day ? _over : 0; }
private:
    uint32_t _day = 0xFFFFFFFFu;
    uint16_t _sent = 0, _over = 0;
    bool     _told = false;
};

const uint32_t COOLDOWN_MS    = 60u * 60u * 1000u;
const uint8_t  COOLDOWN_SLOTS = 24;

class Cooldown {
public:
    // True if an alert for `mac` may be pushed at `now`, and if so notes it.
    bool allow(const uint8_t mac[6], uint32_t now);
    void clear() { _n = 0; }
private:
    uint8_t  _mac[COOLDOWN_SLOTS][6];
    uint32_t _at[COOLDOWN_SLOTS];
    uint8_t  _n = 0;
};

class Queue {
public:
    // Queued at `now`. False if there was no memory for the queue (made at
    // the first add, so a board with push off never pays for it). Full:
    // the oldest goes.
    bool add(const Msg& m, uint32_t now, bool urgent);
    uint8_t    count() const { return _n; }
    const Msg* items() const { return _q; }
    // Waiting for a send, retrying or not: for the title-bar pill.
    bool       waiting() const { return _n > _out; }
    // Ready for a join now? Drops stale entries first.
    bool       due(uint32_t now);
    // A join started with every queued message.
    void       sending(uint32_t now);
    // It ended: the first `sent` went; `ok` false means try again later.
    void       finished(uint8_t sent, bool ok, uint32_t now);
    bool       busy() const { return _out > 0; }
    void       clear();
private:
    void dropFront(uint8_t k);
    Msg*      _q = nullptr;
    uint32_t* _at = nullptr;
    uint8_t   _n = 0, _out = 0;
    bool      _urgent = false;
    uint32_t  _last = 0, _retryAt = 0;
    bool      _retry = false;
};

}  // namespace PushQueue
