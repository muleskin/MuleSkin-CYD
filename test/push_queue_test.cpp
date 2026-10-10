// MuleSkin-CYD — PUSH ALERTS' rules (src/push_queue.cpp).
//
// What this guards: which alerts reach the phone (PUSH WHAT, PUSH AT NIGHT),
// and the timing that keeps "as they happen" from becoming a WiFi join per
// alert -- the gather, the gap, the WITH YOU that skips both, the retry after
// a failed join, the cap, and the half-hour after which an alert is dropped.
#include "test_util.h"
#include "push_queue.h"
#include <cstring>
#include <cstdio>

using namespace PushQueue;

static Msg msg(const char* t) {
    Msg m{};
    snprintf(m.title, sizeof m.title, "%s", t);
    return m;
}

int main() {
    suite("which alerts go");
    ck("WITH YOU always, even set to WITH YOU at night", wanted(What::WITH_YOU, true, true, DetectionType::AIRTAG, true));
    ck("WITH YOU mode sends nothing else", !wanted(What::WITH_YOU, false, false, DetectionType::FLOCK, false));
    ck("IMPORTANT sends a Flock", wanted(What::IMPORTANT, false, false, DetectionType::FLOCK, false));
    ck("...and a deauth flood", wanted(What::IMPORTANT, false, false, DetectionType::DEAUTH, false));
    ck("...but not a passing AirTag", !wanted(What::IMPORTANT, false, false, DetectionType::AIRTAG, false));
    ck("...nor a Ring doorbell", !wanted(What::IMPORTANT, false, false, DetectionType::RING, false));
    ck("ALL sends the AirTag", wanted(What::ALL, false, false, DetectionType::AIRTAG, false));
    ck("at night, set to WITH YOU, a Flock waits for morning", !wanted(What::ALL, true, true, DetectionType::FLOCK, false));
    ck("at night, set to everything, it goes", wanted(What::ALL, false, true, DetectionType::FLOCK, false));
    ck("by day the night setting does nothing", wanted(What::ALL, true, false, DetectionType::FLOCK, false));

    suite("a burst is one join");
    {
        Queue q;
        uint32_t t = 100000;
        q.add(msg("a"), t, false);
        ck("not before the gather", !q.due(t + GATHER_MS - 1));
        q.add(msg("b"), t + 500, false);
        q.add(msg("c"), t + 900, false);
        ck("due once the first has waited", q.due(t + GATHER_MS));
        ck("all three in the batch", q.count() == 3);
        q.sending(t + GATHER_MS);
        ck("not due while out", !q.due(t + GATHER_MS + 1) && q.busy());
        q.finished(3, true, t + GATHER_MS + 2000);
        ck("emptied when all went", q.count() == 0 && !q.busy() && !q.waiting());
    }

    suite("joins are spaced, WITH YOU is not");
    {
        Queue q;
        uint32_t t = 200000;
        q.add(msg("a"), t, false);
        q.sending(t + GATHER_MS);
        q.finished(1, true, t + GATHER_MS + 1000);
        const uint32_t sentAt = t + GATHER_MS;
        q.add(msg("b"), sentAt + 2000, false);
        ck("an ordinary one waits out the gap", !q.due(sentAt + 2000 + GATHER_MS));
        ck("...and goes after it", q.due(sentAt + GAP_MS));
        Queue u;
        u.add(msg("x"), t, false);
        u.sending(t + GATHER_MS);
        u.finished(1, true, t + GATHER_MS + 500);
        u.add(msg("WITH YOU"), t + GATHER_MS + 600, true);
        ck("a WITH YOU goes at once, gap or not", u.due(t + GATHER_MS + 600));
    }

    suite("a failed join tries again later");
    {
        Queue q;
        uint32_t t = 300000;
        q.add(msg("a"), t, true);
        ck("urgent: due now", q.due(t));
        q.sending(t);
        q.finished(0, false, t + 5000);
        ck("kept", q.count() == 1 && q.waiting());
        ck("not before the retry wait", !q.due(t + 5000 + RETRY_MS - 1));
        ck("after it", q.due(t + 5000 + RETRY_MS));
        q.sending(t + 5000 + RETRY_MS);
        q.finished(1, false, t + 6000 + RETRY_MS);
        ck("a partial send drops what went", q.count() == 0);
    }

    suite("the cap, and stale alerts");
    {
        Queue q;
        uint32_t t = 400000;
        for (int i = 0; i < 10; i++) {
            char b[8]; snprintf(b, sizeof b, "m%d", i);
            q.add(msg(b), t + (uint32_t)i, false);
        }
        ck("keeps the CAP newest", q.count() == CAP && strcmp(q.items()[0].title, "m2") == 0);
        q.sending(t + GATHER_MS + 100);
        ck("a full queue mid-send refuses the newest", !q.add(msg("late"), t + GATHER_MS + 200, false));
        q.finished(0, false, t + GATHER_MS + 300);
        ck("...and nothing it held was lost", q.count() == CAP);
        ck("half an hour on, they are dropped", !q.due(t + STALE_MS + 1000) && q.count() == 0);
    }

    suite("queued a moment after the caller read the clock");
    {
        // main.cpp reads `now` at the top of a pass and something later in
        // the same pass queues with millis(): the message is stamped a few
        // ms in the future. It must wait like any other, not count as stale.
        Queue q;
        q.add(msg("summary"), 50005, false);
        ck("not dropped as stale", !q.due(50000) && q.count() == 1);
        ck("and goes once it has gathered", q.due(50005 + GATHER_MS));
    }

    suite("clear");
    {
        Queue q;
        q.add(msg("a"), 1000, true);
        q.clear();
        ck("empty, nothing due", q.count() == 0 && !q.due(5000));
    }
    return report();
}
