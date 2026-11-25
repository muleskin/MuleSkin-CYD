// MuleSkin-CYD -- the Tab5's radio link on the wire: the sniff protocol the
// ESP32-C6 speaks (tools/tab5-c6/squach_sniff.c), as the P4 reads and writes
// it. Header-only and free of ESP-Hosted, so the host tests can hold the
// layout to the C6's (test/tab5_wire_test.cpp); src/tab5_radio.cpp does the
// sending and receiving.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Tab5Wire {

// ---- Message IDs, as squach_sniff.c defines them ----------------------------
const uint32_t CMD_SNIFF   = 0x5100;   // u8 on/off
const uint32_t CMD_CHANNEL = 0x5101;   // u8 1-14
const uint32_t CMD_HOP     = 0x5102;   // u8 n, n channels, u16 dwell ms LE; n = 0 stops
const uint32_t CMD_HELLO   = 0x5103;   // one byte, any
const uint32_t EVT_ACK     = 0x5180;   // u16 cmd, i32 status
const uint32_t EVT_HELLO   = 0x5181;   // char tag[16], u8 sniffing (newer images add a byte)
const uint32_t EVT_FRAME   = 0x5200;   // FrameHead, then the frame bytes
const uint32_t EVT_STATS   = 0x5201;   // u32 seen, forwarded, dropped; u8 channel

const uint16_t FRAME_MAX = 600;        // the C6's SNIFF_MAX
const uint8_t  HOP_MAX   = 14;
const size_t   HOP_MSG_MAX = 1 + HOP_MAX + 2;

// Both ends are little-endian (RISC-V C6, RISC-V P4, and the host tests).
struct __attribute__((packed)) FrameHead { int8_t rssi; uint8_t channel, type, subtype; uint16_t sigLen, dataLen; };
struct __attribute__((packed)) Ack       { uint16_t cmd; int32_t status; };
struct __attribute__((packed)) Hello     { char tag[16]; uint8_t sniffing; };
struct __attribute__((packed)) Stats     { uint32_t seen, forwarded, dropped; uint8_t channel; };

// An EVT_FRAME payload: its header into *head, and how many of the frame's
// bytes can be copied (from d + sizeof(FrameHead)): dataLen, cut to what came
// over the link and to FRAME_MAX. -1 when the payload is shorter than the
// header itself.
inline int frameTake(const uint8_t* d, size_t n, FrameHead* head) {
    if (n < sizeof(FrameHead)) return -1;
    memcpy(head, d, sizeof *head);
    size_t take = head->dataLen;
    const size_t have = n - sizeof(FrameHead);
    if (take > have) take = have;
    if (take > FRAME_MAX) take = FRAME_MAX;
    return (int)take;
}

// A fixed-size payload (Ack, Hello, Stats) into *out; false when short.
// Longer is fine: a newer C6 image may append fields.
template <typename T>
inline bool readPayload(const uint8_t* d, size_t n, T* out) {
    if (n < sizeof(T)) return false;
    memcpy(out, d, sizeof *out);
    return true;
}

// The hello's tag as a C string: 16 bytes, not necessarily terminated.
inline void helloTag(const Hello& h, char out[17]) {
    memcpy(out, h.tag, 16);
    out[16] = 0;
}

// A CMD_HOP payload into msg (HOP_MSG_MAX bytes): its length, or 0 when n is
// over HOP_MAX. n = 0 is the stop.
inline size_t hopMsg(const uint8_t* chans, uint8_t n, uint16_t dwellMs, uint8_t* msg) {
    if (n > HOP_MAX) return 0;
    msg[0] = n;
    for (uint8_t i = 0; i < n; i++) msg[1 + i] = chans[i];
    msg[1 + n] = (uint8_t)(dwellMs & 0xFF);
    msg[2 + n] = (uint8_t)(dwellMs >> 8);
    return (size_t)(3 + n);
}

}  // namespace Tab5Wire
