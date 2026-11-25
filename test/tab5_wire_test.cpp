// Tab5Wire -- include/tab5_wire.h -- the Tab5's C6 sniff link on the wire.
// The bytes here are laid out by hand the way tools/tab5-c6/squach_sniff.c
// sends them (sniff_evt_t, sniff_stats_t, the hello and the ack), so a
// layout drifting on either side shows here and not as a Tab5 that links,
// says hello and then sees nothing.
#include "tab5_wire.h"
#include "test_util.h"
#include <cstring>

using namespace Tab5Wire;

int main() {
    suite("Layouts match squach_sniff.c");
    ck("frame header is 8 bytes (sniff_evt_t)", sizeof(FrameHead) == 8);
    ck("ack is 6 bytes (u16 cmd, i32 status)", sizeof(Ack) == 6);
    ck("hello is 17 bytes (tag[16], u8 sniffing)", sizeof(Hello) == 17);
    ck("stats are 13 bytes (sniff_stats_t)", sizeof(Stats) == 13);
    ck("FRAME_MAX is the C6's SNIFF_MAX", FRAME_MAX == 600);

    suite("A forwarded frame");
    {
        // rssi -61, channel 6, management, beacon (8), 312 on air, 4 sent.
        const uint8_t d[] = {0xC3, 6, 0, 8, 0x38, 0x01, 4, 0, 0x80, 0x00, 0x00, 0x00};
        FrameHead h;
        const int take = frameTake(d, sizeof d, &h);
        ck("all four bytes are taken", take == 4);
        ck("rssi is signed", h.rssi == -61);
        ck("channel, type, subtype", h.channel == 6 && h.type == 0 && h.subtype == 8);
        ck("sigLen little-endian", h.sigLen == 312);
        ck("the frame starts after the header", d[sizeof(FrameHead)] == 0x80);
    }
    {
        // Says 24 bytes follow, but only 5 came over the link.
        uint8_t d[8 + 5] = {0xD0, 1, 2, 0, 24, 0, 24, 0};
        FrameHead h;
        ck("a short frame is cut to what arrived", frameTake(d, sizeof d, &h) == 5);
    }
    {
        // Says 700 follow, and 700 did: still no more than FRAME_MAX.
        static uint8_t d[8 + 700];
        memset(d, 0, sizeof d);
        d[6] = 700 & 0xFF; d[7] = 700 >> 8;
        FrameHead h;
        ck("an oversize frame is cut to FRAME_MAX", frameTake(d, sizeof d, &h) == FRAME_MAX);
    }
    {
        const uint8_t d[7] = {0};
        FrameHead h;
        ck("shorter than its header is refused", frameTake(d, sizeof d, &h) == -1);
        ck("an empty payload is refused", frameTake(d, 0, &h) == -1);
        const uint8_t e[8] = {0xC0, 11, 0, 4, 0, 0, 0, 0};
        ck("a header with no bytes is a frame of none", frameTake(e, sizeof e, &h) == 0);
    }

    suite("The other events");
    {
        // ACK for CMD_HOP, status -1 (ESP_FAIL).
        const uint8_t d[] = {0x02, 0x51, 0xFF, 0xFF, 0xFF, 0xFF};
        Ack a;
        ck("an ack reads", readPayload(d, sizeof d, &a));
        ck("...naming the command", a.cmd == CMD_HOP);
        ck("...with a signed status", a.status == -1);
        ck("a short ack is refused", !readPayload(d, sizeof d - 1, &a));
    }
    {
        // The current image's hello: 16-byte tag, sniffing, then sensing.
        uint8_t d[18] = {0};
        memcpy(d, "squach-sniff 2", 14);
        d[16] = 1; d[17] = 1;
        Hello h;
        ck("a longer hello from a newer image reads", readPayload(d, sizeof d, &h));
        char tag[17];
        helloTag(h, tag);
        ck("its tag", !strcmp(tag, "squach-sniff 2"));
        ck("and that it is sniffing", h.sniffing == 1);
        memset(d, 'x', 16);
        readPayload(d, sizeof d, &h);
        helloTag(h, tag);
        ck("a full 16-byte tag is still terminated", strlen(tag) == 16);
    }
    {
        // seen 1000, forwarded 900, dropped 3, channel 11.
        const uint8_t d[] = {0xE8, 0x03, 0, 0, 0x84, 0x03, 0, 0, 3, 0, 0, 0, 11};
        Stats s;
        ck("stats read", readPayload(d, sizeof d, &s));
        ck("...all three counters", s.seen == 1000 && s.forwarded == 900 && s.dropped == 3);
        ck("...and the channel", s.channel == 11);
    }

    suite("CMD_HOP");
    {
        const uint8_t ch[] = {1, 6, 11};
        uint8_t m[HOP_MSG_MAX];
        const size_t n = hopMsg(ch, 3, 300, m);
        ck("three channels are six bytes", n == 6);
        ck("count, then the channels", m[0] == 3 && m[1] == 1 && m[2] == 6 && m[3] == 11);
        ck("dwell little-endian", m[4] == 0x2C && m[5] == 0x01);
        ck("what the C6 accepts: n >= 1 + d[0] + 2", n >= (size_t)(1 + m[0] + 2));
    }
    {
        uint8_t m[HOP_MSG_MAX];
        ck("the stop is n = 0 and the dwell", hopMsg(nullptr, 0, 250, m) == 3 && m[0] == 0);
        uint8_t ch[15] = {0};
        ck("fourteen channels fit", hopMsg(ch, 14, 100, m) == HOP_MSG_MAX);
        ck("fifteen are refused", hopMsg(ch, 15, 100, m) == 0);
    }

    return report();
}
