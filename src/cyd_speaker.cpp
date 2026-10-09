// MuleSkin-CYD — the 2.8" CYD's speaker. See include/cyd_speaker.h.
#include "cyd_speaker.h"
#include "buzzer.h"

#if defined(SQW_CYD_SPEAKER) && defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>

namespace CydSpeaker {
namespace {
const uint8_t  PIN  = 26;
const uint8_t  CH   = 6;
const uint32_t TONE = 2700;   // Hz: near the small speakers' loudest, and not a squeal
uint32_t s_until = 0;         // millis() the tone ends; 0 = silent
}

void begin() {
    ledcSetup(CH, TONE, 8);
    ledcAttachPin(PIN, CH);
    ledcWrite(CH, 0);
}

void chirp(uint16_t ms) {
    if (ms < 20) ms = 20;
    if (ms > 500) ms = 500;
    const uint32_t end = millis() + ms;
    if (!s_until) ledcWriteTone(CH, TONE);
    if (!s_until || (int32_t)(end - s_until) > 0) s_until = end ? end : 1;
}

void tick() {
    if (s_until && (int32_t)(millis() - s_until) >= 0) quiet();
}

void quiet() {
    ledcWrite(CH, 0);
    s_until = 0;
}

}  // namespace CydSpeaker

#else   // not a board with this speaker: nothing to drive

namespace CydSpeaker {
void begin() {}
void chirp(uint16_t) {}
void tick() {}
void quiet() {}
}

#endif
