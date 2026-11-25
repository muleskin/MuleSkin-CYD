// MuleSkin-CYD -- part of src/main.cpp, included where it stood:
// PUSH ALERTS: each alert to an ntfy server over a short join of a saved
// network, the board's id and Bluetooth name the pushes carry, and the daily
// summary. Which alerts go and when is include/push_queue.h (host-tested).
//
// One translation unit with main.cpp: this uses the globals and helpers
// declared above its #include there, and what follows it uses this, so
// it is not compiled on its own and its place in main.cpp matters.

// PUSH ALERTS (Settings, off by default): each alert to an ntfy server, so it
// lands on the phone as a real push notification -- lock screen, no page left
// open. The board borrows the radio for a short join of whichever saved
// network is in range (home, work, or the phone's own hotspot), posts, and
// lets go: a couple of seconds of blind scanning per batch. Which alerts go,
// and when a batch is sent, is include/push_queue.h (host-tested); this feeds
// it and starts the joins.
static PushQueue::Queue s_pushQ;
static bool             s_pushRadio = false;
static bool             s_pushOnLink = false;   // the batch out is on HUMAN SENSING's link

static bool pushNightNow() {
    if (!Clock::trusted()) return false;
    // PUSH AT NIGHT's own hours, when the web flasher set some.
    const uint8_t qs = Settings::pushQuietStart(), qe = Settings::pushQuietEnd();
    if (qs <= 23 && qe <= 23) {
        const uint8_t h = Clock::hour();
        return qs < qe ? (h >= qs && h < qe) : (h >= qs || h < qe);
    }
    const uint8_t p = Settings::nightMode();
    return p ? NightMode::active(p, Clock::hour()) : Clock::night();
}

// The name at the front of every push: the one set (PUSH NAME), or this
// board's Bluetooth name -- MuleSkin- and the last two bytes of its address,
// the same name a Bluetooth update's picker shows.
// This board's short id: the last two bytes of its Bluetooth address, "5AF6"
// -- what tells two boards on one topic apart in a command (ota_wifi.h).
static const char* boardId() {
    static char id[8] = "";
    if (!id[0]) {
#if defined(ARDUINO_ARCH_ESP32)
        uint8_t mac[6] = { 0 };
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(id, sizeof id, "%02X%02X", mac[4], mac[5]);
#else
        snprintf(id, sizeof id, "0000");
#endif
    }
    return id;
}

// Its Bluetooth name, "MuleSkin-5AF6".
static const char* btBoardName() {
    static char name[16] = "";
    if (!name[0]) snprintf(name, sizeof name, "MuleSkin-%s", boardId());
    return name;
}

static const char* pushBoardName() {
    return Settings::pushName()[0] ? Settings::pushName() : btBoardName();
}

static void queuePush(const char* title, const char* body, uint8_t prio, const char* tags, bool urgent,
                      const char* seq = nullptr, const char* act = nullptr) {
    if (!Settings::pushOn()) return;
    PushQueue::Msg m{};
    snprintf(m.title, sizeof m.title, "%s: %s", pushBoardName(), title);
    char when[12] = "";
    if (Clock::isSet()) {
        bool pm = false;
        Clock::formatTime(when, sizeof when, true, &pm);
        strncat(when, pm ? " PM" : " AM", sizeof when - strlen(when) - 1);
    }
    snprintf(m.body, sizeof m.body, "%s%s%s", body, when[0] ? ", " : "", when);
    m.prio = prio;
    snprintf(m.tags, sizeof m.tags, "%s", tags);
    if (seq) snprintf(m.seq, sizeof m.seq, "%s", seq);
    if (act) snprintf(m.act, sizeof m.act, "%s", act);
    s_pushQ.add(m, millis(), urgent);
}

static PushQueue::Cooldown s_pushCool;   // one push per device an hour (push_queue.h)
static uint16_t s_pushCooled = 0;        // held back by it today: the self-test's count
static PushQueue::DailyCap s_pushCap;    // DAILY_CAP ordinary alerts a day (push_queue.h)

static uint32_t pushDay() { return Clock::isSet() ? Clock::localDay() : 0; }

// The ignore list, kept in step across the boards on one push topic. A change
// made HERE -- the alert's IGNORE, the IGNORED DEVICES screen, the console or
// the web flasher -- goes to the command topic as "SYNC IGNORE <mac> <type>
// <board>" (or SYNC UNIGNORE), and every other board applies it without a
// word. A change that ARRIVED from the topic is applied with s_ignSyncMute set,
// so it is never sent on again: no echo between boards.
static bool s_ignSyncMute = false;
static void ignoreSync(const uint8_t* mac, DetectionType type, bool added) {
    if (s_ignSyncMute || !Settings::pushOn() || !Settings::pushUrl()[0]) return;
    PushQueue::Msg m{};
    m.cmd = true;
    if (added)
        snprintf(m.body, sizeof m.body, "SYNC IGNORE %02x%02x%02x%02x%02x%02x %u %s",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned)type, boardId());
    else
        snprintf(m.body, sizeof m.body, "SYNC UNIGNORE %02x%02x%02x%02x%02x%02x %s",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], boardId());
    s_pushQ.add(m, millis(), false);
    Serial.printf("[ignore] %s, for the other boards\n", added ? "added" : "removed");
}

// PAUSE 2 H from a notification: ordinary pushes from this board held until
// then (WITH YOU still goes). RAM only -- a restart ends it.
static uint32_t s_pushPausedUntil = 0;
static uint32_t pushPausedMin() {
    if (!s_pushPausedUntil) return 0;
    const int32_t left = (int32_t)(s_pushPausedUntil - millis());
    if (left <= 0) { s_pushPausedUntil = 0; return 0; }
    return (uint32_t)left / 60000u + 1;
}

static void queuePushAlert(const Detection& d, uint16_t followMins) {
    if (!Settings::pushOn()) return;
    PushQueue::Rules rules;
    rules.what             = (PushQueue::What)Settings::pushWhat();
    rules.chosen           = Settings::pushTypes();
    rules.nightWithYouOnly = Settings::pushNightWithYou();
    rules.night            = pushNightNow();
    rules.homeMode         = Settings::pushHome();
    rules.home             = engine.homeInView(millis());
    if (!PushQueue::wanted(rules, d.type, followMins != 0)) return;
    if (!followMins && pushPausedMin()) {
        s_pushCooled++;
        Serial.printf("[push] paused: not pushing %s (%lu min more)\n", detectionTypeName(d.type), (unsigned long)pushPausedMin());
        return;
    }
    // Not for a room's movement: postKeyed() already makes it news only after
    // five still minutes, and somebody watching a room wants each time it
    // starts moving again. DAILY_CAP below still holds it.
    if (!followMins && d.type != DetectionType::MOTION && !s_pushCool.allow(d.mac, millis())) {
        s_pushCooled++;
        Serial.printf("[push] %s %02x:%02x:%02x:%02x:%02x:%02x pushed within the hour; not again\n",
                      detectionTypeName(d.type), d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
        return;
    }
    if (!followMins) {
        const PushQueue::CapSay say = s_pushCap.take(pushDay());
        if (say == PushQueue::CapSay::HOLD) return;
        if (say == PushQueue::CapSay::TELL) {
            char b[100];
            snprintf(b, sizeof b, "%u alerts pushed today. The rest stay on the board until tomorrow; WITH YOU still comes.",
                     (unsigned)PushQueue::DAILY_CAP);
            queuePush("daily push limit", b, 3, "no_bell", false, "daily-limit");
            Serial.println("[push] daily limit reached");
            return;
        }
    }
    char title[40], body[100], seq[24], act[32];
    // The notification's IGNORE button posts this back (see ota_wifi.h).
    snprintf(act, sizeof act, "IGNORE %02x%02x%02x%02x%02x%02x %u %s",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5], (unsigned)d.type, boardId());
    const char* vendor = vendorText(d);
    // One notification per device (where the phone's app supports replacing
    // one), and a WITH YOU its own, so it never overwrites an ordinary alert.
    snprintf(seq, sizeof seq, "%s-%02x%02x%02x%02x%02x%02x", followMins ? "withyou" : "dev",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    if (followMins) {
        snprintf(title, sizeof title, "WITH YOU: %s", detectionTypeName(d.type));
        snprintf(body, sizeof body, "%s%swith you %u min, %d dBm, %02X:%02X:%02X:%02X:%02X:%02X",
                 vendor, vendor[0] ? " -- " : "", (unsigned)followMins, (int)d.rssi,
                 d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
        queuePush(title, body, 5, "rotating_light", true, seq, act);
#if defined(SENSE_CYD_APP)
    } else if (d.type == DetectionType::MOTION) {
        // What the motion model made of it, at the moment it called it: a
        // router's dBm says nothing about a person. Not the network's name --
        // no push carries one -- the board's name in front says which room.
        const SenseReport& r = SenseStore::last();
        snprintf(title, sizeof title, "MOTION in the room");
        snprintf(body, sizeof body, "Something moving. Motion %u/100, trust %u%%.",
                 (unsigned)r.score, (unsigned)r.rel);
        queuePush(title, body, 4, "walking", false, seq, act);
#endif
    } else {
        const bool away = strcmp(vendorText(d), "Away-Owner") == 0;
        snprintf(title, sizeof title, away ? "%s away from its owner" : "%s nearby", detectionTypeName(d.type));
        snprintf(body, sizeof body, "%s%s%d dBm, %02X:%02X:%02X:%02X:%02X:%02X",
                 vendor, vendor[0] ? ", " : "", (int)d.rssi,
                 d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
        queuePush(title, body, 4, "warning", false, seq, act);
    }
}

// What PUSH ALERTS last did, for the self-test and DIAGNOSTICS: a push that
// silently stops arriving should show on the board, not only on the phone.
static struct {
    uint32_t day = 0;          // Clock::localDay() the counts below belong to
    uint16_t sent = 0;         // messages the server took today
    uint8_t  result = 0;       // OtaWifi::PushResult of the last batch
    int      code = 0;         // its last HTTP status
    uint32_t atMs = 0;         // when it ended; 0: no batch since boot
} s_pushStat;

static void pushStatRollDay() {
    const uint32_t day = Clock::isSet() ? Clock::localDay() : 0;
    if (day != s_pushStat.day) { s_pushStat.day = day; s_pushStat.sent = 0; s_pushCooled = 0; }
}

// The evening summary: once a day at 8 PM, with PUSH ALERTS on and the clock
// real -- the day's count by type from the black box (TODAY's own count), and
// the first and last sighting. Quiet priority: it is a report, not an alarm.
static void servicePushSummary() {
    static uint32_t sentDay = 0;          // NVS's answer, read once
    static bool     read = false;
    if (!Settings::pushOn() || !Settings::pushSummary() || !Clock::trusted()) return;
    if (Clock::hour() < 20) return;
    if (!read) { sentDay = Settings::pushSummaryDay(); read = true; }
    const uint32_t day = Clock::localDay();
    if (day == sentDay) return;
    sentDay = day;
    Settings::setPushSummaryDay(day);
    Today::recount();
    const Today::Stats& s = Today::current();
    char body[160];
    if (!s.total) {
        snprintf(body, sizeof body, "Nothing caught today.");
    } else {
        // The three most-seen types.
        uint8_t top[3] = { 0, 0, 0 };
        for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT && t < 32; t++)
            for (uint8_t k = 0; k < 3; k++)
                if (s.perType[t] > s.perType[top[k]]) {
                    for (uint8_t j = 2; j > k; j--) top[j] = top[j - 1];
                    top[k] = t;
                    break;
                }
        int n = snprintf(body, sizeof body, "%u caught:", (unsigned)s.total);
        for (uint8_t k = 0; k < 3 && top[k] && n < (int)sizeof body; k++)
            n += snprintf(body + n, sizeof body - n, "%s %u %s", k ? "," : "", (unsigned)s.perType[top[k]],
                          detectionTypeName((DetectionType)top[k]));
        struct tm a, b;
        const time_t fa = (time_t)s.firstEpoch, la = (time_t)s.lastEpoch;
        localtime_r(&fa, &a);
        localtime_r(&la, &b);
        if (n < (int)sizeof body)
            snprintf(body + n, sizeof body - n, ". First %d:%02d %s, last %d:%02d %s",
                     a.tm_hour % 12 ? a.tm_hour % 12 : 12, a.tm_min, a.tm_hour < 12 ? "AM" : "PM",
                     b.tm_hour % 12 ? b.tm_hour % 12 : 12, b.tm_min, b.tm_hour < 12 ? "AM" : "PM");
    }
    // How the push limits did today: sent, and what each one kept back --
    // the numbers to look at before changing PUSH WHAT or the limits.
    pushStatRollDay();
    const uint16_t over = s_pushCap.over(day);
    const size_t bl = strlen(body);
    if (bl < sizeof body - 8)
        snprintf(body + bl, sizeof body - bl, ". Pushed %u; held %u (hourly), %u (daily cap)",
                 (unsigned)s_pushStat.sent, (unsigned)s_pushCooled, (unsigned)over);
    // A signed rules set that arrived since the last summary (without a
    // firmware update -- web-flasher/signatures.txt): said once.
    {
        Preferences rp;
        uint32_t serial = 0;
        if (rp.begin("sigx", true)) { serial = rp.getUInt("serial", 0); rp.end(); }
        Preferences sp;
        if (serial && sp.begin("settings", false)) {
            if (sp.getUInt("rulesTold", 0) < serial) {
                const size_t rl = strlen(body);
                if (rl < sizeof body - 24)
                    snprintf(body + rl, sizeof body - rl, ". New rules: set %lu, %u rules",
                             (unsigned long)serial, (unsigned)extraRuleCount());
                sp.putUInt("rulesTold", serial);
            }
            sp.end();
        }
    }
    // Which firmware, and which settings store: a board left on an old
    // version or the 20 KB store shows in the evening, unplugged.
    const size_t vl = strlen(body);
    if (vl < sizeof body - 24)
        snprintf(body + vl, sizeof body - vl, ". %s, %u KB store", FIRMWARE_VERSION, nvsStoreKb());
    PushQueue::Msg m{};
    snprintf(m.title, sizeof m.title, "%s today", pushBoardName());
    snprintf(m.body, sizeof m.body, "%s", body);
    m.prio = 2;
    snprintf(m.tags, sizeof m.tags, "bar_chart");
    s_pushQ.add(m, millis(), false);
    Serial.printf("[push] daily summary queued: %s\n", body);
}


// One line: "4 sent today, last OK 12 min ago" -- or why the last one failed.
// Returns 0 fine, 1 worth a look, as the self-test grades.
static uint8_t pushStatusText(char* out, size_t n) {
    if (!Settings::pushOn()) { snprintf(out, n, "off"); return 0; }
    if (!Settings::pushUrl()[0]) { snprintf(out, n, "on, but no server set"); return 1; }
    if (const uint32_t pm = pushPausedMin()) { snprintf(out, n, "paused from the phone, %lu min more", (unsigned long)pm); return 0; }
    pushStatRollDay();
    char cooled[40] = "";
    const uint16_t capped = s_pushCap.over(pushDay());
    if (s_pushCooled || capped)
        snprintf(cooled, sizeof cooled, ", %u held back", (unsigned)(s_pushCooled + capped));
    if (!s_pushStat.atMs) { snprintf(out, n, "none sent since boot%s", cooled); return 0; }
    const uint32_t ago = (millis() - s_pushStat.atMs) / 60000u;
    using R = OtaWifi::PushResult;
    const char* why = nullptr;
    switch ((R)s_pushStat.result) {
        case R::SENT:      break;
        case R::NO_SAVED:  why = "no saved WiFi"; break;
        case R::NO_JOIN:   why = "WiFi did not join"; break;
        case R::NO_MEMORY: why = "out of memory"; break;
        default:
            why = s_pushStat.code == 401 || s_pushStat.code == 403 ? "server refused the token"
                : s_pushStat.code == 429 ? "server rate limit (429)"
                : s_pushStat.code == 0   ? "server did not answer" : "server error";
            break;
    }
    // The command topic (IGNORE / SNOOZE from a notification): only its
    // failures are news. 404 is a server with nothing there yet.
    const int cmd = OtaWifi::cmdLastCode();
    const bool cmdBad = cmd != 0 && cmd != 200 && cmd != 404;
    if (!why) {
        if (cmdBad)
            snprintf(out, n, "%u sent today%s, last OK %lu min ago; IGNORE topic %s %d",
                     (unsigned)s_pushStat.sent, cooled, (unsigned long)ago, cmd < 0 ? "no answer" : "HTTP", cmd < 0 ? 0 : cmd);
        else
            snprintf(out, n, "%u sent today%s, last OK %lu min ago", (unsigned)s_pushStat.sent, cooled, (unsigned long)ago);
        return cmdBad ? 1 : 0;
    }
    if (s_pushStat.code > 0)
        snprintf(out, n, "last failed %lu min ago: %s (HTTP %d); %u sent today",
                 (unsigned long)ago, why, s_pushStat.code, (unsigned)s_pushStat.sent);
    else
        snprintf(out, n, "last failed %lu min ago: %s; %u sent today", (unsigned long)ago, why, (unsigned)s_pushStat.sent);
    return 1;
}

static void servicePush(uint32_t now) {
    // A join finished: the radio back to detection, the sent ones off the queue.
    if (s_pushRadio && !OtaWifi::pushBusy()) {
        if (!s_pushOnLink) engine.stopUpdateRadio();   // on sensing's link it never took the radio
        s_pushRadio = false;
        // A rate limit (429) is not fixed by asking again in two minutes --
        // that is what kept ntfy.sh's quota spent in 2026-10. Wait as long
        // as the server says, or 15 minutes doubling to two hours.
        static uint8_t limited = 0;
        uint32_t retryMs = PushQueue::RETRY_MS;
        if (OtaWifi::pushLastCode() == 429) {
            if (limited < 4) limited++;
            const uint32_t asked = OtaWifi::pushRetryAfterS();
            retryMs = asked ? (asked < 60 ? 60000u : (asked > 7200 ? 7200000u : asked * 1000u))
                            : (15u * 60u * 1000u) << (limited - 1);
            Serial.printf("[push] rate limited: next try in %lu min\n", (unsigned long)(retryMs / 60000));
        } else if (OtaWifi::pushResult() == OtaWifi::PushResult::SENT) {
            limited = 0;
        }
        s_pushQ.finished(OtaWifi::pushSentCount(), OtaWifi::pushResult() == OtaWifi::PushResult::SENT, now, retryMs);
        pushStatRollDay();
        s_pushStat.sent  += OtaWifi::pushSentCount();
        s_pushStat.result = (uint8_t)OtaWifi::pushResult();
        s_pushStat.code   = OtaWifi::pushLastCode();
        s_pushStat.atMs   = now ? now : 1;
    }
    servicePushSummary();
    // IGNORE pressed on a notification, read at the last join.
    {
        OtaWifi::setBoardId(boardId());
        uint8_t mac[6], type = 0, kind = 0;
        bool mine = true;
        while (OtaWifi::takeCommand(mac, type, kind, mine)) {
            const DetectionType t = type < (uint8_t)DetectionType::COUNT ? (DetectionType)type : DetectionType::UNKNOWN;
            char body[100], seq[24], undo[32];
            snprintf(seq, sizeof seq, "done-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            // The confirmation's Undo button posts this back (see ota_wifi.h).
            snprintf(undo, sizeof undo, "%s %02x%02x%02x%02x%02x%02x %s", kind == 1 ? "UNSNOOZE" : "UNIGNORE",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], boardId());
            // Changes from the topic never go back to it (ignoreSync).
            s_ignSyncMute = true;
            struct Unmute { ~Unmute() { s_ignSyncMute = false; } } unmute;
            // Another board's own-screen change: applied here without a word.
            // This board's own SYNC, read back off the topic: already done.
            if (kind == 6 || kind == 7) {
                if (!mine) {
                    if (kind == 6) IgnoreList::add(mac, t); else IgnoreList::remove(mac);
                    Serial.printf("[ignore] %s from another board\n", kind == 6 ? "added" : "removed");
                }
                continue;
            }
            // Another board's button: do it here too, but let that board be
            // the one that says so -- one confirmation, not one per board.
            // PAUSE and RESUME are only ever that board's.
            if (!mine && kind >= 4) continue;
            if (!mine) {
                if (kind == 0)      IgnoreList::add(mac, t);
                else if (kind == 1) IgnoreList::snoozeFor(mac, 24u * 60u * 60u * 1000u);
                else if (kind == 2) IgnoreList::remove(mac);
                else if (kind == 3) IgnoreList::unsnooze(mac);
                Serial.printf("[ignore] another board's %s, applied here too\n",
                              kind == 0 ? "IGNORE" : kind == 1 ? "SNOOZE" : kind == 2 ? "UNIGNORE" : "UNSNOOZE");
                continue;
            }
            if (kind == 4) {
                s_pushPausedUntil = millis() + 2u * 60u * 60u * 1000u;
                if (!s_pushPausedUntil) s_pushPausedUntil = 1;
                Serial.println("[push] paused for two hours from the phone");
                if (state == AppState::CLEAR) Theme::showToast("PUSHES PAUSED", "Two hours, from the phone", Theme::CYAN, 3000);
                snprintf(undo, sizeof undo, "RESUME %s", boardId());
                queuePush("paused", "Pushes paused for 2 hours. WITH YOU still comes.", 2, "pause_button", false, "pause", undo);
            } else if (kind == 5) {
                s_pushPausedUntil = 0;
                Serial.println("[push] resumed from the phone");
                queuePush("resumed", "Pushes are back on.", 2, "arrow_forward", false, "pause");
            } else if (kind == 2) {
                if (IgnoreList::remove(mac)) {
                    Serial.printf("[ignore] undone from the phone: %02x:%02x:%02x:%02x:%02x:%02x\n",
                                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    snprintf(body, sizeof body, "%02X:%02X:%02X:%02X:%02X:%02X alerts again.",
                             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    queuePush("un-ignored", body, 2, "leftwards_arrow_with_hook", false, seq);
                }
            } else if (kind == 3) {
                IgnoreList::unsnooze(mac);
                Serial.printf("[ignore] snooze ended from the phone: %02x:%02x:%02x:%02x:%02x:%02x\n",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                snprintf(body, sizeof body, "%02X:%02X:%02X:%02X:%02X:%02X alerts again.",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                queuePush("snooze ended", body, 2, "leftwards_arrow_with_hook", false, seq);
            } else if (kind == 1) {
                IgnoreList::snoozeFor(mac, 24u * 60u * 60u * 1000u);
                Serial.printf("[ignore] snoozed a day from the phone: %02x:%02x:%02x:%02x:%02x:%02x %s\n",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], detectionTypeName(t));
                if (state == AppState::CLEAR) Theme::showToast("SNOOZED FROM PHONE", detectionTypeName(t), Theme::CYAN, 3000);
                snprintf(body, sizeof body, "%s %02X:%02X:%02X:%02X:%02X:%02X: no alerts for a day (or until a restart).",
                         detectionTypeName(t), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                queuePush("snoozed", body, 2, "zzz", false, seq, undo);
            } else if (IgnoreList::add(mac, t)) {
                Serial.printf("[ignore] from the phone: %02x:%02x:%02x:%02x:%02x:%02x %s\n",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], detectionTypeName(t));
                if (state == AppState::CLEAR) Theme::showToast("IGNORED FROM PHONE", detectionTypeName(t), Theme::CYAN, 3000);
                // Said back, quietly: the button only lands at the next join,
                // and this is how the phone learns that it did.
                snprintf(body, sizeof body, "%s %02X:%02X:%02X:%02X:%02X:%02X: no more alerts.",
                         detectionTypeName(t), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                queuePush("ignored", body, 2, "white_check_mark", false, seq, undo);
            }
        }
    }
    // PUSH AT HOME's home: the saved network marked USE, kept current.
    {
        static uint32_t homeAt = 0;
        if (!homeAt || now - homeAt > 60000) {
            homeAt = now ? now : 1;
            engine.setHomeSsid(OtaWifi::savedCount() ? OtaWifi::savedSsidAt(OtaWifi::savedUse()) : "");
        }
        serviceHomeAps();
    }
    uiClearSetPushWaiting(Settings::pushOn() && s_pushQ.waiting() ? s_pushQ.count() : 0);
    if (s_pushRadio) return;
    if (!Settings::pushOn()) { if (s_pushQ.count()) s_pushQ.clear(); return; }
    if (!s_pushQ.due(now)) return;
    // HUMAN SENSING joined: over its link, which is already up and stays up
    // -- a MOTION push lands while the room is being watched, not after.
#if defined(SENSE_CYD_APP)
    const bool onLink = senseCydRunning() && senseCydMode() == SENSE_MODE_STA;
#else
    const bool onLink = false;
#endif
    if (OtaWifi::timeSyncBusy() || OtaWifi::state() != OtaWifi::State::OFF) return;
    // Otherwise only with the radio idle: never under an update, a time join,
    // a raw WiFi scan, a rest, or HUMAN SENSING still joining. They come back
    // round in a moment.
    if (!onLink && !engine.radioFree()) return;
    if (!onLink) engine.startUpdateRadio();
    if (!OtaWifi::pushStart(s_pushQ.items(), s_pushQ.count(), onLink)) {
        if (!onLink) engine.stopUpdateRadio();
        s_pushQ.sending(now);
        s_pushQ.finished(0, false, now);
        s_pushStat.result = (uint8_t)OtaWifi::pushResult();
        s_pushStat.code   = 0;
        s_pushStat.atMs   = now ? now : 1;
        return;
    }
    s_pushRadio  = true;
    s_pushOnLink = onLink;
    s_pushQ.sending(now);
    Serial.printf("[push] sending %u\n", (unsigned)s_pushQ.count());
}
