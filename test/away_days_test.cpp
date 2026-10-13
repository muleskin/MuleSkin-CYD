// MuleSkin-CYD — "near you on N days", away from home (src/away_days.cpp).
//
// What this guards: a tracker heard on three days AWAY from home is the alert,
// once a day; one heard only at home -- a neighbour's tag in range of a board
// that never leaves -- never is; a day counts once however often it is heard;
// the table survives a restart (save/load) and drops the stalest when full.
#include "test_util.h"
#include "away_days.h"
#include <cstring>

using namespace AwayDays;

int main() {
    const uint8_t tag[6] = { 0xf7, 0x3a, 0x46, 0x74, 0x8d, 0x79 };
    const uint8_t other[6] = { 1, 2, 3, 4, 5, 6 };

    suite("a tag that follows you, away from home");
    {
        Table t;
        for (int k = 0; k < 20; k++) t.note(tag, 100, true);
        ck("heard all day, one day", t.days(tag) == 1);
        t.note(tag, 101, true);
        ck("two days: not yet", !t.due(tag, 101));
        t.note(tag, 102, true);
        ck("three days: the alert", t.days(tag) == 3 && t.due(tag, 102));
        ck("...once that day", !t.due(tag, 102));
        t.note(tag, 103, true);
        ck("...and again the next", t.due(tag, 103));
    }

    suite("a neighbour's tag beside a board at home");
    {
        Table t;
        for (uint32_t d = 100; d < 110; d++) for (int k = 0; k < 50; k++) t.note(tag, d, false);
        ck("ten days at home: no day counted", t.days(tag) == 0);
        ck("no alert", !t.due(tag, 109));
    }

    suite("mixed: home days do not count, away days do");
    {
        Table t;
        t.note(tag, 100, true);
        t.note(tag, 101, false);
        t.note(tag, 102, false);
        ck("one away day, two home days", t.days(tag) == 1 && !t.due(tag, 102));
        t.note(tag, 103, true);
        t.note(tag, 104, true);
        ck("three away days", t.days(tag) == 3 && t.due(tag, 104));
    }

    suite("kept across a restart");
    {
        Table a;
        a.note(tag, 100, true); a.note(tag, 101, true); a.note(other, 101, true);
        ck("changed: dirty", a.takeDirty() && !a.takeDirty());
        uint8_t blob[1 + SLOTS * sizeof(Slot)];
        const size_t n = a.save(blob, sizeof blob);
        Table b;
        b.load(blob, n);
        b.note(tag, 102, true);
        ck("the count goes on after load", b.days(tag) == 3 && b.due(tag, 102) && b.days(other) == 1);
        Table c;
        c.load(blob, n - 1);
        ck("a blob of the wrong size is ignored", c.days(tag) == 0);
    }

    suite("full table");
    {
        Table t;
        for (uint8_t i = 0; i < SLOTS; i++) { uint8_t m[6] = { 9, 9, 9, 9, 9, i }; t.note(m, 100 + i, true); }
        t.note(tag, 200, true);
        uint8_t first[6] = { 9, 9, 9, 9, 9, 0 }, last[6] = { 9, 9, 9, 9, 9, SLOTS - 1 };
        ck("a new tag gets in", t.days(tag) == 1);
        ck("...the stalest made room", t.days(first) == 0 && t.days(last) == 1);
    }
    suite("listed and removed (AWAYDAYS LIST / DEL)");
    {
        Table t;
        t.note(tag, 100, true); t.note(other, 100, true); t.note(other, 101, true);
        ck("two listed", t.count() == 2 && t.at(0) && t.at(1) && !t.at(2));
        t.takeDirty();
        ck("one removed", t.remove(tag) && t.count() == 1 && t.days(tag) == 0 && t.days(other) == 2);
        ck("...and saved", t.takeDirty());
        ck("not there twice", !t.remove(tag));
        t.clear();
        ck("cleared", t.count() == 0 && t.takeDirty());
    }
    return report();
}
