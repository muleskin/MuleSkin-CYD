// MuleSkin-CYD — the board's alert chirp, whatever makes it.
//
// SQW_HAS_BUZZER is 1 on boards that can chirp, and Buzzer:: is the driver:
// the CrowPanel 7's buzzer (behind its helper MCU), or the 2.8" CYD's speaker
// connector. The BUZZER setting row and the chirp rules in main.cpp are
// written against these, once.
#pragma once

#if defined(CYD) && !defined(RLPHANTOM) && !defined(RLPHANTOM_R) && !defined(FREENOVE32) && \
    !defined(CYD32C) && !defined(NM_CYD_C5)
#define SQW_CYD_SPEAKER 1   // the ESP32-2432S028R: cyd, cyd-fast, cyd-ili9341(-fast)
#endif

#if defined(CROWPANEL7)
#include "crowpanel7_buzzer.h"
namespace Buzzer = CrowBuzzer;
#define SQW_HAS_BUZZER 1
#elif defined(SQW_CYD_SPEAKER)
#include "cyd_speaker.h"
namespace Buzzer = CydSpeaker;
#define SQW_HAS_BUZZER 1
#else
#define SQW_HAS_BUZZER 0
#endif
