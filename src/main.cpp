// MuleSkin-CYD — main firmware
// Wires the state machine (DESIGN.md §9) across the UI modules
// and the DetectionEngine.
//
// Some of it lives in src/main_*.inc, each #included where it used to stand
// (one translation unit, the same order): the small boards' key cursor, PUSH
// ALERTS, the PIN lock, board power and batteries, GPS and wardriving, and
// CHARGE MODE. grep src/main*.

#include <Arduino.h>
#include "serial_flush.h"
#include <SPI.h>
#include <Wire.h>
#include "shake.h"          // the small boards' BMI270; no-ops everywhere else
#include <TFT_eSPI.h>
#if defined(FREENOVE_S3_35)
#include "st77922_display.h"
#include "st77922_qspi.h"
#include "st77922_touch.h"
#endif
#if defined(CROWPANEL7)
#include "crowpanel7_board.h"
#include "crowpanel7_display.h"
#include "crowpanel7_rgb.h"
#include "crowpanel7_blit.h"
#include "crowpanel7_backlight.h"
#include "gt911_touch.h"
#include "crowpanel7_probe.h"
#include "buzzer.h"   // the alert chirp: the CrowPanel 7 buzzer or the 2.8" CYD speaker
// The [frame] line's "rows" is whoever actually wrote them.
#define SQW_PUSH_ROWS() CrowBlit::lastRows()
#elif defined(TAB5)
// The M5Stack Tab5 (upstream SquachWatch v1.47): a MIPI-DSI panel behind
// M5GFX, its touch, and the ESP32-C6 radio's one-time update.
#include "tab5_board.h"
#include "tab5_display.h"
#include "tab5_panel.h"
#include "tab5_blit.h"
#include "tab5_radio.h"
#define SQW_PUSH_ROWS() Tab5Blit::lastRows()
#else
#define SQW_PUSH_ROWS() FramePush::lastRows()
#endif
// The frame buffer forced into INTERNAL RAM on the boards whose blit reads it
// scattered (the CrowPanel's twitch, the Tab5's column-wise walk): plain
// calloc() of anything over CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL is served
// from PSRAM on a PSRAM core, so the threshold is raised around the one
// allocation. See the createSprite() calls below.
#if (defined(CROWPANEL7) && CROWPANEL_FRAME_INTERNAL) || (defined(TAB5) && TAB5_FRAME_INTERNAL)
#define SQW_FRAME_FORCE_INTERNAL 1
#else
#define SQW_FRAME_FORCE_INTERNAL 0
#endif
#if defined(CYD32C) || defined(CYD35C) || defined(TDECK_PLUS)
#include "gt911_touch.h"
#endif
#if defined(WAVESHARE_C5)
#include "waveshare_c5_board.h"   // the CH32 helper (reset, backlight) and the CST3530
#endif
// The 3.5" comes resistive (ESP32-3248S035R: touch on the display's own bus)
// and capacitive (ESP32-3248S035C: a GT911, the 3.2" capacitive's chip on the
// same pins). CYD35 is both, for the panel and its two-band drawing; CYD35R
// is the resistive one's touch, and CYD35C's touch is the 3.2" capacitive's.
// (From upstream SquachWatch v1.31.0, PR #24 by VV-B0Y.)
#if defined(CYD35) && !defined(CYD35C)
#define CYD35R 1
#endif
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>  // AWOK's own per-rotation touch-cal storage; see the AWOK block below pollTouch()'s globals
#include "auto_time.h"    // AUTO TIME's schedule (tested on the desktop)
#include "night_mode.h"   // NIGHT DIM's hours (tested on the desktop)
#include "tracker_follow.h" // a tag travelling with you (tested on the desktop)
#include <new>            // std::nothrow, for the follow table
#include <esp_heap_caps.h>   // heap_caps_get_largest_free_block() -- diagnostics screen
#if defined(NM_CYD_C5) || defined(TAB5)
#include <esp_memory_utils.h> // esp_ptr_external_ram(): is the frame in PSRAM? (IDF 5 only)
#endif
#include <esp_system.h>      // esp_reset_reason() -- diagnostics screen
// The core dump's own summary -- which task, and where. The emulator has
// neither header, and nothing to summarise.
#if __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#define HAVE_COREDUMP_SUMMARY (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
#else
#define HAVE_COREDUMP_SUMMARY 0
#endif
// The whole NVS store, for a wipe that really erases -- see physicalNvsWipe().
// The emulator has no flash and gets the logical wipe instead.
#if __has_include(<nvs_flash.h>)
#include <nvs_flash.h>
#include <nvs.h>
#include <string>
#include <vector>
#define HAVE_NVS_ERASE 1
#else
#define HAVE_NVS_ERASE 0
#endif
#include <esp_heap_caps.h>
#include "ui_diagnostics.h"   // CrashReport, used by the breadcrumb below
#include "blackbox.h"
#include "gnss.h"
#if defined(CYD_GPS)
#include "sd_row.h"
static void cydGpsStart();
static void cydGpsTick(uint32_t now);
#endif
#include "privacy.h"
#include "lora_sniffer.h"   // the watch's SX1262; inline no-ops elsewhere
#include "wardrive.h"
#include "dex.h"
#include "ui_dex.h"
#include "ui_meeting.h"
#include "ui_timesync.h"
#include "ui_today.h"
#include "push_queue.h"
#include "nvs_move.h"
#include "away_days.h"
#include "version_cmp.h"
#if defined(ARDUINO_ARCH_ESP32)
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>          // esp_read_mac(), on IDF 5 (the C5)
#endif
#include <esp_system.h>       // ...and on IDF 4
#endif
#include <time.h>
#include "regulars.h"
#include "notices.h"
#include "theme.h"            // the crash card on the splash
#include "clock.h"            // ...and the ten-minute IGNORE on it

// Written every second, read once on the next boot. RTC_NOINIT_ATTR is the
// point: it survives a software reset WITHOUT being zeroed on the way back
// up, which is exactly what a post-mortem needs and what a normal static
// cannot do. The magic is how we tell a real breadcrumb from whatever was in
// RTC RAM after a cold boot.
static const uint32_t CRUMB_MAGIC = 0x5175A0FEu;
RTC_NOINIT_ATTR static struct {
    uint32_t magic;
    uint32_t uptimeMs;
    uint32_t heapFree;
    uint32_t heapBlock;
    uint32_t lifetime;
    uint8_t  screen;
} g_crumb;
// Why this boot happened, for the black box's boot record.
static esp_reset_reason_t g_resetReason = ESP_RST_UNKNOWN;

// Snapshotted at boot, before the live breadcrumb starts overwriting it.
static CrashReport g_lastCrash = {};

// A wipe restarts the board -- see performWipe() -- and this says how it is to
// come back: unlocked and straight to the main screen after a duress PIN or a
// forgotten-PIN wipe, so the unlock looks like an unlock; locked after the
// tenth wrong guess. RTC RAM survives the restart and nothing else, and it is
// only believed after a software reset, never after a power-on.
static const uint32_t WIPEBOOT_MAGIC = 0x57A1E000u;
enum class WipeBoot : uint8_t { NONE, UNLOCKED, LOCKED };
RTC_NOINIT_ATTR static uint32_t g_wipeBoot;
// The boot update check ran and the frame buffer then failed to allocate:
// restart once without the check rather than run on with no frame buffer.
// Same RTC-memory shape as the wipe flag, and believed only after a software
// reset.
static const uint32_t CHKSKIP_MAGIC = 0x5C1BB00Cu;
RTC_NOINIT_ATTR static uint32_t g_bootCheckSkip;
static bool takeBootCheckSkip() {
    const uint32_t v = g_bootCheckSkip;
    g_bootCheckSkip = 0;
    return esp_reset_reason() == ESP_RST_SW && v == CHKSKIP_MAGIC;
}
#if defined(TWATCH_S3)
// The watch's radio self-heal: how many restarts in a row it has made because
// it heard nothing at all. Same RTC-memory shape as the flags above: a magic
// in the top bits, believed only after the heal's own deep-sleep wake (or a
// software reset, the older way). See
// twatchRadioHealTick().
static const uint32_t HEAL_MAGIC = 0x4EA10000u;
RTC_NOINIT_ATTR static uint32_t g_healWord;
static uint8_t s_healCount = 0;   // this boot's view of it, read once in setup
static void takeHealCount() {
    const uint32_t v = g_healWord;
    g_healWord = 0;
    s_healCount = ((esp_reset_reason() == ESP_RST_DEEPSLEEP || esp_reset_reason() == ESP_RST_SW) && (v & 0xFFFF0000u) == HEAL_MAGIC) ? (uint8_t)(v & 0xFF) : 0;
}
#endif
// Power dips: how many boots in a row have been brownout resets. A board on
// a weak USB port or hub can brown out several times at WiFi start before a
// boot sticks; the one that sticks says so on the main screen. Same RTC-memory
// shape as the flags above, magic in the top bits.
static const uint32_t BROWN_MAGIC = 0xB0B0B000u;
RTC_NOINIT_ATTR static uint32_t g_brownWord;
static uint8_t s_brownouts = 0;   // this boot's count, 0 when it was not one
static void takeBrownouts(esp_reset_reason_t r) {
    if (r != ESP_RST_BROWNOUT) { g_brownWord = 0; s_brownouts = 0; return; }
    const uint32_t v = g_brownWord;
    const uint8_t prev = ((v & 0xFFFFFF00u) == BROWN_MAGIC) ? (uint8_t)(v & 0xFF) : 0;
    s_brownouts = prev < 255 ? (uint8_t)(prev + 1) : 255;
    g_brownWord = BROWN_MAGIC | s_brownouts;
}

static WipeBoot takeWipeBoot() {
    const uint32_t v = g_wipeBoot;
    g_wipeBoot = 0;
    if (esp_reset_reason() != ESP_RST_SW) return WipeBoot::NONE;
    if ((v & 0xFFFFFF00u) != WIPEBOOT_MAGIC) return WipeBoot::NONE;
    const uint8_t m = (uint8_t)(v & 0xFF);
    if (m == (uint8_t)WipeBoot::UNLOCKED) return WipeBoot::UNLOCKED;
    if (m == (uint8_t)WipeBoot::LOCKED)   return WipeBoot::LOCKED;
    return WipeBoot::NONE;
}

static void crashReportInit() {
    const esp_reset_reason_t r = esp_reset_reason();
#if defined(TWATCH_S3)
    takeHealCount();
#endif
    g_resetReason = r;
    takeBrownouts(r);
    const bool panicked = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
                           r == ESP_RST_TASK_WDT || r == ESP_RST_WDT);
    // A magic that survived with garbage behind it -- 31 days up and 20 MB
    // free, on a photo -- is RTC RAM that half-survived a power dip.
    if (g_crumb.magic == CRUMB_MAGIC &&
        (g_crumb.uptimeMs > 30UL * 24 * 3600 * 1000 || g_crumb.heapFree > 400000)) g_crumb.magic = 0;
    if (panicked && g_crumb.magic == CRUMB_MAGIC) {
        g_lastCrash.valid     = true;
        g_lastCrash.uptimeMs  = g_crumb.uptimeMs;
        g_lastCrash.heapFree  = g_crumb.heapFree;
        g_lastCrash.heapBlock = g_crumb.heapBlock;
        g_lastCrash.lifetime  = g_crumb.lifetime;
        g_lastCrash.screen    = g_crumb.screen;
    }
#if HAVE_COREDUMP_SUMMARY
    // The resets that write a core dump are the panics and the two watchdogs
    // that panic -- not the RTC watchdog, after which the dump in flash is an
    // earlier crash's. The summary is what the esp-coredump tool leads with.
    if (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT) {
        // On the stack: it is read once, here, and ~200 bytes of BSS for
        // the life of the board would be paying for it forever.
        esp_core_dump_summary_t s;
        if (esp_core_dump_get_summary(&s) == ESP_OK) {
            g_lastCrash.haveDump = true;
            strncpy(g_lastCrash.task, s.exc_task, sizeof g_lastCrash.task - 1);
            g_lastCrash.task[sizeof g_lastCrash.task - 1] = '\0';
            g_lastCrash.pc    = s.exc_pc;
#if CONFIG_IDF_TARGET_ARCH_RISCV
            // Both of these structs are architecture-specific, and the RISC-V
            // ones share no field names with the Xtensa ones -- see
            // espcoredump/include/port/riscv/esp_core_dump_summary_port.h.
            // The Xtensa exc_cause/exc_vaddr pair is mcause/mtval here.
            g_lastCrash.cause = s.ex_info.mcause;
            g_lastCrash.vaddr = s.ex_info.mtval;
            // No backtrace, and not because this is unfinished. RISC-V has no
            // windowed register ABI, so a backtrace cannot be walked on the
            // device at all: the IDF stores a raw stack dump instead and
            // expects GDB or the ELF to turn it into frames on a host. The
            // crash screen therefore shows task, PC, cause and address on this
            // board and no frames, which is the truth rather than a gap.
            g_lastCrash.btN = 0;
#else
            g_lastCrash.cause = s.ex_info.exc_cause;
            g_lastCrash.vaddr = s.ex_info.exc_vaddr;
            // The backtrace usually starts at the faulting PC itself; the
            // frames above it are the ones worth the screen space.
            uint32_t i = (s.exc_bt_info.depth && s.exc_bt_info.bt[0] == s.exc_pc) ? 1 : 0;
            uint8_t  n = 0;
            for (; i < s.exc_bt_info.depth && i < 16 && n < 4; i++) g_lastCrash.bt[n++] = s.exc_bt_info.bt[i];
            g_lastCrash.btN = n;
#endif
            char running[APP_ELF_SHA256_SZ] = { 0 };
            esp_ota_get_app_elf_sha256(running, sizeof running);
            g_lastCrash.dumpOlder =
                strncmp((const char*)s.app_elf_sha256, running, sizeof running - 1) != 0;
        }
    }
#endif
    g_crumb.magic = CRUMB_MAGIC;
}

// Once a second is plenty: this is for telling a slow heap death from a
// sudden one, and a second's resolution answers that.
static void crashCrumbTick(uint32_t now, uint32_t lifetime, uint8_t screen) {
    static uint32_t last = 0;
    if (now - last < 1000) return;
    last = now;
    g_crumb.uptimeMs  = now;
    g_crumb.heapFree  = ESP.getFreeHeap();
    // Internal SRAM, as ESP.getFreeHeap() beside it is: on a PSRAM board
    // MALLOC_CAP_8BIT alone answers with PSRAM's megabytes, and the crumb
    // could never say a crash was a fragmented heap. (See detection.cpp.)
    g_crumb.heapBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    g_crumb.lifetime  = lifetime;
    g_crumb.screen    = screen;
}

// The crash, on the splash. DIAGNOSTICS has shown the last crash since
// v1.7.x, and a user in a boot loop cannot reach DIAGNOSTICS: the board
// dies before the menu. So on a boot after a panic the splash holds for
// nine seconds with the same lines in a box at the bottom, and a photo of
// the splash is the bug report.
static bool crashCardWanted() { return g_lastCrash.valid || g_lastCrash.haveDump; }
static const char* resetReasonName();
static void drawCrashCard(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    const int lines = 1 + (g_lastCrash.haveDump ? 2 : (g_lastCrash.valid ? 1 : 0));
    const int bh = 8 + lines * 11;
    const int y0 = h - bh - 4;
    t.fillRoundRect(4, y0, w - 8, bh, 4, Theme::BG);
    t.drawRoundRect(4, y0, w - 8, bh, 4, Theme::RED);
    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::RED, Theme::BG);
    int y = y0 + 4;
    char line[64];
    if (g_lastCrash.valid)
        snprintf(line, sizeof line, "LAST CRASH: %lum%02lus up, %lu free, %lu block",
                 (unsigned long)(g_lastCrash.uptimeMs / 60000), (unsigned long)((g_lastCrash.uptimeMs / 1000) % 60),
                 (unsigned long)g_lastCrash.heapFree, (unsigned long)g_lastCrash.heapBlock);
    else
        snprintf(line, sizeof line, "LAST RESET: %s", resetReasonName());
    t.setCursor(10, y); t.print(line); y += 11;
    if (g_lastCrash.haveDump) {
        snprintf(line, sizeof line, "IN: %s @ %08lX%s", g_lastCrash.task, (unsigned long)g_lastCrash.pc,
                 g_lastCrash.dumpOlder ? " (old fw)" : "");
        t.setCursor(10, y); t.print(line); y += 11;
        size_t o = snprintf(line, sizeof line, "BT:");
        for (uint8_t i = 0; i < g_lastCrash.btN && i < 4; i++)
            o += snprintf(line + o, sizeof line - o, " %08lX", (unsigned long)g_lastCrash.bt[i]);
        t.setCursor(10, y); t.print(line); y += 11;
    } else if (g_lastCrash.valid) {
        snprintf(line, sizeof line, "ON: screen %u, %lu detections", (unsigned)g_lastCrash.screen,
                 (unsigned long)g_lastCrash.lifetime);
        t.setCursor(10, y); t.print(line); y += 11;
    }
}
#include "state.h"
#include "theme.h"
#include "detection.h"
#include "clock.h"
#include "ui_zone.h"
#include "ui_wifinets.h"
#include "ui_myalerts.h"
#include "my_alerts.h"
#if defined(SENSE_CYD_APP)
// HUMAN SENSING: WiFi motion sensing on the board's own radio
// (include/sense_cyd.h; upstream SquachWatch v1.42-v1.49).
#include "sense_store.h"
#include "ui_sensing_cyd.h"
#include "sense_cyd.h"
#include "sense_motion.h"
#endif
#include "flood_bench.h"
#include "ui_boot.h"
#include "ui_clear.h"
#include "ui_alert.h"
#include "tag_ring.h"
#include "ui_log.h"
#include "ui_watchalert.h"
#include "ui_settings.h"
#include "ui_diagnostics.h"
#include "ui_hunt.h"
#include "ui_colorcheck.h"
#include "detection_info.h"
#include "ui_diary.h"
#include "ui_outfit.h"
#include "ui_outfit_unlock.h"
#include "ui_ignorelist.h"
#include "frame_push.h"
#include "draw_band.h"
#include "frame_prof.h"
#include "fast_sprite.h"
#include "ui_phone.h"
#include "ui_autoupdate.h"
#include "ignore_list.h"
#include "ignore_list.h"
#include "ui_detfilter.h"
#include "ui_beaconwarn.h"
#include "ui_power.h"
#include "security.h"
#include "ui_security.h"
#include "muleskin.h"
#include "cap_touch.h"
#if defined(TWATCH_S3)
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#endif
#include "touch_cal.h"
#include "settings.h"
#include "signatures.h"
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include "ui_update.h"
#include "ui_wifipass.h"
#include "ui_sysprops.h"
#include "status_light.h"
#include "ui_light.h"
#if MULESKIN_LORA
#include "ui_lorachat.h"
#endif

// Two CYD board variants are supported from this one firmware:
//   - jczn_2432s028r (original): resistive XPT2046 touch on its own
//     dedicated SPI bus, backlight on GPIO21. Confirmed against
//     Espressif's official board-variant file for this exact board
//     (arduino-esp32 variants/jczn_2432s028r/pins_arduino.h):
//       display: DC=2 MISO=12 MOSI=13 SCK=14 CS=15 BL=21  (VSPI, via TFT_eSPI)
//       touch:   CS=33 IRQ=36 SCK=25  MOSI=32 MISO=39      (independent bus)
//   - JC2432W328C: capacitive CST816/CST820 touch over I2C, backlight
//     on GPIO27. Confirmed empirically against a physical unit: same
//     display driver/pins as above, touch chip answers at I2C 0x15 on
//     SDA=33/SCL=32 with a reset pulse on GPIO25.
// Both variants route their touch controller through the same
// GPIO25/32/33 trio (SPI vs I2C), so probing for the I2C chip at boot
// tells us which board this is — see setup().
//   - ESP32-3248S035R (3.5", built separately as env:cyd35): resistive
//     XPT2046 again, CS=33 sharing the *display's* SPI bus (SCK/MOSI/
//     MISO = 14/13/12) instead of getting a dedicated peripheral —
//     this board has no capacitive-touch chip at all, so the I2C probe
//     below is skipped entirely rather than just failing. Raw reads go
//     through TFT_eSPI's own touch accessors, same as AWOK below, not
//     the standalone XPT2046_Touchscreen library — a separate SPIClass
//     on the same physical bus as TFT_eSPI produced constant garbage
//     reads and a free-running IRQ when that was tried.
//   - AWOK 2.4" (Marauder V6.1, built separately as env:awok): resistive
//     XPT2046 sharing the display's VSPI bus like cyd35, read the same
//     way. The constructor still gets
//     built below regardless of board (it costs nothing unused), but
//     neither AWOK nor cyd35 ever calls touch.begin() or
//     touchSPI.begin() on it.
// Boards whose resistive touch chip shares the DISPLAY's SPI bus rather than
// getting a dedicated one. Two peripherals driving one set of pins is the
// thing this avoids: the XPT2046_Touchscreen library is never begin()'d on
// any of them, and raw reads go through TFT_eSPI's own accessors.
//
// Every board's raw values then go through the same TouchFit mapping (see
// pollTouch()). The two names below survive for what still differs:
//
//   TOUCH_ON_DISPLAY_BUS  -- AWOK. Reads through the same filter TFT_eSPI's
//     getTouch() applied (rawReadFiltered()), as the 3.5" does, and may have
//     an old TFT_eSPI calibration blob to convert for SKIP.
//
//   TOUCH_RAW_SHARED_BUS  -- the RL Phantom's resistive variant. A plain
//     pressure-gated raw read, and the 2.8"-style old calibration.
#if defined(AWOK)
    #define TOUCH_ON_DISPLAY_BUS 1
#endif
#if defined(RLPHANTOM_R)
    #define TOUCH_RAW_SHARED_BUS 1
#endif
// Everything that is true of BOTH: no dedicated touch peripheral, so nothing
// ever calls touch.begin()/touchSPI.begin().
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(TOUCH_RAW_SHARED_BUS)
    #define TOUCH_SHARES_DISPLAY_BUS 1
#endif

#if defined(CYD35R)
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#elif defined(TOUCH_SHARES_DISPLAY_BUS)
    // No dedicated touch bus on AWOK — TFT_eSPI drives touch on the
    // display's own VSPI. Values below are placeholders so the compile
    // still works; the AWOK branches skip touchSPI.begin() entirely.
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#else
    #define TOUCH_SCK  25
    #define TOUCH_MOSI 32
    #define TOUCH_MISO 39
#endif
// AWOK's user setup already #defines TOUCH_CS=21 (pulled in via the
// -include in platformio.ini) — awok_user_setup.h's TFT_eSPI touch
// path needs that value armed, so this can't unconditionally redefine
// it to 33 the way the other two boards share.
#ifndef TOUCH_CS
#define TOUCH_CS   33
#endif
#define TOUCH_IRQ  36
#define CAP_SDA    33
#define CAP_SCL    32
#define CAP_RST    25
// Backlight brightness (Settings menu): all three boards' backlight
// pins are driven at boot regardless of which one is actually wired
// (see the digitalWrite(HIGH) comment in setup() — same reasoning
// applies here), each on its own LEDC channel so ledcWrite can dim
// whichever one is real without needing to know which board this is.
// AWOK's BL sits on GPIO32; the other boards' pins (21, 27) are simply
// unused GPIOs on AWOK, so driving all three is harmless.
#define BL_PIN_ORIG 21
#define BL_PIN_CAP  27
#define BL_PIN_AWOK 32
#if defined(SQW_SMALL)
#define BL_PIN_S3   38   // the StickS3's and the Cardputer ADV's
#elif defined(FREENOVE_S3_35)
#define BL_PIN_S3   41   // the Freenove 3.5"'s, through a BSS138 (HIGH is on)
#elif defined(TDECK_PLUS)
#define BL_PIN_S3   42   // the T-Deck's 16-step driver chip: HIGH is full, pulses step it (tdeckBacklight)
#else
#define BL_PIN_S3   45   // the T-Watch S3's and the Freenove S3's, both
#endif
#define BL_CH_ORIG  0
#define BL_CH_CAP   1
#define BL_CH_AWOK  2

// invertDisplay() sets an ABSOLUTE panel state -- it doesn't toggle
// relative to whatever TFT_INVERSION_ON/OFF a board's user-setup header
// set at compile time, it just overwrites it. That header define is
// therefore dead code for actually choosing a board's polarity -- a
// real bug found on real hardware (AWOK), several rounds of flipping
// the header setting with zero visible effect, before finding this is
// the actual control point. Settings::inverted() is a cosmetic
// per-user theme toggle, unrelated to a given panel's actual required
// polarity, so every invertDisplay() call XORs the two: this constant
// is the panel's own baseline, and the user's cosmetic toggle flips
// relative to it. File-scope (not local to setup()) so the Settings >
// INVERT row handler in loop() can use the same XOR instead of
// clobbering this baseline with an absolute call.
#if defined(WAVESHARE_C5)
// Waveshare's own ST7789 init sends INVON (0x21). UNCONFIRMED by eye until
// the board is looked at; INVERT in Settings flips it if wrong.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(TWATCH_S3)
// Confirmed on the watch 2026-09-22: the ST7789 wants inversion on (as
// LilyGo's own setup says); false showed every colour inverted.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(RLPHANTOM_R_ALT)
// The S024R batch that wants ILI9341_2 also wants inversion on: confirmed on
// the reporter's board (upstream #88), washed-out colours without it.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(SQW_SMALL)
// M5GFX sets invert for the StickS3's ST7789P3 and the Cardputer's ST7789V2
// (upstream confirmed it on the stick, 2026-10-04).
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(TDECK_PLUS)
// LilyGo's T-Deck setup turns inversion on for its ST7789. UNCONFIRMED by
// eye (upstream too); INVERT in Settings flips it if wrong.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(FREENOVE_S3_35)
// Freenove's init for the 3.5" ST77922 turns inversion on (0x21), which is
// what St77922TFT::invertDisplay(true) sends. UNCONFIRMED by eye (upstream
// too); INVERT in Settings flips it if wrong.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(FREENOVE_S3)
// Freenove's own setup for the S3 2.8" (FNK0104AB) turns inversion on, and
// confirmed on an FNK0104B 2026-09-25: colours right with INVERT untouched.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(CYD32C)
// The same IPS ST7789 as the Freenove 3.2". UNCONFIRMED until looked at;
// INVERT in Settings flips it if wrong.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(FREENOVE32)
// Freenove's own setup for the 3.2" turns inversion on. UNCONFIRMED here
// until the board is looked at; INVERT in Settings flips it if wrong.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(CYD35)
// UNCONFIRMED on real hardware post-fix: the original port's "true"
// guess predates discovering the override bug above, so whatever
// testing produced that value was toggling a header define that does
// nothing -- not a real confirmation. Trying false first, same as
// AWOK's ILI9341 (this panel's ST7796 may differ; adjust from live
// observation).
constexpr bool PANEL_NEEDS_INVERSION = false;
#elif defined(AWOK)
// Confirmed on real hardware: true visibly changed something (proving
// this, not the dead awok_user_setup.h define, is the actual control
// point) but looked inverted/wrong. false is the ILI9341's normal
// (non-inverted) polarity, matching the original CYD board this panel
// shares a driver with.
constexpr bool PANEL_NEEDS_INVERSION = false;
#elif defined(CROWPANEL7)
// An RGB panel has no inversion register at all; Panel_RGB's invertDisplay()
// is a no-op and CrowPanelTFT hides it. False keeps the XOR in the Settings
// handler meaningful rather than pretending a control exists.
constexpr bool PANEL_NEEDS_INVERSION = false;
#elif defined(TAB5)
// A DSI video panel behind M5GFX; the app's INVERT setting has nothing to
// send, so false keeps the Settings handler's XOR honest.
constexpr bool PANEL_NEEDS_INVERSION = false;
#else
constexpr bool PANEL_NEEDS_INVERSION = false;
#endif

// TFT_eSprite::createSprite() no-ops (returns the existing buffer
// untouched) if the sprite is already created, so the only way to
// reuse an already-allocated buffer at a new width/height is to poke
// its own bookkeeping fields directly -- createSprite() itself does
// nothing more than this plus the calloc. Both are protected in
// TFT_eSPI/TFT_eSprite, reachable from a subclass. This only produces
// a *valid* buffer when the new w*h matches what was actually
// allocated -- true here because every rotation of a rectangular
// panel needs the same total pixel count (320x240 and 240x320 are
// both 76800 pixels), so one boot-time allocation covers every
// orientation forever and rotate never needs to free/realloc again.
// ...and it sits on FastSprite, which takes over the sprite's slow
// primitives; see fast_sprite.h.
class ResizableSprite : public FastSprite {
public:
    explicit ResizableSprite(TFT_eSPI* tft) : FastSprite(tft) {}
    void resizeInPlace(int16_t w, int16_t h) {
        if (!_created) return;
        _iwidth = _dwidth = _bitwidth = w;
        _iheight = _dheight = h;
        cursor_x = 0;
        cursor_y = 0;
        _sx = 0;
        _sy = 0;
        _sw = w;
        _sh = h;
        rotation = 0;
        setViewport(0, 0, _dwidth, _dheight);
        setPivot(_iwidth / 2, _iheight / 2);
    }
};

// ---- Globals ----
#if defined(CROWPANEL7)
// Not a TFT_eSPI: an 800x480 RGB parallel panel driven by ESP-IDF's esp_lcd, wearing
// TFT_eSPI's interface because `canvas` below upcasts the sprite to the
// device type and only this hierarchy gives the two a common base. Nothing
// is ever sent to an SPI display bus on this board -- see the header.
CrowPanelTFT        tft;
#elif defined(TAB5)
// Likewise not a TFT_eSPI: a MIPI-DSI panel driven by M5GFX, wearing the
// library's interface for the same reason -- see tab5_display.h.
Tab5TFT             tft;
#elif defined(FREENOVE_S3_35)
// Likewise not a TFT_eSPI: an ST77922 on a four-lane QSPI bus, wearing the
// library's interface for the same reason -- see st77922_display.h.
St77922TFT          tft;
#else
TFT_eSPI            tft = TFT_eSPI();
#endif
// All screens draw into this off-screen buffer, pushed to the physical
// display in one shot at the end of each loop(). Without it, every
// screen's erase-then-redraw sequence is briefly visible on real
// hardware — most noticeable as flickering text.
//
// cyd35 exception: at 320x480 this sprite needs ~150KB contiguous heap
// (8-bit), and that board's largest free block measured ~110KB on real
// hardware (no PSRAM, and WiFi/BLE fragment the heap before setup()
// even runs) -- createSprite() reliably fails there. Rather than a
// banded/partial-height rewrite of every draw call, cyd35 skips the
// double buffer entirely and draws straight to the panel via `canvas`
// below, accepting the erase/redraw flicker the buffer normally hides.
ResizableSprite     frame = ResizableSprite(&tft);
// A pointer, not a reference: confirmed on real hardware that
// re-createSprite()'ing `frame` after a rotation can fail even with
// generous total free heap (NimBLE/WiFi churn fragments it -- see the
// rotate handler in loop()). When that happens we permanently fail
// over to drawing straight into `tft` for the rest of the session,
// same tradeoff cyd35 already accepts by default -- which needs
// `canvas` to be reseatable at runtime, not bound once at startup.
#if defined(CYD35)
    TFT_eSPI*        canvas = &tft;
#else
    TFT_eSPI*        canvas = &frame;
#endif
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
// cyd35's touch bus IS the display's bus, shared via CS rather than a
// separate peripheral — see the CYD35 touch-init branch in setup(),
// which uses tft.getSPIinstance() instead of this object. A second,
// independent SPIClass(VSPI) pointed at the same pins TFT_eSPI already
// owns fights it at the GPIO-matrix level rather than sharing cleanly;
// touchSPI here is only ever used on the original (HSPI) board.
SPIClass         touchSPI(HSPI);
// Set once in setup() by probing for the capacitive controller —
// decides which branch pollTouch() takes for the rest of the run.
bool                usingCapTouch = false;
// Set false if a post-boot frame.createSprite() ever fails (rotate —
// see loop()). cyd35's CLEAR screen checks this to fall back from its
// banded render to direct-to-tft; the other board's `canvas` pointer
// gets reseated to &tft directly at the point of failure instead.
bool                frameBufferOk = true;
DetectionEngine     engine;
AppState            state     = AppState::BOOT;
uint32_t            bootStart = 0;
uint32_t            alertStart= 0;
uint32_t            watchAlertStart = 0;
uint32_t            lastTouch = 0;
bool                prevTouchValid = false; // last frame's tp.valid, for true press/release edge detection (see loop())
DetectionType       lastAlertType = DetectionType::UNKNOWN;
Confidence          lastAlertConf = Confidence::HIGH_CONF;
uint32_t            lastAlertHits = 1; // times this exact MAC+type has ever matched — see MuleSkin's "seen before" reaction
int8_t              lastAlertRssi = 0; // signal strength of that hit — scales how hard MuleSkin reacts to it
const uint16_t      TOUCH_DEBOUNCE_MS = 200;
// The longest a touch can last and still be a tap on a list. A thumb that
// rests on a row for longer was reaching in to scroll, not choosing -- people
// kept opening rows they were only scrolling past -- so a long touch that
// never moved is spent, not acted on. Long presses that mean something (a
// LOG row's WATCH/HUNT) are timed separately and unaffected.
static const uint32_t TAP_MAX_MS = 500;

// The ALERT screen carries real information (type, confidence, MAC,
// RSSI) — tapping it away is the expected dismiss, but the automatic
// fallback still needs to actually clear itself in a reasonable time
// if nobody's there to tap it.
const uint32_t      ALERT_AUTO_DISMISS_MS = 10000;
// Longer than an alert's: this one is a reward, not a warning, and the
// reveal animation alone eats the first second of it.
const uint32_t      OUTFIT_UNLOCK_AUTO_MS = 12000;
// TFT_eSPI rotation: all four orientations are supported (0/2 portrait,
// 1/3 landscape), cycled in order by the rotate button in the title
// bar -- except AWOK, which has no rotate button (see loop()) and
// stays fixed at its case's one physical orientation, confirmed on
// real hardware to be portrait/rotation 0.
#if defined(AWOK)
uint8_t             screenRotation = 0;
#else
uint8_t             screenRotation = 1;
#endif
// Timestamp of the last screen/rotation change — drives a brief CRT
// tear/glitch overlay on the new frame so transitions have some punch
// instead of just snapping straight to the next screen.
uint32_t            transitionStart = 0;
static const uint32_t TRANSITION_MS = 220;

// RGB/BGR color order (Settings menu): setRotation() bakes the
// compile-time TFT_RGB_ORDER into the MADCTL byte it writes, same as
// every rotation -- there's no library API to change just that one bit
// afterward, so this reissues MADCTL by hand. The 0x36/0x80/0x40/0x20/
// 0x08 values are the standard MIPI DCS MADCTL command and MY/MX/MV/BGR
// bits, confirmed identical across this project's own vendored
// TFT_eSPI driver headers for ST7789, ILI9341, and ST7796 -- not
// reachable as named macros from here (they live in each driver's own
// TFT_Drivers/<X>_Defines.h, which main.cpp has no normal include path
// to), so hardcoded directly rather than re-derived by guesswork.
static const uint8_t MADCTL_CMD = 0x36;
static const uint8_t MADCTL_MY  = 0x80;
static const uint8_t MADCTL_MX  = 0x40;
static const uint8_t MADCTL_MV  = 0x20;
static const uint8_t MADCTL_BGR = 0x08;

// Per-rotation MX/MY/MV bits (color-order bit excluded, ORed in
// separately below) -- copied verbatim from this project's vendored
// TFT_Drivers/<X>_Rotation.h for whichever driver this board actually
// uses, not re-derived, so a transcription slip can't quietly rotate
// or mirror the image on some rotation instead of just its color.
// ILI9341 and ST7796's tables happen to be identical; ST7789's is its
// own.
static uint8_t madctlRotationBits(uint8_t rotation) {
#if defined(ST7789_DRIVER)
    static const uint8_t BITS[4] = {
        0, MADCTL_MX | MADCTL_MV, MADCTL_MX | MADCTL_MY, MADCTL_MV | MADCTL_MY
    };
#else  // ILI9341_DRIVER or ST7796_DRIVER -- identical rotation table
    static const uint8_t BITS[4] = {
        MADCTL_MX, MADCTL_MV, MADCTL_MY, MADCTL_MX | MADCTL_MY | MADCTL_MV
    };
#endif
    return BITS[rotation % 4];
}

// Same XOR-against-a-compile-time-baseline convention
// PANEL_NEEDS_INVERSION/Settings::inverted() already uses, for the same
// reason: the header's TFT_RGB_ORDER is this panel's own correct
// default, and Settings::rgbSwapped() is a "no, actually swap it" user
// override on top for boards whose panel batch disagrees with the
// header's guess -- not a replacement for it. Call after every
// setRotation(), since the rotation-dependent bits above have to be
// reissued alongside it either way.
static void applyColorOrder() {
#ifdef TFT_RGB_ORDER
    bool baselineBgr = (TFT_RGB_ORDER != TFT_RGB);
#else
    bool baselineBgr = true;
#endif
    bool wantBgr = baselineBgr != Settings::rgbSwapped();
    uint8_t bits = madctlRotationBits(screenRotation) | (wantBgr ? MADCTL_BGR : 0);
    tft.writecommand(MADCTL_CMD);
    tft.writedata(bits);
}

// ---- Touch helpers ----
struct TouchPoint { bool valid; int x; int y; };

// Defined further down with the other raw readers, but pollTouch() needs it
// above them on the boards whose touch shares the display bus: there is no
// dedicated peripheral to read from, so the raw values come through here.
static bool rawReadResistive(int16_t& a, int16_t& b);

// CST816/CST820 native coordinate range — factory defaults measured
// against one specific JC2432W328C unit's 4 corners in landscape
// (rotation 1). Not const: overwritten at boot if a saved calibration
// exists (see loadOrDefaultCal()/TouchCal), and by the long-press
// calibration flow (see checkCalibrationTrigger()).
#if defined(CROWPANEL7)
// The GT911 reports panel pixels, so these make initTouchFit()'s affine map a
// plain divide by CROWPANEL_SCALE (2 here, 1 in the -native build) -- no
// calibration is needed or wanted.
static uint16_t CAP_NX_MIN = CROW_CAP_NX_MIN, CAP_NX_MAX = CROW_CAP_NX_MAX;
static uint16_t CAP_NY_MIN = CROW_CAP_NY_MIN, CAP_NY_MAX = CROW_CAP_NY_MAX;
#elif defined(TAB5)
// M5GFX reports panel pixels of the rotated panel; a divide by TAB5_SCALE.
static uint16_t CAP_NX_MIN = TAB5_CAP_NX_MIN, CAP_NX_MAX = TAB5_CAP_NX_MAX;
static uint16_t CAP_NY_MIN = TAB5_CAP_NY_MIN, CAP_NY_MAX = TAB5_CAP_NY_MAX;
#else
#if defined(FREENOVE_S3_35)
// The ST77922's own touch reports panel pixels: a pass-through.
static uint16_t CAP_NX_MIN = St77922Touch::NX_MIN, CAP_NX_MAX = St77922Touch::NX_MAX;
static uint16_t CAP_NY_MIN = St77922Touch::NY_MIN, CAP_NY_MAX = St77922Touch::NY_MAX;
#elif defined(WAVESHARE_C5)
// The CST3530 is expected to report the panel's own 240x320 pixels (Waveshare's
// demo hands its numbers to LVGL unscaled). A starting point only: the
// five-target calibration still runs on first boot.
static uint16_t CAP_NX_MIN = 0, CAP_NX_MAX = 239;
static uint16_t CAP_NY_MIN = 0, CAP_NY_MAX = 319;
#else
static uint16_t CAP_NX_MIN = 32,  CAP_NX_MAX = 166;
static uint16_t CAP_NY_MIN = 10,  CAP_NY_MAX = 308;
#endif
#endif
// Resistive XPT2046 raw ADC range — same idea, factory default was a
// flat 200-3800 for both axes; not const for the same reason.
static uint16_t RAW_X_MIN = 200, RAW_X_MAX = 3800;
static uint16_t RAW_Y_MIN = 200, RAW_Y_MAX = 3800;

// Minimum acceptable raw-unit spread for a calibration axis to count
// as valid -- see TouchCal::load()/runInteractive()'s header comment
// for the full reasoning. Capacitive touch's whole legitimate range is
// naturally small (factory default X span is just 134 units,
// CAP_NX_MIN/MAX above), so it needs a much looser floor than
// resistive, whose real range should span most of a 12-bit ADC (up to
// 4095). Confirmed on real hardware: a resistive calibration with a
// spread of ~180-190 -- comfortably clearing a capacitive-safe
// threshold -- was still nowhere near a real full-range calibration
// and left touch unusable.
static const int16_t CAP_TOUCH_MIN_SPREAD = 50;
static const int16_t RESISTIVE_MIN_SPREAD = 800;

#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
// TFT_eSPI::setTouch()'s own calibration format, as older firmware saved it
// on AWOK and the 3.5": [0] and [2] are the raw readings at the low edge of
// each axis, [1] and [3] the SPANS from there (calibrateTouch() subtracts
// before it exports), and [4] a bitflag byte where only bits 0-2
// (swap/invert_x/invert_y) mean anything -- see Touch.cpp. Checked before it
// is trusted, since an interrupted NVS write is a confirmed real-world way
// to get garbage here. Both boards are resistive, so the resistive floor.
static bool touchCalPlausible(const uint16_t* p) {
    if (p[4] > 7) return false;
    auto ok = [](uint16_t lo, uint16_t span) {
        return lo >= 1 && lo <= 4095 && span >= RESISTIVE_MIN_SPREAD && lo + span <= 4200;
    };
    return ok(p[0], p[1]) && ok(p[2], p[3]);
}
#endif

// ---- The mapping in force: raw touch -> screen, every board ----
// A TouchFit::Fit (touch_fit.h) from the five-target calibration, or, until
// a board has one, whatever it used before -- see initTouchFit() below.
// Rotation is applied after the Fit, so one calibration serves all four.
static TouchFit::Fit s_touchFit = TouchFit::fromRanges(RAW_X_MIN, RAW_X_MAX, RAW_Y_MIN, RAW_Y_MAX, 240, 320);

// Where s_touchFit came from, for the diagnostics screen and for deciding
// whether boot has to ask for a calibration.
enum class CalSource : uint8_t { BUILT_IN, OLD_SAVED, SAVED };
static CalSource s_calSource = CalSource::BUILT_IN;

#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
// ---- Calibrations older firmware saved through TFT_eSPI ----
// AWOK and the 3.5" used to hand touch to TFT_eSPI's calibrateTouch() /
// getTouch(). Its blob is only read now, once, so an owner who SKIPs the
// new calibration keeps the touch they had: fromTftEspi() turns it into a
// Fit. The 3.5" kept one per rotation, because the blob bakes in the
// rotation it was taken at; any one of them is enough for a Fit, which
// does not.
#if defined(CYD35R)
static const char* TFT_ESPI_TOUCH_NS = "cyd35touch";
#else
static const char* TFT_ESPI_TOUCH_NS = "awoktouch";
#endif

static bool loadTftEspiBlob(const char* key, uint16_t* out) {
    Preferences p;
    p.begin(TFT_ESPI_TOUCH_NS, true);
    bool has = p.isKey(key) && p.getBytes(key, out, 5 * sizeof(uint16_t)) == 5 * sizeof(uint16_t);
    p.end();
    return has && touchCalPlausible(out);
}

// The hold-at-boot reset clears these too, or SKIP would bring back the
// very calibration the owner was trying to get rid of.
static void clearTftEspiBlobs() {
    Preferences p;
    p.begin(TFT_ESPI_TOUCH_NS, false);
    p.clear();
    p.end();
}

// Blob -> Fit. True if a usable blob was found.
static bool fitFromTftEspiBlobs(TouchFit::Fit& out) {
    // The screen size rotation `r` has, from the one we are in now.
    const int w = tft.width(), h = tft.height();
    auto dims = [&](uint8_t r, int& rw, int& rh) {
        const bool same = (r & 1) == (screenRotation & 1);
        rw = same ? w : h;
        rh = same ? h : w;
    };
    uint16_t blob[5];
#if defined(CYD35R)
    for (uint8_t i = 0; i < 4; i++) {
        const uint8_t r = (uint8_t)((screenRotation + i) & 3);   // this rotation's first
        char key[8];
        snprintf(key, sizeof(key), "cal5_%u", r);
        if (!loadTftEspiBlob(key, blob)) continue;
        int rw, rh;
        dims(r, rw, rh);
        if (TouchFit::fromTftEspi(blob, r, rw, rh, out)) return true;
    }
    return false;
#else
    // AWOK has no rotate button, so its one blob was taken in the rotation
    // it is in now.
    if (!loadTftEspiBlob("cal5", blob)) return false;
    int rw, rh;
    dims(screenRotation, rw, rh);
    return TouchFit::fromTftEspi(blob, screenRotation, rw, rh, out);
#endif
}
#endif  // TOUCH_ON_DISPLAY_BUS || CYD35

static bool readTouchRaw(int16_t& a, int16_t& b);

// Raw touch -> screen, the same way on every board: the Fit gives a point
// in the panel's native (rotation 0) frame, and the rotation step turns it
// into this rotation's coordinates. No board has its own maths any more.
#if defined(SQW_SMALL) || defined(SQW_TRACKBALL)
static TouchPoint stickPoll();   // the buttons or keys stand in for a finger; below
#endif
#if defined(SQW_TRACKBALL)
static bool tdeckBallPending();
static void tdeckBallForget();
static bool tdeckBallIdle();
static void tdeckBallTouched();
#endif
static TouchPoint pollTouch() {
#if defined(SQW_SMALL)
    return stickPoll();
#endif
    TouchPoint tp = { false, 0, 0 };
    int16_t a, b;
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
    // A moving finger on these resistive panels fails the "two reads agree"
    // filter now and then, and each failure looked like a lift: the drag
    // ended, the next good read began a new press, and lifting off that one
    // was a tap on whatever lay under it -- scrolling picked rows at random
    // (upstream #134). Gaps this short are ridden over at the last good
    // point; only a longer silence is a release.
    static TouchPoint s_held = { false, 0, 0 };
    static uint32_t   s_heldMs = 0;
    constexpr uint32_t HOLD_OVER_MS = 120;
    if (!readTouchRaw(a, b)) {
        if (s_held.valid && millis() - s_heldMs <= HOLD_OVER_MS) return s_held;
        s_held.valid = false;
        return tp;
    }
#elif defined(SQW_TRACKBALL)
    // The T-Deck's two pointers. A finger on the glass wins, puts the
    // highlight out and throws away what the ball was doing. Off the glass
    // the ball's cursor runs, but only while there is something for it to
    // do: the stops' hit-test sweep is not worth a frame while nobody rolls.
    if (!readTouchRaw(a, b)) return tdeckBallIdle() ? tp : stickPoll();
    tdeckBallTouched();
#else
    if (!readTouchRaw(a, b)) return tp;
#endif
    const int w = tft.width(), h = tft.height();
    float sx, sy;
    TouchFit::toScreen(s_touchFit, a, b, screenRotation, sx, sy);
    // Truncated, as the map() calls this replaced were.
    tp.x = (int)sx;
    tp.y = (int)sy;
    // Clamp into bounds instead of invalidating -- the map extrapolates, it
    // doesn't clip, so a touch landing just past a calibrated edge
    // (completely normal: fingers don't land exactly on the pixel a
    // calibration sampled) used to read as "no touch at all" rather than
    // "slightly imprecise at the edge". Confirmed on real hardware that even
    // a technically-valid calibration could leave EVERY touch just outside
    // bounds and the whole screen unresponsive, since a rejected touch and
    // no touch look identical downstream. A hard sanity cap (2x the screen
    // dimension either direction) still rejects genuinely wild readings.
    bool sane = tp.x > -w && tp.x < 2 * w && tp.y > -h && tp.y < 2 * h;
    if (sane) {
        if (tp.x < 0) tp.x = 0; else if (tp.x >= w) tp.x = w - 1;
        if (tp.y < 0) tp.y = 0; else if (tp.y >= h) tp.y = h - 1;
    }
    tp.valid = sane;
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
    if (sane) { s_held = tp; s_heldMs = millis(); }
#endif
    return tp;
}

// ---- Raw readers ----
static bool rawReadCap(int16_t& a, int16_t& b) {
    uint16_t nx, ny;
    if (!CapTouch::read(nx, ny)) return false;
    a = (int16_t)nx;
    b = (int16_t)ny;
    return true;
}

static bool rawReadResistive(int16_t& a, int16_t& b) {
#if defined(TOUCH_SHARES_DISPLAY_BUS) || defined(CYD35R)
    // None of these boards' `touch` (XPT2046_Touchscreen) object is ever
    // begin()'d -- a second SPI driver on the display's own bus produced
    // garbage -- so this goes through TFT_eSPI's raw-touch accessors.
    // getTouchRaw() alone always returns true in TFT_eSPI 2.5.43, so the
    // actual "is a finger down" gate is the pressure threshold.
    if (tft.getTouchRawZ() < 350) return false;
    uint16_t rx, ry;
    tft.getTouchRaw(&rx, &ry);
    a = (int16_t)rx;
    b = (int16_t)ry;
    return true;
#else
    if (!touch.tirqTouched() || !touch.touched()) return false;
    TS_Point p = touch.getPoint();
    a = (int16_t)p.x;
    b = (int16_t)p.y;
    return true;
#endif
}

#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
// AWOK and the 3.5" used TFT_eSPI's getTouch(), whose filtering is part of
// how their touch feels. It is private to the library (validTouch()), so it
// is repeated here step for step: wait for the pressure to stop rising,
// then two reads that must agree within _RAWERR (20).
//
// A cheap pressure read comes FIRST. The debounce loop pays a delay(1) per
// turn even when nobody is touching, and on the 3.5" that measured 12.9 ms
// of a 106 ms frame -- more than a tenth of the device's time asking a
// screen nobody was touching. Same 600 threshold the library uses, so a
// touch it would have seen is a touch this sees.
static bool rawReadFiltered(int16_t& a, int16_t& b) {
    const uint16_t THRESHOLD = 600, RAWERR = 20;
    if (tft.getTouchRawZ() <= THRESHOLD) return false;
    uint16_t z1 = 1, z2 = 0;
    while (z1 > z2) { z2 = z1; z1 = tft.getTouchRawZ(); delay(1); }
    if (z1 <= THRESHOLD) return false;
    uint16_t x1, y1, x2, y2;
    tft.getTouchRaw(&x1, &y1);
    delay(1);
    if (tft.getTouchRawZ() <= THRESHOLD) return false;
    delay(2);
    tft.getTouchRaw(&x2, &y2);
    if (abs((int)x1 - (int)x2) > RAWERR || abs((int)y1 - (int)y2) > RAWERR) return false;
    a = (int16_t)x1;
    b = (int16_t)y1;
    return true;
}
#endif

// The one reader pollTouch(), the calibration and the diagnostics screen all
// use, so what the calibration measures is exactly what touch then reads.
static bool readTouchRaw(int16_t& a, int16_t& b) {
#if defined(SQW_SMALL)
    (void)a; (void)b;
    return false;   // no touch panel at all: the buttons drive it
#elif defined(CROWPANEL7) || defined(CYD32C) || defined(CYD35C) || defined(TDECK_PLUS)
    // The GT911 already reports panel pixels; TouchFit divides by the scale,
    // so nothing else differs.
    uint16_t x, y;
    if (!Gt911::read(x, y)) return false;
    a = (int16_t)x; b = (int16_t)y;
    return true;
#elif defined(FREENOVE_S3_35)
    uint16_t x, y;
    if (!St77922Touch::read(x, y)) return false;
    a = (int16_t)x; b = (int16_t)y;
    return true;
#elif defined(TAB5)
    uint16_t x, y;
    if (!Tab5Touch::read(x, y)) return false;
    a = (int16_t)x; b = (int16_t)y;
    return true;
#elif defined(WAVESHARE_C5)
    uint16_t x, y;
    if (!Cst3530::read(x, y)) return false;
    a = (int16_t)x; b = (int16_t)y;
    return true;
#else
    if (usingCapTouch) return rawReadCap(a, b);
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
    return rawReadFiltered(a, b);
#else
    return rawReadResistive(a, b);
#endif
#endif
}

#if defined(STICKS3)
// The StickS3's M5PM1 power chip, on I2C SDA 47 / SCL 48 at 0x6E. Its GPIO2
// switches the rail the panel runs from, off until asked for -- the same
// writes M5GFX makes before it touches the screen. GPIO3 enables the
// speaker's amplifier, which hisses if left on: driven low, as M5 does.
// (Upstream SquachWatch v1.31.0.)
static bool pm1Bit(uint8_t reg, uint8_t mask, bool on) {
    Wire.beginTransmission(0x6E);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x6E, 1) != 1) return false;
    uint8_t v = Wire.read();
    v = on ? (uint8_t)(v | mask) : (uint8_t)(v & ~mask);
    Wire.beginTransmission(0x6E);
    Wire.write(reg); Wire.write(v);
    return Wire.endTransmission() == 0;
}
static void sticksPowerUp() {
    Wire.begin(47, 48, 100000);
    bool ok = pm1Bit(0x16, 1 << 2, false);    // GPIO2: plain GPIO
    ok = pm1Bit(0x10, 1 << 2, true)  && ok;   // an output
    ok = pm1Bit(0x13, 1 << 2, false) && ok;   // push-pull
    ok = pm1Bit(0x11, 1 << 2, true)  && ok;   // high: panel rail on
    ok = pm1Bit(0x16, 1 << 3, false) && ok;   // GPIO3: plain GPIO
    ok = pm1Bit(0x10, 1 << 3, true)  && ok;   // an output
    ok = pm1Bit(0x13, 1 << 3, false) && ok;   // push-pull
    ok = pm1Bit(0x11, 1 << 3, false) && ok;   // low: amplifier off
    // No I2C idle sleep: one left asleep stops answering.
    Wire.beginTransmission(0x6E);
    Wire.write(0x09); Wire.write(0x00);
    ok = Wire.endTransmission() == 0 && ok;
    delay(100);
    Serial.printf("[pmu] M5PM1 %s: panel rail on, speaker amp off\n", ok ? "answered" : "DID NOT ANSWER");
}
#endif

#if defined(TDECK_PLUS)
// ---- T-Deck Plus: power rail, bus tidying, the stepped backlight, keyboard --
// From upstream SquachWatch v1.36.0. GPIO10 is the board's power enable: on
// battery nothing past the S3 has power until it is HIGH. The SD slot and the
// SX1262 share the display's SPI bus, so their chip selects are parked HIGH
// before TFT_eSPI's first byte, and MISO gets a pull-up so an idle bus reads
// quiet. From LilyGo's UnitTest sketch.
static const int TDECK_POWERON = 10, TDECK_SD_CS = 39, TDECK_LORA_CS = 9, TDECK_MISO = 38;
static void tdeckPowerUp() {
    pinMode(TDECK_POWERON, OUTPUT); digitalWrite(TDECK_POWERON, HIGH);
    pinMode(TDECK_SD_CS, OUTPUT);   digitalWrite(TDECK_SD_CS, HIGH);
    pinMode(TDECK_LORA_CS, OUTPUT); digitalWrite(TDECK_LORA_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);        digitalWrite(TFT_CS, HIGH);
    pinMode(TDECK_MISO, INPUT_PULLUP);
    delay(50);   // the rail, the panel and the touch chip come up
}
// The backlight is not a PWM pin. GPIO42 drives a 16-step LED driver that
// counts pulses: a rising edge from off is full brightness, each short
// low-high pulse after that steps it down one level, and holding it low
// turns it off. A 5 kHz PWM on it would step through all sixteen levels three
// hundred times a second. So the firmware's 0..255 duty becomes 0..16 steps,
// sent the way LilyGo's setBrightness() sends them: from OFF every time (so
// the chip's state never drifts from the firmware's idea of it), and the
// pulses inside a critical section -- a task switch mid-pulse held the pin
// low long enough to read as OFF upstream, and the screen went black.
static void tdeckBacklight(uint8_t duty) {
    static int level = -1;               // what was last asked for; -1 = never
    const int want = duty == 0 ? 0 : 1 + (int)(((uint32_t)duty * 15 + 127) / 255);
    if (want == level) return;
    level = want;
    digitalWrite(BL_PIN_S3, LOW);
    delay(3);                             // long enough low: off, whatever it was doing
    if (want == 0) return;
    portDISABLE_INTERRUPTS();
    digitalWrite(BL_PIN_S3, HIGH);        // on, at full
    delayMicroseconds(30);
    for (int i = 0; i < 16 - want; i++) {
        digitalWrite(BL_PIN_S3, LOW);
        delayMicroseconds(1);
        digitalWrite(BL_PIN_S3, HIGH);
        delayMicroseconds(1);
    }
    portENABLE_INTERRUPTS();
}
#endif

// Set by the power-saver timer in loop(). Kept out of Settings on purpose:
// it is a live state, not a preference, and it must never be persisted --
// coming back from a reboot into a dimmed screen with no memory of why would
// look exactly like a broken backlight.
static bool s_screenDimmed = false;
// The BOOT button (GPIO0) on the plain-ESP32 CYDs: a short press turns the
// screen off or on, a long one starts CHARGE MODE. The S3 boards and the
// CrowPanel are left out until somebody has pressed theirs -- except the
// Freenove 3.5", whose one button is GPIO0 like a CYD's (upstream v1.34).
// Not the Waveshare C5: its BOOT is GPIO28, and GPIO0 is the I2C bus's SDA --
// every byte of touch traffic would read as a press.
// Not the Tab5 either: a P4's GPIO0 is not a button there (touch drives it).
#if defined(ESP32) && (!defined(SQW_S3) || defined(FREENOVE_S3_35)) && !defined(CROWPANEL7) && !defined(TAB5) && !defined(WAVESHARE_C5)
#define SQW_BOOT_BTN 1
#else
#define SQW_BOOT_BTN 0
#endif
// POCKET MODE: the boards with a button for it. The T-Deck uses its
// trackball, held 2 s (tdeckPocketBall). (Upstream SquachWatch v1.49.)
#if defined(TWATCH_S3) || SQW_BOOT_BTN || defined(TDECK_PLUS)
#define SQW_POCKET 1
#else
#define SQW_POCKET 0
#endif
#if defined(TWATCH_S3)
// A watch goes dark, not dim. When the screen timeout lands, the backlight
// goes off and the ST7789 is put to sleep (DISPOFF, SLPIN: about a milliamp
// instead of tens), and pushFrame() stops sending frames it would not show.
// A tap or the crown wakes it: SLPOUT needs 120 ms before DISPON, which is
// the one delay a person can feel here, and it is once per wake.
static bool s_panelAsleep = false;
// On the cable, read every two seconds in twatchRadioTick(). The screen
// stays lit while it is true: a watch on its charger is a desk clock.
static bool     s_onUsb = true;
static bool s_radiosResting = false;   // the duty cycle's state; see twatchRadioTick()
#endif
#if SQW_POCKET
// POCKET MODE. A crown press (or the CYDs' BOOT button) with the screen on
// turns it off at once, cable or not, and only another press turns it back
// on: a touch does not, so a board in a pocket or a bag cannot be woken by
// whatever it is rubbing against. Alerts still borrow the screen while they
// are up (see the power saver in loop()), then it goes dark again.
static bool     s_crownDark   = false;
// When the button was pressed: an alert already on screen then is one the
// press was meant to put out, so only alerts after it light the screen.
static uint32_t s_crownPressAt = 0;
// The first time ever, the screen stays on a moment to say how to get out.
static uint32_t s_pocketNoteUntil = 0;
static const uint32_t POCKET_NOTE_MS = 4000;
#if defined(TWATCH_S3)
static const char* const POCKET_HOW = "Press the crown to wake";
#elif defined(TDECK_PLUS)
static const char* const POCKET_HOW = "Press the trackball to wake";
#else
static const char* const POCKET_HOW = "Press BOOT again to wake";
#endif

// Every short press of the button: in or out of pocket mode, or just a wake
// when it was the screen timeout that dimmed the screen.
static void pocketPress(uint32_t now) {
    if (s_crownDark) {
        s_crownDark = false;
        s_pocketNoteUntil = 0;
        lastTouch = now;
        Serial.println("[pocket] off");
    } else if (s_screenDimmed) {
        lastTouch = now;
    } else {
        s_crownDark = true;
        s_crownPressAt = now;
        if (!Settings::pocketNoteShown()) {
            Settings::markPocketNoteShown();
            s_pocketNoteUntil = now + POCKET_NOTE_MS;
            if (!s_pocketNoteUntil) s_pocketNoteUntil = 1;
            Theme::showToast("POCKET MODE", POCKET_HOW, Theme::CYAN, POCKET_NOTE_MS);
        }
        Serial.println("[pocket] on");
    }
}
#if defined(TDECK_PLUS)
// The T-Deck has no free button (its BOOT is the trackball's press), so the
// ball held 2 s is the press that turns the screen off, and while it is off
// any press of the ball turns it back on. A press that did either is spent:
// the UI never sees it, nor anything until the ball comes back up.
static const uint32_t TB_POCKET_HOLD_MS = 2000;
static bool s_tbPocketSpent = false;
static bool tdeckPocketBall(bool pressed, uint32_t pressAt, uint32_t now) {
    if (!pressed) {
        const bool spent = s_tbPocketSpent;
        s_tbPocketSpent = false;
        return spent || s_crownDark;
    }
    if (s_tbPocketSpent) return true;
    if (s_crownDark || now - pressAt >= TB_POCKET_HOLD_MS) {
        s_tbPocketSpent = true;
        pocketPress(now);
        return true;
    }
    return false;
}
#endif
#endif

static void applyCpuClock();
// NIGHT DIM in force right now (see nightDimNow()); applyBrightness() reads it.
static bool s_nightDim = false;
// NIGHT POWER's dark: the saver has the screen dimmed for the night, and it
// goes all the way off, whatever DIM LEVEL says (see the power saver).
static bool s_nightDark = false;
#if defined(SENSE_CYD_APP)
// HUMAN SENSING's SLEEP: the backlight off while sensing runs on (VITALS by
// the bed all night); the next tap or key only wakes it. (Upstream v1.49.)
static bool s_senseDark = false;
static void applyBrightness();
static void senseWake()  { if (!s_senseDark) return; s_senseDark = false; applyBrightness(); Serial.println("[sense] screen on"); }
static void senseSleep() { if (s_senseDark) return; s_senseDark = true; applyBrightness(); Serial.println("[sense] SLEEP: screen off, sensing runs on"); }
#endif
static void applyBrightness() {
    uint8_t duty = s_screenDimmed ? Settings::dimLevel()
                 : s_nightDim    ? NightMode::duty(Settings::brightness())
                 :                 Settings::brightness();
    if (s_screenDimmed && s_nightDark) duty = 0;
#if defined(SENSE_CYD_APP)
    if (s_senseDark) duty = 0;
#endif
#if defined(TWATCH_S3)
    if (s_screenDimmed) {
        if (!s_panelAsleep) {
            ledcWrite(BL_CH_ORIG, 0);
            tft.writecommand(0x28);   // DISPOFF
            tft.writecommand(0x10);   // SLPIN
            s_panelAsleep = true;
            Serial.println("[panel] asleep");
            applyCpuClock();   // SLEEP CPU
        }
        return;
    }
    if (s_panelAsleep) {
        tft.writecommand(0x11);   // SLPOUT
        delay(120);
        tft.writecommand(0x29);   // DISPON
        s_panelAsleep = false;
        FramePush::invalidate();
        Serial.println("[panel] awake");
        applyCpuClock();   // back to CPU CLOCK
    }
#endif
#if defined(CROWPANEL7)
    // No backlight GPIO on this board: an STC8H1K28 helper MCU owns it and
    // takes a bare I2C command byte on an inverted scale. CrowBL::set()
    // keeps the firmware's own 0..255-with-255-brightest convention.
    CrowBL::set(duty);
#elif defined(TAB5)
    // M5GFX's brightness: the backlight is a PWM pin it owns.
    Tab5Panel::backlight(duty);
#elif defined(TDECK_PLUS)
    if (s_crownDark && s_screenDimmed) duty = 0;   // POCKET MODE is OFF, whatever DIM LEVEL says
    tdeckBacklight(duty);   // a stepped driver chip, not a PWM pin
#elif defined(WAVESHARE_C5)
    // No backlight GPIO: the CH32 helper's PWM, over I2C.
    WsC5::setBacklight(duty);
#elif defined(NM_CYD_C5)
    // One backlight, and a pin-based call. Arduino core 3 dropped the LEDC
    // channel API entirely: ledcWrite takes the PIN now, and the channel is
    // allocated inside ledcAttach. Writing BL_CH_ORIG/CAP/AWOK here would be
    // writing to GPIO 0, 1 and 2 -- and on this board GPIO1 is the touch chip
    // select. Three shots in the foot from what looks like dead code.
    ledcWrite(TFT_BL, duty);
#else
#if SQW_BOOT_BTN
    if (s_crownDark && s_screenDimmed) duty = 0;   // the button turns it OFF, whatever DIM LEVEL says
#endif
    ledcWrite(BL_CH_ORIG, duty);
    ledcWrite(BL_CH_CAP,  duty);
    ledcWrite(BL_CH_AWOK, duty);
#endif
}

// 240, 160 or 80 MHz. Never lower: the radio needs an 80 MHz APB clock, and
// the display's SPI divisor and the console's baud divisor are both derived
// from it, so going under would take out scanning, the panel and the serial
// console in one move. Only called when the value actually changes --
// setCpuFrequencyMhz() reconfigures peripherals, so calling it every frame
// would be both wasteful and a good way to find a driver's re-entrancy bug.
static uint16_t s_cpuMhzApplied = 240;
// TERMINAL MODE at rest (see terminalRestBudgetUs): 80 MHz. Not on the
// CrowPanel 7, whose RGB panel is fed by the CPU, or the Tab5 (a P4: other
// clocks). (Upstream v1.49.)
static bool s_termRest = false;
static void applyCpuClock() {
    uint16_t want = Settings::cpuMhz();
#if !defined(CROWPANEL7) && !defined(TAB5)
    if (s_termRest && want > 80) want = 80;
#endif
#if defined(TWATCH_S3)
    // Asleep, the watch only listens and waits for a tap: SLEEP CPU, which
    // never goes above the screen-on clock. About 25 mA at 80 instead of 240.
    if (s_panelAsleep) { const uint16_t idle = Settings::idleCpuMhz(); if (idle < want) want = idle; }
#endif
    if (want == s_cpuMhzApplied) return;
    setCpuFrequencyMhz(want);
    s_cpuMhzApplied = want;
}

static int16_t minSpread() {
    return usingCapTouch ? CAP_TOUCH_MIN_SPREAD : RESISTIVE_MIN_SPREAD;
}

// Loads the saved Fit, or builds the mapping this board used before the
// five-target calibration existed, so a board that has not calibrated yet
// (or skips it) behaves exactly as it did.
//   - 2.8" boards and the Phantom: their old min/max calibration if one is
//     saved, else the compiled-in ranges. The old pollTouch() maths was
//     exactly fromRanges() plus rotation; test/touchfit_test.cpp checks it.
//   - AWOK and 3.5": their old TFT_eSPI blob, converted.
static void initTouchFit() {
    const bool portrait = (screenRotation & 1) == 0;
    const int w0 = portrait ? tft.width() : tft.height();
    const int h0 = portrait ? tft.height() : tft.width();

    TouchFit::Fit saved;
    if (TouchCal::loadFit(saved) && saved.w0 == w0 && saved.h0 == h0) {
        s_touchFit = saved;
        s_calSource = CalSource::SAVED;
        Serial.println("Loaded saved touch calibration.");
        return;
    }

    s_calSource = CalSource::BUILT_IN;
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
    TouchFit::Fit old;
    if (fitFromTftEspiBlobs(old)) {
        s_touchFit = old;
        s_calSource = CalSource::OLD_SAVED;
        Serial.println("Touch: using the calibration older firmware saved.");
        return;
    }
#endif
    TouchCal::Cal cal;
    if (TouchCal::load(cal, minSpread())) {
        // How older firmware's applyCal() read the four numbers.
        if (usingCapTouch) {
            CAP_NX_MAX = (uint16_t)cal.aTop;  CAP_NX_MIN = (uint16_t)cal.aBottom;
            CAP_NY_MIN = (uint16_t)cal.bLeft; CAP_NY_MAX = (uint16_t)cal.bRight;
        } else {
            RAW_X_MAX = (uint16_t)cal.aTop;   RAW_X_MIN = (uint16_t)cal.aBottom;
            RAW_Y_MIN = (uint16_t)cal.bLeft;  RAW_Y_MAX = (uint16_t)cal.bRight;
        }
        s_calSource = CalSource::OLD_SAVED;
        Serial.println("Touch: using the calibration older firmware saved.");
    }
    s_touchFit = usingCapTouch
        ? TouchFit::fromRanges(CAP_NX_MIN, CAP_NX_MAX, CAP_NY_MIN, CAP_NY_MAX, w0, h0)
        : TouchFit::fromRanges(RAW_X_MIN, RAW_X_MAX, RAW_Y_MIN, RAW_Y_MAX, w0, h0);
}

// esp_reset_reason() as a short, human-readable string -- the
// diagnostics screen's answer to "did it actually crash", the exact
// question a live serial monitor was needed for earlier today.
static const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "PANIC/crash";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "unknown";
    }
}

// ---- State transitions ----
#if defined(CYD35)
// BOOT/CLEAR/ALERT all share the one half-height `frame` sprite for
// their two-pass rendering (see each state's case in loop()), but each
// screen's own xxxInit() clears *canvas -- the real display for this
// board, not `frame` itself -- so it never actually touches the
// buffer the two-pass rendering reads from. Harmless back when only
// CLEAR ever used `frame` (its own content was always self-consistent
// frame to frame), but once BOOT/ALERT started reusing that same
// buffer, switching screens could leave one screen's leftover pixels
// sitting in whatever region the next screen's own drawing never
// touches (confirmed on real hardware: CLEAR's background animation
// stops short of the button bar row's gaps, so BOOT's last frame was
// showing through there as colored noise after switching to CLEAR).
// One full clear of the real physical buffer on entry to any of these
// three screens is enough -- nothing after that leaves stray pixels
// behind on its own.
static void clearSharedFrameBuffer() {
    if (frameBufferOk) frame.fillRect(0, 0, frame.width(), frame.height(), Theme::BG);
}
#endif

// Long-press-to-watch/hunt confirmation -- see the LOG case in loop().
// Owned here rather than in ui_log.cpp so main.cpp can decide what
// WATCH/HUNT actually do (call DetectionEngine::watchBle/watchWifi/
// huntBle/huntWifi) without that module needing to know about
// DetectionEngine's tracking API at all, just how to draw/hit-test the
// panel it's given.
static bool    s_confirmPending = false;
static uint8_t s_confirmMac[6];
// Which DEVICE the MORE INFO page is about, not just which type: the label
// and name off the log entry, which is what device_info.cpp matches on. The
// alert keeps its own copy for when its MORE INFO is tapped.
static char s_confirmVendor[16] = "";
static char s_confirmName[sizeof(Detection::name)]     = "";
static char s_alertVendor[16]   = "";
static char s_alertName[sizeof(Detection::name)]       = "";
static char    s_confirmLabel[24];
// PRIVACY MODE's view of the two strings the confirm panel and MORE INFO
// draw. The real ones stay in s_confirmLabel / s_confirmName: they are what
// WATCH and HUNT hand the engine.
static const char* privLabel(const char* in) { static char b[40]; return Privacy::name(in, b, sizeof b); }
// True when s_confirmLabel is a device's own name (masked in PRIVACY MODE);
// false when it is the type or maker it fell back to, which privacy.h leaves
// alone -- RING showed as "RIN***" (upstream #92).
static bool s_confirmLabelIsName = false;
static const char* confirmLabelOnScreen() {
    return s_confirmLabelIsName ? privLabel(s_confirmLabel) : s_confirmLabel;
}
static const char* privName(const char* in)  { static char b[40]; return Privacy::name(in, b, sizeof b); }
// LOG's long-press sets this per-row: BLE vs WiFi is a property of the
// entry, not of the screen.
static bool    s_confirmIsBle = true;
static bool s_alertLastFree = false;
static bool s_alertSpam     = false;   // this alert is a spam flood's one announcement
#if SQW_HAS_BUZZER
// The chirp's length. A pass on this board is 34-59 ms and the OFF goes out
// on the first pass at or after this, so the real sound runs 80-140 ms:
// a chirp, not an alarm. The probe's bench chirp is 120.
static const uint16_t BUZZ_CHIRP_MS = 80;
#endif

// Whether a sighting may take the screen, and AUTO SNOOZE's bookkeeping
// with it. Call it once for each alert about to be raised and obey the
// answer -- it counts, so asking twice spends two of the device's allowance.
//
// The WATCH target is exempt outright. Asking to be told about one device is
// the clearest statement of intent this board takes, and a feature whose
// whole job is to interrupt less must never be the thing that overrides it.
#if defined(TWATCH_S3)
static bool twatchStill();
#endif
static void queuePushAlert(const Detection& d, uint16_t followMins);   // PUSH ALERTS, below
static void serviceHomeAps();   // the home network's own access points, below
// The sighting a screen should announce now, or null: one the engine has
// pointed latest() at since a screen last asked (latestSeq() moved), and only
// while it is fresh. Asking consumes it, so each catch is announced once.
//
// This replaced "firstSeen within 200 ms of this pass", which dropped any
// catch the loop did not look at quickly enough: at 5 fps (POWER SAVER) a pass
// is 200 ms, and one slow pass (an SD append, a flash erase) was enough. The
// catch stayed in the LOG and the counters, with no card, no sound and no
// push. A catch made while a screen that does not announce was up (SETTINGS,
// LOG) is still let go: by the time the main screen is back it is older than
// ALERT_FRESH_MS, as it was before. (Upstream SquachWatch B2, 2026-11-14.)
static const uint32_t ALERT_FRESH_MS = 2000;
static uint32_t s_alertSeqSeen = 0;
static const Detection* freshCatch(uint32_t now) {
    const uint32_t seq = engine.latestSeq();
    if (seq == s_alertSeqSeen) return nullptr;
    const Detection* d = engine.latest();
    if (!d) { s_alertSeqSeen = seq; return nullptr; }
    // Stamped on a radio task a moment after `now` was read: the next pass.
    const int32_t age = (int32_t)(now - d->firstSeen);
    if (age < 0) return nullptr;
    s_alertSeqSeen = seq;
    return age <= (int32_t)ALERT_FRESH_MS ? d : nullptr;
}

static bool alertMayInterrupt(const Detection& d) {
    const bool exempt = engine.isWatched(d.mac, true) || engine.isWatched(d.mac, false);
#if defined(TWATCH_S3)
    // TAGS + RINGS: logged, never announced. A day's wear was 50 screen
    // wakes an hour on the drive home, 52 of 83 sightings AirTags from 42
    // different passing cars, and 126 of 256 at work from twelve Rings.
    if (!exempt && Settings::quietTrackers()) {
        switch (d.type) {
            case DetectionType::AIRTAG: case DetectionType::TILE: case DetectionType::SAMSUNG_TAG:
            case DetectionType::GOOGLE_TAG: case DetectionType::RING:
                return false;
            default: break;
        }
    }
#endif
    // An AirTag-shaped advert that is NOT the away-from-owner beacon --
    // AirPods pairing, mostly, graded LOW (signatures.h) -- is logged and
    // never takes the screen, whatever ALERT FILTER says. A tag away from its
    // owner is HIGH and alerts as always.
    if (!exempt && d.type == DetectionType::AIRTAG && d.conf == Confidence::LOW_CONF) return false;
    // A flood of fake tags of this type: one alert says so, and the rest are
    // logged without interrupting until it has been quiet five minutes. See
    // spam_watch.h. A device you asked to WATCH still always gets through.
    s_alertSpam = false;
    if (!exempt && d.channel == 0 && engine.spam().active((uint8_t)d.type, millis())) {
        if (!engine.spam().takeAnnounce((uint8_t)d.type)) return false;
        s_alertSpam     = true;
        s_alertLastFree = false;
        Serial.printf("[spam] %s flood announced: %u fakes so far\n", detectionTypeName(d.type),
                      (unsigned)engine.spam().fakes((uint8_t)d.type));
        return true;
    }
#if defined(TWATCH_S3)
    const bool still = twatchStill();
#else
    const bool still = false;
#endif
    const DetectionEngine::AlertGate g = engine.alertGate(d.mac, Settings::autoQuietAfter(), exempt, still);
#if defined(TWATCH_S3)
    if (g == DetectionEngine::AlertGate::HOLD)
        Serial.printf("[snooze] %02x:%02x held, watch %s\n", d.mac[4], d.mac[5], still ? "still" : "moving");
#endif
    switch (g) {
        case DetectionEngine::AlertGate::HOLD:       return false;
        case DetectionEngine::AlertGate::ALLOW_LAST: s_alertLastFree = true;  break;
        default:                                     s_alertLastFree = false; break;
    }
#if SQW_HAS_BUZZER
    // The chirp, on the board that has something to chirp with. HERE and not
    // in enterAlert(): this is the one gate every AUTOMATIC announcement
    // passes -- the main screen, the lock screen, the update window -- and
    // none of the manual ones (NEARBY's hold), which open a card for
    // something already on the glass.
    // Everything upstream of this line is the board's own idea of
    // alert-worthy: fresh (freshCatch()), past ALERT FILTER,
    // not ignored or snoozed, and now past AUTO SNOOZE. The buzzer adds four
    // of its own: it is switched on; the row is one the log never held (a
    // device coming back is news to the screen, not to the room); the saver
    // has not dimmed the screen -- read now, because the alert un-dims it
    // later in this same pass; and it is not the hour the card calls AT
    // NIGHT. Never at boot (no automatic alert runs before loop()) and never
    // through a wipe (performWipe() silences it first).
    if (Settings::buzzerOn() && &d == engine.latest() && engine.latestIsNew() &&
        !s_screenDimmed && !Clock::night() && state != AppState::MEETING)
        Buzzer::chirp(BUZZ_CHIRP_MS);
#endif
    // The phone hears what the screen announces, by the same gate: every
    // automatic alert, none of the manual ones -- filtered by PUSH WHAT.
    queuePushAlert(d, 0);
    return true;
}

// The current alert's target, captured in enterAlert(). Kept separate
// from the s_confirm* trio above on purpose: those belong to LOG's
// long-press confirm panel, and an alert arriving while that panel is
// open would otherwise redirect a WATCH/HUNT the user was part-way
// through choosing at a completely different device.
static uint8_t s_alertMac[6];
static char    s_alertLabel[24];
static bool    s_alertIsBle = true;
// Captured alongside mac/label at row-hold time (LOG) or straight from
// the current alert (ALERT, see enterAlert()) so MORE INFO knows what
// to explain without needing the original Detection* to still be valid
// by the time it's tapped.
static DetectionType s_confirmType = DetectionType::UNKNOWN;

// The MORE INFO explanation panel -- opened by LOG's confirm panel or
// ALERT's own MORE INFO button (mutually exclusive with LOG's confirm
// panel and with everything else on whichever screen is showing it,
// since only one state is ever active). s_infoShowingPrimer tracks
// which of the (at most) two pages is up: the one-time RSSI/confidence
// primer first if it's never been shown (see Settings::infoPrimerShown()),
// then s_confirmType's own explanation either way.
static bool          s_infoPending       = false;

// The key cursor below: the boards with no touch panel, and the T-Deck Plus,
// whose trackball (SQW_TRACKBALL, platformio.ini) drives the same cursor
// beside its touch panel (see pollTouch). (Upstream SquachWatch v1.46.)
#if defined(SQW_SMALL) || defined(SQW_TRACKBALL)
#define SQW_KEYCURSOR 1
#else
#define SQW_KEYCURSOR 0
#endif
#include "main_keycursor.inc"   // the small boards' key cursor, trackball and Cardputer typing
static uint8_t       s_tappedBlip[6];          // radarBlipAt()'s answer
static bool          s_infoShowingPrimer = false;
// Same "ignore the touch that opened this" gate s_confirmArmed uses,
// applied to the info panel's own GOT IT button.
static bool          s_infoArmed         = false;
// True once the touch that triggered the long-press has actually been
// released. The confirm panel appears mid-hold (finger still down at
// whatever row/result was long-pressed), and without this gate the
// panel's own tap handling -- which only checks "is a touch down past
// the debounce window", not "did a NEW touch just start" -- could fire
// immediately using that still-held position, landing on whichever
// button happens to sit under it. Reset to false every time a fresh
// long-press opens the panel; only tap handling that runs after this
// flips true is ever allowed to register a WATCH/HUNT/CANCEL tap.
static bool    s_confirmArmed = false;

// Every way into touch calibration goes through here: first boot, the
// title-bar long-press, and Settings > CALIBRATE TOUCH. Callers decide where
// to go afterwards, which is the only thing that ever differed between them.
//
// Drawn straight onto tft, not *canvas -- canvas is an offscreen sprite on
// most boards and nothing in the flow pushes it. The frame push remembers
// what it last sent, so it is told the panel changed under it.
//
// SKIP (offered whenever there is a mapping to fall back on) keeps the
// current one, and saves it as the calibration so first boot never asks
// again; from SETTINGS it is simply a cancel.
// The compiled-in ranges are a 2.8" board's. Anywhere else -- the digitisers
// on the display's own bus -- they put taps nowhere near the finger, so a
// board with nothing better has no SKIP to offer.
#if defined(TOUCH_SHARES_DISPLAY_BUS) || defined(CYD35R) || defined(SQW_S3)
static const bool DEFAULT_TOUCH_USABLE = false;
#else
static const bool DEFAULT_TOUCH_USABLE = true;
#endif

static bool s_calRanThisBoot = false;   // for the RADIO report on the watch
static void runTouchCalibration() {
    s_calRanThisBoot = true;
    TouchFit::Fit fit;
#if defined(TWATCH_S3)
    TouchCal::setDensityScale(1.6f);   // 240 px across 27 mm, against the 2.8" board's 5.6 px/mm
#endif
    const bool canSkip = s_calSource != CalSource::BUILT_IN || DEFAULT_TOUCH_USABLE;
    const TouchCal::Outcome r =
        TouchCal::runInteractive(tft, readTouchRaw, screenRotation,
                                 canSkip ? &s_touchFit : nullptr, minSpread(),
                                 Theme::BG, Theme::WHITE, Theme::CYAN, fit);
    if (r == TouchCal::Outcome::SAVED) {
        TouchCal::saveFit(fit);
        s_touchFit = fit;
        s_calSource = CalSource::SAVED;
    } else if (r == TouchCal::Outcome::SKIPPED && s_calSource != CalSource::SAVED) {
        TouchCal::saveFit(s_touchFit);
        s_calSource = CalSource::SAVED;
    }
    FramePush::invalidate();
    // The finger that just did all that counts as a touch: lastTouch was
    // still 0 from boot, so with the power saver on, a calibration longer
    // than the screen timeout came back to a screen already dimmed.
    lastTouch = millis();
}

static void enterBoot() {
    state = AppState::BOOT;
    bootStart = millis();
    transitionStart = bootStart;
    uiBootInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterLocked();
// The frame buffer lent to a WiFi download, and taken back; defined with the update code below.
static void lendFrameToDownload();
static void restoreFrameBuffer();
// Every way home goes through here, which makes it the one place the lock has
// to be honoured: while locked, "home" is the lock screen.

// The screens that run the raw WiFi scan, which switches detection off while
// it runs. Leaving one by anything but its own BACK -- the lock icon, the
// gear, an auto-lock -- has to stop it, or detection stays off until a restart.
static bool onRawScanScreen() {
    return state == AppState::WIFI_ADD
        ;
}
static void enterClear() {
    if (Security::locked()) { enterLocked(); return; }
    restoreFrameBuffer();   // lent to a download that did not end in a restart
    state = AppState::CLEAR;
    transitionStart = millis();
    uiClearInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static uint32_t outfitUnlockStart = 0;

static void enterOutfitUnlock(uint8_t idx) {
    state = AppState::OUTFIT_UNLOCK;
    outfitUnlockStart = millis();
    transitionStart = outfitUnlockStart;
    uiOutfitUnlockInit(*canvas, idx);
}

// The companion's card. Same screen state and the same dismiss path -- it is
// the same card in a different mode, so there is nothing here for the rest
// of the loop to learn.
static void enterPetUnlock() {
    state = AppState::OUTFIT_UNLOCK;
    outfitUnlockStart = millis();
    transitionStart = outfitUnlockStart;
    uiPetUnlockInit(*canvas);
}

// Pops the celebration if MuleSkin has earned anything that has not been
// shown yet. Polled from CLEAR rather than pushed from the unlock sites:
// an outfit can be earned mid-ALERT (the detection that crossed the
// threshold is the one being alerted about) or from the werewolf summon
// on the FIRE background, and CLEAR is the one screen both of those
// paths land back on. Returns true if the screen changed.
static bool maybeEnterOutfitUnlock() {
    uint8_t idx;
    // With the mascot never shown the cards are consumed and dropped: they
    // are a picture of him in the new outfit, or of him with his pet.
    if (MuleSkin::consumeOutfitUnlock(idx)) {
        if (!MuleSkin::MASCOT_SHOWN) return false;
        enterOutfitUnlock(idx);
        return true;
    }
    // The pet after the outfits, not before: catching the lil guy can only
    // earn the companion, but a detection that crosses a threshold at the
    // same moment would have its costume pushed behind a card it has nothing
    // to do with. Whichever is left is picked up on the next poll.
    if (MuleSkin::consumePetUnlockCard()) {
        if (!MuleSkin::MASCOT_SHOWN) return false;
        enterPetUnlock();
        return true;
    }
    return false;
}

// The alert card takes no touch until the finger that opened it has lifted.
// Opened by a press-and-hold on NEARBY, the card appeared under a finger that
// was still down, and the first frame read that as a tap and closed it.
static bool s_alertArmed = true;

// MuleSkin's line for a first-of-its-kind catch, said once he is back on the
// main screen: his bubble lives there, not on the card.
static char s_firstLine[64] = "";

// Every catch MuleSkin hears about goes through here, so the things the
// engine's numbers do not carry -- the device's neighbour-name, whether the
// type is his nemesis, whether it came closer than ever -- reach him too.
static void muleskinCatch(DetectionType type, const uint8_t* mac, uint32_t hits, int8_t rssi, Confidence conf) {
    MuleSkin::catchContext(Regulars::nameFor(mac), Regulars::takeNewRegular(mac),
                          Dex::nemesis(engine) == type, Dex::takeNewClosest(type));
    MuleSkin::trigger(MuleSkin::Event::DETECTION, type, engine.lifetimeTotal(), hits, rssi, conf);
}

#if defined(TWATCH_S3)
enum class Buzz : uint8_t { ALERT, WATCH, SAMPLE, MESSAGE };
static void twatchBuzz(Buzz kind);
#endif
// SNOOZE on the alert card: each tap steps through these, and the pick
// locks in SNOOZE_PICK_MS after the last tap (upstream SquachWatch v1.40.0).
// 0 = not picking.
struct SnoozeStep { uint32_t ms; const char* label; const char* toast; };
static const SnoozeStep SNOOZE_STEP[] = {
    {  10u * 60u * 1000u, "10 MIN",  "for 10 min"  },
    {  30u * 60u * 1000u, "30 MIN",  "for 30 min"  },
    {  60u * 60u * 1000u, "1 HOUR",  "for an hour" },
    { 240u * 60u * 1000u, "4 HOURS", "for 4 hours" },
    // Until the night ends (NightMode::morningHour), worked out when the pick
    // locks in. Only with the clock set: without it there is no morning.
    { 0,                  "MORNING", nullptr       },
};
static uint8_t snoozeSteps() {
    const uint8_t all = sizeof SNOOZE_STEP / sizeof SNOOZE_STEP[0];
    return Clock::trusted() ? all : (uint8_t)(all - 1);
}
static const uint32_t SNOOZE_PICK_MS = 3000;
static uint8_t  s_snoozeStep  = 0;
static uint32_t s_snoozeTapAt = 0;

static void enterAlert(const Detection& d) {
    state = AppState::ALERT;
    s_snoozeStep = 0;
#if defined(TWATCH_S3)
    twatchBuzz(Buzz::ALERT);
#endif
    // FIRST. uiAlertInit() clears the card's banner flags, and it used to run
    // at the END of this function -- after the two uiAlertSet* calls below --
    // so it wiped them both every time. FIRST OF ITS KIND and AT NIGHT have
    // therefore never once appeared on an alert card. The spoken line still
    // worked, which is presumably why nobody noticed the banner was missing.
    s_infoPending = false;
    uiAlertInit(*canvas, d);
#if defined(CYD_GPS)
    // Only for a live catch with a fresh fix: a card opened later from
    // NEARBY shows no place, because the board may have moved since.
    {
        const uint32_t nowMs = millis();
        if (Gnss::fresh(nowMs) && !d.restored && nowMs - d.lastSeen <= Gnss::FRESH_MS)
            uiAlertSetPlace(true, Gnss::fix().lat7, Gnss::fix().lon7, Gnss::faked());
    }
#endif
    // The lifetime count for the type includes this one, so one means first.
    {
        const bool first = engine.lifetimeTypeCount(d.type) == 1;
        uiAlertSetFirst(first);
        // The hour, from the real clock: a tracker at two in the morning
        // is a different thing from one at lunch, and the card says so.
        const bool night = Clock::night();
        uiAlertSetNight(night);
        uiAlertSetLastFree(s_alertLastFree);
        // Spent here: a card opened by hand (NEARBY) must not inherit a
        // flood's banner from the last automatic one.
        uiAlertSetSpam(s_alertSpam, engine.spam().fakes((uint8_t)d.type));
        s_alertSpam = false;
        if (night && !first) {
            static const char* const NIGHT_LINES[] = {
                "A %s at this hour. That's not nothing.", "%s. At night. I don't love it.",
                "Who runs a %s past midnight? Noted.",   "A %s while the street's asleep. Hm.",
            };
            snprintf(s_firstLine, sizeof s_firstLine, NIGHT_LINES[random(0, 4)],
                     detectionTypeName(d.type));
        }
        if (first) {
            static const char* const FIRST_LINES[] = {
                "A %s! Never had one of those.", "First %s ever. Mark the date.",
                "New one for the book: %s.",     "A %s. So that's what they look like.",
            };
            snprintf(s_firstLine, sizeof s_firstLine, FIRST_LINES[random(0, 4)],
                     detectionTypeName(d.type));
        }
    }
    s_alertArmed = false;
    alertStart = millis();
    transitionStart = alertStart;
    lastAlertType = d.type;
    snprintf(s_alertVendor, sizeof s_alertVendor, "%s", vendorText(d));
    memcpy(s_alertName,   d.name,   sizeof s_alertName);
    lastAlertConf = d.conf;
    lastAlertHits = d.hits;
    lastAlertRssi = d.rssi;
    // Copied rather than kept as a Detection* -- the log is a ring
    // buffer that keeps being written while the alert is up, so the
    // entry this came from can be overwritten before HUNT is tapped.
    memcpy(s_alertMac, d.mac, 6);
    s_alertIsBle = (d.channel == 0);   // same discriminator LOG's long-press uses
    {
        const char* lbl = d.name[0] ? d.name : vendorText(d);
        strncpy(s_alertLabel, lbl, sizeof(s_alertLabel) - 1);
        s_alertLabel[sizeof(s_alertLabel) - 1] = 0;
    }
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterWatchAlert() {
    state = AppState::WATCH_ALERT;
#if defined(TWATCH_S3)
    twatchBuzz(Buzz::WATCH);
#endif
    watchAlertStart = millis();
    transitionStart = watchAlertStart;
    uiWatchAlertInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

// IN A MEETING, from the main screen's bar. BACK goes home.
static void enterToday() {
    state = AppState::TODAY;
    transitionStart = millis();
    uiTodayInit(*canvas);
}

static void enterMeeting() {
    state = AppState::MEETING;
    transitionStart = millis();
    uiMeetingInit(*canvas);
}

// The LOG's per-device panel (WATCH / IGNORE / HUNT / INFO), opened on `d`:
// a long press on a LOG row, or a tap on its blip on the main screen's radar.
static void openConfirmFor(const Detection* d) {
    memcpy(s_confirmMac, d->mac, 6);
    s_confirmIsBle = (d->channel == 0);
    s_confirmType  = d->type;
    snprintf(s_confirmVendor, sizeof s_confirmVendor, "%s", vendorText(*d));
    memcpy(s_confirmName, d->name, sizeof s_confirmName);
    s_confirmLabelIsName = d->name[0] != 0;
    const char* lbl = s_confirmLabelIsName ? d->name : vendorText(*d);
    strncpy(s_confirmLabel, lbl, sizeof(s_confirmLabel) - 1);
    s_confirmLabel[sizeof(s_confirmLabel) - 1] = 0;
    uiLogSetConfirmRing(s_confirmIsBle && TagRing::canRing(d->type));
    s_confirmPending = true;
    s_confirmArmed   = false;
}

// Set when TERMINAL MODE opened the LOG's panel for one of its rows: once the
// panel (or INFO after it) is closed, back to the terminal rather than the
// LOG screen it was drawn over. enterLog() clears it. (Upstream v1.43.)
static bool s_logFromTerminal = false;

static void enterLog() {
    state = AppState::LOG;
    transitionStart = millis();
    s_confirmPending = false;
    s_infoPending    = false;
    s_logFromTerminal = false;
    uiLogInit(*canvas);
}

static void enterSettings() {
    restoreFrameBuffer();   // lent to a download that did not end in a restart
    state = AppState::SETTINGS;
    transitionStart = millis();
    uiSettingsInit(*canvas);
}

static void enterDiagnostics() {
    state = AppState::DIAGNOSTICS;
    transitionStart = millis();
    OtaCore::refreshOther();    // reads flash: once on the way in, not per frame
    uiDiagnosticsInit(*canvas);
}

static void enterUpdate() {
    state = AppState::UPDATE;
    transitionStart = millis();
    uiUpdateInit(*canvas);
}

// ---- AUTO UPDATE -----------------------------------------------------------
// serviceAutoUpdate() puts the countdown up (ui_autoupdate.h); when it runs out,
// or somebody taps NOW, the update is driven from the UPDATE state's tick in
// place of the taps the manual flow takes, on the saved WiFi.
static uint8_t           s_autoVer[3]  = { 0, 0, 0 };
static const uint16_t    AUTO_COUNT_S  = 30;

static struct {
    bool     active = false, connected = false, installed = false;
    uint32_t failAt = 0;
} s_auto;

static void enterAutoUpdate() {
    state = AppState::AUTO_UPDATE;
    transitionStart = millis();
    lastTouch = transitionStart;     // undims a sleeping screen, like an alert
    if (s_screenDimmed) { s_screenDimmed = false; applyBrightness(); }
    uiAutoUpdateInit(*canvas, s_autoVer, AUTO_COUNT_S, transitionStart);
}

static void enterDex() {
    state = AppState::DEX;
    transitionStart = millis();
    uiDexInit(*canvas);
}

// INVERT and ROT on the console -- see clock.cpp. Consumed in loop().
volatile bool g_consoleInvert = false;
volatile bool g_consoleAdc = false;     // ADC: the spare analog inputs, for finding a battery sense line
volatile bool g_consoleXyzzy = false;   // XYZZY: the TERMINAL background types the magic word now
volatile bool g_consoleBtUpdate = false;  // UPDATE BT: the UPDATE screen's Bluetooth button, from the console
volatile int8_t g_consolePush = -1;       // PUSH OFF / ON / TEST: the PUSH ALERTS row, and a test message
volatile bool g_consoleRing     = false;   // RING: ring the tracker heard most recently, for the bench
volatile bool g_consoleWho      = false;   // WHO: which board this is (the web flasher, the bench scripts)
volatile bool g_consoleSelfTest = false;  // SELFTEST: check every part, one line each (runSelfTest)
volatile bool g_consoleLegend = false;  // LEGEND: wear the Legend look (and its aura) until the next boot, or take it off
volatile bool g_consoleOutfitSet = false;  // OUTFIT n: wear costume n until the next boot, for timing it; -1 takes it off
volatile int8_t g_consoleOutfit = -1;
volatile bool g_consoleAura = false;  // AURA: the APPEARANCE page's AURA row, LIT or OUT, from the console
volatile bool g_consolePins = false;    // PINS: digital levels, for finding a button
volatile bool g_consoleI2c = false;     // I2C: a scan of the touch bus
volatile bool g_consoleRotate = false;
volatile bool g_consoleWatchTest = false; // WATCHTEST: watch the newest Bluetooth device, fire its alert
volatile bool g_consoleRadioTest = false; // RADIO TEST: cycle even on the cable with the screen on (bench)
volatile bool g_consoleBatt    = false;   // BATT: one reading, now
volatile bool g_consoleBattLog = false;   // BATTLOG: every sample kept, newest first
volatile bool g_consoleHeal = false;  // RADIO HEAL: take the self-heal path now, as if deaf (watch)
volatile bool g_consoleXtal = false;   // XTAL: CLOCK CHECK from the console (watch)
volatile bool g_consoleMotion = false; // MOTION: the motion sensor's reading and verdict (watch)
volatile bool g_consoleBuzz = false;  // BUZZ: play the alert pattern now and report the driver (watch)
volatile bool g_consoleRtc = false;   // RTC: read the clock chip (watch)
volatile bool g_consolePmu = false;   // PMU: dump the power chip's registers (watch)
volatile uint8_t g_consoleGps = 0;    // GPS, WARDRIVE and WIGLE commands: the S3 Plus's GNSS (watch)
volatile int32_t g_consoleFakeLat7 = 0, g_consoleFakeLon7 = 0;   // GPS FAKE lat lon

#ifdef BENCH_TOOLS
// UPDATE NOW and UPDATE STOP on the console, for the bench: the unattended
// update AUTO UPDATE does, without waiting for night, and the CANCEL button.
// Bench builds only.
volatile bool g_benchUpdateNow  = false;
volatile bool g_benchAlertPush  = false;
volatile bool g_benchUpdateStop = false;
#endif

// Why OtaWifi::begin() said no. It refuses while the board is locked, and
// also while a cancelled attempt's task is still unwinding the request it was
// waiting on -- a second or two, and nothing the owner did wrong.
// The settings store's size in KB: 128 on the v3.2.0 tables, 20 on an older
// table that took its updates over the air. 0 when there is no flash to ask.
static unsigned nvsStoreKb() {
#if defined(ARDUINO_ARCH_ESP32)
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    return p ? (unsigned)(p->size / 1024) : 0u;
#else
    return 0;
#endif
}

// Still on the 20 KB store: a card on the main screen, on each of the first
// three boots that reach it (the count kept in NVS, three writes in all), and
// MuleSkin's idle line now and then after that -- until a USB install moves
// it. The self-test's "settings store" line says the same.
static void serviceSmallStoreNotice(uint32_t now) {
    static bool done = false;
    if (done || state != AppState::CLEAR || now < 20000) return;
    done = true;
    const unsigned kb = nvsStoreKb();
    if (!kb || kb >= 64) return;
    Notices::setSmallStore(true);
    Preferences p;
    if (!p.begin("settings", false)) return;
    const uint8_t told = p.getUChar("smallTold", 0);
    if (told < 3) {
        p.putUChar("smallTold", (uint8_t)(told + 1));
        Theme::showToast("PLUG IN ONCE", "Web flasher, no erase: bigger settings store", Theme::AMBER, 8000);
        Serial.println("[nvs] still the 20 KB store: one USB install from the web flasher moves it to 128 KB");
    }
    p.end();
}

// The settings store's fill, 0-100: used entries of all it can hold. See the
// self-test's "settings store" line and DIAGNOSTICS.
static uint8_t nvsFullPercent() {
#if defined(ARDUINO_ARCH_ESP32)
    nvs_stats_t st = {};
    if (nvs_get_stats(NULL, &st) != ESP_OK || !st.total_entries) return 0;
    return (uint8_t)((st.used_entries * 100u + st.total_entries - 1) / st.total_entries);
#else
    return 0;
#endif
}

static const char* updateRefusedWhy() {
    return Security::locked() ? "Unlock the board first" : "Try again in a moment";
}

static void startAutoUpdate() {
    s_auto = {};
    if (!OtaWifi::hasSaved()) {
        Theme::showToast("NO WIFI TO USE", "Save a network in WIFI NETWORKS", Theme::AMBER);
        enterClear();
        return;
    }
    enterUpdate();
    engine.startUpdateRadio();
    if (!OtaWifi::begin()) {
        engine.stopUpdateRadio();
        Theme::showToast("CAN'T START UPDATE", updateRefusedWhy(), Theme::AMBER);
        enterClear();
        return;
    }
    s_auto.active = true;
    Serial.printf("[auto] updating to v%u.%u.%u on the saved WiFi\n",
                  (unsigned)s_autoVer[0], (unsigned)s_autoVer[1], (unsigned)s_autoVer[2]);
}

// The taps the manual flow would make, made by the tick instead.
static void autoUpdateTick(uint32_t now) {
    if (!s_auto.active) return;
    const OtaWifi::State ws = OtaWifi::state();
    if (ws == OtaWifi::State::PICK && !s_auto.connected) {
        s_auto.connected = true;
        OtaWifi::connectSaved();
    } else if (ws == OtaWifi::State::READY && !s_auto.installed) {
        s_auto.installed = true;
        if (OtaWifi::upToDate()) {
            // The boot check said newer; the site disagrees. Nothing to do,
            // and Bluetooth is already gone, so this restarts the board.
            s_auto.active = false;
            if (!OtaWifi::end()) { engine.stopUpdateRadio(); enterClear(); }
        } else {
            lendFrameToDownload();
            OtaWifi::install();
        }
    } else if (ws == OtaWifi::State::FAILED) {
        // On screen for a minute, then back to work on the old version.
        if (!s_auto.failAt) s_auto.failAt = now;
        else if (now - s_auto.failAt > 60000) {
            s_auto.active = false;
            if (!OtaWifi::end()) { engine.stopUpdateRadio(); enterClear(); }
            else uiUpdateInit(*canvas);
        }
    } else if (ws == OtaWifi::State::OFF && !OtaCore::restartPending()) {
        s_auto.active = false;      // somebody tapped CANCEL
    }
}

// The frame buffer, handed back: 77 KB at 320x240, the biggest single
// allocation on the heap. The duress wipe does it because the board is
// about to restart and the copying the wipe does ran the heap dry without
// it. Afterwards the screens draw straight to the panel, the fallback a
// failed rotate already uses.
static void releaseFrameBuffer(const char* who) {
#if !defined(CYD35)
    if (!frameBufferOk) return;
    frame.deleteSprite();
    frameBufferOk = false;
    canvas = &tft;
    tft.fillScreen(Theme::BG);
    Serial.printf("[%s] frame buffer released: largest block %lu\n", who,
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#else
    (void)who;
#endif
}

// A WiFi download lends it out too. The buffer went back to being kept
// through updates in v1.12.0, once plain HTTP had done away with the one
// 17 KB block TLS wanted -- but it is not one block the download needs, it
// is room: the WiFi driver and the TCP stack take every packet out of the
// heap, and by v1.19.0 the board had 18 KB free and a 6 KB largest block
// the moment the download began. The first 16-30 KB arrived, then nothing
// could be received, and the connection closed. So the buffer is lent from
// INSTALL until the board is back on an ordinary screen; the update screen
// already knows how to draw without it. A successful update restarts; a
// failed or cancelled one gets the buffer back on its way out, or, if the
// heap will not give a 77 KB block again, draws unbuffered until a restart
// (the same fallback a failed rotate takes).
static bool s_frameLent = false;
static void lendFrameToDownload() {
    if (!frameBufferOk) return;
#if defined(ESP32)
    // Not on a board with PSRAM. There the internal heap has 80-110 KB to
    // spare during a download, and the frame is often in PSRAM anyway, so
    // lending it frees nothing the download wants. Worse, the QSPI/RGB
    // panels (the Freenove 3.5in above all) cannot be drawn straight to
    // through TFT_eSPI: the update screen came out as noise (#136).
    if (psramFound()) return;
#endif
    releaseFrameBuffer("ota");
    s_frameLent = true;
}
static void restoreFrameBuffer() {
    if (!s_frameLent) return;
    s_frameLent = false;
#if !defined(CYD35)
    if (frameBufferOk) return;
    frame.setColorDepth(8);
#if SQW_FRAME_FORCE_INTERNAL
    heap_caps_malloc_extmem_enable(1u << 20);   // back into internal RAM, see setup()
#endif
    const bool back = frame.createSprite(tft.width(), tft.height());
#if SQW_FRAME_FORCE_INTERNAL
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
    if (back) {
        frame.setTextSize(1);
        frameBufferOk = true;
        canvas = &frame;
        Serial.println("[ota] frame buffer back");
    } else {
        Serial.printf("[ota] frame buffer could not come back (largest block %lu); unbuffered until restart\n",
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
#endif
}

static void enterWifiPass(const char* ssid) {
    state = AppState::WIFI_PASS;
    transitionStart = millis();
    uiWifiPassInit(*canvas, ssid);
}

// WIFI NETWORKS, and the scan it adds from. The password keyboard is the
// update flow's; this flag says whose turn it is when it comes back.
static bool s_passForNets = false;
static void enterSysProps() {
    state = AppState::SYS_PROPS;
    transitionStart = millis();
    // MuleSkin said this line out loud until now, and two boards talking to
    // each other painted over it. Taken here so he does not say it twice.
    (void)OtaCore::takeAvailableNotice();
    uiSysPropsInit(*canvas);
}

static void enterWifiNets() {
    state = AppState::WIFI_NETS;
    transitionStart = millis();
    uiWifiNetsInit(*canvas);
}
#if defined(SENSE_CYD_APP)
// HUMAN SENSING: the board's own radio, borrowed from the detector while this
// screen is up (the sensing tick in loop(), include/sense_cyd.h). The engine's
// buffers (~8.5 KB) come from the heap then, and only then.
static void enterSensing() {
    state = AppState::SENSING;
    transitionStart = millis();
    uiSensingInit(*canvas);
}

// The status light's flash for a movement (StatusLight's alert rule), and
// SLEEP's glance: the screen lit for a few seconds, then dark again unless
// somebody touched it meanwhile.
static uint32_t s_motionLightUntil = 0;
static uint32_t s_senseGlanceUntil = 0;
static const uint32_t MOTION_LIGHT_MS  = 8000;
static const uint32_t SENSE_GLANCE_MS  = 10000;

// SENSE ARM: whether the setting has sensing running without its screen now.
// AT NIGHT goes by the screen's NIGHT DIM hours when set, else the clock's
// own night -- and never on a clock it cannot believe.
static bool senseArmed() {
    const uint8_t arm = Settings::senseArm();
    if (arm == SenseMotion::ARM_OFF) return false;
    const uint8_t p = Settings::nightMode();
    const bool night = Clock::trusted() && (p ? NightMode::active(p, Clock::hour()) : Clock::night());
    return SenseMotion::armedNow(arm, Clock::trusted(), night);
}

// HUMAN SENSING's catch, a Detection like any other (DetectionType::MOTION):
// the LOG, the counters, IGNORE, SNOOZE, the TYPE FILTER and PUSH ALERTS all
// see it, one row per room (SenseMotion::macFor).
//
// Every loop pass while sensing runs. PRESENCE (MOTION now, or within the
// hold) keeps the row live, once a second; postKeyed() makes it news again
// only after MOTION_GONE_MS of a still room. The news is announced the way
// the alert screens announce -- ALERT FILTER, IGNORE, AUTO SNOOZE, then the
// buzzer and PUSH ALERTS -- with the status light, and SLEEP's screen lit
// for a glance. The card is raised only from the main screen (SENSE ARM):
// on the sensing screen PRESENCE already says it, and leaving that screen
// would hand the radio back and end the sensing.
static void serviceMotion(uint32_t now) {
    static uint32_t postedAt = 0;
    if (!SenseStore::fresh(now)) return;
    const SenseReport& r = SenseStore::last();
    if (!r.presence || !senseModeLive(r.mode)) return;
    if (postedAt && now - postedAt < 1000) return;
    postedAt = now;
    Detection d;
    memset(&d, 0, sizeof d);
    SenseMotion::macFor(senseCydNetwork(), d.mac);
    d.rssi      = r.rssi;
    d.channel   = (uint8_t)r.chan;   // the router's: a WiFi row
    d.type      = DetectionType::MOTION;
    d.vendor    = "WiFi sensing";
    snprintf(d.name, sizeof d.name, "%s", senseCydNetwork());
    d.firstSeen = d.lastSeen = now;
    d.hits      = 1;
    d.conf      = SenseMotion::confFor(r.rel);
    d.active    = true;
    engine.postKeyed(d, DetectionEngine::MOTION_GONE_MS);
    const Detection* c = freshCatch(now);
    if (!c || c->type != DetectionType::MOTION || c->conf < Settings::minConfidence() ||
        IgnoreList::silenced(c->mac) || !alertMayInterrupt(*c))
        return;
    Serial.printf("[sense] MOTION on %s: logged and announced\n", c->name);
    s_motionLightUntil = now + MOTION_LIGHT_MS;
    if (!s_motionLightUntil) s_motionLightUntil = 1;
    if (state == AppState::SENSING && s_senseDark) {
        senseWake();
        s_senseGlanceUntil = now + SENSE_GLANCE_MS;
        if (!s_senseGlanceUntil) s_senseGlanceUntil = 1;
    } else if (state == AppState::CLEAR) {
        // Armed, on the main screen: the card, as any catch gets there.
        uiAlertSetRedacted(false);
        enterAlert(*c);
    }
}

// SLEEP's glance is over: dark again, unless a touch or a key since has
// cleared it (the screen is wanted) or the screen was left.
static void serviceSenseGlance(uint32_t now) {
    if (!s_senseGlanceUntil || (int32_t)(now - s_senseGlanceUntil) < 0) return;
    s_senseGlanceUntil = 0;
    if (state == AppState::SENSING) senseSleep();
}
#endif
// MY ALERTS, and the two trips to the keyboard it makes: the pattern, then
// its label (or a new label for one already saved). The keyboard is the
// WiFi password one in its plain-text mode; this says what it is typing for.
enum class AlertTyping : uint8_t { NONE, PATTERN, LABEL_NEW, RELABEL };
static AlertTyping s_alertTyping = AlertTyping::NONE;
static char        s_alertPattern[MyAlerts::PAT_MAX + 1] = "";
static uint8_t     s_alertSlot = 0;
static void enterMyAlerts() {
    state = AppState::MY_ALERTS;
    transitionStart = millis();
    uiMyAlertsInit(*canvas);
}
static void enterAlertKeyboard(AlertTyping what, const char* prompt, const char* subject, const char* initial,
                               uint8_t maxLen, bool allowEmpty) {
    s_alertTyping = what;
    state = AppState::WIFI_PASS;
    transitionStart = millis();
    uiWifiPassInitText(*canvas, prompt, subject, initial, maxLen, allowEmpty);
}
// The keyboard came back from one of MY ALERTS' trips with OK or BACK.
static void myAlertsTyped(WifiPassResult r) {
    const AlertTyping what = s_alertTyping;
    s_alertTyping = AlertTyping::NONE;
    char text[MyAlerts::PAT_MAX + 1];
    snprintf(text, sizeof text, "%s", uiWifiPassText());
    uiWifiPassClear();
    if (r == WifiPassResult::BACK) {
        if (what == AlertTyping::LABEL_NEW) Theme::showToast("NOT ADDED", nullptr, Theme::AMBER);
        enterMyAlerts();
        return;
    }
    switch (what) {
        case AlertTyping::PATTERN:
            if (MyAlerts::check(text) == MyAlerts::Check::TOO_WIDE) {
                // A star alone, or one letter and a star, is every network in the street.
                Theme::showToast("TOO WIDE", "Needs 2+ letters besides * and ?", Theme::AMBER);
                enterMyAlerts();
                return;
            }
            snprintf(s_alertPattern, sizeof s_alertPattern, "%s", text);
            enterAlertKeyboard(AlertTyping::LABEL_NEW, "LABEL (OPTIONAL) FOR", s_alertPattern, "",
                               MyAlerts::LABEL_MAX, true);
            return;
        case AlertTyping::LABEL_NEW: {
            const int8_t slot = MyAlerts::add(s_alertPattern, text);
            if (slot < 0) {
                Theme::showToast("LIST FULL", "Remove one first", Theme::AMBER);
            } else {
                uiMyAlertsSelect(slot);
                if (MyAlerts::check(s_alertPattern) == MyAlerts::Check::WIDE)
                    Theme::showToast("SAVED, BUT WIDE", "Short patterns catch other networks", Theme::AMBER);
                else
                    Theme::showToast("WATCHING", s_alertPattern, Theme::CYAN);
            }
            break;
        }
        case AlertTyping::RELABEL:
            MyAlerts::setLabel(s_alertSlot, text);
            Theme::showToast(text[0] ? "LABEL SAVED" : "LABEL CLEARED", nullptr, Theme::CYAN);
            break;
        default: break;
    }
    enterMyAlerts();
}

static void enterWifiAdd() {
    state = AppState::WIFI_ADD;
    transitionStart = millis();
    engine.startRawWifiScan();
    uiWifiAddInit(*canvas);
}

// WIFI TIME, from the main screen's bar: join a saved network just long
// enough to set the clock. Detection is paused for it (the radio is shared)
// and resumed in the TIME_SYNC case the moment the job lets the radio go.
// With nothing saved it goes to WIFI NETWORKS to add one instead.
static bool s_timeSyncRadio = false;
static void enterTimeSync() {
    // AUTO TIME already has a join going: show that one rather than refuse.
    if (s_timeSyncRadio && OtaWifi::timeSyncBusy()) {
        state = AppState::TIME_SYNC;
        transitionStart = millis();
        uiTimeSyncInit(*canvas);
        return;
    }
    if (!OtaWifi::savedCount()) {
        Theme::showToast("NO WIFI SAVED", "Add yours, then tap WIFI TIME again", Theme::AMBER);
        enterWifiNets();
        return;
    }
    engine.startUpdateRadio();
    if (!OtaWifi::timeSyncStart()) {
        engine.stopUpdateRadio();
        Theme::showToast("CAN'T START", "The radio is busy -- try again", Theme::AMBER);
        return;
    }
    s_timeSyncRadio = true;
    state = AppState::TIME_SYNC;
    transitionStart = millis();
    uiTimeSyncInit(*canvas);
}

// Detection back the moment a time join lets the radio go, whichever screen
// is up: WIFI TIME's own, or the main screen behind an AUTO TIME join.
static void serviceTimeSyncRadio() {
    if (s_timeSyncRadio && !OtaWifi::timeSyncBusy()) {
        engine.stopUpdateRadio();
        s_timeSyncRadio = false;
        Serial.printf("[time] join over: clock %s\n", Clock::trusted() ? "set" : "not set");
    }
}

#include "main_push.inc"   // PUSH ALERTS: the queue, the joins, the daily summary

// AUTO TIME: WIFI TIME without the screen, from the main screen, once a day
// -- or once, a couple of minutes after a boot whose own check didn't set
// the clock -- and an hour after a try that didn't answer. The pill in the
// title bar says SCAN PAUSED for the few seconds it holds the radio.
static void serviceAutoTime(uint32_t now) {
    static uint32_t lastTry = 0;
    if (!Settings::autoTime() || state != AppState::CLEAR || s_timeSyncRadio) return;
    if (!OtaWifi::savedCount() || OtaWifi::timeSyncBusy() || OtaWifi::state() != OtaWifi::State::OFF) return;
    if (engine.radiosResting() || engine.updateRadioOn()) return;
    if (now - lastTouch < 15000) return;   // not under somebody's finger
    if (!AutoTime::due(now, Clock::lastSyncMs(), lastTry)) return;   // auto_time.h, tested
    lastTry = now;
    engine.startUpdateRadio();
    if (!OtaWifi::timeSyncStart()) { engine.stopUpdateRadio(); return; }
    s_timeSyncRadio = true;
    Serial.println("[time] AUTO TIME: joining for the clock");
}

// NIGHT DIM: inside the chosen hours, with the clock set, the screen runs at
// a quarter of its brightness -- except for half a minute after a touch, and
// an alert counts as one (it moves lastTouch). Checked every loop; the
// backlight is only touched when the answer changes.
static bool nightDimNow(uint32_t now) {
    if (!Settings::nightMode() || !Clock::trusted()) return false;
    if (now - lastTouch < 30000) return false;
    return NightMode::active(Settings::nightMode(), Clock::hour());
}
#if !defined(TWATCH_S3)
// NIGHT POWER's DARK+WIFI: while the screen is dark for the night on the main
// screen, the WiFi sniffer listens 5 s of every 30 and rests the rest, with
// Bluetooth on throughout (restRadios(false), the watch's BLE-always rest).
// Anything else -- a touch, an alert, another screen, an update or a join
// (startUpdateRadio wakes them first) -- has the radios awake at once.
static void serviceNightWifi(uint32_t now) {
    static uint32_t phaseAt = 0;
    const bool want = Settings::nightPower() == 2 && s_nightDark && state == AppState::CLEAR &&
                      OtaWifi::state() == OtaWifi::State::OFF;
    if (!want) {
        if (engine.radiosResting()) { engine.wakeRadios(); Serial.println("[night] WiFi awake"); }
        phaseAt = now;
        return;
    }
    if (!engine.radiosResting()) {
        if (now - phaseAt < NightMode::WIFI_ON_MS) return;
        if (engine.restRadios(false)) Serial.println("[night] WiFi resting, Bluetooth on");
        phaseAt = now;   // a refused rest tries again after another on-window
    } else if (now - phaseAt >= NightMode::WIFI_REST_MS) {
        engine.wakeRadios();
        phaseAt = now;
    }
}
#endif

static void serviceNightDim(uint32_t now) {
    const bool want = nightDimNow(now);
    if (want != s_nightDim) {
        s_nightDim = want;
        applyBrightness();
        Serial.printf("[night] %s\n", want ? "dim" : "full brightness");
    }
}


// A tracker travelling with you (include/tracker_follow.h): every 30 s, feed
// the tags in the LOG to the follow table and raise the first one that has
// stayed with you twenty minutes while the devices around kept changing. Once
// per tag, never for an ignored one, and only from the main screen or the LOG
// -- the meeting sign, settings and the rest are left alone. It skips AUTO
// SNOOZE on purpose: a tag that keeps coming back is the point.
// On the heap, made at the first scan (well after boot): as a static its
// 192 bytes moved the heap's layout enough to cost the largest free block
// 4 KB, measured.
// A tracker away from its owner that turns up near this board on several
// different days is the other shape of being followed: not one long walk
// (WITH YOU) but every day. The days are counted AWAY FROM HOME only
// (away_days.h): a board at home would otherwise flag a neighbour's tag next
// door. Once a minute, from the main screen or the LOG: such a tracker on
// DAYS_ALERT away-days gets the alert and a push, once a day each. The table
// lives in NVS ("awaydays"), saved at most every ten minutes.
static AwayDays::Table s_awayDays;
// DAYSFAKE (bench): a tracker counted on the three days before today, away
// from home -- the alert without the three days. Applied on the next pass.
static uint8_t s_awayFake[6];
static bool    s_awayFakePending = false;
void benchAwayDays(const uint8_t mac[6]) { memcpy(s_awayFake, mac, 6); s_awayFakePending = true; }
static bool s_awaySaveNow = false;   // a console change: saved on the next pass
static void awayDaysLoad() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    Preferences p;
    if (p.begin("awaydays", true)) {
        uint8_t blob[1 + AwayDays::SLOTS * sizeof(AwayDays::Slot)];
        const size_t n = p.getBytes("t", blob, sizeof blob);
        s_awayDays.load(blob, n);
        p.end();
    }
}
// The LOG's "2d away" (uiLogSetAwayDays).
static uint8_t awayDaysFor(const uint8_t mac[6]) { return s_awayDays.days(mac); }
// AWAYDAYS LIST prints "AWD <mac> <days> <last day> <told day>" per tracker,
// "AWD HOME <access points known> <home in view 0/1> <home name>", and
// "AWD END <n> <today>" (days since 1970, local; 0 = clock not set).
// AWAYDAYS DEL <mac> forgets one, AWAYDAYS CLEAR all. For the web flasher's
// AWAY DAYS panel, and a board that counted a bench test's fake days.
static bool awayDaysConsole(const char* line) {
    if (strncasecmp(line, "AWAYDAYS", 8) != 0) return false;
    awayDaysLoad();
    const char* a = line + 8;
    while (*a == ' ') a++;
    if (!*a || strcasecmp(a, "LIST") == 0) {
        for (uint8_t i = 0; i < s_awayDays.count(); i++) {
            const AwayDays::Slot* s = s_awayDays.at(i);
            Serial.printf("AWD %02x%02x%02x%02x%02x%02x %u %lu %lu\n", s->mac[0], s->mac[1], s->mac[2], s->mac[3],
                          s->mac[4], s->mac[5], (unsigned)s->days, (unsigned long)s->lastDay, (unsigned long)s->toldDay);
        }
        Serial.printf("AWD HOME %u %d %s\n", (unsigned)engine.homeAps().count(),
                      engine.homeSsid()[0] && engine.homeInView(millis()) ? 1 : 0, engine.homeSsid());
        Serial.printf("AWD END %u %lu\n", (unsigned)s_awayDays.count(),
                      (unsigned long)(Clock::isSet() ? Clock::localDay() : 0));
        return true;
    }
    if (strcasecmp(a, "CLEAR") == 0) {
        s_awayDays.clear();
        s_awaySaveNow = true;
        Serial.println("[awaydays] cleared");
        return true;
    }
    if (strncasecmp(a, "DEL ", 4) == 0) {
        const char* p = a + 4;
        uint8_t mac[6];
        bool ok = true;
        for (int i = 0; i < 6 && ok; i++) {
            while (*p == ':' || *p == '-' || *p == ' ') p++;
            char hx[3] = { p[0], p[0] ? p[1] : '\0', '\0' };
            char* end = nullptr;
            const long v = strtol(hx, &end, 16);
            ok = hx[0] && hx[1] && end == hx + 2;
            mac[i] = (uint8_t)v;
            p += 2;
        }
        if (!ok) { Serial.println("[awaydays] bad: a MAC is twelve hex digits"); return true; }
        const bool gone = s_awayDays.remove(mac);
        if (gone) s_awaySaveNow = true;
        Serial.println(gone ? "[awaydays] removed" : "[awaydays] not on the list");
        return true;
    }
    Serial.println("[awaydays] one of: AWAYDAYS LIST, AWAYDAYS DEL <mac>, AWAYDAYS CLEAR");
    return true;
}
static void serviceTrackerDays(uint32_t now) {
    static uint32_t last = 0, savedAt = 0;
    if (now - last < 60000 && !s_awaySaveNow) return;
    last = now;
    awayDaysLoad();
    // A change is saved now, or ten minutes after the last save: a handful
    // of writes a day to the settings store, not one a minute. One from the
    // console (DEL, CLEAR) is saved straight away.
    static bool pending = false;
    if (s_awayDays.takeDirty()) pending = true;
    if (pending && (!savedAt || now - savedAt > 10u * 60u * 1000u || s_awaySaveNow)) {
        uint8_t blob[1 + AwayDays::SLOTS * sizeof(AwayDays::Slot)];
        const size_t n = s_awayDays.save(blob, sizeof blob);
        Preferences p;
        if (n && p.begin("awaydays", false)) { p.putBytes("t", blob, n); p.end(); }
        savedAt = now ? now : 1;
        pending = false;
    }
    s_awaySaveNow = false;
    // A real clock, not the note-to-self guess a cold boot starts from: a
    // guess can be days behind, and would count a day that was not one.
    if (!Clock::trusted() || (state != AppState::CLEAR && state != AppState::LOG)) return;
    const uint32_t today = Clock::localDay();
    if (s_awayFakePending) {
        s_awayFakePending = false;
        for (uint32_t back = 3; back >= 1; back--) s_awayDays.note(s_awayFake, today - back, true);
        Serial.printf("[bench] away days now %u\n", (unsigned)s_awayDays.days(s_awayFake));
    }
    // Away from home: no home set (every day counts, as before), or home not
    // heard in the last few minutes.
    const bool away = !engine.homeSsid()[0] || !engine.homeInView(now);
    // Just booted and home not heard yet: not known to be away. Counting
    // then gave a board sitting at home an away day at every restart (seen
    // 2026-10-13) -- the scan needs a moment to hear the router.
    if (away && engine.homeSsid()[0] && !engine.homeSeenMs() && now < DetectionEngine::HOME_MS) return;
    const Detection* due = nullptr;
    uint8_t dueDays = 0;
    for (uint8_t i = 0; i < engine.logCount(); i++) {
        const Detection* d = engine.logAt(i);
        if (!d || d->restored || !d->active) continue;
        if (d->type != DetectionType::AIRTAG && d->type != DetectionType::GOOGLE_TAG &&
            d->type != DetectionType::SAMSUNG_TAG) continue;
        if (d->conf != Confidence::HIGH_CONF || IgnoreList::silenced(d->mac)) continue;   // away from its owner only
        s_awayDays.note(d->mac, today, away);
        if (!due && s_awayDays.due(d->mac, today)) { due = d; dueDays = s_awayDays.days(d->mac); }
    }
    if (due) {
        const Detection* d = due;
        const uint8_t days = dueDays;
        Serial.printf("[days] %s %02x:%02x:%02x:%02x:%02x:%02x away from its owner, near you on %u days\n",
                      detectionTypeName(d->type), d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5],
                      (unsigned)days);
        char title[40], body[100], seq[24], act[32];
        snprintf(title, sizeof title, "%s near you on %u days", detectionTypeName(d->type), (unsigned)days);
        snprintf(body, sizeof body, "Away from its owner, seen on %u different days. %d dBm, %02X:%02X:%02X:%02X:%02X:%02X",
                 (unsigned)days, (int)d->rssi, d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
        snprintf(seq, sizeof seq, "days-%02x%02x%02x%02x%02x%02x", d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
        snprintf(act, sizeof act, "IGNORE %02x%02x%02x%02x%02x%02x %u %s",
                 d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5], (unsigned)d->type, boardId());
        if (Settings::pushOn()) queuePush(title, body, 5, "rotating_light", true, seq, act);
        s_alertSpam = false;
        s_alertLastFree = false;
        enterAlert(*d);
        uiAlertSetDays(days);       // one a minute at most: the next waits its turn
    }
}

// The home network's own access points (home_aps.h), kept in NVS
// ("homeaps": "ssid" = the home name they belong to, "b" = the list). Loaded
// when the home name is set or changes -- a list learned for another name is
// dropped -- and grown by every join to the home network.
static void serviceHomeAps() {
    static char     loadedFor[33] = "";
    static bool     loaded = false;
    static uint32_t joinSeq = 0;
    const char* h = engine.homeSsid();
    HomeAps::Set& aps = engine.homeAps();
    if (!loaded || strcmp(loadedFor, h) != 0) {
        loaded = true;
        strncpy(loadedFor, h, sizeof loadedFor - 1);
        loadedFor[sizeof loadedFor - 1] = 0;
        aps.clear();
        Preferences p;
        if (h[0] && p.begin("homeaps", true)) {
            char ssid[33] = "";
            p.getString("ssid", ssid, sizeof ssid);
            if (strcmp(ssid, h) == 0) {
                uint8_t blob[2 + HomeAps::SLOTS * 6];
                aps.load(blob, p.getBytes("b", blob, sizeof blob));
            }
            p.end();
        }
        Serial.printf("[home] %s: %u access point(s) known\n", h[0] ? h : "(none)", (unsigned)aps.count());
    }
    uint8_t b[6];
    int8_t  idx = -1;
    const uint32_t q = OtaWifi::joinedBssid(b, &idx);
    if (q == joinSeq) return;
    joinSeq = q;
    if (!h[0] || idx < 0 || idx >= (int8_t)OtaWifi::savedCount() || strcmp(OtaWifi::savedSsidAt((uint8_t)idx), h) != 0) return;
    if (!aps.learn(b)) return;
    uint8_t blob[2 + HomeAps::SLOTS * 6];
    const size_t n = aps.save(blob, sizeof blob);
    Preferences p;
    if (n && p.begin("homeaps", false)) { p.putString("ssid", h); p.putBytes("b", blob, n); p.end(); }
    Serial.printf("[home] learned %02x:%02x:%02x:%02x:%02x:%02x for %s (%u known)\n",
                  b[0], b[1], b[2], b[3], b[4], b[5], h, (unsigned)aps.count());
}

static TrackerFollow::Table* s_follow = nullptr;
static void serviceFollowing(uint32_t now) {
    static uint32_t lastScan = 0;
    if (now - lastScan < 30000) return;
    lastScan = now;
    if (!s_follow && !(s_follow = new (std::nothrow) TrackerFollow::Table())) return;
    const uint32_t seq = engine.newRows();
    // Twice: the addresses already followed first, then the new ones, which
    // may be one of those after an address change (TrackerFollow::heard).
    for (int pass = 0; pass < 2; pass++)
    for (uint8_t i = 0; i < engine.logCount(); i++) {
        const Detection* d = engine.logAt(i);
        if (!d || d->restored || !d->lastSeen) continue;
        if (s_follow->known(d->mac) != (pass == 0)) continue;
        switch (d->type) {
            // An Apple advert counts only as the Find My beacon a tag sends
            // away from its owner (HIGH, "Away-Owner"): your own AirPods case
            // or AirTag beside your own phone sends none, and was the most
            // common WITH YOU that was not one.
            // The same for Google's and Samsung's tags, whose adverts say it
            // too (HIGH = separated). Tile's says nothing either way: all count.
            case DetectionType::AIRTAG: case DetectionType::GOOGLE_TAG: case DetectionType::SAMSUNG_TAG:
                if (d->conf != Confidence::HIGH_CONF) continue;
                break;
            case DetectionType::TILE: break;
            default: continue;
        }
        // Ignored and snoozed tags are followed too -- never announced (see
        // below) -- so that when one changes its address the IGNORE or
        // SNOOZE can go with it.
        uint8_t from[6];
        if (s_follow->heard(d->mac, d->firstSeen, d->lastSeen, seq,
                            (uint8_t)d->type, d->rssi, from)) {
            Serial.printf("[follow] %s %02x:%02x:%02x:%02x:%02x:%02x is %02x:%02x:%02x:%02x:%02x:%02x with a new address\n",
                          detectionTypeName(d->type), from[0], from[1], from[2], from[3], from[4], from[5],
                          d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
            s_awayDays.relink(from, d->mac);
            if (IgnoreList::carryOver(from, d->mac))
                Serial.println("[follow] ...its IGNORE / SNOOZE goes with it");
            // A HUNT on it follows it too, readings and all.
            if (engine.isHunted(from, true)) {
                engine.retargetHunt(d->mac);
                Serial.println("[follow] ...and the hunt follows it");
                Theme::showToast("HUNT FOLLOWS", "Same tag, new address", Theme::CYAN);
            }
        }
    }
    s_follow->expire(now);
    if (state != AppState::CLEAR && state != AppState::LOG) return;
    const int k = s_follow->due(now, seq);
    if (k < 0) return;
    // Followed only so its IGNORE or SNOOZE can follow it: never announced.
    if (IgnoreList::silenced(s_follow->e[k].mac)) { s_follow->e[k].alerted = true; return; }
    for (uint8_t i = 0; i < engine.logCount(); i++) {
        const Detection* d = engine.logAt(i);
        if (!d || memcmp(d->mac, s_follow->e[k].mac, 6) != 0) continue;
        const uint16_t mins = s_follow->minutes(k);
        s_follow->e[k].alerted = true;
        Serial.printf("[follow] %s %02x:%02x:%02x:%02x:%02x:%02x with you %u min, %lu new devices since\n",
                      detectionTypeName(d->type), d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5],
                      (unsigned)mins, (unsigned long)(seq - s_follow->e[k].seqAtFirst));
        s_alertSpam = false;
        s_alertLastFree = false;
        enterAlert(*d);
        uiAlertSetFollow(mins);
        queuePushAlert(*d, mins ? mins : 1);
#if SQW_HAS_BUZZER
        // The one alert that is about YOU: it chirps whatever else is true,
        // with BUZZER on -- new device or not, night or not.
        if (Settings::buzzerOn()) Buzzer::chirp(BUZZ_CHIRP_MS);
#endif
        lastTouch = now;   // lights a dimmed screen, like any alert
        return;
    }
}

// SELFTEST on the console (and the web flasher's SELF-TEST button): every
// part of the board that can be checked without a person looking at it, one
// line each, then a summary. PASS is working, WARN is working but worth a
// look (no SD card, no saved WiFi, the clock not set), FAIL is broken. It
// changes nothing and joins nothing -- detection runs on throughout.
static void runSelfTest() {
    unsigned pass = 0, warn = 0, fail = 0;
    auto line = [&](int lvl, const char* what, const char* fmt, ...) {
        char detail[96];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof detail, fmt, ap);
        va_end(ap);
        static const char* const L[] = { "PASS", "WARN", "FAIL" };
        Serial.printf("[selftest] %s  %-12s %s\n", L[lvl], what, detail);
        (lvl == 0 ? pass : lvl == 1 ? warn : fail)++;
    };
    Serial.printf("[selftest] MuleSkin %s on %s, up %lu s, last reset %s\n",
                  FIRMWARE_VERSION, OtaCore::buildName(), (unsigned long)(millis() / 1000), resetReasonName());

    // Memory: what the radios and the display allocate from.
    const uint32_t freeH = ESP.getFreeHeap();
    const uint32_t block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    line(block < 12000 ? 2 : block < 20000 ? 1 : 0, "memory", "%lu free, largest block %lu",
         (unsigned long)freeH, (unsigned long)block);
    // The frame buffer the main screen draws into.
    line(frame.created() ? 0 : 1, "framebuffer", frame.created() ? "allocated" : "none (drawing direct, slower)");
    // Touch: a calibration of this board's own, or the built-in guess.
    line(s_calSource == CalSource::SAVED ? 0 : 1, "touch",
         s_calSource == CalSource::SAVED ? "calibrated" : "built-in ranges (Settings > CALIBRATE TOUCH)");
    // The two radios, by what they have actually handed over.
    line(advertRate() > 0 ? 0 : 2, "bluetooth", "%lu adverts/s", (unsigned long)advertRate());
    line(wifiFramesSeen() > 0 ? 0 : 2, "wifi sniff", "%lu frames so far", (unsigned long)wifiFramesSeen());
    // Storage.
    line(BlackBox::ready() ? 0 : 1, "black box", BlackBox::ready() ? "%u detections, %u crashes kept" : "off (no room in flash)",
         (unsigned)BlackBox::detectionsKept(), (unsigned)BlackBox::crashesKept());
    if (BlackBox::ready() && BlackBox::crashesKept()) line(1, "crashes", "%u kept -- CRASH REPORT in the web flasher", (unsigned)BlackBox::crashesKept());
    line(engine.sdReady() ? 0 : 1, "sd card", engine.sdReady() ? "logging" : "none (optional)");
    {
        Preferences p;
        const bool ok = p.begin("settings", true);
        p.end();
        line(ok ? 0 : 2, "settings", ok ? "readable" : "NVS will not open");
        char push[128];
        const uint8_t pg = pushStatusText(push, sizeof push);
        line(pg, "push alerts", "%s", push);
        // How full the store is. Full at boot, the Arduino core erases the
        // lot -- every setting, WiFi network and the touch calibration -- so
        // this warns well before: 75% WARN, 90% FAIL. A board still on the
        // old table (an update that came over the air) has the 20 KB store
        // at 0x9000; one USB install moves it to the 128 KB one.
        const uint8_t pct = nvsFullPercent();
        const unsigned nvsKb = nvsStoreKb();
        const bool oldTable = nvsKb && nvsKb < 64;
        line(pct >= 90 ? 2 : pct >= 75 ? 1 : 0, "settings store", "%u%% full of %u KB%s", (unsigned)pct, nvsKb,
             pct >= 75 ? " -- back up settings in the web flasher"
                       : oldTable ? " (old layout: one USB install from the web flasher makes it 128 KB)" : "");
    }
    // Time and the network it comes from.
    line(Clock::trusted() ? 0 : 1, "clock", Clock::trusted() ? "set" : "not set (WIFI TIME)");
    line(OtaWifi::savedCount() ? 0 : 1, "wifi saved", "%u network(s)", (unsigned)OtaWifi::savedCount());
    // Updates and their keys.
    line(OtaCore::available() ? 0 : 1, "updates", OtaCore::available() ? "second app slot ready, %u trusted key(s)" : "no second app slot", OtaCore::trustedKeyCount());
    line(0, "rules", "%u extra detection rules", (unsigned)extraRuleCount());
    Serial.printf("[selftest] done: %u pass, %u warn, %u fail\n", pass, warn, fail);
}

// LIVE VIEW (the console's LIVE ON): every two seconds, each device heard in
// those two seconds -- type, address, signal now, vendor, name.
static void serviceLive(uint32_t now) {
    static uint32_t last = 0;
    if (!g_liveOn || now - last < 2000) return;
    last = now;
    for (uint8_t i = 0; i < engine.logCount(); i++) {
        const Detection* d = engine.logAt(i);
        if (d && !d->restored && d->lastSeen && now - d->lastSeen <= 2000) printLive(*d, false);
    }
}

// AUTO UPDATE (SYSTEM page, off by default): when a newer release is known
// -- from the boot check, or the daily AUTO TIME join that asks too --
// install it at night, on its own: a 30 s countdown with SKIP, then the
// unattended join-check-install-restart above, which falls back to the old
// version if anything fails. Night is NIGHT DIM's hours, or 1-5 AM without
// them; only from the main screen, after ten minutes untouched, unlocked, and
// one try a night.
//
// AFTER 3 DAYS waits three days from when THIS board first heard of the
// release; EARLY goes the first night. Set one board EARLY and a release
// that turns out bad is found there, and pulled from the site, before the
// rest ever install it -- no server-side rollout needed.
static const uint32_t AUTO_WAIT_S = 3u * 24u * 3600u;
static void serviceAutoUpdate(uint32_t now) {
    static uint32_t lastTry = 0;
    if (!Settings::autoUpdate() || state != AppState::CLEAR) return;
    if (!OtaCore::available() || !OtaCore::availableVersion()[0] || !OtaWifi::hasSaved()) return;
    if (Security::locked() || !Clock::trusted() || OtaWifi::timeSyncBusy()) return;
    {
        // Asked once a minute: NVS is read, and written only for a new version.
        static uint32_t lastAsk = 0;
        static bool     waiting = true;
        if (!lastAsk || now - lastAsk > 60000u) {
            lastAsk = now;
            const uint32_t ep   = Clock::nowEpoch();
            const uint32_t seen = Settings::autoSeen(OtaCore::availableVersion(), ep);
            waiting = Settings::autoUpdateMode() == 1 && ep - seen < AUTO_WAIT_S;
        }
        if (waiting) return;
    }
    const uint8_t p = Settings::nightMode();
    const uint8_t h = Clock::hour();
    const bool night = p ? NightMode::active(p, h) : (h >= 1 && h < 5);
    if (!night || now - lastTouch < 10u * 60u * 1000u) return;
    if (lastTry && now - lastTry < 6u * 3600u * 1000u) return;
    lastTry = now;
    unsigned v[3];
    if (!Version::parse(OtaCore::availableVersion(), v)) return;
    const unsigned a = v[0], b = v[1], c = v[2];
    s_autoVer[0] = (uint8_t)a; s_autoVer[1] = (uint8_t)b; s_autoVer[2] = (uint8_t)c;
    Serial.printf("[auto] night update to v%u.%u.%u: counting down\n", a, b, c);
    enterAutoUpdate();
}

// HUNT and WATCH follow a device's own signal. A MOTION row is a room, with
// no signal of its own to follow (the dBm is the router's): the buttons say
// so rather than starting a hunt whose gauge would never move.
static bool refuseFollowMotion(DetectionType t) {
    if (t != DetectionType::MOTION) return false;
    Theme::showToast("NOTHING TO FOLLOW", "MOTION is a room, not a device", Theme::AMBER);
    return true;
}

static void enterHunt() {
    state = AppState::HUNT;
    transitionStart = millis();
    uiHuntInit(*canvas);
}

// true when reached via the first-boot flow (DONE -> CLEAR, straight
// into the normal onboarding overlay if this is also a first boot),
// false when reached later via Settings' "CHECK COLORS" row
// (DONE -> back to SETTINGS, matching OUTFIT's same back-to-where-you-
// came-from feel).
static bool s_colorCheckFromSettings = false;

static void enterColorCheck(bool fromSettings) {
    state = AppState::COLOR_CHECK;
    transitionStart = millis();
    s_colorCheckFromSettings = fromSettings;
    uiColorCheckInit(*canvas);
}

static void enterDiary() {
    state = AppState::DIARY;
    transitionStart = millis();
    uiDiaryInit(*canvas);
}

static void enterOutfit() {
    state = AppState::OUTFIT;
    transitionStart = millis();
    uiOutfitInit(*canvas);
}

static void enterIgnoreList() {
    state = AppState::IGNORE_LIST;
    transitionStart = millis();
    uiIgnoreListInit(*canvas);
}


static void enterDetFilter(bool keepScroll = false) {
    state = AppState::DETECTION_FILTER;
    transitionStart = millis();
    uiDetFilterInit(*canvas, keepScroll);
}

static void enterBeaconWarn() {
    state = AppState::BEACON_WARN;
    transitionStart = millis();
    uiBeaconWarnInit(*canvas);
}

#include "main_pin.inc"   // the PIN lock, its flows, and the wipe

static void enterPower() {
    state = AppState::POWER_SAVER;
    transitionStart = millis();
    uiPowerInit(*canvas);
}

#if MULESKIN_LORA
static void enterLoraChat() {
    state = AppState::LORA_CHAT;
    transitionStart = millis();
    uiLoraChatInit(*canvas);
}
#endif

static void enterLight() {
    state = AppState::STATUS_LIGHT;
    transitionStart = millis();
    uiLightInit(*canvas);
}

#include "main_boards.inc"   // board power and batteries: T-Watch S3, Freenove S3 / T-Deck, Marauder V8

// Runtime UART speed -- set per-board in platformio.ini (-DSERIAL_BAUD=...)
// for hardware confirmed to hold a faster rate cleanly; boards without
// an explicit override fall back to this conservative default rather
// than inheriting cyd's verified-fast rate on unverified hardware.
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 921600
#endif

// The serial boot banner. Box-drawing and block characters, so it wants a
// UTF-8 terminal -- every monitor used with this board is one, and the
// console runs at 2,000,000 baud where a few hundred extra bytes cost
// nothing. Written out as literal characters rather than \u escapes so the
// art is legible here, which is the only place anyone will edit it.
//
// The version is NOT typed in. The line this replaced said "v1.0" from the
// day it was written to the day it was deleted, which is what happens to a
// hand-written version string. FIRMWARE_VERSION is stamped from the git tag
// at build time by extra_script.py -- the same one the boot screen and the
// diary already show.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
static void printBootBanner() {
    Serial.println("╔══════════════════════════════════════════════════╗");
    Serial.println("║   .-\"\"\"-.                                        ║");
    Serial.println("║  /  ^ ^  \\     ███    M U L E S K I N    ███     ║");
    Serial.println("║  | [o|o] |     surveillance detector             ║");
    // %-13.13s holds the right border in place whatever the tag turns out
    // to be: the fixed text ahead of it is 37 columns and the box is 50.
    // The precision matters as much as the width -- a working tree builds as
    // "v1.5.16-dirty" and a commit past a tag as "v1.5.16-3-g554330d", both
    // of which walk the border off the end of the line. Truncated here only;
    // the boot screen and the diary still show the version in full.
    Serial.printf ("║  |   -   |     MULESKIN  .  %-13.13s        ║\n", FIRMWARE_VERSION);
    // Same %-34s trick as the version line above: the reason is variable
    // length ("interrupt watchdog" is the longest at eighteen characters)
    // and the right border has to stay put.
    char rst[40];
    snprintf(rst, sizeof(rst), "last reset: %s", resetReasonName());
    Serial.printf ("║  \\  \\_/  /     %-34s║\n", rst);
    Serial.println("║   )     (      2.4 GHz  .  ESP32  .  CYD         ║");
    Serial.println("║  /_/   \\_\\                                       ║");
    Serial.println("╚══════════════════════════════════════════════════╝");

    // A panic, a watchdog or a brownout is worth shouting about rather than
    // leaving as one word inside a box. The crash itself is already sitting
    // in the coredump partition (0x3F0000, 64K -- see the partition table in
    // platformio.ini), and nobody goes looking for it unless they are told
    // it is there. This is the difference between a bug report that says
    // "it keeps restarting" and one that says which task died.
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            Serial.println("*** That was not a clean boot.");
            Serial.println("*** The crash is saved in flash. To read it out:");
            Serial.println("***   esptool read_flash 0x3F0000 0x10000 core.bin");
            Serial.println("***   espcoredump.py info_corefile -c core.bin firmware.elf");
            if (g_lastCrash.haveDump) {
                Serial.printf("*** In task %s at 0x%08lx (cause %lu, address 0x%08lx)%s\n",
                              g_lastCrash.task, (unsigned long)g_lastCrash.pc,
                              (unsigned long)g_lastCrash.cause, (unsigned long)g_lastCrash.vaddr,
                              g_lastCrash.dumpOlder ? " -- written by other firmware" : "");
                Serial.print("*** Backtrace:");
                for (uint8_t i = 0; i < g_lastCrash.btN; i++)
                    Serial.printf(" 0x%08lx", (unsigned long)g_lastCrash.bt[i]);
                Serial.println();
            }
            break;
        default:
            break;
    }
}

// ---- Arduino setup / loop ----
#if defined(TWATCH_S3)
static void wardriveBegin();
#endif
#if defined(TDECK_PLUS)
// The keyboard: LilyGo's helper chip (an ESP32-C3 running their firmware) on
// the touch chip's I2C bus at 0x55. Asked, it answers one byte: the character
// of the key pressed since last time, 0x0D for enter, 0x08 for backspace,
// 0x00 for nothing. Shift and the symbol key are applied inside it. Polled
// every 25 ms. The key goes to the WiFi password or the PIN pad when one is
// up; anywhere else it is dropped. (Upstream SquachWatch v1.36.0.)
static const uint8_t TDECK_KBD = 0x55;
static void tdeckKeysTick(uint32_t now) {
    static uint32_t at = 0;
    if (now - at < 25) return;
    at = now;
    Wire.beginTransmission(TDECK_KBD);
    if (Wire.endTransmission() != 0) return;
    if (Wire.requestFrom((int)TDECK_KBD, 1) != 1) return;
    const uint8_t k = (uint8_t)Wire.read();
    if (k == 0 || k == 0x0C) return;             // nothing, or alt+C (its own)
    const char c = k == 0x0D ? '\n' : k == 0x08 ? '\b' : (char)k;
    if (state == AppState::WIFI_PASS) uiWifiPassKey(c, now);
    else if (state == AppState::PIN_ENTRY || state == AppState::LOCKED) uiPhoneKey(c);
#if defined(SENSE_CYD_APP)
    // HUMAN SENSING's buttons from the keyboard (upstream v1.49): Backspace is
    // BACK, R re-calibrates, S is SLEEP (any key wakes), O goes straight back
    // to the detector; , and . turn the page, ENTER and L run SNEAK, N starts
    // a new VITALS session.
    else if (state == AppState::SENSING && s_senseDark) senseWake();
    else if (state == AppState::SENSING) {
        s_senseGlanceUntil = 0;   // a key during a movement's glance: the screen stays on
#if SENSE_RICH
        if (uiSensingKey(c, now)) {}
        else
#endif
        if (c == 0x08) s_tdeckBackKey = true;   // BACK, as the cursor presses it
        else if (c == 'r' || c == 'R') {
            senseCydCommand("cal");
            Theme::showToast("RECALIBRATING", "Keep the room still for 15 s", Theme::AMBER);
        } else if (c == 's' || c == 'S') {
            senseSleep();
        } else if (c == 'o' || c == 'O') {
            Theme::showToast("SENSING OFF", "Detector back on", Theme::CYAN);
            enterClear();
        }
    }
#endif
#if defined(SQW_TRACKBALL)
    // Anywhere else Backspace is BACK, as the trackball's cursor would press
    // it (upstream SquachWatch v1.46).
    else if (c == 0x08) s_tdeckBackKey = true;   // Backspace
#endif
}
#endif

// ---- text straight at the panel ----------------------------------------------
// Three screens write at the panel rather than into the frame: the update
// check at boot, the calibration-reset prompt and the charge screen. On the
// Freenove 3.5" the panel is not TFT_eSPI's to write -- its text landed on the
// QSPI bus as a green hash upstream (2026-10-11) -- so there they draw into
// the frame and push it, once the frame exists. Everywhere else PANEL is the
// panel and panelShow() is nothing, which is what they did.
#if defined(FREENOVE_S3_35)
#define PANEL ((TFT_eSPI&)frame)
static bool panelTextOk() { return frameBufferOk && frame.created(); }
static void panelShow() {
    if (!panelTextOk()) return;
    St77922Blit::invalidate();   // every row: nothing drew its way to this
    St77922Blit::push(frame.buf(), frame.bufW(), frame.bufH(), tft.getRotation());
}
#else
#define PANEL tft
static bool panelTextOk() { return true; }
static void panelShow()   {}
#endif

// The battery for TERMINAL MODE's LOG title bar (uiClearSetBatterySource),
// read off the same line the SETTINGS battery row prints, so there is one
// reader per board and its two-second cache: "81% 4.12V" (a "+" after it
// while the Marauder's gauge sees it charging), "CHG 81%" on the watch, and
// "NONE" / "NO GAUGE" / "?" with no cell to read. The voltage-divider boards
// cannot tell a cable from a full cell, so they never say CHG. (Upstream
// v1.43 reads the chips directly; MuleSkin already had these lines.)
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(MARAUDER_V8) || defined(TDECK_PLUS) || defined(TWATCH_S3)
static bool terminalBatteryState(uint8_t& pct, bool& charging) {
    char b[24];
#if defined(TWATCH_S3)
    twatchBatteryLine(b, sizeof b);
#else
    boardBatteryLine(b, sizeof b);
#endif
    const bool chg = strncmp(b, "CHG ", 4) == 0;
    const char* num = chg ? b + 4 : b;
    if (*num < '0' || *num > '9') return false;
    const int p = atoi(num);
    pct = (uint8_t)(p < 0 ? 0 : p > 100 ? 100 : p);
    charging = chg || strchr(b, '+') != nullptr;
    return true;
}
#endif

#if defined(TAB5)
// ---- Tab5: the one-time radio update ---------------------------------------
// (Upstream SquachWatch v1.47.) A Tab5 from the box runs M5Stack's firmware on
// its C6 radio chip, which links but cannot sniff. When the link came up and
// the hello did not answer, this asks once per boot: UPDATE writes upstream's
// C6 image (carried inside this firmware) and restarts; LATER boots on with
// the radio off. Two writes that never brought a hello and it stops offering:
// the C6 has no way back from a bad image short of soldered wires, so it is
// never written blind or in a loop.
static inline void pushFrame(int x, int y);
static void tab5RadioScreen(const char* head, const char* l1, const char* l2, const char* l3,
                            const char* bLeft, const char* bRight, int pct) {
    TFT_eSPI& t = *canvas;
    const int w = t.width(), h = t.height();
    t.fillScreen(Theme::BG);
    t.setTextFont(1);
    t.setTextSize(4);
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor((w - t.textWidth(head)) / 2, h / 6);
    t.print(head);
    t.setTextSize(2);
    t.setTextColor(Theme::WHITE, Theme::BG);
    int y = h / 6 + 70;
    for (const char* l : { l1, l2, l3 }) {
        if (l && *l) { t.setCursor((w - t.textWidth(l)) / 2, y); t.print(l); }
        y += 34;
    }
    if (pct >= 0) {
        const int bw = w * 2 / 3, bx = (w - bw) / 2, by = y + 20;
        t.drawRect(bx, by, bw, 28, Theme::CYAN);
        t.fillRect(bx + 3, by + 3, (bw - 6) * pct / 100, 22, Theme::CYAN);
    }
    const int bh = 70, by = h - bh - 30, bw = w / 3;
    if (bLeft)  Theme::drawButton(t, w / 2 - bw - 20, by, bw, bh, bLeft, false, 2);
    if (bRight) Theme::drawButton(t, w / 2 + 20, by, bw, bh, bRight, false, 2);
    if (canvas != &tft) pushFrame(0, 0);
}

// Which of the two buttons tab5RadioScreen drew a tap landed on: -1 none,
// 0 left, 1 right. Waits for a whole tap (down, then up).
static int tab5RadioWaitTap() {
    const int w = tft.width(), h = tft.height(), bh = 70, by = h - bh - 30, bw = w / 3;
    bool down = false; int x = 0, y = 0;
    for (;;) {
        TouchPoint tp = pollTouch();
        if (tp.valid) { down = true; x = tp.x; y = tp.y; }
        else if (down) {
            down = false;
            if (y >= by && y < by + bh) {
                if (x >= w / 2 - bw - 20 && x < w / 2 - 20) return 0;
                if (x >= w / 2 + 20 && x < w / 2 + 20 + bw) return 1;
            }
        }
        delay(20);
    }
}

static uint32_t s_radioPctAt = 0;
static void tab5RadioProgress(uint32_t done, uint32_t total) {
    // A frame every few hundred milliseconds is plenty, and keeps the push
    // from slowing the write.
    const uint32_t now = millis();
    if (done < total && now - s_radioPctAt < 300) return;
    s_radioPctAt = now;
    char l[40];
    snprintf(l, sizeof l, "%lu of %lu KB", (unsigned long)(done / 1024), (unsigned long)(total / 1024));
    tab5RadioScreen("UPDATING RADIO", "Writing the radio's firmware.", "Keep the Tab5 plugged in.", l,
                    nullptr, nullptr, (int)(done * 100 / total));
}

static void tab5RadioUpdatePrompt() {
    if (!Tab5Radio::needsUpdate()) return;
    if (Tab5Radio::updateTries() >= 2) {
        Serial.println("[c6] radio update tried twice without a hello; not offering again (docs/TAB5.md)");
        tab5RadioScreen("RADIO UPDATE", "The radio update did not take, twice.", "MuleSkin runs without WiFi and",
                        "Bluetooth. See docs/TAB5.md for help.", nullptr, "OK", -1);
        tab5RadioWaitTap();
        return;
    }
    tab5RadioScreen("RADIO UPDATE", "This Tab5's radio still runs its", "factory firmware. One update, about a",
                    "minute, and it hears WiFi and Bluetooth.", "LATER", "UPDATE", -1);
    if (tab5RadioWaitTap() != 1) {
        Serial.println("[c6] radio update: LATER; booting with the radio off");
        return;
    }
    char why[64] = "";
    s_radioPctAt = 0;
    tab5RadioProgress(0, Tab5Radio::imageSize());
    if (Tab5Radio::update(tab5RadioProgress, why, sizeof why)) {
        tab5RadioScreen("RADIO UPDATED", "Restarting to use it.", "", "", nullptr, nullptr, 100);
        delay(1500);
        ESP.restart();
    }
    tab5RadioScreen("RADIO UPDATE", "It did not work:", why, "Nothing changed. Try again next boot.", nullptr, "OK", -1);
    tab5RadioWaitTap();
}
#endif

void setup() {
    // Before anything opens NVS -- crashReportInit() does: settings left in
    // the old 20 KB store by a USB install onto the new table move across.
    const int nvsMoved = NvsMove::run();
    Theme::setFrameSprite(&frame);   // see Theme::drawArtwork()
    // Before anything else can allocate: the breadcrumb has to be read out
    // while it is still the previous life's, not this one's.
    crashReportInit();
    // Room for a whole console line: the web flasher's settings restore sends
    // NVS SET lines of up to ~540 characters (see nvs_backup.h). The sender
    // also has to pace them -- the UART's own 128-byte FIFO overflows at
    // 2 Mbaud while the board is busy -- which the flasher does.
    Serial.setRxBufferSize(1024);
    Serial.begin(SERIAL_BAUD);
#if SQW_BOOT_BTN
    pinMode(0, INPUT_PULLUP);   // BOOT: see bootButtonTick()
#endif
#if defined(SQW_S3) || defined(TAB5) || defined(WAVESHARE_C5)
    // Native USB: with nothing reading the port, every print would otherwise
    // wait its full timeout for a host, and after the chatty first-boot
    // calibration the loop crawled so slowly the screen looked frozen black.
    // Drop the bytes instead when nobody is listening. (Upstream's Tab5 showed
    // the same thing: laggy and freezing only while on the cable.)
    Serial.setTxTimeoutMs(0);
#endif
    delay(200);
    Serial.println();
    printBootBanner();
    if (nvsMoved > 0)  Serial.printf("[nvs] moved %d settings into the new, larger store\n", nvsMoved);
    if (nvsMoved < 0)  Serial.println("[nvs] could not move the old settings store; will try again next boot");
#if defined(TWATCH_S3)
    twatchPowerUp();
#endif
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(MARAUDER_V8) || defined(TDECK_PLUS) || defined(TWATCH_S3)
    // Read lazily, at TERMINAL MODE's first frame: the gauge and the power
    // chip are up by then.
    uiClearSetBatterySource(terminalBatteryState);
#endif
#if defined(TDECK_PLUS)
    tdeckPowerUp();
#endif
#if defined(STICKS3)
    sticksPowerUp();
#endif
#if defined(CYD35) || defined(NM_CYD_C5)
    // On the NM-CYD-C5 this answers a specific open question rather than a
    // general one. Its bootloader prints
    //   E MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!
    // on every boot. That is arduino-esp32 issue #12587, open since 2026-05-12
    // and not caused by anything here, and the reporter says PSRAM works
    // anyway -- so this line is how THIS board says whether it does, rather
    // than the project taking a stranger's word for it. Nothing in the
    // firmware asks for PSRAM on a CYD, so the answer is informational
    // either way.
    //
    // One-time diagnostic: is PSRAM actually present on this unit? The
    // "no PSRAM" conclusion driving the no-full-framebuffer tradeoff
    // (see `frame`'s declaration up top) was from an earlier pass --
    // worth confirming directly before deciding whether a PSRAM-backed
    // full double buffer is even on the table.
    Serial.printf("PSRAM found: %s (%u bytes)\n", psramFound() ? "yes" : "no", (unsigned)ESP.getPsramSize());
#endif

    // Backlight: the original board uses GPIO21 for this, the
    // JC2432W328C uses GPIO27 (confirmed by sweeping candidate pins on
    // a physical unit). Driving both HIGH is harmless either way —
    // each is simply an unused GPIO on the "other" board — and means
    // the screen lights up before we've even figured out which board
    // this is.
    //
    // GPIO21 exception on AWOK: it's TOUCH_CS there, not a spare. A
    // brief digitalWrite HIGH pre-init just holds CS deasserted and
    // would be harmless on its own, but the LEDC attach further down
    // would fight TFT_eSPI's control of the pin, so pin 21 is skipped
    // off entirely on AWOK for consistency with that.
// Not on AWOK (TOUCH_CS there) and not on either RL Phantom, where GPIO21 is
// the capacitive controller's INTERRUPT line. Driving it high at boot is the
// same mistake as the LEDC attach further down, just earlier.
// ... and not on the CrowPanel 7, where GPIO21 is the panel's BLUE-0 data
// line. Driving it high before the panel driver claims it is a stripe down the
// picture at best.
#if !defined(AWOK) && !defined(RLPHANTOM) && !defined(RLPHANTOM_R) && !defined(SQW_S3) && !defined(CROWPANEL7) && !defined(CYD32C) && !defined(CYD35C) && !defined(TAB5) && !defined(WAVESHARE_C5)
    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
#endif
#if defined(SQW_S3)
    // GPIO27 and GPIO32 are the S3's PSRAM lines: touching either hangs the
    // chip until the watchdog reboots it. Both S3 boards' backlight is GPIO45.
    pinMode(BL_PIN_S3, OUTPUT); digitalWrite(BL_PIN_S3, HIGH);
#elif defined(CROWPANEL7)
    // Nothing at all. This board's backlight is not a GPIO -- it is an I2C
    // command to the helper MCU, sent once Wire is up. GPIO27/32 are the same
    // OPI PSRAM lines the watch avoids, and every other candidate here is an
    // RGB data line.
#elif defined(TAB5)
    // Nothing: M5GFX owns the backlight pin, and 21/27/32 are somebody else's
    // on a P4.
#elif defined(WAVESHARE_C5)
    // No backlight pin and no reset pin: both are the CH32 helper's, on I2C.
    // It has to release the panel from reset before tft.init() talks to it
    // (TFT_RST is -1). The backlight stays dark until applyBrightness().
    WsC5::begin();
#elif defined(NM_CYD_C5)
    // One backlight, TFT_BL (GPIO25), and ONLY that pin. Confirmed on the
    // board: the branch below drives 27 and 32, and on a C5 that is
    //   GPIO27 -- the WS2812 status light. Held HIGH here it is a stuck data
    //           line, and StatusLight::begin() then talks to it over RMT.
    //   GPIO32 -- does not exist. The C5's GPIO range stops well below it, and
    //           the Arduino layer says so, loudly and three times:
    //             __pinMode(): Invalid IO 32 selected
    //             perimanGetPinBus(): Invalid pin: 32
    //             __digitalWrite(): IO 32 is not set as GPIO
    // Harmless in the sense that nothing crashes, and worth removing anyway:
    // three errors in the first 300 ms of every boot is noise that hides the
    // next real one.
    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#else
    pinMode(27, OUTPUT); digitalWrite(27, HIGH);
#if !defined(FREENOVE32) && !defined(CYD32C) && !defined(CYD35C)   // not a spare pin on the Freenove, and the 2432S032C's touch SCL; both light 27 alone
    pinMode(32, OUTPUT); digitalWrite(32, HIGH);  // AWOK's real BL pin; unused GPIO on the other two boards
#endif
#endif

    tft.init();

#if defined(CROWPANEL7_PANELTEST)
    // Bring-up only: prove the panel before anything else can be blamed for
    // it. Colour bars in a known order (so a red/blue swap in the pin map is
    // visible rather than merely wrong), a one-pixel grid (so a pixel clock
    // that is running away shows as a moving moire instead of "looks fine"),
    // and the measured frame period -- quiet, then with a flash write every
    // 20 ms in alternate three-second phases, which is what flash costs the
    // picture in numbers.
    crowPanelTest();
#endif

    // Load persisted settings before the first real pixel is drawn, so
    // boot itself already reflects the saved theme/invert choice
    // instead of flashing the defaults for a moment first. Moved ahead
    // of tft.setRotation() below so that call can already use the
    // saved rotation instead of always starting from the board default.
    Settings::load();
    Clock::begin();   // after Settings: the zone is applied there, the history here
#if defined(CYD_GPS)
    cydGpsStart();
#endif
    OtaWifi::loadExtraRules();   // detection rules delivered since the build, before any scanning
#if defined(TWATCH_S3)
    twatchRtcBegin();      // after Clock::begin(): a real time beats the note's guess
    twatchHapticBegin();
    twatchMotionBegin();
    twatchApplySteady();   // after Settings::load()
    twatchApplyCharge();
#endif
    Security::begin();
    IgnoreList::setChangeHook(ignoreSync);   // the other boards hear of this one's ignore list changes
    // Retired features' NVS, erased once on a board upgraded from a build
    // that had them: MuleSkinMesh's squad phrase and key ("meshtalk"), and
    // BINGO's card ("bingo", removed in v3.2.5). The check is a read, so a
    // board with nothing left to erase writes nothing.
#if HAVE_NVS_ERASE
    for (const char* ns : { "meshtalk", "bingo" }) {
        nvs_iterator_t it = sqwNvsEntryFind(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY);
        if (it) {
            nvs_release_iterator(it);
            Preferences old;
            if (old.begin(ns, false)) { old.clear(); old.end(); }
            Serial.printf("[boot] erased the retired \"%s\" settings\n", ns);
        }
    }
#endif
    // Which version lives in this slot, and whether this boot is a fresh
    // update on probation or the aftermath of one that was rolled back.
    OtaCore::boot();
#if !defined(AWOK) && !defined(SQW_SMALL)   // the small M5 boards are landscape only
    // AWOK has no rotate button and stays fixed at its one physical
    // orientation (see screenRotation's own comment above) -- only
    // boards that can actually rotate restore a saved orientation.
    screenRotation = Settings::rotation();
#endif
    // Landscape (320 wide × 240 tall) — the CYD's natural orientation
    // with the ST7789 driver. The 0xC2 unlock that was here was for
    // ST7796U and will lock up an ST7789 panel; do not re-add it unless
    // we confirm the panel is actually ST7796.
    tft.setRotation(screenRotation);
#if defined(SQW_SMALL)
    Theme::setRotateIconVisible(false);
    Theme::setCompact(true);
#if defined(STICKS3)
    pinMode(STICK_KEY1, INPUT_PULLUP);
    pinMode(STICK_KEY2, INPUT_PULLUP);
#endif
    // M5GFX's own settings, and upstream looked at the colours on the
    // bench: no first-boot check. CHECK COLORS (SYSTEM) is laid out for 135
    // rows now, should a panel ever disagree.
    if (!Settings::colorChecked()) Settings::markColorChecked();
#endif
#if defined(AWOK)
    // No rotate button on this board (see the rotate handler in
    // loop(), not even compiled in on AWOK) -- hide the icon too so
    // there's nothing dead-looking left in the title bar.
    Theme::setRotateIconVisible(false);
#endif
    // See PANEL_NEEDS_INVERSION's definition up top for why this XORs
    // against a per-board baseline instead of calling invertDisplay()
    // with Settings::inverted() directly.
    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
    applyColorOrder();
    tft.fillScreen(Theme::BG);

    // Hand the digitalWrite(HIGH) backlight pins above off to LEDC PWM
    // so the settings-menu brightness slider can dim them — same
    // "drive both boards' pin, only one is really wired" reasoning as
    // the digitalWrite call, just with a duty cycle instead of a flat
    // HIGH. BL_CH_ORIG/GPIO21 is skipped on AWOK because it's TOUCH_CS
    // there (same reasoning as the digitalWrite skip above) — attaching
    // LEDC to it would fight TFT_eSPI's control of the pin.
// GPIO21 is the backlight on the 2.8" boards and NOT on two others: it is
// TOUCH_CS on AWOK, and the capacitive controller's INTERRUPT line on the RL
// Phantom. Driving a 5 kHz PWM onto either is the kind of fault that looks
// like dead touch, which is exactly how it presented on the Phantom.
#if defined(CROWPANEL7)
    // No LEDC at all: the backlight is an I2C command byte. Claiming channels
    // here would only take them away from something that has a pin.
#elif defined(TAB5)
    // No LEDC here either: M5GFX set the backlight's PWM up with the panel.
#elif defined(TDECK_PLUS)
    // No LEDC: GPIO42 is a pulse-counting 16-step driver, and a PWM on it
    // would walk the brightness round and round. tdeckBacklight() steps it.
    pinMode(BL_PIN_S3, OUTPUT);
#elif defined(SQW_S3)
    // One backlight, GPIO45, on the first channel. The CYD pins below are
    // flash/PSRAM lines and the power chip's interrupt on an S3.
#if defined(STICKS3)
    // 256 Hz, as M5's own library runs it: at 5 kHz the StickS3 whined.
    ledcSetup(BL_CH_ORIG, 256, 8);
#else
    ledcSetup(BL_CH_ORIG, 5000, 8);
#endif
    ledcAttachPin(BL_PIN_S3, BL_CH_ORIG);
#elif defined(WAVESHARE_C5)
    // No LEDC: the backlight is the CH32 helper's PWM (WsC5::setBacklight).
#elif defined(NM_CYD_C5)
    // Backlight is TFT_BL (GPIO25) on this board and nothing else is a
    // backlight. The CYD pins below are actively wrong here, not merely
    // unused: BL_PIN_CAP is 27, which is this board's WS2812 status LED, and
    // BL_PIN_AWOK is 32, which the C5 does not have at all. Attaching either
    // is the same class of mistake the AWOK and Phantom guards above exist
    // to stop, so this board gets its own branch rather than a new exclusion
    // bolted onto theirs.
    //
    // ledcAttach(pin, freq, bits) is core 3's replacement for the
    // ledcSetup + ledcAttachPin pair; it picks the channel itself.
    ledcAttach(TFT_BL, 5000, 8);
#else
#if !defined(AWOK) && !defined(RLPHANTOM) && !defined(RLPHANTOM_R) && !defined(CYD32C) && !defined(CYD35C)
    ledcSetup(BL_CH_ORIG, 5000, 8);
    ledcAttachPin(BL_PIN_ORIG, BL_CH_ORIG);
#endif
    ledcSetup(BL_CH_CAP, 5000, 8);
    ledcAttachPin(BL_PIN_CAP, BL_CH_CAP);
#if !defined(FREENOVE32) && !defined(CYD32C) && !defined(CYD35C)   // see the pinMode(32) above; a channel with no pin is harmless to write
    ledcSetup(BL_CH_AWOK, 5000, 8);
    ledcAttachPin(BL_PIN_AWOK, BL_CH_AWOK);
#endif
#endif
    applyBrightness();
    // A saved core clock has to be restored here too, or the setting silently
    // reverts to 240 MHz on every reboot and looks like it never took.
    applyCpuClock();
    // The light on the back, and its half-second sweep during the splash.
    StatusLight::begin();
#if defined(SQW_CYD_SPEAKER)
    Buzzer::begin();   // silent until an alert, with BUZZER on
#endif
    StatusLight::boot(millis());

    // The boot check: a few seconds on the saved WiFi asking the site whether
    // there is a newer release, and only ever HERE, before Bluetooth exists.
    // Joining WiFi on a running board means giving Bluetooth up until the
    // next restart, so on a running board the same question is a whole mode
    // (UPDATE OVER WIFI). And before the frame buffer, too: the TLS
    // handshake wants about 40 KB in one piece, and with the buffer in place
    // the largest block is 35 KB -- measured, the first try returned -1.
    // Tells, never installs. Skipped with no saved network, with the board
    // locked, or with UPDATE CHECK off.
    bool bootCheckRan = false;
    if (takeBootCheckSkip()) {
        Serial.println("[ota] boot check skipped: the frame buffer failed after the last one");
    } else if (!(Settings::updateCheck() && !Security::locked() && OtaCore::available() && OtaWifi::hasSaved())) {
        // Say why, so a quiet boot log is not a mystery.
        Serial.printf("[ota] boot check off: %s\n",
                      !Settings::updateCheck() ? "UPDATE CHECK is off" :
                      Security::locked()       ? "the board is locked" :
                      !OtaCore::available()    ? "no second app slot" : "no saved WiFi network");
    } else {
        bootCheckRan = true;
        // The backlight down first, for the same reason it goes down at the
        // radio start below: WiFi's RF calibration plus a full backlight is
        // more than a weak USB port holds, and the first run of this check
        // browned the Phantom out into a second boot.
#if defined(CROWPANEL7)
        CrowBL::set(24);
#elif defined(TAB5)
        Tab5Panel::backlight(24);
#elif defined(TDECK_PLUS)
        tdeckBacklight(24);
#elif defined(WAVESHARE_C5)
        WsC5::setBacklight(24);
#else
        ledcWrite(BL_CH_ORIG, 24);
        ledcWrite(BL_CH_CAP,  24);
        ledcWrite(BL_CH_AWOK, 24);
#endif
        // The frame does not exist yet on the 3.5", so that board checks
        // without the message rather than with a garbled one.
        if (panelTextOk()) {
            PANEL.fillRect(0, 0, tft.width(), tft.height(), Theme::BG);
            PANEL.setTextSize(1);
            PANEL.setTextWrap(false);
            PANEL.setTextColor(Theme::CYAN, Theme::BG);
            const char* m = "CHECKING FOR UPDATES...";
            PANEL.setCursor((tft.width() - PANEL.textWidth(m)) / 2, tft.height() / 2 - 4);
            PANEL.print(m);
            panelShow();
        }
        OtaWifi::bootCheck(9000);
        if (panelTextOk()) { PANEL.fillRect(0, 0, tft.width(), tft.height(), Theme::BG); panelShow(); }
    }


#if defined(CYD35)
    // No FULL-screen double buffer on this board — confirmed on real
    // hardware that the 320x480 panel's ~150KB sprite need exceeds the
    // largest contiguous free heap block (~110KB, no PSRAM). `canvas`
    // aliases `tft` directly for this build (see its declaration up
    // top) and is what most screens draw into, unbuffered.
    //
    // The CLEAR screen is the exception: it's the most visibly animated
    // (MuleSkin + a background effect), so it gets a real half-height
    // sprite (~76KB at 8-bit, comfortably fits) and renders in two
    // bands via setViewport() -- see the AppState::CLEAR case in
    // loop(). `advance` on uiClearTick()/MuleSkin::tick() gates state
    // mutation to the first band only, so calling them twice per logical
    // frame doesn't double animation speed.
    canvas->setTextSize(1);
    frame.setColorDepth(8);
    if (!frame.createSprite(tft.width(), tft.height() / 2)) {
        Serial.println("ERROR: cyd35 half-height frame buffer allocation failed");
    }
    frame.setTextSize(1);
#else
    // 8-bit (palette) mode: 320x240 needs ~75KB instead of ~150KB at
    // 16-bit — the full 16-bit buffer didn't fit in the available
    // contiguous heap on this board.
    frame.setColorDepth(8);
#if (defined(SQW_S3) && !defined(FREENOVE_S3_40) && !defined(FREENOVE_S3_35)) || SQW_FRAME_FORCE_INTERNAL
    // (Not the Freenove 4.0": 480x320 at 8 bits is 154 KB, which goes to
    // PSRAM, as upstream SquachWatch does for its 480x320 S3 boards.)
    // 57.6 KB fits in internal RAM with room to spare (75 KB at 320x240), and a sprite in
    // PSRAM pushes slower and cannot go by DMA. Keep it inside.
    //
    // CrowPanel: not an economy but the fix for a twitching picture -- the
    // panel's DMA reads its framebuffer from PSRAM, and the UI's scattered
    // writes into a PSRAM sprite starve it (see crowpanel7_board.h).
    // Tab5: the blit reads it a column at a time, cheap in SRAM and slow in
    // PSRAM; 640x360 at 8 bits is 225 KB, which the P4's SRAM has room for.
    frame.setAttribute(PSRAM_ENABLE, false);
#endif
#if SQW_FRAME_FORCE_INTERNAL
    // PSRAM_ENABLE=false only makes TFT_eSPI call plain calloc() -- and on a
    // PSRAM-enabled Arduino core plain calloc() of anything over
    // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (4 KB here) is served from PSRAM
    // regardless. Measured: the frame landed at 0x3D8BC308, which is PSRAM.
    // Raise that threshold for this one allocation and put it straight back.
    //
    // NOT the C5, though its frame goes to PSRAM the same way and draws at
    // 8-20 fps for it (bg 15-77 ms, 2026-10-02). Kept internal there, the
    // 75 KB left 38 KB of heap after WiFi and Bluetooth's start-up fell over
    // a null pointer and rebooted the board every five seconds. The frame
    // stays external on the C5 until something else makes room.
    heap_caps_malloc_extmem_enable(1u << 20);
#endif
    const bool frameOk = frame.createSprite(tft.width(), tft.height());
#if SQW_FRAME_FORCE_INTERNAL
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
#if SQW_FRAME_FORCE_INTERNAL || defined(NM_CYD_C5)
    // Say where it landed: a frame in the wrong memory is the CrowPanel's
    // twitch and the C5's eight frames a second.
    if (frameOk) {
        const void* a = frame.buf();
#if defined(NM_CYD_C5) || defined(TAB5)
        const bool ext = esp_ptr_external_ram(a);
#else
        // The S3's PSRAM window; internal SRAM sits at 0x3FC8xxxx.
        const bool ext = ((uintptr_t)a >= 0x3C000000u && (uintptr_t)a < 0x3E000000u);
#endif
        Serial.printf("[boot] frame %ux%u at %p (%s)\n", (unsigned)frame.bufW(), (unsigned)frame.bufH(), a,
                      ext ? "PSRAM" : "internal");
    }
#endif
    if (!frameOk) {
        Serial.println("ERROR: frame buffer allocation failed (low memory)");
#if HAVE_NVS_ERASE
        if (bootCheckRan) {
            // The check's leftovers took the block. Once more, without it.
            g_bootCheckSkip = CHKSKIP_MAGIC;
            serialFlush();
            delay(20);
            esp_restart();
        }
#else
        (void)bootCheckRan;
#endif
    }
    frame.setTextSize(1);
#endif

    // Builds a 512-byte lookup table and nothing else; it touches no bus
    // and can go anywhere after the display is up.
    FramePush::begin();

#if defined(CYD35R)
    // The standalone XPT2046_Touchscreen library (own SPIClass, own
    // IRQ pin) produced constant garbage reads and a free-running IRQ
    // here -- not a wrong-pin problem, a second SPI master fighting
    // TFT_eSPI for the same physical bus. Same wiring shape as AWOK
    // (touch shares the display's own SPI bus, no dedicated
    // peripheral), so this uses the same fix: drive touch entirely
    // through TFT_eSPI's own calibrateTouch()/setTouch()/getTouch()
    // path instead, which shares the bus properly. No I2C cap-touch
    // chip on this board either, so no probe -- no touch.begin(), no
    // touchSPI. All subsequent touch reads go through pollTouch()'s
    // CYD35 branch (tft.getTouch()).
    usingCapTouch = false;
    Serial.println("cyd35 build -- XPT2046 on shared VSPI bus via TFT_eSPI.");
#elif defined(TOUCH_RAW_SHARED_BUS)
    // RL Phantom, resistive variant. The chip sits on the display's own bus
    // (TOUCH_CS=33, armed by TFT_eSPI once rlphantom_r_user_setup.h is in
    // scope), so there is no probe, no touch.begin() and no touchSPI -- but
    // unlike AWOK the raw values come back to us and pollTouch() does its own
    // rotation maths on them, so touch follows the screen round.
    usingCapTouch = false;
    Serial.println("RL Phantom (resistive) -- XPT2046 on shared bus, raw reads + rotation maths.");
#elif defined(CROWPANEL7)
    // The GT911, on the same I2C bus as the backlight helper -- which is why
    // there is no Wire.end() anywhere on this path. It will not answer until
    // the helper has been told to wake it and its INT line pulsed low, so
    // Gt911::begin() does that dance before it gives up.
    usingCapTouch = Gt911::begin();
    Serial.println(usingCapTouch ? "CrowPanel 7 -- GT911 capacitive touch answered."
                                 : "CrowPanel 7 -- GT911 did not answer; no touch.");
    // Here and not with StatusLight::begin(): this is the first point the
    // I2C bus is up and the helper has been spoken to. Sends OFF only, for a
    // buzzer a crash may have left sounding -- the helper keeps its state
    // across our reset. Not a boot beep.
    Buzzer::begin();
#elif defined(TAB5)
    // The ST7123's own touch, through M5GFX, which brought it up with the
    // panel in tft.init().
    usingCapTouch = Tab5Panel::up() && Tab5Panel::hasTouch();
    Serial.println(usingCapTouch ? "Tab5 -- ST7123 touch through M5GFX."
                                 : "Tab5 -- no touch controller; no touch.");
    // Spelled out, because a brownout here looked like a lock-up on the
    // console until the reset reason was read (upstream, 2026-10-19).
    Serial.printf("[power] boot: %s (esp_reset_reason %d)\n", resetReasonName(), (int)esp_reset_reason());
#elif defined(FREENOVE_S3_35)
    // The Freenove S3 3.5"'s touch is in the ST77922 itself, on I2C SDA 38 /
    // SCL 39 at 0x55, reset on GPIO48, interrupt on GPIO47 (unused: polled).
    St77922Touch::begin(38, 39, 48, 47);
    // That pulsed the chip's reset, which may be the display's too on a
    // TDDI part: set the panel up again as a precaution, put its inversion
    // back, and forget what it was showing.
    St77922::reinit();
    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
    St77922Blit::invalidate();
    usingCapTouch = St77922Touch::probe();
    Serial.println(usingCapTouch ? "Freenove S3 3.5 -- ST77922 touch answered."
                                 : "Freenove S3 3.5 -- ST77922 touch did not answer; no touch.");
    { char b[16]; boardBatteryLine(b, sizeof b); Serial.printf("[batt] %s (raw GPIO%d %u mV)\n", b, BOARD_BAT_ADC, (unsigned)analogReadMilliVolts(BOARD_BAT_ADC)); }
#elif defined(SQW_SMALL)
    usingCapTouch = false;
#if defined(CARDPUTER_ADV)
    Serial.printf("Cardputer ADV -- no touch panel; keyboard %s.\n", cardKeysBegin() ? "answered" : "DID NOT ANSWER");
#else
    Serial.println("StickS3 -- no touch panel; buttons on GPIO11 and GPIO12.");
#endif
    Shake::begin();   // the BMI270 shares the bus the board just brought up
#elif defined(TDECK_PLUS)
    // The T-Deck's GT911, on I2C SDA 18 / SCL 8 (the keyboard's bus too), INT
    // on 16, no reset line. Raw panel pixels in the panel's own frame, so the
    // five-target calibration maps them like the capacitive CYDs'.
    usingCapTouch = Gt911::begin();
    Serial.println(usingCapTouch ? "T-Deck Plus -- GT911 capacitive touch answered."
                                 : "T-Deck Plus -- GT911 did not answer; no touch.");
#if defined(SQW_TRACKBALL)
    tdeckBallBegin();                     // the trackball: a cursor beside the touch panel
#endif
    { char b[16]; boardBatteryLine(b, sizeof b); Serial.printf("[batt] %s (raw GPIO%d %u mV)\n", b, BOARD_BAT_ADC, (unsigned)analogReadMilliVolts(BOARD_BAT_ADC)); }
#elif defined(CYD32C) || defined(CYD35C)
    // The GT911, on the CYD's capacitive I2C pins. Raw panel pixels in the
    // panel's own portrait frame, so the five-target calibration maps them
    // onto the screen like any other capacitive CYD.
    usingCapTouch = Gt911::begin();
    Serial.println(usingCapTouch ? "GT911 capacitive touch answered."
                                 : "GT911 did not answer; no touch.");
#elif defined(TWATCH_S3)
    // The T-Watch's FT6336, on I2C SDA 39 / SCL 40 at 0x38. No reset line;
    // the AXP2101 powers it (ALDO3) in twatchPowerUp(), before this runs.
    CapTouch::begin(39, 40, -1, 0x38);
    usingCapTouch = CapTouch::probe();
    Serial.println(usingCapTouch ? "T-Watch S3 -- FT6336 capacitive touch answered."
                                 : "T-Watch S3 -- FT6336 did not answer; no touch.");
#elif defined(FREENOVE_S3)
    // The Freenove S3 2.8"'s FT6336, on I2C SDA 16 / SCL 15 at 0x38, reset on
    // GPIO18 (active low). The ES8311 codec shares the bus at 0x18. FNK0104A
    // is the same board with no touch panel: nothing answers, and the screen
    // runs without touch rather than on a guessed fallback.
    CapTouch::begin(16, 15, 18, 0x38);
    usingCapTouch = CapTouch::probe();
    Serial.println(usingCapTouch ? "Freenove S3 -- FT6336 capacitive touch answered."
                                 : "Freenove S3 -- FT6336 did not answer; no touch (FNK0104A?).");
    { char b[16]; boardBatteryLine(b, sizeof b); Serial.printf("[batt] %s (raw GPIO9 %u mV)\n", b, (unsigned)analogReadMilliVolts(9)); }
#elif defined(WAVESHARE_C5)
    // The CST3530, on the helper chip's I2C bus (already up from
    // WsC5::begin()), reset through the helper's EXIO0. Units are the panel's
    // own portrait frame, mapped by the five-target calibration like the
    // other capacitive boards. Capacitive whatever the probe says: the chip
    // sleeps until touched, so the probe usually fails on a working board.
    Cst3530::begin();
    usingCapTouch = true;
    Serial.println("Waveshare C5 -- CST3530 capacitive touch.");
#elif defined(TOUCH_ON_DISPLAY_BUS)
    // AWOK's XPT2046 sits on the display's own shared VSPI bus (TOUCH_CS=21,
    // already armed by TFT_eSPI itself once awok_user_setup.h's #define
    // is in scope) and is driven entirely through TFT_eSPI's own touch
    // path -- no I2C cap-touch probe (this board has no cap-touch chip
    // at all), no touch.begin(), no touchSPI. All subsequent touch
    // reads go through pollTouch()'s AWOK branch (tft.getTouch()).
    usingCapTouch = false;
    Serial.println("AWOK build -- XPT2046 on shared VSPI bus via TFT_eSPI.");
#else
    // Touch: probe for the capacitive controller first (I2C 0x15 on
    // SDA=33/SCL=32, reset on GPIO25 — the JC2432W328C). If it doesn't
    // answer, release the I2C bus and fall back to the resistive
    // XPT2046 on its own dedicated SPI bus (the original board) — a
    // genuinely separate HSPI peripheral on pins that don't overlap
    // the display's VSPI pins at all, so there's no GPIO-matrix
    // conflict either way.
    CapTouch::begin(CAP_SDA, CAP_SCL, CAP_RST);
    usingCapTouch = CapTouch::probe();
    if (usingCapTouch) {
        Serial.println("Capacitive touch (CST816/820) detected -- JC2432W328C-style board.");
    } else {
        Wire.end();
        Serial.println("No capacitive touch found -- assuming resistive XPT2046.");
        touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
        touch.begin(touchSPI);
        touch.setRotation(0);
    }
#endif

#if defined(MARAUDER_V8)
    gaugeBegin();
    { char b[20]; boardBatteryLine(b, sizeof b); Serial.printf("[batt] %s\n", b); }
#endif
    // Recovery escape hatch: hold touch ANYWHERE for ~1s right here to
    // wipe a saved calibration back to defaults. A bad calibration can
    // make touch too inaccurate to reliably re-tap a "recalibrate"
    // button, so this needs no precision at all — just a hold anywhere
    // during the window right after boot.
    PANEL.fillRect(0, 0, tft.width(), tft.height(), Theme::BG);
    PANEL.setTextColor(Theme::AMBER, Theme::BG);
    PANEL.setTextSize(1);
    PANEL.setCursor(4, 4);
    PANEL.print("Hold anywhere now to reset touch calibration...");
    panelShow();
    {
        uint32_t holdStart = 0;
        uint32_t windowStart = millis();
        while (millis() - windowStart < 1200) {
            int16_t a, b;
#if defined(CROWPANEL7) || defined(CYD32C) || defined(CYD35C) || defined(FREENOVE_S3_35) || defined(TDECK_PLUS) || defined(SQW_SMALL) || defined(TAB5) || defined(WAVESHARE_C5)
            // The GT911 (and the ST77922's touch) is neither of the two
            // below; the two-way dispatch polled a capacitive controller that
            // is not on this bus.
            bool down = readTouchRaw(a, b);
#else
            bool down = usingCapTouch ? rawReadCap(a, b) : rawReadResistive(a, b);
#endif
            if (down) {
                if (holdStart == 0) holdStart = millis();
                else if (millis() - holdStart > 800) {
                    TouchCal::reset();
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35R)
                    // TouchCal::reset() clears the Fit and the 2.8"-style
                    // calibration; these boards' old TFT_eSPI blobs live in
                    // a namespace of their own, and SKIP would bring them
                    // straight back.
                    clearTftEspiBlobs();
#endif
                    PANEL.fillRect(0, 0, tft.width(), tft.height(), Theme::BG);
                    PANEL.setTextColor(Theme::AMBER, Theme::BG);
                    PANEL.setTextSize(2);
                    const char* msg = "CALIBRATION RESET";
                    PANEL.setCursor((tft.width() - PANEL.textWidth(msg)) / 2, tft.height() / 2 - 8);
                    PANEL.print(msg);
                    panelShow();
                    delay(1200);
                    break;
                }
            } else {
                holdStart = 0;
            }
            delay(10);
        }
    }
    PANEL.fillRect(0, 0, tft.width(), tft.height(), Theme::BG);
    panelShow();

    // Touch calibration. Every board gets the five-target calibration once:
    // at first boot, and once more after updating from a firmware that used
    // the old corner calibrations, which were wrong in portrait on the 2.8"
    // and sat under the lip of a case on AWOK. A board with a mapping worth
    // keeping is offered SKIP, which keeps it and stops asking.
    //
    // Nobody touching the screen (a board on a desk, powered from USB) is
    // not an answer: the flow times out, boot carries on with the old
    // mapping, and it asks again next boot.
    //
    // Hardware only. The emulators build this file too, and their taps are
    // injected against the compiled-in ranges -- a calibration screen would
    // just sit there waiting for a finger.
    initTouchFit();
#if defined(ESP32) && !defined(CROWPANEL7) && !defined(FREENOVE_S3_35) && !defined(SQW_SMALL) && !defined(TAB5)   // nothing to calibrate with no touch
    if (s_calSource != CalSource::SAVED) {
        Serial.println("Touch: no five-target calibration yet -- running it now.");
        runTouchCalibration();
    }
#endif
#if defined(CROWPANEL7) || defined(FREENOVE_S3_35) || defined(TAB5)
    // Never on first boot here. The GT911 (and the ST77922's own touch, and
    // the Tab5's ST7123 through M5GFX) reports panel pixels, so the
    // compiled-in fit is already the identity and there is nothing to
    // calibrate -- making someone tap five targets would only replace an
    // exact mapping with a hand-aimed one. SETTINGS can still run it.
    Serial.println("Touch: GT911 reports panel pixels; no calibration needed.");
#endif

    // Seed the PRNG so the digital rain starts in a fresh-looking state
    // on every boot.
#if defined(SQW_S3) || defined(CROWPANEL7) || defined(NM_CYD_C5) || defined(TAB5)
    // GPIO34 is not an ADC pin on an S3 (it is an octal PSRAM line on these
    // boards, the CrowPanel's N16R8 module included): analogRead() refuses
    // it and the seed was a constant. The hardware RNG instead -- before WiFi
    // starts it is running on the bootloader's entropy rather than radio
    // noise, still no constant.
    //
    // The NM-CYD-C5 is the same fault with less ambiguity: GPIO34 does not
    // exist on a C5 at all, and the board says so twice on every boot --
    //   E adc_common: adc_io_to_channel(29): invalid gpio number
    //   __analogRead(): Pin 34 is not ADC pin!
    // -- after which the seed is 0 and the digital rain opens on the same
    // frame every time. Confirmed on hardware, not inferred.
    randomSeed(esp_random());
#else
    // Analog read on a floating pin is plenty.
    randomSeed(analogRead(34));
#endif

    // The backlight goes down while the radios come up, and back to your
    // setting once they are running.
    //
    // WiFi calibrates its RF front end when it starts and Bluetooth does the
    // same a moment later, and together they are the largest current the
    // board ever draws. On top of a full-brightness backlight that was more
    // than a weak USB port could hold: the JC2432W328C capacitive board
    // browned out at exactly this point on every boot -- three seconds a
    // cycle, forever -- off any supply short of a powered hub. The backlight
    // is the one large load that nobody misses for a second at boot.
#if defined(CROWPANEL7)
    CrowBL::set(24);
#elif defined(TAB5)
    Tab5Panel::backlight(24);
#elif defined(TDECK_PLUS)
    tdeckBacklight(24);
#elif defined(WAVESHARE_C5)
    WsC5::setBacklight(24);
#else
    ledcWrite(BL_CH_ORIG, 24);
    ledcWrite(BL_CH_CAP,  24);
    ledcWrite(BL_CH_AWOK, 24);
#endif

    // The black box, before the radios: this boot's record -- with the crash
    // in it when there was one -- then the log as the last boot left it, so
    // nothing live has landed in the log yet. After the boot check, so the
    // record has the time when there is a network to ask.
    if (BlackBox::begin()) {
        BlackBox::BootRecord br;
        memset(&br, 0, sizeof br);
        br.reason = (uint8_t)g_resetReason;
        br.epoch  = Clock::trusted() ? Clock::nowEpoch() : 0;
        if (g_lastCrash.valid) {
            br.flags    |= BlackBox::BOOT_CRUMB;
            br.upSec     = g_lastCrash.uptimeMs / 1000u;
            br.heapFree  = g_lastCrash.heapFree;
            br.heapBlock = g_lastCrash.heapBlock;
            br.screen    = g_lastCrash.screen;
        }
        if (g_lastCrash.haveDump) {
            br.flags |= BlackBox::BOOT_DUMP;
            if (g_lastCrash.dumpOlder) br.flags |= BlackBox::BOOT_DUMP_OLDER;
            br.pc    = g_lastCrash.pc;
            br.cause = g_lastCrash.cause;
            strncpy(br.task, g_lastCrash.task, sizeof br.task - 1);
        }
        BlackBox::noteBoot(br);
    }

#if defined(TWATCH_S3)
    // The wardrive log, and its GPS if wardriving was left on.
    wardriveBegin();
#endif

#if defined(TWATCH_S3)
    // What the watch is like at the moment the radios start. A boot that
    // ran the touch calibration first hears the room; a plain boot comes up
    // deaf. Logged so the two kinds of boot can be compared line by line.
    {
        nvs_stats_t st = {};
        nvs_get_stats(NULL, &st);
        snprintf(g_bootRadioLine, sizeof g_bootRadioLine,
                 "radios started at %lu ms; chip %.1f C; heap %lu; nvs used %u free %u; calibration %s; self-heal restart %u",
                 (unsigned long)millis(), temperatureRead(), (unsigned long)ESP.getFreeHeap(),
                 (unsigned)st.used_entries, (unsigned)st.free_entries,
                 s_calRanThisBoot ? "RAN this boot" : "skipped", (unsigned)s_healCount);
        Serial.printf("[boot] %s\n", g_bootRadioLine);
    }
#endif
    engine.init();
#if defined(TAB5)
    tab5RadioUpdatePrompt();   // only when the C6 linked but has no sniff extension
#endif
#if MULESKIN_LORA
    // The LoRa radio, after WiFi and Bluetooth: its own task and its own bus.
    // Receive only. Console LORA for what it hears.
    Lora::begin();
#endif
#if defined(CROWPANEL7_PERIPH_PROBE)
    crowPeriphProbe();
#endif
    // After the engine: the card leans on the lifetime counts to pick which
    // type sits out, and those are read in init().
    Dex::begin();
    Regulars::begin();
    MyAlerts::begin();
    // AWAYDAYS on the console, and "2d away" on the LOG (the away-days table).
    awayDaysLoad();
    Clock::setConsoleHook(awayDaysConsole);
    uiLogSetAwayDays(awayDaysFor);
    MuleSkin::setIdleProvider([]() { return Notices::idleLine(engine); });
    applyBrightness();
    // After a wipe the board comes back the way the wipe asked: straight to the
    // main screen, unlocked, with no splash and no boot quip, after a duress
    // PIN -- so it reads as an unlock and not a restart -- or locked after the
    // tenth wrong guess.
    const WipeBoot wb = takeWipeBoot();
    if (wb == WipeBoot::UNLOCKED) {
        Security::forceUnlock();
        enterClear();
    } else {
        if (wb == WipeBoot::LOCKED) Security::lock();
        MuleSkin::trigger(MuleSkin::Event::BOOTED, DetectionType::UNKNOWN, engine.lifetimeTotal());
        enterBoot();
    }
}

// ---- PRIM: what each drawing primitive costs on the real sprite ----
// Every framerate conversation before this was a guess about whether a
// fillCircle is expensive or a gradient is, made without a single number.
// PRIM on the console draws each primitive N times into the frame buffer
// and prints the microseconds per call. The frame it scribbles on is
// repainted on the very next pass, so nothing is visible.
volatile bool g_benchPrimNow = false;
#if defined(ARDUINO_ARCH_ESP32)   // the board only: the emulator's sprite has none of this
template <typename F>
static void primTime(const char* name, uint32_t n, F body) {
    const uint32_t t0 = micros();
    for (uint32_t i = 0; i < n; i++) body(i);
    const uint32_t us = micros() - t0;
    Serial.printf("[prim] %-28s %6lu.%02lu us/call  (%lu x)\n", name,
                  (unsigned long)(us / n), (unsigned long)((us * 100UL / n) % 100), (unsigned long)n);
}
static void runPrimBench() {
    if (!frameBufferOk) { Serial.println("[prim] no frame buffer"); return; }
    ResizableSprite& f = frame;
    const int w = f.width(), h = f.height();
    volatile uint32_t sink = 0;
    Serial.printf("[prim] sprite %dx%d, 8-bit\n", w, h);
    primTime("drawFastHLine 320",   2000, [&](uint32_t i){ f.drawFastHLine(0, (int)(i & 127), w, (uint16_t)i); });
    primTime("drawFastVLine 200",   2000, [&](uint32_t i){ f.drawFastVLine((int)(i & 255), 0, 200, (uint16_t)i); });
    primTime("fillRect 20x20",      2000, [&](uint32_t i){ f.fillRect((int)(i & 255), (int)((i >> 2) & 127), 20, 20, (uint16_t)i); });
    primTime("fillRect full screen",  20, [&](uint32_t i){ f.fillRect(0, 0, w, h, (uint16_t)i); });
    primTime("drawPixel",          20000, [&](uint32_t i){ f.drawPixel((int)(i & 255), (int)((i >> 4) & 127), (uint16_t)i); });
    primTime("drawLine 100x60",     1000, [&](uint32_t i){ f.drawLine((int)(i & 127), 0, (int)(i & 127) + 100, 60, (uint16_t)i); });
    primTime("fillCircle r10",      1000, [&](uint32_t i){ f.fillCircle(20 + (int)(i & 255), 20 + (int)((i >> 2) & 127), 10, (uint16_t)i); });
    primTime("fillCircle r30",       300, [&](uint32_t i){ f.fillCircle(40 + (int)(i & 127), 40 + (int)((i >> 1) & 127), 30, (uint16_t)i); });
    primTime("fillRoundRect 30x20 r5",1000,[&](uint32_t i){ f.fillRoundRect((int)(i & 255), (int)((i >> 2) & 127), 30, 20, 5, (uint16_t)i); });
    primTime("fillEllipse 12x8",    1000, [&](uint32_t i){ f.fillEllipse(20 + (int)(i & 255), 20 + (int)((i >> 2) & 127), 12, 8, (uint16_t)i); });
    primTime("fillTriangle 30x20",  1000, [&](uint32_t i){ int x = (int)(i & 255), y = (int)((i >> 2) & 127); f.fillTriangle(x, y + 20, x + 15, y, x + 30, y + 20, (uint16_t)i); });
    f.setTextSize(1); f.setTextColor(Theme::CYAN, Theme::BG);
    primTime("print 11 chars size 1", 300, [&](uint32_t i){ f.setCursor((int)(i & 127), (int)((i >> 1) & 127)); f.print("HELLO WORLD"); });
    f.setTextSize(2);
    primTime("print 11 chars size 2", 300, [&](uint32_t i){ f.setCursor((int)(i & 63), (int)((i >> 1) & 127)); f.print("HELLO WORLD"); });
    f.setTextSize(1);
    f.setTextFont(2);
    primTime("print 11 chars font 2",  300, [&](uint32_t i){ f.setCursor((int)(i & 127), (int)((i >> 1) & 127)); f.print("HELLO WORLD"); });
    f.setTextFont(1);
    primTime("Bangers NEARBY LG",      50, [&](uint32_t i){ Theme::drawBangersText(f, 40, 60, "NEARBY", Theme::AMBER, Theme::BangersSize::LG); });
    primTime("Bangers NEARBY MD",      50, [&](uint32_t i){ Theme::drawBangersText(f, 40, 60, "NEARBY", Theme::AMBER, Theme::BangersSize::MD); });
    primTime("Theme::blend",        20000, [&](uint32_t i){ sink += Theme::blend((uint16_t)i, (uint16_t)(i * 3), (uint16_t)(i & 255)); });
    primTime("color565",            20000, [&](uint32_t i){ sink += f.color565((uint8_t)i, (uint8_t)(i >> 2), (uint8_t)(i >> 4)); });
    primTime("sinf",                20000, [&](uint32_t i){ sink += (uint32_t)(sinf((float)i * 0.01f) * 100.0f); });
    primTime("float mul+add",       20000, [&](uint32_t i){ sink += (uint32_t)((float)i * 1.7f + 3.2f); });
    primTime("int mul+add",         20000, [&](uint32_t i){ sink += i * 17u + 3u; });
    // The library's own versions of the four the sprite subclass takes over,
    // so the gain is on the record next to the cost. And then the proof.
    primTime("drawPixel (library)",  20000, [&](uint32_t i){ f.basePixel((int)(i & 255), (int)((i >> 4) & 127), (uint16_t)i); });
    primTime("drawFastVLine 200 (library)", 2000, [&](uint32_t i){ f.baseVLine((int)(i & 255), 0, 200, (uint16_t)i); });
    primTime("drawLine 100x60 (library)",   1000, [&](uint32_t i){ f.baseLine((int)(i & 127), 0, (int)(i & 127) + 100, 60, (uint16_t)i); });
    primTime("11 chars size 1 (library)",    300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.baseChar((int)(i & 127) + k * 6, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 1); });
    primTime("11 chars size 1 (fast)",       300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.drawChar((int)(i & 127) + k * 6, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 1); });
    primTime("11 chars size 2 (library)",    300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.baseChar((int)(i & 63) + k * 12, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 2); });
    primTime("11 chars size 2 (fast)",       300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.drawChar((int)(i & 63) + k * 12, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 2); });
    const int bad = f.selfCheck(Serial);
    Serial.printf("[check] %s\n", bad == 0 ? "every fast path is pixel-identical to the library" : "FAST PATHS DIFFER -- do not ship");
    primTime("index arithmetic only",20000, [&](uint32_t i){ sink += (uint32_t)((int)(i & 255) + (int)((i >> 4) & 127)); });
    Serial.printf("[prim] done (%lu)\n", (unsigned long)sink);
}
#endif

// ---- Frame timing ----
// Wall-clock cost of the two things that actually set the frame rate:
// pushing the sprite over SPI, and everything else put together. Both
// are exponentially smoothed (1/8 per frame) because the raw per-frame
// numbers jitter by several ms and are unreadable on a live screen.
//
// The push is the interesting one: it's a fixed 153,600 bytes at
// 16bpp, so it costs whatever SPI_FREQUENCY says it costs and nothing
// else in the firmware can move it. At 40MHz that's ~30.7ms, which is
// most of a frame -- reading the real number here is the only way to
// tell whether a change to that clock did what the arithmetic claims.
static uint32_t s_pushUsAvg  = 0;
static uint32_t s_frameUsAvg = 0;
static uint32_t s_loopsSinceSay = 0;
static uint32_t s_pushAccumUs = 0;   // summed within a frame: cyd35 pushes twice
#if defined(CYD35)
static uint32_t s_bandUs[2] = {0, 0};      // measurement: the 3.5"'s two draw passes
#endif

#if SQW_DEVTOOLS
// SHOT: the frame as it is about to go out, cursor and all, as hex rows of
// the 8-bit (RGB332) sprite. tools/shot.py turns it into a PNG. Dev builds
// only. (Upstream SquachWatch v1.42.)
static void devShotDump() {
    const int w = frame.bufW(), h = frame.bufH();
    const uint8_t* p = (const uint8_t*)frame.buf();
    if (!p || frame.getColorDepth() != 8) { Serial.println("[shot] no 8-bit frame"); return; }
    static const char HEX_[] = "0123456789abcdef";
    // Each row is "R<y>:<hex>" so a log line from another task landing in the
    // middle costs nothing. The USB CDC drops what does not fit, so every
    // write is retried until it has all gone.
    char* row = (char*)malloc(2 * w + 12);
    if (!row) { Serial.println("[shot] no memory"); return; }
    Serial.printf("[shot] BEGIN %d %d %u", w, h, (unsigned)state);
    Serial.println();
    for (int y = 0; y < h; y++) {
        int n = snprintf(row, 12, "R%d:", y);
        for (int x = 0; x < w; x++) {
            const uint8_t v = p[y * w + x];
            row[n++] = HEX_[v >> 4]; row[n++] = HEX_[v & 15];
        }
        row[n++] = 10;   // newline
        const uint8_t* q = (const uint8_t*)row;
        const uint32_t give = millis() + 500;
        while (n > 0 && (int32_t)(millis() - give) < 0) {
            const size_t sent = Serial.write(q, (size_t)n);
            q += sent; n -= (int)sent;
            if (!sent) delay(1);
        }
    }
    Serial.println("[shot] END");
    free(row);
}
#endif
static inline void pushFrame(int x, int y) {
#if SQW_DEVTOOLS
    if (g_devShot) { g_devShot = false; devShotDump(); }
#endif
#if defined(TWATCH_S3)
    if (s_panelAsleep) return;   // nothing to show it to; see applyBrightness()
#endif
    uint32_t t0 = micros();
    // The overlapped push converts the next 64 bytes while the previous 64
    // are on the wire, instead of spinning -- see frame_push.h. It declines
    // rather than half-draws, and the ordinary push is what it declines to;
    // both leave the frame fully on the panel before the clock below stops,
    // so DIAGNOSTICS and the [frame] line measure the same thing either way.
    // The buffer and its REAL size: with a viewport set the sprite reports the
    // viewport's size, not the buffer's, and cyd35 pushes through one.
#if defined(CROWPANEL7)
    // No wire to overlap and no pushSprite fallback: the framebuffer is
    // memory, and CrowBlit writes the changed rows straight into it, doing
    // the 8->16 bit conversion on the way. It declines only if the panel
    // never came up, in which case there is nowhere to put the frame at all.
    CrowBlit::push(frame.buf(), frame.bufW(), frame.bufH(), x, y);
#elif defined(TAB5)
    // Same shape: the framebuffer is memory and Tab5Blit writes the changed
    // columns into it, turned landscape and doubled on the way.
    Tab5Blit::push(frame.buf(), frame.bufW(), frame.bufH(), x, y);
#elif defined(FREENOVE_S3_35)
    // The ST77922 over QSPI: TFT_eSPI cannot reach it, so no pushSprite
    // fallback either -- St77922Blit or nothing (the panel never came up).
    (void)x; (void)y;
    St77922Blit::push(frame.buf(), frame.bufW(), frame.bufH(), tft.getRotation());
#else
    if (frame.getColorDepth() != 8 ||
        !FramePush::push(tft, frame.buf(), frame.bufW(), frame.bufH(), x, y)) {
        frame.pushSprite(x, y);
        FramePush::invalidate();   // the panel now holds something push() did not record
    }
#endif
    s_pushAccumUs += micros() - t0;
}

// Draw a whole screen the way the 3.5" has to: into the half-height sprite
// twice, once for each band, and push each band as it is finished.
//
// Every screen except the main one, boot and the alerts has always drawn
// STRAIGHT AT THE PANEL on that board -- canvas points at tft there, because
// the sprite is only half a screen -- so you watch each rectangle and each
// line land, one at a time. That is the flicker. It also means a menu, which
// does not change from one frame to the next, is re-sent to the panel in full
// forever: on every other board it goes through the sprite, the row hashes
// all match and it sends nothing at all.
//
// `draw` is called once per band and handed the sprite and an `advance` flag,
// false on the second pass, for anything that steps on its own clock. A
// screen has to be safe to draw twice with the same `now` to come through
// here: no random(), no state that moves by the call rather than by the
// clock. The UI screens use no random() at all, and the two things that do
// move by the call -- the animated background and the mascot -- both already
// take the flag, because the main screen needed exactly this.
//
// Everywhere else this is one call on the full-screen sprite, which is what
// those boards already did, and loop()'s own push at the end still ships it.
template <typename F>
static inline void drawTwoBand(F&& draw) {
#if defined(CYD35)
    if (frameBufferOk) {
        const int halfH = tft.height() / 2;
        // The sprite is cleared before each band. It is ONE half-height
        // buffer used for both, so whatever a screen does not repaint is the
        // OTHER band's picture, and it showed: a screen that fills its
        // background once in its Init and then draws rows over it (the
        // diary) had the top half's leftovers under the bottom half, which
        // read as the screen drawn twice (issue #25). Every Init fills with
        // Theme::BG, so that is what an unpainted pixel should be.
        DrawBand::set(0, halfH);
        frame.setViewport(0, 0, tft.width(), tft.height(), true);
        frame.fillRect(0, 0, tft.width(), tft.height(), Theme::BG);
        draw((TFT_eSPI&)frame, true);
        pushFrame(0, 0);
        DrawBand::set(halfH, tft.height());
        frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
        frame.fillRect(0, 0, tft.width(), tft.height(), Theme::BG);
        draw((TFT_eSPI&)frame, false);
        pushFrame(0, halfH);
        DrawBand::all();
        frame.resetViewport();
        return;
    }
#endif
    draw(*canvas, true);
}

static inline uint32_t emaUpdate(uint32_t avg, uint32_t sample) {
    return avg ? avg + ((int32_t)sample - (int32_t)avg) / 8 : sample;
}

// The frame time of the last screen worth timing, for DIAGNOSTICS: the whole
// frame (drawing, push and everything else in the loop), not the backdrop
// alone, and not the menu the reading is taken from. Its own average, begun
// afresh on each such screen once its entry glitch is over, so none of the
// screen before it is mixed in.
static const char* timedScreenName(AppState s) {
    switch (s) {
        case AppState::CLEAR:       return "MAIN";
        case AppState::LOG:         return "LOG";
        case AppState::HUNT:        return "HUNT";
        case AppState::ALERT:       return "ALERT";
        case AppState::WATCH_ALERT: return "WATCH";
        default:                    return nullptr;
    }
}
static const char* s_lastScreenName = nullptr;
static uint32_t    s_lastScreenUs   = 0;
static uint32_t    s_lastScreenAt   = 0;     // transitionStart of the screen being timed

#include "main_gps.inc"   // GPS and wardriving: the T-Watch S3 Plus and the cyd-gps builds

// The runtime log: this boot's minutes, noted every ten minutes. On a board
// that cannot read its battery, the last note before it died is how long
// the charge lasted. RUNTIME on the console lists the last eight boots.
extern volatile bool g_consoleRuntime;   // clock.cpp: RUNTIME
static void runtimeTick(uint32_t now) {
    static uint32_t lastAt = 0;
    static bool first = true;
    if (first || now - lastAt >= 600000UL) {
        first = false;
        lastAt = now;
        Settings::noteRunMinutes(BlackBox::bootNumber(), (uint16_t)(now / 60000UL));
    }
    if (g_consoleRuntime) {
        g_consoleRuntime = false;
        uint16_t b[8], m[8];
        const uint8_t n = Settings::runHistory(b, m, 8);
        Serial.printf("[runtime] this boot %u, up %lu min; newest first:\n", (unsigned)BlackBox::bootNumber(), (unsigned long)(now / 60000UL));
        for (uint8_t i = 0; i < n; i++)
            Serial.printf("[runtime] boot %u ran %uh %02um%s\n", (unsigned)b[i], (unsigned)(m[i] / 60), (unsigned)(m[i] % 60),
                          i == 0 ? " so far (this one)" : "");
    }
}

#include "main_charge.inc"   // CHARGE MODE and the BOOT button

// ---- TERMINAL MODE at rest ---------------------------------------------------
// TERMINAL MODE is text and numbers: once nobody has touched it for a few
// seconds it redraws four times a second, and twice after half a minute, with
// the CPU at 80 MHz. The radios lose nothing: the loop's radio work runs every
// 20 ms through the wait (terminalRestWait), as it does at full speed. A
// touch, a key, the ball or a new catch ends the rest at once. The POWER
// SAVER's own idle pace still applies on top: whichever is slower wins.
// (Upstream SquachWatch v1.49.)
static const uint32_t TERM_REST_AFTER_MS   = 3000;
static const uint32_t TERM_SLOWER_AFTER_MS = 30000;
static uint32_t       s_termWokeAt = 0;   // an input ended the last rest

static void terminalRestSet(bool on) {
    if (on == s_termRest) return;
    s_termRest = on;
    applyCpuClock();
    Serial.printf("[term] %s (CPU %u MHz)\n", on ? "resting" : "awake", (unsigned)s_cpuMhzApplied);
}

// The frame budget at rest, in microseconds; 0 while TERMINAL MODE is not
// resting (or not up).
static uint32_t terminalRestBudgetUs(uint32_t now, uint32_t idleMs) {
    const bool rest = state == AppState::CLEAR && Settings::boringMode() &&
                      idleMs > TERM_REST_AFTER_MS &&
                      now - transitionStart > TERM_REST_AFTER_MS &&
                      now - s_termWokeAt > 1000 &&
                      !Theme::toastUp(now);
    terminalRestSet(rest);
    if (!rest) return 0;
    return idleMs > TERM_SLOWER_AFTER_MS ? 500000UL : 250000UL;
}

static void terminalRestWait(uint32_t ms) {
    const uint32_t until = millis() + ms;
    const uint32_t seq = engine.latestSeq();
    for (;;) {
        const int32_t left = (int32_t)(until - millis());
        if (left <= 0) return;
        delay(left > 20 ? 20 : (uint32_t)left);
        engine.loop();   // the WiFi queue holds a few frames; the hop keeps its own clock
        // A catch: draw it now. The clock stays where it is -- in a busy room
        // catches come every second, and the clock is not switched that often.
        if (engine.latestSeq() != seq) return;
        bool input = false;
#if !defined(SQW_SMALL)
        int16_t a, b;
        if (readTouchRaw(a, b)) input = true;
#endif
#if defined(SQW_TRACKBALL)
        if (tdeckBallPending()) input = true;
#endif
#if defined(STICKS3)
        if (digitalRead(STICK_KEY1) == LOW || digitalRead(STICK_KEY2) == LOW) input = true;
#endif
#if defined(CARDPUTER_ADV)
        if (s_cardKeysOk) { const int cnt = cardRead(0x03); if (cnt > 0 && (cnt & 0x0F)) input = true; }
#endif
        if (input) break;
    }
    // A finger, a key or the ball: full speed for it, and no rest again for a
    // second even if the loop never counts it as a touch (a light brush), so
    // the clock is not switched every frame.
    s_termWokeAt = millis();
    terminalRestSet(false);
}

void loop() {
    // Cheap and unconditional: available() is a register read, and this
    // is the only way in for the one serial command the firmware takes.
    Clock::pollSerial();
    runtimeTick(millis());
#if defined(ESP32) && !defined(TWATCH_S3)
#if SQW_BOOT_BTN
    bootButtonTick(millis());
#endif
#if defined(TDECK_PLUS)
    tdeckKeysTick(millis());
#endif
    if (g_consoleCharge) { g_consoleCharge = false; if (s_chargeMode) exitChargeMode(); else s_chargeWanted = true; }
    if (s_chargeWanted && !s_chargeMode) { s_chargeWanted = false; enterChargeMode(); }
    if (s_chargeMode) { chargeModeTick(millis()); return; }
#endif
    uint32_t frameStartUs = micros();
    s_loopsSinceSay++;   // the real loop rate, pacing delays included; on the [frame] line
    FrameProf::begin();
    s_pushAccumUs = 0;
    FramePush::newFrame();
    uint32_t now = millis();
#if defined(TWATCH_S3)
    twatchCrownTick(now);
    twatchMotionTick(now);
    twatchXtalTick(now);
    if (g_consoleXtal) { g_consoleXtal = false; twatchXtalStart(); }
    twatchRadioTick(now);
    twatchRadioHealTick(now);
    twatchBatteryTick(now);
    if (g_consoleBatt) { g_consoleBatt = false; twatchBatterySample(BlackBox::BATT_WHY_TIMER); }
    if (g_consoleBattLog) {
        g_consoleBattLog = false;
        Serial.println("[battlog] newest first: boot  up(s)  epoch  mV  %  flags(usb/chg/scr/radio)  cpu  why  chipC  steady  adverts  frames");
        BlackBox::forEachBattery([](const BlackBox::BattRecord& r, void*) {
            Serial.printf("[battlog] %u  %lu  %lu  %u  %u  %c%c%c%c  %u  %u\n", (unsigned)r.boot,
                          (unsigned long)r.upSec, (unsigned long)r.epoch, (unsigned)r.mv, (unsigned)r.pct,
                          (r.flags & BlackBox::BATT_USB) ? 'U' : '-', (r.flags & BlackBox::BATT_CHARGING) ? 'C' : '-',
                          (r.flags & BlackBox::BATT_SCREEN_ON) ? 'S' : '-', (r.flags & BlackBox::BATT_RADIOS_ON) ? 'R' : '-',
                          (unsigned)r.cpuMhz10 * 10, (unsigned)r.why);
            Serial.printf("[battlog]    %d C  %u  %lu  %lu\n", (int)r.chipC, (unsigned)r.steady,
                          (unsigned long)r.adverts, (unsigned long)r.frames);
            return true;
        }, nullptr);
    }
#endif
    Clock::tick(now);   // the note to self, when it is due
#ifdef BENCH_TOOLS
    if (g_benchAlertPush) {
        g_benchAlertPush = false;
        Detection d{};
        const uint8_t mac[6] = { 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6 };
        memcpy(d.mac, mac, 6);
        d.type = DetectionType::FLOCK;
        d.rssi = -60;
        queuePushAlert(d, 0);
    }
    if (g_benchUpdateNow && state == AppState::CLEAR) {
        // Waits for the main screen rather than barging in from wherever the
        // board happens to be -- the same place a person would start from.
        g_benchUpdateNow = false;
        startAutoUpdate();
    }
    if (g_benchUpdateStop) {
        g_benchUpdateStop = false;
        s_auto.active = false;
        if (OtaWifi::state() != OtaWifi::State::OFF) {
            if (!OtaWifi::end()) { engine.stopUpdateRadio(); enterClear(); }
        }
    }
#endif
#if defined(ARDUINO_ARCH_ESP32)
    if (g_benchPrimNow) { g_benchPrimNow = false; runPrimBench(); }
#endif
    Dex::tick(now);
    Regulars::tick(now);

    TouchPoint tp = pollTouch();
#if defined(CARDPUTER_ADV)
    // The Cardputer's letters that no button on the screen took (see
    // stickPoll): T opens TODAY and S the settings, from the main screen
    // only (elsewhere those letters belong to the screen's own buttons). W,
    // L and M are the main screen's own WIFI TIME, LOG and IN MEETING
    // buttons. Never on the boot or lock screens; never while typing (those
    // letters are typed instead). No B for the background, as upstream has:
    // RADAR is MuleSkin's only one.
    // (Upstream SquachWatch v1.43, laid out for MuleSkin's screens.)
    if (s_cardHot) {
        const char c = s_cardHot;
        s_cardHot = 0;
        if (state != AppState::BOOT && state != AppState::LOCKED) {
            lastTouch = now;
            const bool home = state == AppState::CLEAR && !uiZoneCardWanted();
            if (c == 't' && home)      { Serial.println("[keys] hotkey TODAY");    enterToday(); }
            else if (c == 's' && home) { Serial.println("[keys] hotkey SETTINGS"); enterSettings(); }
        }
    }
#endif
#if defined(SQW_SMALL)
    // The case is a finger. A WIGGLE or a SHAKE wakes a dimmed screen
    // (lastTouch is the saver's clock); a single bump in a pocket is neither,
    // so it does not light it. A shake or a knock on an awake case keeps it
    // awake. Read every loop, dimmed or not, so the wake is
    // never missed. (Upstream SquachWatch v1.42, where a knock also pokes
    // Squachy and a shake tosses him; MuleSkin has no Squachy.)
    {
        const Shake::Event ev = Shake::poll(now);
        if (ev == Shake::Event::WIGGLE) {
            // Only ever a wake: a fob jiggling in a pocket must not keep an
            // awake screen lit.
            if (s_screenDimmed) { Serial.println("[imu] wake (wiggle)"); lastTouch = now; }
        } else if (ev == Shake::Event::SHAKE) {
            if (s_screenDimmed) Serial.println("[imu] wake");
            lastTouch = now;
        } else if (ev == Shake::Event::TAP && !s_screenDimmed) {
            lastTouch = now;
        }
    }
#endif
#if defined(SQW_SMALL) || defined(SQW_TRACKBALL)
    if (s_stickHome) {
        s_stickHome = false;
        // HOME is the main screen. Never out of the lock screen: that would
        // walk past the PIN.
        if (state != AppState::CLEAR && state != AppState::BOOT && state != AppState::LOCKED) enterClear();
    }
#endif
    // True only on the exact frame a touch begins/ends -- unlike
    // TOUCH_DEBOUNCE_MS below (a cooldown timer that still re-fires on a
    // long-held finger once the cooldown elapses), these compare this
    // frame's tp.valid against last frame's, so they each fire exactly
    // once per physical press no matter how long it's held.
    bool touchJustDown = tp.valid && !prevTouchValid;
    bool touchJustUp    = !tp.valid && prevTouchValid;

    // The tap that wakes a dimmed screen only wakes it. Without this, feeling
    // for the device in the dark cycles a background or opens a menu on the
    // way, because every screen's own handlers see that first touch as a real
    // press. Swallowed for the whole gesture, not just the frame it lands on,
    // so a wake tap that turns into a drag cannot scroll a list either.
    //
    // lastTouch is still updated here, because that is what actually undims
    // on the next frame -- the touch is being consumed, not ignored. In
    // POCKET MODE it undims nothing: only the button does.
    static bool s_swallowTouch = false;
    if (s_screenDimmed && touchJustDown) {
        s_swallowTouch = true;
        lastTouch = now;
    }
    if (!tp.valid) s_swallowTouch = false;
    if (s_swallowTouch) {
        tp.valid = false;
        touchJustDown = false;
        touchJustUp = false;
    }
    engine.loop();
#if defined(SENSE_CYD_APP)
    // HUMAN SENSING on the board's own radio, and only while its screen is up:
    // one radio, so the detector lets go of it the way it does for WIFI TIME
    // (startUpdateRadio) and gets it back the moment the screen goes (BACK,
    // HOME, the gear, a lock, an alert). The network is one of the saved
    // ones (SETTINGS > SYSTEM > WIFI NETWORKS). (Upstream v1.42-v1.49.)
    {
        static bool s_senseNoWifi = false;   // SENSE OPEN NOWIFI: this session only
        if (g_senseConsole && !Security::locked()) {   // SENSE OPEN / CLOSE / ... on the console (bench)
            const uint8_t c = g_senseConsole; g_senseConsole = 0;
            if ((c == 1 || c == 3) && state != AppState::SENSING) { s_senseNoWifi = c == 3; enterSensing(); }
            else if (c == 2 && state == AppState::SENSING) enterClear();
#if SENSE_RICH
            else if ((c == 4 || c == 5) && state == AppState::SENSING) uiSensingSwipe(c == 4 ? 1 : -1);
#endif
            else if (c == 6 && state == AppState::SENSING) senseSleep();
            else if (c == 7) senseWake();
        }
        // Booted straight into SENSING: the detector's radio gets 8 s to
        // settle first. Started at once, the first join came up deaf (no
        // CSI, every ping lost) on upstream's bench; opened later it never did.
        // WIFI TIME (or AUTO TIME) mid-join keeps the radio until it is done.
        // SENSE ARM runs it without the screen too: not with no network saved
        // to sense through (the detector would be paused for nothing), and not
        // while a screen that needs the radio itself is up (SenseMotion::
        // armYields), which gets it and hands it back after.
        const bool onScreen = state == AppState::SENSING;
        const bool armed = !onScreen && OtaWifi::savedCount() && !SenseMotion::armYields(state) && senseArmed();
        const bool want = (onScreen || armed) && millis() > 8000;
        uiClearSetSenseArmed(armed && senseCydRunning());
        // The screen was left while SLEEP had it dark, and sensing runs on
        // (armed): the screen comes back all the same.
        if (!onScreen && s_senseDark) senseWake();
        if (!onScreen) s_senseGlanceUntil = 0;
        serviceSenseGlance(now);
        // ...and so does a push's own join, which ends by dropping the link
        // sensing would have just made.
        if (want && !senseCydRunning() && !s_timeSyncRadio && !OtaWifi::timeSyncBusy() &&
            !OtaWifi::pushBusy() && OtaWifi::state() == OtaWifi::State::OFF) {
            // Remembered (NVS), so a board switched off in SENSING boots back
            // into it (the boot screen's exit). Cleared below when the screen
            // is left any way but a power cut. Not for SENSE ARM, which
            // starts again by itself.
            if (onScreen && !Settings::sensing()) Settings::setSensing(true);
            if (!onScreen) Serial.printf("[sense] SENSE ARM %s: sensing, detector paused\n",
                                         SenseMotion::armLabel(Settings::senseArm()));
            engine.startUpdateRadio();
            SenseStore::begin();
            char ssid[33] = "", pass[65] = "";
            // Which saved network: there is no scan here (the screen should
            // come up at once), so the one marked USE if it has joined before,
            // else the first that has, else USE. A board carried between two
            // places has both saved, and USE may be the one that is not here.
            const uint8_t nSaved = s_senseNoWifi ? 0 : OtaWifi::savedCount();
            s_senseNoWifi = false;
            if (nSaved) {
                const uint8_t use = OtaWifi::savedUse() < nSaved ? OtaWifi::savedUse() : 0;
                int k = OtaWifi::savedResult(use) == OtaWifi::SavedResult::JOINED ? use : -1;
                for (uint8_t i = 0; i < nSaved && k < 0; i++)
                    if (OtaWifi::savedResult(i) == OtaWifi::SavedResult::JOINED) k = i;
                if (k < 0) k = use;
                strncpy(ssid, OtaWifi::savedSsidAt((uint8_t)k), sizeof ssid - 1);
                OtaWifi::savedPassAt((uint8_t)k, pass, sizeof pass);
            }
            senseCydStart(ssid, pass);
            memset(pass, 0, sizeof pass);
        } else if (!want && senseCydRunning()) {
            if (s_senseDark) senseWake();   // left any other way (a lock, an alert): the screen comes back
            if (Settings::sensing()) Settings::setSensing(false);
            senseCydStop();
            SenseStore::end();
            engine.stopUpdateRadio();
            Serial.println("[sense] detector back on");
        } else if (!want && state != AppState::SENSING && Settings::sensing()) {
            Settings::setSensing(false);   // left before the engine had started
        }
        // Left the screen with SENSE ARM keeping it running: no longer a
        // board to boot back into the screen.
        if (!onScreen && Settings::sensing()) Settings::setSensing(false);
        senseCydTick();
        SenseStore::tick(now);
        if (senseCydRunning()) serviceMotion(now);
    }
#endif
#if defined(CYD_GPS)
    cydGpsTick(millis());
#endif
    Lora::tick(now);   // nothing outside a MULESKIN_LORA build
#if MULESKIN_LORA
    // A LoRaWAN operator's network, heard: one Detection per operator
    // (lora_opalert.h), so the ALERT, the LOG, IGNORE and SNOOZE treat it as
    // they treat an AirTag. Channel 0 files it with the Bluetooth rows -- the
    // namespace WATCH and HUNT keep an address in -- and feeding both of
    // those here is what makes HUNT's gauge move when the operator's next
    // frame lands. That is every few minutes, not every second. (Upstream
    // SquachWatch v1.45.)
    {
        Lora::OpAlert::Hit h;
        while (Lora::takeOperatorHit(h)) {
            Detection d;
            memset(&d, 0, sizeof d);
            memcpy(d.mac, h.mac, 6);
            d.rssi      = h.rssi;
            d.channel   = 0;
            d.type      = DetectionType::LORAWAN;
            d.vendor    = "LoRaWAN";
            snprintf(d.name, sizeof d.name, "%s", h.name);
            // loop()'s own `now`, not millis(): the alert test further down
            // this pass measures the catch's age from it.
            d.firstSeen = d.lastSeen = now;
            d.hits      = 1;
            d.conf      = h.exact ? Confidence::HIGH_CONF : Confidence::MED_CONF;
            d.active    = true;
            engine.postKeyed(d, DetectionEngine::LORA_GONE_MS);
            engine.checkWatchBle(d.mac, d.rssi);
            engine.checkHuntBle(d.mac, d.rssi);
        }
    }
#endif
#if MULESKIN_LORA && defined(TWATCH_S3)
    twatchLoraBuzzTick(now);
#endif
    floodTick();   // nothing outside a FLOOD_BENCH build
    // The heap at the first pass of loop(), for DIAGNOSTICS' BOOT line.
    static uint32_t s_loopHeapFree = 0, s_loopHeapLargest = 0;
    if (!s_loopHeapFree) {
        s_loopHeapFree    = ESP.getFreeHeap();
        s_loopHeapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        Serial.printf("[boot] heap at the first loop: %lu free, %lu largest\n",
                      (unsigned long)s_loopHeapFree, (unsigned long)s_loopHeapLargest);
    }
    crashCrumbTick(now, engine.lifetimeTotal(), (uint8_t)state);
    // Confirms a probationary image once it has run long enough, and drives a
    // Bluetooth update's flash work -- the BLE callbacks only hand it jobs.
    OtaCore::tick(now);
    OtaBle::tick(now);
    OtaWifi::tick(now);
    serviceTimeSyncRadio();
    serviceAutoTime(now);
    serviceNightDim(now);
#if !defined(TWATCH_S3)
    serviceNightWifi(now);
#endif
    serviceFollowing(now);
    serviceTrackerDays(now);
    serviceLive(now);
    serviceAutoUpdate(now);
    servicePush(now);
    serviceSmallStoreNotice(now);
    if (state == AppState::CLEAR) {
        const char* sub = nullptr;
        bool good = false;
        if (const char* head = OtaCore::takeBootNote(&sub, &good))
            Theme::showToast(head, sub, good ? Theme::CYAN : Theme::AMBER);
        // A brownout restart: say so once, with what to do about it. A
        // hub-powered CYD dips at WiFi start; the fix is power, not firmware.
        // The toast's second line holds 21 characters.
        else if (s_brownouts) {
            char head[24];
            if (s_brownouts > 1) snprintf(head, sizeof head, "POWER DIPPED x%u", (unsigned)s_brownouts);
            else                 snprintf(head, sizeof head, "POWER DIPPED");
            Theme::showToast(head, "Use 2A or direct USB", Theme::AMBER, 8000);
            Serial.printf("[power] %u brownout restart(s) in a row: weak USB supply -- use a direct port or a 2 A supply\n",
                          (unsigned)s_brownouts);
            s_brownouts = 0;
        }
    }

    // The padlock, left of the rotate icon while a PIN is set, on the screens
    // that draw the corner icons. Ahead of the rotate handler, whose oversized
    // target overlaps it, and it takes the whole gesture -- the same swallow a
    // wake tap gets -- so nothing underneath sees the finger.
    if (touchJustDown && Security::enabled() && !Security::locked() &&
        (state == AppState::CLEAR || state == AppState::LOG || state == AppState::SETTINGS ||
         state == AppState::OUTFIT || state == AppState::DETECTION_FILTER ||
         state == AppState::IGNORE_LIST || state == AppState::POWER_SAVER || state == AppState::SECURITY ||
         state == AppState::STATUS_LIGHT ||
         state == AppState::DIARY || state == AppState::HUNT || state == AppState::DIAGNOSTICS) &&
        Theme::lockButtonHit(tp.x, tp.y, tft.width())) {
        lastTouch = now;
        if (onRawScanScreen()) engine.stopRawScan();
        Security::lock();
        enterLocked();
        s_swallowTouch = true;
        tp.valid = false;
        touchJustDown = false;
        touchJustUp = false;
    }

    // Rotate button lives in the title bar's top-right corner, shown on
    // the CLEAR, LOG, SETTINGS and OUTFIT screens (drawTitleBar always
    // draws it on the two boards that have one — gating the hit-test
    // to these keeps it inert wherever there's no title bar drawn at
    // all, i.e. BOOT/ALERT). Not built at all on AWOK: confirmed on
    // real hardware that board's case only holds the panel in one
    // orientation (portrait), so rotation was pure unused complexity
    // there — see Theme::setRotateIconVisible(false) in setup(), which
    // also hides the icon itself, not just this handler. When
    // Settings::rotationLocked() is on, the icon is not drawn either (see
    // drawTitleBar) and this handler is skipped — so there is no control
    // on screen that looks live and does nothing.
#if !defined(AWOK)
    // Neither corner button answers a finger that is carrying MuleSkin: dragged
    // into a corner, he used to open Settings or rotate the screen mid-carry,
    // and the release that would have dropped him never came.
#if defined(TWATCH_S3)
    // Every AXP2101 register, sixteen to a line, so a deaf boot and a
    // hearing boot can be compared register by register.
    if (g_consoleMotion) {
        g_consoleMotion = false;
        Serial.printf("[motion] %s: x %d y %d z %d, moved %u of the last 128 s, %s; snooze after %u\n",
                      s_bmaOk ? "BMA423" : "no sensor", s_acc[0], s_acc[1], s_acc[2], (unsigned)movedSecs(),
                      twatchStill() ? "STILL" : "MOVING", (unsigned)Settings::autoQuietAfter());
    }
    if (g_consoleBuzz) {
        g_consoleBuzz = false;
        Serial.printf("[buzz] test: screen %s, cpu %u MHz, driver status %02X mode %02X lib %02X\n",
                      s_panelAsleep ? "asleep" : "awake", (unsigned)getCpuFrequencyMhz(),
                      drvRead(0x00), drvRead(0x01), drvRead(0x03));
        twatchBuzz(Buzz::SAMPLE);
        const uint8_t go0 = drvRead(0x0C);
        delay(60);
        const uint8_t go1 = drvRead(0x0C);
        delay(400);
        Serial.printf("[buzz] test: GO %u right after, %u at 60 ms, %u at 460 ms; status %02X\n",
                      go0, go1, drvRead(0x0C), drvRead(0x00));
    }
    if (g_consoleRtc) {
        g_consoleRtc = false;
        bool lost = false, foreign = false;
        const uint32_t t = rtcRead(&lost, &foreign);
        Serial.printf("[rtc] chip %lu, system %lu, %s\n", (unsigned long)t, (unsigned long)Clock::nowEpoch(),
                      lost ? "LOST (backup ran out)" : foreign ? "not set by MuleSkin" : t ? (Clock::trusted() ? "clock trusted" : "clock not trusted") : "no time");
    }
    if (g_consolePmu) {
        g_consolePmu = false;
        for (uint8_t base = 0x00; base < 0xA0; base += 16) {
            char line[80];
            int n = snprintf(line, sizeof line, "[pmu] %02X:", base);
            for (uint8_t k = 0; k < 16; k++) {
                Wire1.beginTransmission(0x34);
                Wire1.write((uint8_t)(base + k));
                uint8_t v = 0xEE;
                if (Wire1.endTransmission(false) == 0 && Wire1.requestFrom((uint8_t)0x34, (uint8_t)1) == 1) v = Wire1.read();
                n += snprintf(line + n, sizeof line - n, " %02X", v);
            }
            Serial.println(line);
        }
    }
    gpsTick();
#endif
#if defined(ESP32) && !defined(SQW_S3) && !defined(CROWPANEL7) && !defined(TAB5)   // hardware only: the emulators build this too
    // ADC: every input-only analog pin the CYDs leave free, in millivolts,
    // averaged over 16 reads. A battery divider shows up as about half the
    // cell's voltage, and moves when the cell is unplugged.
    if (g_consoleSelfTest) {
        g_consoleSelfTest = false;
        runSelfTest();
    }
    if (g_consoleBtUpdate) {
        // UPDATE BT: what UPDATE FIRMWARE > UPDATE OVER BLUETOOTH does, for a
        // bench with no finger on the screen. The pairing code is printed.
        g_consoleBtUpdate = false;
        if (!OtaBle::available()) {
            Serial.println("[ota] no Bluetooth updates on this build");
        } else if (state != AppState::UPDATE) {
            enterUpdate();
            engine.startUpdateRadio();
            if (!OtaBle::begin()) { engine.stopUpdateRadio(); Serial.println("[ota] bluetooth update mode refused"); }
        }
    }
    if (g_consoleWho) {
        // "WHO <push name> <Bluetooth name> <version> <store KB>": what the
        // web flasher and tools/bench show as soon as they connect, so a
        // board on the cable is never taken for another.
        g_consoleWho = false;
        char pn[24];
        snprintf(pn, sizeof pn, "%s", pushBoardName());
        for (char* c = pn; *c; c++) if (*c == ' ') *c = '_';
        Serial.printf("WHO %s %s %s %u\n", pn, btBoardName(), FIRMWARE_VERSION, nvsStoreKb());
    }
    if (g_consolePush >= 0) {
        // PUSH OFF / ON / TEST, for the flasher page and the bench. URL and
        // TOKEN are set by clock.cpp directly; this is the part that needs
        // the queue.
        const int8_t c = g_consolePush;
        g_consolePush = -1;
        if (c < 2 && Settings::pushOn() != (c == 1)) Settings::togglePush();
        if (c == 2) {
            if (Settings::pushOn()) queuePush("MuleSkin test", "Push alerts reach this phone", 3, "white_check_mark", true);
            Serial.printf("[push] test %s\n", Settings::pushOn() ? "queued" : "not queued: PUSH ALERTS is off or has no URL");
        } else {
            Serial.printf("[push] %s: %s\n", Settings::pushOn() ? "on" : "off",
                          Settings::pushUrl()[0] ? Settings::pushUrl() : "no URL set");
        }
    }
    if (g_consoleLegend) {
        g_consoleLegend = false;
        MuleSkin::previewLegend(!MuleSkin::legendPreview());
        Serial.printf("[legend] preview %s\n", MuleSkin::legendPreview() ? "ON" : "off");
    }
    if (g_consoleAura) {
        g_consoleAura = false;
        Settings::toggleAura();
        Serial.printf("[aura] %s\n", Settings::auraShown() ? "LIT" : "OUT");
    }
    if (g_consoleOutfitSet) {
        g_consoleOutfitSet = false;
        MuleSkin::wearForBench(g_consoleOutfit);
        Serial.printf("[outfit] bench %d until the next boot\n", (int)g_consoleOutfit);
    }
    if (g_consoleXyzzy) {
        g_consoleXyzzy = false;
        Theme::summonXyzzy();
        Serial.println("[xyzzy] the terminal types it now (TERMINAL background only)");
    }
    if (g_consoleAdc) {
        g_consoleAdc = false;
        const uint8_t pins[] = { 34, 35, 36, 39 };
        char line[96]; int n = snprintf(line, sizeof line, "[adc]");
        for (uint8_t p : pins) {
            uint32_t mv = 0;
            for (int k = 0; k < 16; k++) mv += analogReadMilliVolts(p);
            n += snprintf(line + n, sizeof line - n, "  GPIO%u %lu mV", p, (unsigned long)(mv / 16));
        }
        Serial.println(line);
    }
    // PINS: the digital level of every pin a button could be on, for finding
    // one by pressing it. GPIO22 is the only spare the CYDs never configure,
    // so it gets a pull-up here; the rest are read as they stand.
    if (g_consolePins) {
        g_consolePins = false;
        static bool once = false;
        if (!once) { once = true; pinMode(22, INPUT_PULLUP); }
        const uint8_t pins[] = { 0, 22, 5, 18, 19, 23, 26, 35, 36, 39 };
        char line[120]; int n = snprintf(line, sizeof line, "[pins]");
        for (uint8_t p : pins) n += snprintf(line + n, sizeof line - n, " %u=%d", p, digitalRead(p));
        Serial.println(line);
    }
    // I2C: what answers on the touch bus. A power chip with a button and a
    // fuel gauge (an IP5306 at 0x75, say) would show up beside the touch chip.
    if (g_consoleI2c) {
        g_consoleI2c = false;
        char line[160]; int n = snprintf(line, sizeof line, "[i2c]");
        for (uint8_t a = 8; a < 120; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0 && n < (int)sizeof line - 8) n += snprintf(line + n, sizeof line - n, " 0x%02X", a);
        }
        Serial.println(line);
    }
#endif
#if SQW_RING
    {
        uint8_t fm[6];
        if (!TagRing::busy() && TagRing::takeFound(fm) && !TagRing::start(engine, fm))
            Serial.println("[ring] busy");
    }
    if (g_consoleRing) {
        g_consoleRing = false;
        uint8_t mac[6];
        if (!TagRing::latest(mac))              Serial.println("[ring] no tracker heard since boot");
        else if (!TagRing::start(engine, mac))  Serial.println("[ring] busy");
        else Serial.printf("[ring] ringing %02X:%02X:%02X:%02X:%02X:%02X\n",
                           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    }
    {
        // A ring has finished: the radio is back, the button comes up, and
        // the answer is a toast on whatever screen is showing.
        const TagRing::Result r = TagRing::poll(engine);
        if (r != TagRing::Result::NONE) {
            uiAlertSetRinging(false);
            uiLogSetRinging(false);
            Theme::showToast(TagRing::title(r), TagRing::detail(r),
                             r == TagRing::Result::RANG ? Theme::GREEN : Theme::AMBER);
        }
    }
#endif
    if (g_consoleInvert) {
        g_consoleInvert = false;
        Settings::toggleInvert();
        tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
        Serial.printf("[console] invert setting %s (panel baseline %s)\n", Settings::inverted() ? "ON" : "OFF",
                      PANEL_NEEDS_INVERSION ? "inverted" : "normal");
    }
    // WATCHTEST: put the newest device in the log on the watch list and count
    // this as its first hit, so the LOCKED ON screen comes up at the next
    // home-screen tick and then fills in from its real signal. Live Bluetooth
    // first, then live WiFi, then anything the black box brought back. It is a
    // real watch: REMOVE FROM WATCH LIST ends it.
    if (g_consoleWatchTest) {
        g_consoleWatchTest = false;
        const Detection* pick = nullptr;
        for (uint8_t pass = 0; pass < 3 && !pick; pass++)
            for (uint8_t i = 0; i < engine.logCount() && !pick; i++) {
                const Detection* d = engine.logAt(i);
                if (!d) continue;
                const bool ble = d->channel == 0;
                if (pass == 2 || (!d->restored && (pass == 0) == ble)) pick = d;
            }
        if (!pick) {
            Serial.println("[watch] test: the log is empty; wait for a detection");
        } else {
            const bool ble = pick->channel == 0;
            const char* label = pick->name[0] ? pick->name : detectionTypeName(pick->type);
            if (ble) engine.watchBle(pick->mac, label);
            else     engine.watchWifi(pick->mac, label);
            engine.forceWatchHit(pick->rssi);
            Serial.printf("[watch] test: watching %s (%s over %s, %d dBm%s); the alert shows on the home screen\n",
                          engine.watchLabel(), detectionTypeName(pick->type), ble ? "Bluetooth" : "WiFi",
                          (int)pick->rssi, pick->restored ? ", from before this boot" : "");
        }
    }
#if defined(SQW_SMALL)
    g_consoleRotate = false;   // landscape only
#endif
    if (g_consoleRotate || (tp.valid && !Settings::rotationLocked() && !MuleSkin::isHeld() &&
        (state == AppState::CLEAR || state == AppState::LOG ||
                      state == AppState::SETTINGS || state == AppState::OUTFIT ||
                      state == AppState::DETECTION_FILTER ||
                      state == AppState::IGNORE_LIST) &&
        Theme::rotateButtonHit(tp.x, tp.y, tft.width()) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS)) {
        if (g_consoleRotate) { g_consoleRotate = false; Serial.printf("[console] rotation -> %u\n", (unsigned)((screenRotation + 1) % 4)); }
        lastTouch = now;
        MuleSkin::trigger(MuleSkin::Event::ROTATED);
        screenRotation = (screenRotation + 1) % 4;
        Settings::saveRotation(screenRotation);
        tft.setRotation(screenRotation);
        FramePush::invalidate();   // the panel was re-initialised; send every row next
        // The MX/MY/MV bits applyColorOrder() writes are rotation-
        // dependent, so it has to be reissued alongside every
        // setRotation() call, not just at boot.
        applyColorOrder();
        // Landscape and portrait need differently-*shaped* buffers, but
        // not differently-*sized* ones -- a rectangular panel has the
        // same total pixel count either way (320x240 and 240x320 are
        // both 76800 px), so the buffer allocated once at boot already
        // fits every orientation. resizeInPlace() (see ResizableSprite
        // above) just re-points the sprite's own width/height/stride
        // bookkeeping at the new shape; it never frees or reallocates.
        //
        // This replaces an earlier delete+recreate-every-rotate
        // approach that looked safe (retried 3x with delays between
        // attempts) but confirmed-failed on real hardware despite 123KB
        // of TOTAL free heap -- the largest contiguous block was only
        // 73.7KB against a 76.8KB need, pure fragmentation from
        // WiFi/BLE churn, not a shortage retries could out-wait (delay()
        // doesn't make the ESP32 heap allocator compact anything). Since
        // resizeInPlace() never frees the buffer, that failure mode is
        // now structurally impossible after the first successful boot
        // allocation -- there's no more free/realloc cycle left to lose
        // the fragmentation gamble against.
        if (frame.created()) {
#if defined(CYD35)
            frame.resizeInPlace(tft.width(), tft.height() / 2);
#else
            frame.resizeInPlace(tft.width(), tft.height());
#endif
        } else if (frameBufferOk) {
            // Never got its one-time boot-time allocation in the first
            // place (see setup()) -- same permanent fallback a failed
            // rotate used to trigger, since there's no buffer to resize.
            Serial.println("[rotate] frame buffer was never allocated -- falling back to unbuffered rendering");
            frameBufferOk = false;
#if !defined(CYD35)
            canvas = &tft;
#endif
        }
        transitionStart = now;
    }
#endif  // !AWOK

    // Settings button lives in the title bar's top-left corner, shown
    // on the same screens as the rotate button. Tapping it while
    // already on the SETTINGS screen backs out to CLEAR instead of
    // re-entering itself — same toggle-off feel as the LOG button. From
    // OUTFIT or DETECTION_FILTER (only reachable from SETTINGS' own
    // rows) it backs out to SETTINGS instead of CLEAR, matching where
    // it was entered from — this is the only way back out of either
    // screen, since OUTFIT's own taps are all claimed by the arrows and
    // DETECTION_FILTER's are all claimed by row toggles.
    if (tp.valid && !MuleSkin::isHeld() &&
        (state == AppState::CLEAR || state == AppState::LOG ||
                      state == AppState::SETTINGS || state == AppState::OUTFIT ||
                      state == AppState::DETECTION_FILTER ||
                      state == AppState::IGNORE_LIST || state == AppState::POWER_SAVER ||
                      state == AppState::SECURITY || state == AppState::STATUS_LIGHT
#if defined(SENSE_CYD_APP)
                      || state == AppState::SENSING
#endif
                      ) &&
        Theme::settingsButtonHit(tp.x, tp.y) &&
        // ...but not where the watch/hunt pill is sitting. The gear's tap box
        // is 55x50, much larger than its 28px glyph, so it reaches into the
        // title bar's middle where the pill lives. The pill is only ever drawn
        // while a target is set, so this gives up nothing the rest of the time.
        !(state == AppState::CLEAR && uiClearWatchPillHit(tp.x, tp.y)) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        if (state == AppState::STATUS_LIGHT) {
            // Back to the APPEARANCE page it was opened from, not the top.
            enterSettings();
            uiSettingsOpenAppearance(true);
        }
        else if (state == AppState::OUTFIT || state == AppState::DETECTION_FILTER ||
            state == AppState::IGNORE_LIST || state == AppState::POWER_SAVER ||
            state == AppState::SECURITY
#if defined(SENSE_CYD_APP)
            || state == AppState::SENSING
#endif
            ) enterSettings();
        else if (state == AppState::SETTINGS) {
            // The gear on a sub-page goes back up a level, the way it does
            // from every other screen Settings opens.
            if (uiSettingsCurrentPage() != SettingsPage::MAIN) uiSettingsOpenPage(SettingsPage::MAIN);
            else                                               enterClear();
        }
        else {
            // The main list, as from every other screen.
            enterSettings();
        }
    }

    // Long-press the middle of the title bar (between the settings and
    // BOTTOM centre now, not the title bar -- the bar is gone. A 1.5 second
    // hold on the middle of the button bar on CLEAR/LOG recalibrates touch.
    //
    // It lands on the LOG button, which is deliberate rather than awkward:
    // the empty slot beside it already carries a four second hold for the
    // outfit unlock, so this follows a gesture the same bar already has
    // instead of inventing one. LOG had no hold of its own.
    //
    // Still no confirmation panel. This is the way back when touch is
    // already too far out to hit a button, which is exactly when a CONFIRM
    // button would be the thing standing between you and a working screen.
    //
    // Honest trade against what it replaces: the old target was a 220x20
    // strip and this is a 96x20 button, so the escape hatch is less than
    // half the size. It is also one somebody might actually find.
    static uint32_t calHoldStart = 0;
    const int calW = tft.width(), calH = tft.height();
    bool overBottomMid = tp.valid && tp.x >= calW / 3 && tp.x < (calW * 2) / 3
                         && tp.y >= calH - 34;
    if ((state == AppState::CLEAR || state == AppState::LOG) && overBottomMid) {
        if (calHoldStart == 0) calHoldStart = now;
        else if (now - calHoldStart > 1500) {
            calHoldStart = 0;
            lastTouch = now;
            // Swallow the rest of this gesture, or the finger coming off the
            // LOG button opens the log the moment calibration finishes. Same
            // whole-gesture swallow the dim-wake tap uses above.
            s_swallowTouch = true;
            runTouchCalibration();
            enterClear();
        }
    } else {
        calHoldStart = 0;
    }

    FrameProf::lap(FrameProf::PRE);
    switch (state) {
        case AppState::BOOT: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR uses --
                // reuses that same already-allocated sprite (see its
                // setup() comment) rather than needing a second
                // allocation, since BOOT/CLEAR/ALERT are never on
                // screen at the same time. uiBootTick() has no internal
                // per-call state to double-advance, so no advance flag
                // needed here unlike uiClearTick().
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiBootTick(frame, now);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiBootTick(frame, now);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiBootTick(tft, now);
            }
#else
            uiBootTick(*canvas, now);
            if (crashCardWanted()) drawCrashCard(*canvas);
#endif
            bool leave = uiBootDone(bootStart, crashCardWanted() ? 9000 : 3000);
            if (leave) {
                // First-ever boot only -- goes straight into the normal
                // onboarding overlay once this screen's own DONE button
                // reaches enterClear(), same as it always did before
                // this existed.
                if (!Settings::colorChecked())    enterColorCheck(false);
                // Something newer is out: the window says so before the
                // board gets on with its day. Not over the first-boot
                // walkthrough, which has a screen of its own to finish.
                else if (OtaCore::availableVersion()[0] && !Security::locked()) enterSysProps();
#if defined(SENSE_CYD_APP)
                // Switched off with HUMAN SENSING up: straight back to it.
                else if (Settings::sensing() && !Security::locked()) enterSensing();
#endif
                else                              enterClear();
            }
            break;
        }
        case AppState::TODAY: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiTodayTick(t, now); });
            // BACK on the finger lifting, like the meeting sign's.
            static bool backArmed = false;
            if (touchJustDown) backArmed = uiTodayHitBack(tp.x, tp.y, tft.width(), tft.height());
            if (touchJustUp && backArmed) {
                backArmed = false;
                lastTouch = now;
                enterClear();
            }
            break;
        }
        case AppState::TIME_SYNC: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiTimeSyncTick(t, now); });
            // Detection comes back in serviceTimeSyncRadio(), every loop.
            // BACK only once the job is over (it is not drawn before), and on
            // the finger lifting, so the touch cannot carry on into LOG.
            static bool backArmed = false;
            if (touchJustDown) backArmed = !OtaWifi::timeSyncBusy() &&
                                           uiTimeSyncHitBack(tp.x, tp.y, tft.width(), tft.height());
            if (touchJustUp && backArmed) {
                backArmed = false;
                lastTouch = now;
                enterClear();
            }
            break;
        }
        case AppState::MEETING: {
            // Nothing takes the sign over: detection runs and logs behind it,
            // but no ALERT, update window or late news replaces it.
            drawTwoBand([&](TFT_eSPI& t, bool) { uiMeetingTick(t, now); });
            // BACK acts on the finger lifting, so the touch that ends here
            // cannot carry on into the main screen's LOG, which sits in the
            // same slot.
            static bool backArmed = false, signArmed = false;
            if (touchJustDown) {
                backArmed = uiMeetingHitBack(tp.x, tp.y, tft.width(), tft.height());
                signArmed = !backArmed && uiMeetingHitSign(tp.x, tp.y, tft.width(), tft.height());
            }
            if (touchJustUp && backArmed) {
                backArmed = false;
                lastTouch = now;
                enterClear();
            } else if (touchJustUp && signArmed) {
                signArmed = false;
                lastTouch = now;
                uiMeetingCycleTimer(now);   // BACK IN 15 / 30 / 60 MIN, or none
            } else if (uiMeetingTimerDone(now)) {
                Serial.println("[meeting] timer ran out: sign down");
                enterClear();
            }
            break;
        }
        case AppState::DEX: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiDexTick(t, now, engine, advance); });
            if (touchJustDown) {
                lastTouch = now;
                if (uiDexHitTest(*canvas, tp.x, tp.y, tft.width(), tft.height()) == DexTap::BACK)
                    enterClear();
            }
            break;
        }
        case AppState::SYS_PROPS: {
            // A detection still takes the screen. The window can open on a
            // board nobody is watching -- AUTO TIME's daily join brings the
            // news -- and it has no timeout, so without this it held every
            // alert back until somebody happened to tap it.
            {
                const Detection* latest = freshCatch(now);
                if (latest &&
                    latest->conf >= Settings::minConfidence() && !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiAlertSetRedacted(false);
                    enterAlert(*latest);
                    break;
                }
                if (engine.watchHitPending()) { enterWatchAlert(); break; }
            }
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiSysPropsTick(t, now, engine, advance); });
            if (touchJustDown) {
                switch (uiSysPropsTouch(*canvas, tp.x, tp.y)) {
                    case SysPropsHit::UPDATE_NOW:
                        // The same start the UPDATE screen's own WiFi button
                        // makes: the radio changes hands and the update screen
                        // carries it from there.
                        enterUpdate();
                        engine.startUpdateRadio();
                        if (!OtaWifi::begin()) {
                            engine.stopUpdateRadio();
                            Theme::showToast("CAN'T START UPDATE", updateRefusedWhy(), Theme::AMBER);
                        }
                        break;
                    case SysPropsHit::CLOSE: enterClear(); break;
                    case SysPropsHit::NONE:  break;
                }
                lastTouch = now;
            }
            break;
        }
        case AppState::CLEAR: {
            // A newer release heard of AFTER the intro: the window, now. The
            // intro only opens it for news that is already known when the
            // intro ends, and on a board whose boot check could not reach the
            // site the news comes later, with AUTO TIME's daily join -- which
            // can land a few seconds either side of that moment, so the
            // window would come up on one boot and not the next. Once per
            // version: takeAvailableNotice() answers once, and a newer version
            // arms it again. Not over a visit's banter any more either: being
            // painted over by it is why this is a window at all.
            if (now - transitionStart > 1500 && OtaCore::takeAvailableNotice()) {
                enterSysProps();
                break;
            }
            if (now - transitionStart > 7000) {
                // Once a day, a hello with the date in it; on the days that
                // count, how long it has been.
                const char* dl = MuleSkin::takeDayLine();
                if (dl) MuleSkin::announce(dl);
            }
            // And the first-of-its-kind line, straight after the card.
            if (s_firstLine[0] && now - transitionStart > 1200) {
                MuleSkin::announce(s_firstLine);
                s_firstLine[0] = '\0';
            }
            // Checked before drawing so the celebration takes over on the
            // same frame it becomes due, rather than after one frame of
            // CLEAR flashing up behind it.
            if (maybeEnterOutfitUnlock()) break;
#if defined(CYD35)
            if (frameBufferOk) {
                // Two passes through the half-height `frame` sprite
                // instead of one direct-to-tft pass -- see the setup()
                // comment by its creation. advance=true only on the
                // first pass so MuleSkin/digital-rain state advances once
                // per logical frame even though this draws twice.
                int halfH = tft.height() / 2;
                uint32_t tBand = micros();
                // The rows each pass can actually paint. Drawing that lands
                // entirely outside them is declined a block at a time rather
                // than clipped a pixel at a time -- see draw_band.h.
                DrawBand::set(0, halfH);
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiClearTick(frame, now, engine, true);
                s_bandUs[0] = micros() - tBand;
                pushFrame(0, 0);
                tBand = micros();
                DrawBand::set(halfH, tft.height());
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiClearTick(frame, now, engine, false);
                s_bandUs[1] = micros() - tBand;
                pushFrame(0, halfH);
                DrawBand::all();
                frame.resetViewport();
            } else {
                // Fallback if a post-boot rotate ever failed to
                // reallocate `frame` (see loop()) -- same direct-to-tft
                // path this board already uses for every other screen.
                uiClearTick(tft, now, engine, true);
            }
#else
            uiClearTick(*canvas, now, engine, true);
#endif
            FrameProf::lap(FrameProf::CHROME);
            // Toasts on the main screen too. They were only drawn on LOG and
            // NEARBY, so SNOOZED and READ, both raised on the way here or while
            // here, went unseen.
            Theme::drawToast(*canvas, now);
            // The clock is set and no zone was ever picked: the card, over
            // everything, until THIS IS RIGHT. A tap on it is the card's; a
            // tap beside it is the main screen's, so he can still be poked.
            if (uiZoneCardWanted()) {
                uiZoneCardDraw(*canvas, now);
                if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    const ZoneHit zh = uiZoneCardHit(tp.x, tp.y, tft.width(), tft.height());
                    if (zh != ZoneHit::NONE) {
                        lastTouch = now;
                        if      (zh == ZoneHit::PREV) Settings::stepTimeZone(-1);
                        else if (zh == ZoneHit::NEXT) Settings::stepTimeZone(1);
                        else if (zh == ZoneHit::OK)   Settings::markTimeZoneChosen();
                        break;
                    }
                }
            }
            // Check for new detection — gated by the settings-menu
            // confidence filter (LOW_CONF/default = no filtering, every
            // match still interrupts with the ALERT screen).
            // A watched target coming back takes priority over a
            // routine signature-match alert -- it's not filtered by
            // the confidence setting either, since it isn't a
            // signature guess at all, it's the exact thing the user
            // explicitly asked to be told about.
            //
            // Neither one interrupts the first-boot walkthrough: a real
            // detection popping the full-screen ALERT (or watch-alert)
            // mid-explanation would cut MuleSkin off before he's done
            // introducing everything. The detection itself still gets
            // logged/counted as normal either way (that already
            // happened before this check runs) -- this only decides
            // whether it pops up over the tutorial. watchHitPending()
            // is deliberately NOT called in that branch since it's
            // consumed on read; leaving it untouched means it stays
            // pending and still fires for real once onboarding ends.
            // Taken (and so consumed) before the chain: a catch that lands
            // during the walkthrough, or alongside a watch hit, is not held
            // back to pop up a moment later.
            const Detection* latest = freshCatch(now);
            if (MuleSkin::onboardingActive()
               ) {
                // deliberately no-op
            } else if (engine.watchHitPending()) {
                enterWatchAlert();
            } else {
                if (latest &&
                    // This sighting's grade, not the type's -- which is what
                    // finally makes ALERT FILTER mean something. Set it to
                    // High and an ESP32 probe request matching a
                    // module-vendor OUI stays in the log without taking over
                    // the screen.
                    latest->conf >= Settings::minConfidence() &&
                    // Your own AirTag and your own doorbell are true
                    // positives every single time, and a detector that
                    // shouts about them constantly is one you stop reading.
                    // Only the ALERT is suppressed -- the detection is
                    // still counted and still written to the LOG above, so
                    // the device stays visible and un-ignorable.
                    !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiAlertSetRedacted(false);
                    enterAlert(*latest);
                }
            }
            // First-boot walkthrough: tapping its bubble advances (or
            // ends) it. Checked before everything else below so it eats
            // the tap on a hit — but it only ever hits its own bubble,
            // never MuleSkin himself or the button bar, so pet-tap,
            // background-cycling and the buttons all keep working
            // normally throughout the walkthrough, same as any other
            // time on this screen. Both this and the pet-tap check are
            // skipped outright in "boring mode": uiClearTick() never
            // draws him there, so hitTest() would otherwise still be
            // checking against wherever he last stood before the mode
            // was turned on — a tap on empty background shouldn't pet
            // a mascot that isn't there.
            // Summoning the werewolf earns the WOLF PELT costume. Read
            // here rather than inside theme.cpp so the background
            // renderer stays unaware of the outfit system.
            if (Theme::consumeWerewolfSummon()) MuleSkin::unlockWolfPelt();
            if (Theme::consumeToasterCatch())   MuleSkin::unlockChromeWing();
            if (Theme::consumeEyeCatch())       MuleSkin::unlockVoidEye();
            if (Theme::consumeLodgeKnock())     MuleSkin::unlockParka();
            if (Theme::consumeSharkCatch())     MuleSkin::unlockShark();
            if (const uint8_t said = Theme::consumeXyzzy()) MuleSkin::magicWord(said);
    if (Theme::consumePetUnlock())      MuleSkin::unlockPet();

            // TERMINAL MODE (the BORING MODE switch): the home screen is
            // uiClearTick()'s desktop of windows, not the radar.
            bool boring = Settings::boringMode();
            // A touch that starts on one of its windows is theirs, from the
            // press to the let-go -- a drag scrolls the log, a tap picks a
            // row, a heading sorts. The title bar (menu, clock, pills) and
            // the buttons are outside every window, so they work as they
            // always have. Not under the time zone card, whose taps are its
            // own. (Upstream v1.43.)
            if (boring && !uiZoneCardWanted() && uiTerminalTouch(tp.x, tp.y, tp.valid, touchJustDown, now)) {
                lastTouch = now;
                // The picked row (or LATEST) asked for the device's menu: the
                // LOG screen's WATCH / HUNT / INFO / IGNORE panel, and back
                // here when it closes (s_logFromTerminal).
                uint8_t want[6];
                if (uiTerminalTakeOpen(want)) {
                    for (uint8_t i = 0; i < engine.logCount(); i++) {
                        const Detection* d = engine.logAt(i);
                        if (!d || memcmp(d->mac, want, 6)) continue;
                        enterLog();
                        openConfirmFor(d);
                        s_logFromTerminal = true;
                        break;
                    }
                }
                break;
            }
            ButtonId barBtn = tp.valid ? Theme::hitTestButtonBar(tp.x, tp.y, tft.width(), tft.height()) : ButtonId::NONE;

            // Left/right 10% slivers of the screen (excluding the button
            // bar row itself, so its own leftmost/rightmost buttons still
            // win there) cycle the background one step. Edge-triggered on
            // touchJustDown rather than the TOUCH_DEBOUNCE_MS cooldown
            // below, so a held finger fires exactly once no matter how
            // long it stays down. This frees up tapping MuleSkin himself
            // for the gesture classifier instead of also nudging the
            // background, so he can be poked without changing the scene.
            const int edgeZoneW = tft.width() / 10;
            // Not in TERMINAL MODE: the windows reach both edges, and the
            // radar they stand in for is not on screen to be changed.
            bool inEdgeZone = !boring && tp.valid && barBtn == ButtonId::NONE &&
                               (tp.x < edgeZoneW || tp.x >= tft.width() - edgeZoneW);

            // MuleSkin gesture state, tracked across frames from the
            // moment a touch lands on him until it releases. A quick tap
            // (released before SQ_HOLD_MS with negligible movement) reads
            // as PETTED -- the only one of the three that counts toward
            // the persisted pet-count/milestones. A stationary press held
            // past SQ_HOLD_MS reads as HELD. Movement past SQ_MOVE_PX
            // reads as PETTING (a drag/stroke) and keeps firing, throttled
            // internally by muleskin.cpp, for as long as the stroke
            // continues.
            static bool     sqActive = false;
            static bool     sqHeld = false;
            static bool     sqPetting = false;
            static uint32_t sqStartMs = 0;
            static int      sqStartX = 0, sqStartY = 0;
            static int32_t  sqLastDx = 0;   // the stroke's reach so far, for the flick
            constexpr uint32_t SQ_HOLD_MS = 600;
            constexpr int32_t  SQ_MOVE_PX = 12;
            constexpr int32_t  SQ_MOVE_PX_SQ = SQ_MOVE_PX * SQ_MOVE_PX;


            // Long-press NEARBY: open the closest live device. The headline
            // sits on top of MuleSkin, so the same touch is also tracked as a
            // possible pet -- a quick tap still pets him, a stroke still
            // strokes him, and only a still press held past the threshold
            // becomes this instead.
            static bool     nbActive = false;
            static uint32_t nbStart  = 0;
            constexpr uint32_t NEARBY_HOLD_MS = 600;

            // Decide up front whether a brand-new touch lands on MuleSkin
            // -- this only updates gesture-tracking state, it doesn't by
            // itself claim the touch, so a miss still falls through to
            // the button-bar branch below instead of being swallowed.
            // (A touch that DOES land on him, or continues an already-
            // active gesture, still takes priority over the button bar
            // in the branch below -- MuleSkin is drawn well clear of the
            // button row, so the two never really compete in practice.)
            if (touchJustDown) {
                // Not where he is not drawn: boring mode, or the MULESKIN
                // background (see uiClearMascotShown()).
                sqActive = uiClearMascotShown() && MuleSkin::hitTest(tp.x, tp.y);
                sqHeld = false;
                sqPetting = false;
                sqStartMs = now;
                sqStartX = tp.x;
                sqStartY = tp.y;
                sqLastDx = 0;
                nbActive = uiClearNearbyHit(tp.x, tp.y);
                nbStart  = now;
            }

            // Any tap at all ends the parade, and is consumed doing it --
            // checked ahead of everything else so a tap cannot both stop
            // the show and cycle a background or pet him on the way out.
            if (MuleSkin::showOffActive() && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                MuleSkin::stopShowOff();
                lastTouch = now;
                sqActive  = false;
            } else if (!boring && MuleSkin::onboardingActive() && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                MuleSkin::onboardingTapAdvance(tp.x, tp.y)) {
                lastTouch = now;
#if MULESKIN_LORA && defined(TWATCH_S3)
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       twatchLoraBadgeHit(tp.x, tp.y)) {
                // The LORA pill: straight to the chats.
                lastTouch = now;
                sqActive  = false;
                enterLoraChat();
#endif
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       uiClearClockHit(tp.x, tp.y)) {
                // The clock: TODAY, what has been around since midnight.
                lastTouch = now;
                enterToday();
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       uiClearWatchPillHit(tp.x, tp.y)) {
                // The watch/hunt pill. Opens the alert screen, which names the
                // target and carries REMOVE FROM WATCH LIST -- the same screen
                // a real sighting would have opened, just asked for rather
                // than waited for.
                lastTouch = now;
                sqActive  = false;
                enterWatchAlert();
            } else if (!boring && touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       Theme::radarBlipAt(tp.x, tp.y, now, s_tappedBlip)) {
                // A blip on the radar: that device's panel on the LOG --
                // WATCH, IGNORE, HUNT or what it is -- as a long press on
                // its LOG row would open.
                lastTouch = now;
                const Detection* hit = nullptr;
                for (uint8_t i = 0; i < engine.logCount() && !hit; i++) {
                    const Detection* d = engine.logAt(i);
                    if (d && memcmp(d->mac, s_tappedBlip, 6) == 0) hit = d;
                }
                if (hit) { enterLog(); openConfirmFor(hit); }
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       Theme::backgroundTap(tp.x, tp.y, now)) {
                // Something tappable in the background itself claimed
                // this touch -- currently only the moon on the FIRE
                // scene. Checked BEFORE the edge zone below on purpose:
                // the moon sits high on the right, and while it has been
                // moved clear of the background-cycling sliver, ordering
                // it first means the egg cannot be broken by a later
                // change to either one.
                lastTouch = now;
            } else if (touchJustDown && inEdgeZone && !Settings::backgroundLocked() &&
                       (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                // The debounce check above matters more here than it
                // looks -- the title-bar SETTINGS icon lives in this
                // same left-edge zone, and tapping it to leave Settings
                // calls enterClear() earlier in this same loop()
                // iteration, before this switch runs. Without this
                // guard, the exact touch that just closed Settings fell
                // straight through into CLEAR's edge-zone gesture on
                // the very same frame and cycled the background right
                // as the screen appeared.
                lastTouch = now;
                if (tp.x < edgeZoneW) Settings::cyclePrevBackground();
                else                  Settings::cycleBackground();
            } else if (tp.valid && nbActive) {
                const int32_t dx = tp.x - sqStartX, dy = tp.y - sqStartY;
                // A much wider allowance than MuleSkin's pet stroke: a thumb
                // held still on a resistive panel wanders several pixels a
                // frame, and at the pet threshold every hold on a real board
                // was cancelled as a stroke and handed to him instead.
                constexpr int32_t NB_MOVE_PX_SQ = 24 * 24;
                if ((dx * dx + dy * dy) > NB_MOVE_PX_SQ) {
                    // A real stroke, not a press: MuleSkin gets it from here on.
                    nbActive = false;
                } else if ((now - nbStart) >= NEARBY_HOLD_MS) {
                    nbActive = false;
                    sqActive = false;
                    lastTouch = now;
                    // Closest means strongest signal, among devices that are
                    // live right now. RSSI is a guess at distance, not a
                    // measurement, which is why the card still shows the dBm.
                    const Detection* best = nullptr;
                    for (uint8_t i = 0; i < engine.logCount(); i++) {
                        const Detection* d = engine.logAt(i);
                        if (d && d->active && (!best || d->rssi > best->rssi)) best = d;
                    }
                    if (best) {
                        uiAlertSetRedacted(false);
                        enterAlert(*best);
                    } else {
                        Theme::showToast("NOTHING NEARBY", "It just left", Theme::CYAN);
                    }
                }
            } else if (uiClearMascotShown() && tp.valid && sqActive) {
                int32_t dx = tp.x - sqStartX;
                int32_t dy = tp.y - sqStartY;
                // Moving BEFORE the hold threshold is a stroke; moving
                // after it is a carry. Latching sqPetting only while
                // !sqHeld is what keeps the two gestures apart -- without
                // that guard the first drag after a successful hold fell
                // straight back into petting, and he could never be
                // picked up at all.
                if (!sqHeld && (dx * dx + dy * dy) > SQ_MOVE_PX_SQ) sqPetting = true;
                sqLastDx = dx;
                if (sqHeld) {
                    MuleSkin::grabTo(tp.x, tp.y);
                } else if (sqPetting) {
                    MuleSkin::trigger(MuleSkin::Event::PETTING);
                } else if ((now - sqStartMs) >= SQ_HOLD_MS) {
                    sqHeld = true;
                    MuleSkin::trigger(MuleSkin::Event::HELD);
                }
            } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                // Left WIFI TIME, middle LOG, right IN MEETING (ButtonId names
                // the slot, not the label).
                if (barBtn == ButtonId::LOG) { MuleSkin::trigger(MuleSkin::Event::LOG_OPENED); enterLog(); }
                else if (barBtn == ButtonId::CLR) enterMeeting();
                else if (barBtn == ButtonId::SCAN) enterTimeSync();
            }

            // Finalize the MuleSkin gesture on the exact frame the touch
            // releases, wherever the finger happens to end up (it can
            // slide off him mid-stroke and still release cleanly).
            if (touchJustUp && sqActive) {
                if (sqHeld) {
                    MuleSkin::release();     // drop him wherever he ended up
                } else if (!sqPetting) {
                    MuleSkin::noteTapAt(sqStartX, sqStartY);
                    MuleSkin::trigger(MuleSkin::Event::PETTED);
                } else if ((now - sqStartMs) < 320 && (sqLastDx > 48 || sqLastDx < -48)) {
                    // A stroke that covered fifty pixels in under a third of a
                    // second is a flick, not a pet.
                    MuleSkin::flick(sqLastDx > 0 ? 1 : -1);
                }
                sqActive = false;
            }
            break;
        }
        case AppState::ALERT: {
            const char* alertInfoText = s_infoShowingPrimer ? DetectionInfo::rssiConfidencePrimer()
                                                              : DetectionInfo::explainFor(s_confirmType, s_confirmVendor, privName(s_confirmName), engine);
            // No heading during the primer page -- it's about RSSI/
            // confidence in general, not any one detection type.
            const char* alertInfoTypeName = s_infoShowingPrimer ? nullptr
                                          : DetectionInfo::titleFor(s_confirmType, s_confirmVendor, privName(s_confirmName));
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR/BOOT
                // use, reusing that same already-allocated sprite.
                // uiAlertTick() (and everything it calls) is a pure
                // function of `now`/the alert's own fixed detection
                // data, so calling it twice with the same `now` is safe.
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiAlertTick(frame, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiAlertTick(frame, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiAlertTick(tft, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
            }
#else
            uiAlertTick(*canvas, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
#endif
            // The finger that opened the card has to lift before the card
            // listens: see s_alertArmed. The auto-dismiss timer below still
            // runs, so a hand left resting on the screen can't pin it open.
            if (!s_alertArmed) {
                if (!tp.valid) s_alertArmed = true;
                else           tp.valid = false;
            }

            // Same "ignore the touch that opened this until it releases"
            // gate LOG's info panel uses, applied here too -- MORE INFO
            // appears on the exact tap that opened it, so without this
            // the still-held finger could instantly dismiss it.
            if (s_infoPending) {
                if (!s_infoArmed) {
                    if (!tp.valid) s_infoArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                           Theme::infoPanelHitDismiss(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    if (s_infoShowingPrimer) {
                        // First page done -- move straight to this
                        // alert's own explanation rather than closing,
                        // and only mark the primer seen once its page
                        // has actually been read past.
                        Settings::markInfoPrimerShown();
                        s_infoShowingPrimer = false;
                        s_infoArmed = false;
                    } else {
                        // GOT IT on the actual explanation (not the
                        // primer) closes the whole alert, straight back
                        // to CLEAR -- same as tapping anywhere else on
                        // the alert screen. Previously this just closed
                        // the info panel back to the plain ALERT screen,
                        // which read as an extra, redundant tap-to-
                        // dismiss step once you'd already read the
                        // explanation.
                        s_infoPending = false;
                        muleskinCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                        enterClear();
                    }
                }
                break;
            }

            // Locked: an alert is for looking at, not acting on. MORE INFO,
            // IGNORE and HUNT would all change the device without the PIN, so
            // any tap -- or the timeout -- goes back to the lock screen.
            if (Security::locked()) {
                if ((tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) ||
                    (now - alertStart) > ALERT_AUTO_DISMISS_MS) {
                    lastTouch = now;
                    enterClear();       // the lock screen, while locked
                }
                break;
            }
            // A SNOOZE time being tapped through. Once picking has started
            // the whole card is the SNOOZE button: a tap anywhere is the next
            // step (a second tap a few pixels off the button used to close
            // the card, upstream 2026-10-22). It locks in only when the taps
            // stop: whatever the button shows then is what you get.
            if (s_snoozeStep) {
                const uint32_t since = now - s_snoozeTapAt;
                if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    lastTouch     = now;
                    s_snoozeStep  = (uint8_t)(s_snoozeStep % snoozeSteps() + 1);
                    s_snoozeTapAt = now;
                    s_alertArmed  = false;
                    Serial.printf("[snooze] tap -> step %u\n", (unsigned)s_snoozeStep);
                    uiAlertSetSnoozePick(SNOOZE_STEP[s_snoozeStep - 1].label, 1000);
                    break;
                }
                if (since >= SNOOZE_PICK_MS) {
                    const SnoozeStep& st = SNOOZE_STEP[s_snoozeStep - 1];
                    char sub[28];
                    if (st.ms) {
                        IgnoreList::snoozeFor(s_alertMac, st.ms);
                        snprintf(sub, sizeof sub, "This one, %s", st.toast);
                    } else {
                        const uint8_t mh = NightMode::morningHour(Settings::nightMode());
                        IgnoreList::snoozeFor(s_alertMac, NightMode::msUntilHour(Clock::hour(), Clock::minute(), mh));
                        const uint8_t h12 = mh % 12 ? mh % 12 : 12;
                        snprintf(sub, sizeof sub, "This one, until %u %s", (unsigned)h12, mh < 12 ? "AM" : "PM");
                    }
                    Serial.printf("[snooze] locked: %s\n", st.label);
                    s_snoozeStep = 0;
                    lastTouch = now;
                    Theme::showToast("SNOOZED", sub, Theme::AMBER);
                    muleskinCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterClear();
                    break;
                }
                uiAlertSetSnoozePick(SNOOZE_STEP[s_snoozeStep - 1].label,
                                     (uint16_t)(1000 - since * 1000 / SNOOZE_PICK_MS));
            }
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                if (uiAlertHitMoreInfo(tp.x, tp.y, tft.width(), tft.height())) {
                    s_confirmType        = lastAlertType;
                    memcpy(s_confirmVendor, s_alertVendor, sizeof s_confirmVendor);
                    memcpy(s_confirmName,   s_alertName,   sizeof s_confirmName);
                    s_infoShowingPrimer  = !Settings::infoPrimerShown();
                    s_infoPending        = true;
                    s_infoArmed          = false;
#if SQW_RING
                } else if (uiAlertHitRing(tp.x, tp.y, tft.width(), tft.height())) {
                    // The card stays up while the tracker is asked to beep,
                    // and its timer starts again so it does not close
                    // mid-ring. The answer arrives as a toast (see loop()).
                    alertStart = now;
                    if (TagRing::busy()) {
                        Theme::showToast("RINGING...", "Still asking it", Theme::AMBER);
                    } else if (TagRing::start(engine, s_alertMac)) {
                        uiAlertSetRinging(true);
                        Theme::showToast("RINGING...", "Asking it to beep", Theme::AMBER);
                        Serial.println("[ring] from the alert");
                    } else {
                        Theme::showToast("CAN'T RING", "Bluetooth is busy", Theme::RED);
                    }
#endif
                } else if (uiAlertHitIgnore(tp.x, tp.y, tft.width(), tft.height())) {
                    // Toggle, not just add: the button reads MUTED once the
                    // device is on the list, so tapping it again has to be
                    // the way back off. Fires the DETECTION trigger for the
                    // same reason HUNT does below -- leaving by this route
                    // is still the user acknowledging the alert.
                    lastTouch = now;
                    if (IgnoreList::contains(s_alertMac)) IgnoreList::remove(s_alertMac);
                    else                                  IgnoreList::add(s_alertMac, lastAlertType);
                    muleskinCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterClear();
                } else if (uiAlertHitSnooze(tp.x, tp.y, tft.width(), tft.height())) {
                    // This device, for a while: no more alerts from it,
                    // while it goes on being scanned, counted and logged like
                    // anything IGNOREd. Each tap steps the time up (10 MIN,
                    // 30 MIN, 1 HOUR, 4 HOURS, round again) and the card
                    // stays up; SNOOZE_PICK_MS after the last tap the pick
                    // locks in (above). No ALWAYS step: IGNORE is the forever
                    // button, and it can be seen and undone from its list.
                    lastTouch = now;
                    s_snoozeStep  = 1;
                    s_snoozeTapAt = now;
                    s_alertArmed  = false;   // the next step needs a fresh tap, not a held finger
                    Serial.println("[snooze] first tap -> step 1");
                    uiAlertSetSnoozePick(SNOOZE_STEP[0].label, 1000);
                } else if (uiAlertHitHunt(tp.x, tp.y, tft.width(), tft.height()) &&
                           refuseFollowMotion(lastAlertType)) {
                    lastTouch = now;
                } else if (uiAlertHitHunt(tp.x, tp.y, tft.width(), tft.height())) {
                    // Same call pair LOG's confirm panel makes. The
                    // DETECTION trigger fires here too: leaving via HUNT
                    // is still the user acknowledging this alert, and
                    // without it a detection chased straight from the
                    // alert would never register with MuleSkin at all --
                    // the only other exits (tap-to-dismiss, and GOT IT
                    // on the info panel) both fire it.
                    if (s_alertIsBle) engine.huntBle(s_alertMac, s_alertLabel);
                    else              engine.huntWifi(s_alertMac, s_alertLabel);
                    muleskinCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterHunt();
                } else {
                    muleskinCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterClear();
                }
            } else if ((now - alertStart) > ALERT_AUTO_DISMISS_MS && !s_snoozeStep) {
                muleskinCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                enterClear();
            }
            break;
        }
        case AppState::OUTFIT_UNLOCK: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick the other
                // full-screen states use. This one draws MuleSkin, so
                // `advance` would matter -- except uiOutfitUnlockTick()
                // drives him through drawWaving(), which is a pure
                // function of `now` with no state to double-advance.
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiOutfitUnlockTick(frame, now, engine);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiOutfitUnlockTick(frame, now, engine);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiOutfitUnlockTick(tft, now, engine);
            }
#else
            uiOutfitUnlockTick(*canvas, now, engine);
#endif
            const bool timedOut = (now - outfitUnlockStart) > OUTFIT_UNLOCK_AUTO_MS;
            const bool tapped   = tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                                  uiOutfitUnlockDismissable(now);
            if (tapped || timedOut) {
                if (tapped) lastTouch = now;
                // Straight into the next one if two were earned at once,
                // rather than bouncing through CLEAR between them.
                if (!maybeEnterOutfitUnlock()) enterClear();
            }
            break;
        }
        case AppState::WATCH_ALERT: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR/BOOT/
                // ALERT use -- unlike plain uiAlertTick(), this one
                // does draw MuleSkin, so advance has to gate his state
                // mutation to exactly one of the two passes, same as
                // uiClearTick()/uiBootTick().
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiWatchAlertTick(frame, now, engine, true);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiWatchAlertTick(frame, now, engine, false);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiWatchAlertTick(tft, now, engine, true);
            }
#else
            uiWatchAlertTick(*canvas, now, engine, true);
#endif
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                // The button ends the watch; anywhere else just dismisses the
                // alert and leaves it running. Both land back on CLEAR.
                const bool remove = uiWatchAlertHitRemove(*canvas, tp.x, tp.y);
                if (remove) {
                    engine.clearWatch();
                    Theme::showToast("UNWATCHED", nullptr, Theme::CYAN);
                }
                // Which of the two a tap was, and where it landed: a REMOVE
                // that reads as a plain close leaves the watch running, and
                // from the wrist the two look the same until it pops up again.
                Serial.printf("[watch] tap at %d,%d: %s\n", (int)tp.x, (int)tp.y,
                              remove ? "REMOVE, no longer watching" : "closed, still watching");
                enterClear();
            }
            // No timeout: the thing you asked to be told about is here, and
            // the alert stays until you have seen it and tapped.
            break;
        }
        case AppState::LOG: {
            const char* infoText = s_infoShowingPrimer
                                  ? DetectionInfo::rssiConfidencePrimer()
                                  : DetectionInfo::explainFor(s_confirmType, s_confirmVendor, privName(s_confirmName), engine);

            // No heading during the primer page -- it's about RSSI/
            // confidence in general, not any one detection type.
            const char* infoTypeName = s_infoShowingPrimer ? nullptr
                                     : DetectionInfo::titleFor(s_confirmType, s_confirmVendor, privName(s_confirmName));
            // Nothing on LOG moves by the call -- the note about
            // drawActiveBackground in ui_log.cpp is a comment, not a call.
            drawTwoBand([&](TFT_eSPI& t, bool) {
                uiLogTick(t, now, engine, 0, s_confirmPending, confirmLabelOnScreen(),
                          s_infoPending, infoTypeName, infoText,
                          engine.isWatched(s_confirmMac, s_confirmIsBle),
                          engine.isHunted(s_confirmMac, s_confirmIsBle),
                          IgnoreList::contains(s_confirmMac));
                Theme::drawToast(t, now);
            });

            // Opened from TERMINAL MODE: once its panel (and INFO, if that
            // came next) is closed, straight back to the terminal.
            if (s_logFromTerminal && !s_confirmPending && !s_infoPending) {
                s_logFromTerminal = false;
                enterClear();
                break;
            }

            // Same "ignore the touch that opened this until it releases"
            // gate s_confirmArmed uses on the confirm panel, applied to
            // the info panel's own GOT IT button -- it appears mid-hold
            // on the exact same MORE INFO tap that opened it, so without
            // this the still-held finger could instantly dismiss it.
            if (s_infoPending) {
                if (!s_infoArmed) {
                    if (!tp.valid) s_infoArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                           Theme::infoPanelHitDismiss(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    if (s_infoShowingPrimer) {
                        // First page done -- move straight to this
                        // target's own explanation rather than closing,
                        // and only mark the primer seen once its page
                        // has actually been read past.
                        Settings::markInfoPrimerShown();
                        s_infoShowingPrimer = false;
                        s_infoArmed = false;
                    } else {
                        s_infoPending = false;
                    }
                }
                break;
            }

            // The confirm panel is modal: while it's up, a tap only
            // ever means WATCH, HUNT, INFO, or CANCEL on it, nothing
            // else on this screen (the button bar, another long-press,
            // scrolling) is reachable underneath it.
            if (s_confirmPending) {
                if (!s_confirmArmed) {
                    // Still the same touch that opened the panel --
                    // ignore it until it's released (see s_confirmArmed's
                    // comment) so it can't register as an instant tap.
                    if (!tp.valid) s_confirmArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    LogConfirmTap ctap = uiLogHitConfirm(tp.x, tp.y, tft.width(), tft.height());
                    if ((ctap == LogConfirmTap::WATCH || ctap == LogConfirmTap::HUNT) &&
                        refuseFollowMotion(s_confirmType)) {
                        lastTouch = now;
                        s_confirmPending = false;
                    } else if (ctap == LogConfirmTap::WATCH) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // Toggling like IGNORE beside it -- the only way to
                        // end a watch that isn't a reboot or the wipe.
                        if (engine.isWatched(s_confirmMac, s_confirmIsBle)) {
                            engine.clearWatch();
                            Theme::showToast("UNWATCHED", nullptr, Theme::CYAN);
                        } else if (s_confirmIsBle) {
                            engine.watchBle(s_confirmMac, s_confirmLabel);
                        } else {
                            engine.watchWifi(s_confirmMac, s_confirmLabel);
                        }
                    } else if (ctap == LogConfirmTap::IGNORE) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // Toggling, so the toast has to say which way it went --
                        // "IGNORED" after un-ignoring would be worse than no
                        // feedback at all.
                        const bool wasOn = IgnoreList::contains(s_confirmMac);
                        if (wasOn) IgnoreList::remove(s_confirmMac);
                        else       IgnoreList::add(s_confirmMac, s_confirmType);
                        Theme::showToast(wasOn ? "UN-IGNORED" : "IGNORED",
                                         detectionTypeName(s_confirmType),
                                         Theme::colorFor(s_confirmType));
                    } else if (ctap == LogConfirmTap::HUNT) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // Toggles, same as WATCH above it. Stopping a hunt
                        // stays here: HUNT MODE is somewhere to GO, and there
                        // is nowhere to go once the target is gone.
                        if (engine.isHunted(s_confirmMac, s_confirmIsBle)) {
                            engine.clearHunt();
                            Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                        } else {
                            if (s_confirmIsBle) engine.huntBle(s_confirmMac, s_confirmLabel);
                            else                engine.huntWifi(s_confirmMac, s_confirmLabel);
                            enterHunt();
                        }
                    } else if (ctap == LogConfirmTap::INFO) {
                        lastTouch = now;
                        s_confirmPending = false;
                        s_infoShowingPrimer = !Settings::infoPrimerShown();
                        s_infoPending = true;
                        s_infoArmed   = false;
                    } else if (ctap == LogConfirmTap::CANCEL) {
                        lastTouch = now;
                        s_confirmPending = false;
#if SQW_RING
                    } else if (ctap == LogConfirmTap::RING) {
                        // The panel closes; the answer comes as a toast over
                        // the LOG when the tracker has been asked (loop()).
                        lastTouch = now;
                        s_confirmPending = false;
                        if (TagRing::busy()) {
                            Theme::showToast("RINGING...", "Still asking it", Theme::AMBER);
                        } else if (TagRing::start(engine, s_confirmMac)) {
                            uiLogSetRinging(true);
                            Theme::showToast("RINGING...", "Asking it to beep", Theme::AMBER);
                            Serial.println("[ring] from the LOG");
                        } else {
                            Theme::showToast("CAN'T RING", "Bluetooth is busy", Theme::RED);
                        }
#endif
                    }
                }
                break;
            }

            // Tap commits on release, not on press, and only if the
            // touch never moved past the scroll threshold -- firing on
            // press meant a swipe that started on a button/row acted on
            // it instantly, before the drag had any chance to be
            // recognized as a scroll instead. Same tracked-across-
            // frames shape as MuleSkin's own PETTED/HELD gesture and the
            // CLR-hold costume unlock.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
                lastY = tp.y;
                gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiLogScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    ButtonId b = Theme::hitTestButtonBar(gestureStartX, gestureStartY, tft.width(), tft.height());
                    if (b == ButtonId::CLR)  {
                        engine.clearLog();
                        BlackBox::markCleared();   // or a restart brings it all back
                        MuleSkin::trigger(MuleSkin::Event::LOG_CLEARED);
                        enterClear();
                    }
                    if (b == ButtonId::LOG)  { enterClear(); }   // toggle off
                }
                gestureActive = false;
            }

            // Long-press a log entry to bring up the WATCH/HUNT/
            // CANCEL panel -- disambiguated from the drag-to-scroll
            // gesture above by requiring the touch to stay roughly still past a
            // hold threshold (same pattern CLEAR uses for petting
            // MuleSkin). BLE vs WiFi is inferred from channel: postBle()
            // always leaves it 0 (see detection.h), every WiFi-sourced
            // entry (including DEAUTH) carries the real 1..13 channel
            // it was captured on.
            static bool     rowHoldFired = false;
            static uint32_t rowHoldStart = 0;
            static int      rowHoldX = 0, rowHoldY = 0;
            constexpr uint32_t ROW_HOLD_MS    = 500;
            constexpr int32_t  ROW_MOVE_PX_SQ = 12 * 12;
            if (touchJustDown) {
                rowHoldFired = false;
                rowHoldStart = now;
                rowHoldX = tp.x;
                rowHoldY = tp.y;
            }
            if (tp.valid && !rowHoldFired) {
                int32_t hdx = tp.x - rowHoldX, hdy = tp.y - rowHoldY;
                if ((hdx * hdx + hdy * hdy) <= ROW_MOVE_PX_SQ && (now - rowHoldStart) >= ROW_HOLD_MS) {
                    int row = uiLogRowAt(*canvas, tp.x, tp.y, tft.width(), tft.height());
                    // Through the LOG screen's own accessor: past the rows
                    // held in RAM it is reading the black box, and a long
                    // press on one of those has to reach the same device.
                    const Detection* d = (row >= 0) ? uiLogRow(engine, row) : nullptr;
                    if (d) {
                        rowHoldFired = true;
                        openConfirmFor(d);
                    }
                }
            }
            break;
        }
        case AppState::SETTINGS: {
            // Nothing on this screen moves by the call -- no background, no
            // mascot -- so both passes are the same picture and the row
            // hashes find nothing to send once it has settled.
            drawTwoBand([&](TFT_eSPI& t, bool) { uiSettingsTick(t, now, engine); });
            // Row taps commit on release, not on press, and only if
            // the touch never moved past the scroll threshold -- same
            // fix as LOG: firing on press meant a swipe that
            // started on a row acted on it instantly, before the drag
            // could be recognized as a scroll instead.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            // A tap is short. A thumb that rests on a row for half a second
            // was reaching for the list to scroll it, not choosing the row --
            // people kept opening things they were only scrolling past -- so
            // a long touch that never moved is spent, not acted on. Buttons
            // that need a hold (nothing on this screen) are unaffected.
            static uint32_t gestureDownMs = 0;
            // While a confirm panel is up it owns the screen: it answers the
            // tap, and the list underneath neither scrolls nor acts. Handled
            // before the gesture machinery rather than inside it so a drag
            // that starts on the panel cannot scroll the list out from under
            // the question being asked.
            static int confirmDownX = 0, confirmDownY = 0;
            if (uiSettingsConfirmRow() != SettingsRow::NONE) {
                // Where the finger went DOWN, not where it came up: tp is not
                // reliable on the release edge, which is why the row hit test
                // below keeps its own start coordinates too.
                if (touchJustDown) { confirmDownX = tp.x; confirmDownY = tp.y; }
                if (touchJustUp) {
                    lastTouch = now;
                    const SettingsRow pending = uiSettingsConfirmRow();
                    const SettingsConfirmTap ct =
                        uiSettingsHitConfirm(confirmDownX, confirmDownY, tft.width(), tft.height());
                    if (ct == SettingsConfirmTap::CONFIRM) {
                        uiSettingsSetConfirm(SettingsRow::NONE);
                        if (pending == SettingsRow::CALIBRATE) {
                            runTouchCalibration();
                            enterSettings();
                        } else if (pending == SettingsRow::RESET_STATS) {
                            engine.resetLifetime();
                            Dex::reset();
                            Regulars::reset();
                        } else if (pending == SettingsRow::REPLAY_INTRO) {
                            MuleSkin::replayIntro();
                            enterClear();
                        }
                    } else if (ct == SettingsConfirmTap::CANCEL) {
                        uiSettingsSetConfirm(SettingsRow::NONE);
                    }
                }
                gestureActive = false;
                break;
            }
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
                lastY = tp.y;
                gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiSettingsScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs > TAP_MAX_MS) {
                    gestureActive = false;
                    break;
                }
                if (!gestureMoved) {
                    lastTouch = now;
                    // The pinned strip along the bottom: up a level from a
                    // sub-page, out of Settings from the main list. Reachable
                    // from anywhere in the list, which is the point of it.
                    if (uiSettingsTapPinnedBack(*canvas, gestureStartX, gestureStartY,
                                                 tft.width(), tft.height())) {
                        if (uiSettingsCurrentPage() != SettingsPage::MAIN)
                            uiSettingsOpenPage(SettingsPage::MAIN);
                        else
                            enterClear();
                        gestureActive = false;
                        break;
                    }
                    // Headings are labels. They used to fold their group away,
                    // which nobody found on purpose and everybody found by
                    // accident; a tap on one lands on no row and does nothing.
                    SettingsRow row = uiSettingsHitTest(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height());
                    // Switched off by a mode: say so, rather than doing nothing
                    // and reading as a broken row.
                    if (uiSettingsRowIsOff(row)) {
                        Theme::showToast("TERMINAL MODE IS ON", nullptr, Theme::CYAN);
                        gestureActive = false;
                        break;
                    }
                    switch (row) {
                        case SettingsRow::SYSTEM:
                            uiSettingsOpenPage(SettingsPage::SYSTEM);
                            break;
                        // Tapping a tracking row stops it. This and the pill on
                        // CLEAR are the only two ways to end a watch short of a
                        // reboot; before either existed there were none.
                        case SettingsRow::WATCH_TARGET:
                            engine.clearWatch();
                            Theme::showToast("WATCH STOPPED", nullptr, Theme::CYAN);
                            break;
                        case SettingsRow::HUNT_TARGET:
                            engine.clearHunt();
                            Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                            break;
                        case SettingsRow::THEME:      Settings::cyclePalette(); break;
                        case SettingsRow::BACKGROUND: Settings::cycleBackground(); break;
                        case SettingsRow::BACKGROUND_LOCK: Settings::toggleBackgroundLocked(); break;
                        case SettingsRow::UPDATE_CHECK:    Settings::toggleUpdateCheck();     break;
                        case SettingsRow::AUTO_TIME:       Settings::toggleAutoTime();        break;
                        case SettingsRow::NIGHT_MODE:      Settings::cycleNightMode();        break;
                        case SettingsRow::NIGHT_POWER:     Settings::cycleNightPower();       break;
                        case SettingsRow::AUTO_UPDATE:     Settings::cycleAutoUpdate();       break;
                        case SettingsRow::PUSH_ALERTS:
                            // Set up from the flasher page, where a URL and
                            // a token are typed on a real keyboard.
                            if (!Settings::pushUrl()[0])
                                Theme::showToast("NOT SET UP YET", "flasher.oillie.cloud: PUSH ALERTS", Theme::AMBER);
                            else
                                Settings::togglePush();
                            break;
                        case SettingsRow::PUSH_WHAT:  Settings::cyclePushWhat();          break;
                        case SettingsRow::PUSH_NIGHT: Settings::togglePushNightWithYou(); break;
                        case SettingsRow::PUSH_HOME:  Settings::cyclePushHome();          break;
                        case SettingsRow::PUSH_DAILY: Settings::togglePushSummary();      break;
                        case SettingsRow::UPDATE_CHANNEL:
                            Settings::toggleLabChannel();
                            Theme::showToast(Settings::labChannel() ? "UPDATES: LAB" : "UPDATES: STABLE",
                                             Settings::labChannel() ? "Test builds first" : "Releases only", Theme::CYAN);
                            break;
                        case SettingsRow::TIME_ZONE:
                            // Left half back, right half forward -- see the row's label.
                            Settings::stepTimeZone(gestureStartX < tft.width() / 2 ? -1 : 1);
                            Settings::markTimeZoneChosen();
                            break;
                        case SettingsRow::INVERT:
                            Settings::toggleInvert();
                            // XOR against the panel's own baseline, not an
                            // absolute call -- see PANEL_NEEDS_INVERSION.
                            tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
                            break;
                        case SettingsRow::RGB_SWAP:
                            Settings::toggleRgbSwap();
                            applyColorOrder();
                            break;
                        case SettingsRow::ROTATION_LOCK: Settings::toggleRotationLock(); break;
                        case SettingsRow::BRIGHTNESS: {
                            // Only the row's - and + buttons move it; a tap on
                            // the label or the number does nothing.
                            const int d = uiSettingsStepHit(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height());
                            if (d) { Settings::adjustBrightness((int8_t)(d * 16)); applyBrightness(); }
                            break;
                        }
                        case SettingsRow::CONFIDENCE: Settings::cycleMinConfidence(); break;
                        case SettingsRow::AUTO_QUIET:  Settings::cycleAutoQuiet(); break;
#if SQW_HAS_BUZZER
                        case SettingsRow::BUZZER:
                            // Switching it ON plays the chirp once, the job the
                            // light's TEST row does: the owner hears what they
                            // signed up for before a camera does. OFF is silent.
                            Settings::toggleBuzzer();
                            if (Settings::buzzerOn()) Buzzer::chirp(BUZZ_CHIRP_MS);
                            break;
#endif
#if SQW_WIFI_5G
                        case SettingsRow::WIFI_BANDS:
                            Settings::setWifi5(!Settings::wifi5());
                            setWifi5Enabled(Settings::wifi5());   // from the next sweep
                            break;
#endif
                        case SettingsRow::DETECTION_FILTER: enterDetFilter(); break;
                        case SettingsRow::POWER_SAVER: enterPower(); break;
                        case SettingsRow::PRIVACY:
                            Settings::togglePrivacyMode();
                            Theme::showToast(Settings::privacyMode() ? "PRIVACY MODE ON" : "PRIVACY MODE OFF",
                                             Settings::privacyMode() ? "Addresses and names are hidden" : nullptr, Theme::CYAN);
                            break;
#if defined(ESP32) && !defined(TWATCH_S3)
                        case SettingsRow::CHARGE_MODE: s_chargeWanted = true; break;
                        case SettingsRow::LAST_RUN: break;   // a reading, not a switch
#endif
#if defined(TWATCH_S3)
                        case SettingsRow::WATCH_RADIO: Settings::cycleRadioDuty(); break;
                        case SettingsRow::WATCH_LISTEN: Settings::cycleBleListen(); break;
                        case SettingsRow::WATCH_IDLE_CPU: Settings::cycleIdleCpu(); applyCpuClock(); break;
                        case SettingsRow::WATCH_BATTERY: break;   // a reading, not a switch
                        case SettingsRow::WATCH_TEMP: break;   // a reading, not a switch
                        case SettingsRow::WATCH_SETTINGS: uiSettingsOpenPage(SettingsPage::WATCH); break;
                        case SettingsRow::WATCH_QUIET_TAGS:
                            Settings::toggleQuietTrackers();
                            Theme::showToast("TAGS + RINGS", Settings::quietTrackers() ? "Logged, no wake or buzz" : "Alert like the rest",
                                             Theme::CYAN);
                            break;
#if MULESKIN_LORA
                        case SettingsRow::WATCH_LORA:
                            Settings::cycleLoraListen();
                            Lora::applyListen(Settings::loraListen());
                            Theme::showToast("LORA", Settings::loraListenName(), Theme::CYAN);
                            break;
                        case SettingsRow::WATCH_LORA_CHATS: enterLoraChat(); break;
#endif
                        case SettingsRow::WATCH_WARDRIVE:
                            // Through the console's own path, so the row and
                            // WARDRIVE ON/OFF can never disagree about what
                            // switching it does (the GPS goes with it).
                            g_consoleGps = Wardrive::enabled() ? 6 : 5;
                            Theme::showToast(Wardrive::enabled() ? "WARDRIVE OFF" : "WARDRIVE ON",
                                             Wardrive::enabled() ? nullptr : "Logging once the GPS has a fix",
                                             Theme::CYAN);
                            break;
                        case SettingsRow::WATCH_BUZZ:
                            // OFF, HIGH, MED, LOW. Each level plays the alert
                            // pattern once, so the choice is made by feel; OFF
                            // stays silent, so the tap never feels like a test
                            // that passed while alerts are actually off.
                            Settings::cycleBuzz();
                            if (Settings::buzz()) {
                                drvWrite(0x01, 0x00);
                                twatchHapticApply();
                                twatchBuzz(Buzz::SAMPLE);
                            }
                            Serial.printf("[buzz] %s\n", Settings::buzzModeName());
                            break;
#endif
#if defined(CYD_GPS)
                        case SettingsRow::WATCH_WARDRIVE:
                            // Through the console's path, as on the watch.
                            g_consoleGps = Wardrive::enabled() ? 6 : 5;
                            Theme::showToast(Wardrive::enabled() ? "WARDRIVE OFF" : "WARDRIVE ON",
                                             Wardrive::enabled() ? nullptr
                                                 : !engine.sd().ready() ? "Needs an SD card"
                                                 : "Logging once the GPS has a fix",
                                             Theme::CYAN);
                            break;
#endif
                        case SettingsRow::STATUS_LIGHT: enterLight(); break;
                        case SettingsRow::SECURITY:    enterSecurity(); break;
                        case SettingsRow::IGNORED_DEVICES:  enterIgnoreList(); break;
                        case SettingsRow::MY_ALERTS:        enterMyAlerts(); break;
#if defined(SENSE_CYD_APP)
                        case SettingsRow::SENSING:
                            // Opens HUMAN SENSING; the detector pauses while it is up.
                            Theme::showToast("HUMAN SENSING", "Detector paused while this is open", Theme::CYAN);
                            enterSensing();
                            break;
                        case SettingsRow::SENSE_ARM: {
                            Settings::cycleSenseArm();
                            const uint8_t a = Settings::senseArm();
                            Theme::showToast(a == SenseMotion::ARM_OFF ? "SENSE ARM OFF" : "SENSE ARM",
                                             a == SenseMotion::ARM_OFF   ? "Sensing only on its screen"
                                             : !OtaWifi::savedCount()    ? "Needs a saved WiFi network"
                                             : a == SenseMotion::ARM_NIGHT ? "At night: sensing, detector paused"
                                             :                               "Always: sensing, detector paused",
                                             Theme::CYAN, 3000);
                            break;
                        }
#endif
                        // These ask first -- see the confirm panel over in
                        // ui_settings. A row earns one when tapping it a
                        // second time does not put things back: calibration
                        // overwrites the calibration you are using, reset
                        // zeroes a count that most of the outfits are gated
                        // on, and the intro takes the screen over.
                        case SettingsRow::CALIBRATE:
                        case SettingsRow::RESET_STATS:
                        case SettingsRow::REPLAY_INTRO:
                            uiSettingsSetConfirm(row);
                            break;
                        case SettingsRow::BORING_MODE:
                            // TERMINAL MODE (this was BORING MODE): no panel
                            // either way. It had one on the way in when it
                            // only hid MuleSkin's rows; TERMINAL MODE is a
                            // dashboard people pick on purpose, its row stays
                            // where it was, and the same tap puts the radar
                            // back. (Upstream v1.43.)
                            Settings::toggleBoringMode();
                            break;
                        case SettingsRow::CHECK_COLORS: enterColorCheck(true); break;
                        case SettingsRow::DIAGNOSTICS:  enterDiagnostics(); break;
                        case SettingsRow::WIFI_NETWORKS: enterWifiNets(); break;
                        case SettingsRow::UPDATE_FIRMWARE: enterUpdate(); break;
                        case SettingsRow::SHOW_OFF:
                            MuleSkin::startShowOff();
                            enterClear();
                            break;
                        case SettingsRow::SHADES_COLOR: MuleSkin::cycleShadesColor(); break;
                        case SettingsRow::MULESKIN_SIZE: Settings::cycleMuleSkinSize(); break;
                        case SettingsRow::OUTFIT:       enterOutfit(); break;
                        case SettingsRow::PET:          MuleSkin::cyclePet(); break;
                        case SettingsRow::BANTER:       Settings::cycleBanter(); break;
                        case SettingsRow::VIEW_DIARY:   enterDiary(); break;
                        case SettingsRow::DEX:          enterDex(); break;
                        case SettingsRow::APPEARANCE:  uiSettingsOpenAppearance(true); break;
                        case SettingsRow::AURA:        Settings::toggleAura(); break;
                        // From a sub-page, back to the main list; from the
                        // main list, out.
                        case SettingsRow::BACK:
                            if (uiSettingsCurrentPage() != SettingsPage::MAIN) uiSettingsOpenPage(SettingsPage::MAIN);
                            else                                               enterClear();
                            break;
                        default: break;
                    }
                }
                gestureActive = false;
            }
            break;
        }
        case AppState::UPDATE: {
            // An update in progress owns the screen. Dimming, auto-lock and
            // the idle frame cap all run off lastTouch, so holding it at now
            // keeps all three away until it is over.
            if (OtaBle::state() != OtaBle::State::OFF || OtaWifi::state() != OtaWifi::State::OFF ||
                OtaCore::restartPending()) lastTouch = now;
            {
                // Without the frame buffer every draw lands on the panel as it
                // happens, so a full redraw each frame would flicker. Redraw
                // only when something on the screen would actually change.
                // A full repaint only when the screen changes to something
                // else; while a download runs, just the bar and its numbers,
                // painted over what is already there, four times a second.
                //
                // frameBufferOk was standing in for "the drawing is buffered",
                // and on the 3.5" those are not the same thing: the buffer
                // exists but every screen except the main one drew straight at
                // the panel, so this screen took the full-repaint branch and
                // repainted the glass itself, whole, every frame. That is the
                // flicker and the diagonal tear -- the panel was scanning out
                // rows while they were still being drawn. Through the bands it
                // is buffered like everything else, and a screen that is not
                // changing sends no rows at all.
                if (frameBufferOk) {
                    drawTwoBand([&](TFT_eSPI& t, bool) { uiUpdateTick(t, now); });
                } else {
                    static uint32_t lastKey = 0xFFFFFFFFUL, lastLive = 0;
                    const uint32_t key = ((uint32_t)OtaWifi::state() << 24) | ((uint32_t)OtaBle::state() << 20) |
                                         ((uint32_t)OtaWifi::netCount() << 1) |
                                         (OtaWifi::canTryAgain() ? 2u : 0u) | (OtaCore::restartPending() ? 1u : 0u);
                    if (key != lastKey) {
                        uiUpdateTick(*canvas, now, true);
                        lastKey  = key;
                        lastLive = now;
                    } else if (now - lastLive >= 250) {
                        uiUpdateTick(*canvas, now, false);
                        lastLive = now;
                    }
                }
            }
            autoUpdateTick(now);
            // touchJustDown, not the debounce timer: lastTouch is pinned above.
            if (touchJustDown) {
                int netIndex = -1;
                switch (uiUpdateHitTest(*canvas, tp.x, tp.y, &netIndex)) {
                    case UpdateHit::WIFI_START:
                        engine.startUpdateRadio();
                        if (!OtaWifi::begin()) {
                            engine.stopUpdateRadio();
                            Theme::showToast("CAN'T START UPDATE", updateRefusedWhy(), Theme::AMBER);
                        }
                        break;
                    case UpdateHit::NETWORK: {
                        const OtaWifi::Net* n = OtaWifi::net((uint8_t)netIndex);
                        if (!n) break;
                        const int8_t k = OtaWifi::savedIndexOf(n->ssid);
                        if (k >= 0) {
                            OtaWifi::connectSavedAt((uint8_t)k);
                        } else if (n->open) {
                            OtaWifi::connect(n->ssid, "", true);
                        } else {
                            enterWifiPass(n->ssid);
                        }
                        break;
                    }
                    case UpdateHit::RESCAN:    OtaWifi::rescan();   break;
                    case UpdateHit::INSTALL:   lendFrameToDownload(); OtaWifi::install(); break;
                    case UpdateHit::TRY_AGAIN: OtaWifi::tryAgain(); break;
                    case UpdateHit::BT_START:
                        // The radio first: NimBLE will not register the update
                        // service while a scan is still running.
                        engine.startUpdateRadio();
                        if (!OtaBle::begin()) {
                            engine.stopUpdateRadio();
                            Theme::showToast("CAN'T START UPDATE", "Leave and try again", Theme::AMBER);
                        }
                        break;
                    case UpdateHit::SWITCH:        uiUpdateAskSwitch(true);  break;
                    case UpdateHit::SWITCH_CANCEL: uiUpdateAskSwitch(false); break;
                    case UpdateHit::SWITCH_CONFIRM:
                        uiUpdateAskSwitch(false);
                        if (OtaCore::switchToOther() != OtaCore::Fail::NONE)
                            Theme::showToast("CAN'T SWITCH", "That version won't start", Theme::AMBER);
                        break;
                    case UpdateHit::CANCEL:
                    case UpdateHit::OK:
                        if (OtaWifi::state() != OtaWifi::State::OFF) {
                            // end() answers false now: nothing was handed
                            // back, so detection just starts again. It kept
                            // the true-means-restart answer from when an
                            // update cost the board its Bluetooth.
                            if (!OtaWifi::end()) engine.stopUpdateRadio();
                        } else {
                            OtaBle::end();
                            engine.stopUpdateRadio();
                        }
                        uiUpdateInit(*canvas);
                        break;
                    case UpdateHit::BACK:
                        enterSettings();
                        uiSettingsOpenPage(SettingsPage::SYSTEM);
                        break;
                    default: break;
                }
            }
            break;
        }
        case AppState::WIFI_PASS: {
            // Still inside update mode, with detection paused: the same pin on
            // lastTouch keeps auto-lock from taking the screen mid-password.
            lastTouch = now;
            // The board redraws only what changed, so it is the same with the
            // frame buffer and without it (given up for a download).
            drawTwoBand([&](TFT_eSPI& t, bool) {
#if defined(CYD35)
                // This screen repaints only what changed, which needs a
                // buffer that still holds the rest. The 3.5" has half a
                // buffer shared by two bands, so there it draws everything,
                // every band: the keyboard's top half was being pushed to
                // the bottom half as well (issue #25).
                if (frameBufferOk) uiWifiPassRedrawAll();
#endif
                uiWifiPassTick(t, now);
            });
            if (touchJustDown)    uiWifiPassTouch(tp.x, tp.y, now, WifiPassTouch::DOWN);
            else if (tp.valid)    uiWifiPassTouch(tp.x, tp.y, now, WifiPassTouch::MOVE);
            else if (touchJustUp) uiWifiPassTouch(tp.x, tp.y, now, WifiPassTouch::UP);
            const WifiPassResult r = uiWifiPassResult();
            if (s_alertTyping != AlertTyping::NONE) {
                if (r != WifiPassResult::NONE) myAlertsTyped(r);
            } else if (r == WifiPassResult::OK && s_passForNets) {
                s_passForNets = false;
                const bool ok = OtaWifi::saveNetwork(uiWifiPassSsid(), uiWifiPassText());
                uiWifiPassClear();
                Theme::showToast(ok ? "SAVED" : "LIST FULL", ok ? "Checked at the next boot" : "Remove one first",
                                 ok ? Theme::CYAN : Theme::AMBER);
                enterWifiNets();
            } else if (r == WifiPassResult::BACK && s_passForNets) {
                s_passForNets = false;
                uiWifiPassClear();
                enterWifiNets();
            } else if (r == WifiPassResult::OK) {
                OtaWifi::connect(uiWifiPassSsid(), uiWifiPassText(), true);
                uiWifiPassClear();
                enterUpdate();
            } else if (r == WifiPassResult::BACK) {
                uiWifiPassClear();
                enterUpdate();
            }
            break;
        }
        case AppState::WIFI_NETS: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiWifiNetsTick(t, now); });
            if (touchJustDown) {
                int row = -1;
                switch (uiWifiNetsHit(*canvas, tp.x, tp.y, &row)) {
                    case WifiNetsHit::ROW: uiWifiNetsSelect(row); break;
                    case WifiNetsHit::USE: {
                        const int s = uiWifiNetsSelected();
                        if (s >= 0 && s < OtaWifi::savedCount()) {
                            OtaWifi::useSaved((uint8_t)s);
                            Theme::showToast("TRIED FIRST", OtaWifi::savedSsidAt((uint8_t)s), Theme::CYAN);
                        }
                        break;
                    }
                    case WifiNetsHit::REMOVE: {
                        const int s = uiWifiNetsSelected();
                        if (s >= 0 && s < OtaWifi::savedCount()) {
                            OtaWifi::removeSaved((uint8_t)s);
                            uiWifiNetsSelect(OtaWifi::savedCount() ? (int)OtaWifi::savedUse() : -1);
                            Theme::showToast("REMOVED", nullptr, Theme::CYAN);
                        }
                        break;
                    }
                    case WifiNetsHit::ADD:
                        if (OtaWifi::savedCount() >= OtaWifi::SAVED_MAX)
                            Theme::showToast("LIST FULL", "Remove one first", Theme::AMBER);
                        else
                            enterWifiAdd();
                        break;
                    case WifiNetsHit::BACK: enterSettings(); break;
                    default: break;
                }
            }
            break;
        }
#if defined(SENSE_CYD_APP)
        case AppState::SENSING: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiSensingTick(t, now, advance); });
            // A tap acts on release, where the finger went down.
            static bool sGesture = false;
            static int  sDownX = 0, sDownY = 0, sLastX = 0, sLastY = 0;
            // A touch during a movement's glance: the screen is wanted, it stays on.
            if (touchJustDown) s_senseGlanceUntil = 0;
            // Asleep (SLEEP): the touch that wakes it does nothing else.
            if (s_senseDark) { if (touchJustUp) { senseWake(); lastTouch = now; sGesture = false; } break; }
            if (touchJustDown) { sGesture = true; sDownX = sLastX = tp.x; sDownY = sLastY = tp.y; }
            if (tp.valid) { sLastX = tp.x; sLastY = tp.y; }
            if (touchJustUp && sGesture && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                sGesture = false;
#if SENSE_RICH
                // A sideways drag of 60 px or more is a swipe to the next or
                // previous screen; anything else is a tap where it went down.
                const int sdx = sLastX - sDownX, sdy = sLastY - sDownY;
                if (sdx * sdx > 60 * 60 && sdx * sdx > 2 * sdy * sdy) { uiSensingSwipe(sdx < 0 ? 1 : -1); lastTouch = now; break; }
#endif
                const SensingTap tap = uiSensingTap(sDownX, sDownY, tft.width(), tft.height(), now);
                if (tap != SensingTap::NONE) lastTouch = now;
                if (tap == SensingTap::BACK) {
                    enterSettings();
                } else if (tap == SensingTap::RECAL) {
                    senseCydCommand("cal");
                    Theme::showToast("RECALIBRATING", "Keep the room still for 15 s", Theme::AMBER);
                } else if (tap == SensingTap::ADD_WIFI) {
                    // No saved network: straight to the list to add one. Leaving
                    // SENSING hands the radio back, which the add screen's scan needs.
                    enterWifiNets();
                } else if (tap == SensingTap::SLEEP) {
                    // The screen off; sensing runs on. A tap wakes it.
                    senseSleep();
                }
            }
            break;
        }
#endif
        case AppState::MY_ALERTS: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiMyAlertsTick(t, now); });
            if (touchJustDown) {
                int slot = -1;
                const int sel = uiMyAlertsSelected();
                const bool haveSel = sel >= 0 && MyAlerts::used((uint8_t)sel);
                switch (uiMyAlertsHit(*canvas, tp.x, tp.y, &slot)) {
                    case MyAlertsHit::ROW: uiMyAlertsSelect(slot); break;
                    case MyAlertsHit::ADD:
                        if (MyAlerts::count() >= MyAlerts::MAX)
                            Theme::showToast("LIST FULL", "Remove one first", Theme::AMBER);
                        else
                            enterAlertKeyboard(AlertTyping::PATTERN, "WIFI NAME TO WATCH", "* = anything, ? = one",
                                               "", MyAlerts::PAT_MAX, false);
                        break;
                    case MyAlertsHit::LABEL:
                        if (haveSel) {
                            s_alertSlot = (uint8_t)sel;
                            enterAlertKeyboard(AlertTyping::RELABEL, "LABEL FOR", MyAlerts::pattern((uint8_t)sel),
                                               MyAlerts::label((uint8_t)sel), MyAlerts::LABEL_MAX, true);
                        }
                        break;
                    case MyAlertsHit::REMOVE:
                        if (haveSel) {
                            MyAlerts::remove((uint8_t)sel);
                            uiMyAlertsSelect(MyAlerts::nth(0));
                            Theme::showToast("REMOVED", nullptr, Theme::CYAN);
                        }
                        break;
                    case MyAlertsHit::BACK: enterSettings(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::WIFI_ADD: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiWifiAddTick(t, now, engine); });
            if (touchJustDown) {
                int row = -1;
                switch (uiWifiAddHit(*canvas, tp.x, tp.y, engine, &row)) {
                    case WifiAddHit::ROW: {
                        char ssid[33];
                        snprintf(ssid, sizeof ssid, "%s", engine.rawWifiSsid((uint8_t)row));
                        const bool open = engine.rawWifiOpen((uint8_t)row);
                        engine.stopRawScan();
                        if (!ssid[0]) {
                            Theme::showToast("HIDDEN NETWORK", "No name to save", Theme::AMBER);
                            enterWifiNets();
                        } else if (open) {
                            const bool ok = OtaWifi::saveNetwork(ssid, "");
                            Theme::showToast(ok ? "SAVED" : "LIST FULL", ok ? "Open network" : "Remove one first",
                                             ok ? Theme::CYAN : Theme::AMBER);
                            enterWifiNets();
                        } else {
                            s_passForNets = true;
                            enterWifiPass(ssid);
                        }
                        break;
                    }
                    case WifiAddHit::RESCAN: engine.stopRawScan(); engine.startRawWifiScan(); break;
                    case WifiAddHit::BACK:   engine.stopRawScan(); enterWifiNets(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::AUTO_UPDATE: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiAutoUpdateTick(t, now, engine); });
            lastTouch = now;     // no dimming, no auto-lock, mid-count
            AutoUpdateHit hit = AutoUpdateHit::NONE;
            if (touchJustDown) hit = uiAutoUpdateHit(*canvas, tp.x, tp.y);
            if (hit == AutoUpdateHit::SKIP) { Serial.println("[auto] skipped"); enterClear(); break; }
            if (hit == AutoUpdateHit::NOW || uiAutoUpdateSecondsLeft(now) <= 0) startAutoUpdate();
            break;
        }
        case AppState::IGNORE_LIST: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiIgnoreListTick(t, now); });
            // Same drag-to-scroll / act-on-release gesture the detection
            // filter uses: committing on press would make a swipe that
            // starts on a REMOVE button fire it before the drag is
            // recognised as a scroll.
            static bool ilActive = false, ilMoved = false;
            static int  ilStartX = 0, ilStartY = 0, ilLastY = -1;
            if (touchJustDown) {
                ilActive = true; ilMoved = false;
                ilStartX = tp.x; ilStartY = tp.y; ilLastY = tp.y;
            }
            if (tp.valid && ilActive) {
                int dy = tp.y - ilLastY;
                if (abs(dy) > 10) {
                    ilMoved = true;
                    uiIgnoreListScroll(dy > 0 ? -1 : 1);
                    ilLastY = tp.y;
                }
            }
            if (touchJustUp && ilActive) {
                if (!ilMoved && Theme::pinnedBackHit(ilStartX, ilStartY, tft.width(), tft.height())) {
                    ilActive = false;
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!ilMoved) {
                    uint8_t hit = uiIgnoreListHitRemove(*canvas, ilStartX, ilStartY,
                                                        tft.width(), tft.height());
                    if (hit != 0xFF) {
                        lastTouch = now;
                        const uint8_t* mac = IgnoreList::macAt(hit);
                        if (mac) {
                            // Copy first: remove() backfills the hole with
                            // the last entry, so the pointer it was read
                            // from stops meaning what it meant.
                            uint8_t tmp[6];
                            memcpy(tmp, mac, 6);
                            IgnoreList::remove(tmp);
                            uiIgnoreListScroll(0);   // re-clamp after shrink
                        }
                    }
                }
                ilActive = false;
            }
            break;
        }
        case AppState::DETECTION_FILTER: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiDetFilterTick(t, now, engine); });
            // Same drag-to-scroll / tap-on-release-to-toggle gesture
            // the Settings screen above uses, and for the same reason:
            // committing on press would make a swipe that starts on a
            // row toggle it before the drag is recognized as a scroll.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
                lastY = tp.y;
                gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiDetFilterScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    gestureActive = false;
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    DetectionType hit = uiDetFilterHitTest(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height());
                    // iBeacons ask first, on the way ON only: see
                    // ui_beaconwarn.h for why this one type does.
                    if (hit == DetectionType::IBEACON && !Settings::typeEnabled(hit)) enterBeaconWarn();
                    else if (hit != DetectionType::COUNT) Settings::toggleType(hit);
                }
                gestureActive = false;
            }
            break;
        }
        case AppState::BEACON_WARN: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiBeaconWarnTick(t, now, engine, advance); });
            if (touchJustDown) {
                switch (uiBeaconWarnHitTest(*canvas, tp.x, tp.y)) {
                    case BeaconWarnHit::ENABLE:
                        if (!Settings::typeEnabled(DetectionType::IBEACON))
                            Settings::toggleType(DetectionType::IBEACON);
                        enterDetFilter(true);
                        break;
                    case BeaconWarnHit::KEEP_OFF: enterDetFilter(true); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::SECURITY: {
            drawTwoBand([&](TFT_eSPI& t, bool) {
                uiSecurityTick(t, now, engine);
                Theme::drawToast(t, now);
            });
            // The same drag-to-scroll, act-on-release gesture as POWER SAVER.
            static bool gestureActive = false, gestureMoved = false;
            static int  gestureStartX = 0, gestureStartY = 0, lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) { gestureMoved = true; uiSecurityScroll(dy > 0 ? -1 : 1); lastY = tp.y; }
            }
            if (touchJustUp && gestureActive) {
                gestureActive = false;
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    const bool on = Security::enabled();
                    // Everything below PIN LOCK is inert until a PIN exists,
                    // and PIN LENGTH is inert once one does -- the row draws
                    // dim to match.
                    switch (uiSecurityHitTest(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height())) {
                        case SecurityRow::PIN_LOCK:
                            startPinFlow(on ? PinFlow::OFF_VERIFY : PinFlow::SET_NEW);
                            break;
                        case SecurityRow::PIN_LENGTH:
                            if (!on) {
                                const Security::PinLen n = Security::pinLen();
                                Security::setPinLength(n == Security::PinLen::FOUR ? Security::PinLen::SIX
                                                     : n == Security::PinLen::SIX  ? Security::PinLen::EIGHT
                                                                                   : Security::PinLen::FOUR);
                            }
                            break;
                        case SecurityRow::CHANGE_PIN:   if (on) startPinFlow(PinFlow::CHANGE_CUR); break;
                        case SecurityRow::DURESS_PIN:
                            if (on) startPinFlow(Security::hasDuress() ? PinFlow::DURESS_OFF : PinFlow::DURESS_CUR);
                            break;
                        case SecurityRow::AUTO_LOCK:    if (on) Security::cycleAutoLock(); break;
                        case SecurityRow::LOCK_AT_BOOT: if (on) Security::setLockAtBoot(!Security::lockAtBoot()); break;
                        case SecurityRow::WIPE_ON_FAIL: if (on) Security::setWipeOnFail(!Security::wipeOnFail()); break;
                        case SecurityRow::LOCK_ALERTS:  if (on) Security::cycleLockAlerts(); break;
                        default: break;
                    }
                }
            }
            break;
        }
        case AppState::PIN_ENTRY: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiPhoneTick(t, now, engine, advance); });
            if (touchJustDown) uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::DOWN);
            if (!uiPhoneDone()) break;
            if (!uiPhonePinReady()) { memset(s_pinFirst, 0, sizeof s_pinFirst); enterSecurity(); break; }   // BACK
            char d[9];
            strncpy(d, uiPhonePinDigits(), sizeof d - 1);
            d[sizeof d - 1] = '\0';
            const bool same = strcmp(d, s_pinFirst) == 0;
            bool done = false;
            switch (s_pinFlow) {
                case PinFlow::SET_NEW:
                case PinFlow::CHANGE_NEW:
                    if (Security::isDuress(d)) { startPinFlow(s_pinFlow, "THAT IS THE DURESS PIN"); break; }
                    memcpy(s_pinFirst, d, sizeof s_pinFirst);
                    startPinFlow(s_pinFlow == PinFlow::SET_NEW ? PinFlow::SET_AGAIN : PinFlow::CHANGE_AGAIN);
                    break;
                case PinFlow::SET_AGAIN:
                case PinFlow::CHANGE_AGAIN: {
                    const bool first = (s_pinFlow == PinFlow::SET_AGAIN);
                    if (!same) { startPinFlow(first ? PinFlow::SET_NEW : PinFlow::CHANGE_NEW, "DIDN'T MATCH - AGAIN"); break; }
                    Security::setPin(s_pinFirst);
                    // Said once, where it is set: what the lock is for, and
                    // what it is not.
                    if (first) Theme::showToast("PIN LOCK ON", "Snoops, not USB cables", Theme::AMBER);
                    else       Theme::showToast("PIN CHANGED", nullptr, Theme::GREEN);
                    done = true;
                    break;
                }
                case PinFlow::OFF_VERIFY:
                    if (!Security::verify(d)) { startPinFlow(PinFlow::OFF_VERIFY, "WRONG - CURRENT PIN"); break; }
                    Security::disable();
                    Theme::showToast("PIN LOCK OFF", nullptr, Theme::AMBER);
                    done = true;
                    break;
                case PinFlow::CHANGE_CUR:
                case PinFlow::DURESS_CUR:
                    if (!Security::verify(d)) { startPinFlow(s_pinFlow, "WRONG - CURRENT PIN"); break; }
                    startPinFlow(s_pinFlow == PinFlow::CHANGE_CUR ? PinFlow::CHANGE_NEW : PinFlow::DURESS_NEW);
                    break;
                case PinFlow::DURESS_NEW:
                    if (Security::verify(d)) { startPinFlow(PinFlow::DURESS_NEW, "MUST DIFFER FROM PIN"); break; }
                    memcpy(s_pinFirst, d, sizeof s_pinFirst);
                    startPinFlow(PinFlow::DURESS_AGAIN);
                    break;
                case PinFlow::DURESS_AGAIN:
                    if (!same) { startPinFlow(PinFlow::DURESS_NEW, "DIDN'T MATCH - AGAIN"); break; }
                    Security::setDuress(s_pinFirst);
                    Theme::showToast("DURESS PIN SET", "Wipes, then unlocks", Theme::RED);
                    done = true;
                    break;
                case PinFlow::DURESS_OFF:
                    if (!Security::verify(d)) { startPinFlow(PinFlow::DURESS_OFF, "WRONG - CURRENT PIN"); break; }
                    Security::clearDuress();
                    Theme::showToast("DURESS PIN OFF", nullptr, Theme::AMBER);
                    done = true;
                    break;
            }
            memset(d, 0, sizeof d);
            if (done) { memset(s_pinFirst, 0, sizeof s_pinFirst); enterSecurity(); }
            break;
        }
        case AppState::LOCKED: {
            // Detection runs on underneath, and ALERTS WHEN LOCKED decides how
            // much of a new one reaches the glass. The same test CLEAR makes.
            {
                const Security::LockAlerts la = Security::lockAlerts();
                const Detection* latest = freshCatch(now);
                if (la != Security::LockAlerts::NONE && latest &&
                    latest->conf >= Settings::minConfidence() && !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiAlertSetRedacted(la == Security::LockAlerts::TYPE_ONLY);
                    enterAlert(*latest);
                    break;
                }
                if (la == Security::LockAlerts::FULL && engine.watchHitPending()) { enterWatchAlert(); break; }
            }
            const uint32_t wait = Security::lockoutRemainingMs(now);
            static char waitMsg[24];
            if (wait) {
                snprintf(waitMsg, sizeof waitMsg, "WAIT %lu s", (unsigned long)((wait + 999) / 1000));
                uiPhonePinWait(waitMsg);
            } else {
                uiPhonePinWait(nullptr);
            }
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiPhoneTick(t, now, engine, advance); });
            if (touchJustDown) uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::DOWN);
            if (uiPhonePinForgot()) {
                // A forgotten PIN: every secret goes, and the PIN with it, and
                // the board comes back unlocked. Settings and outfits stay.
                Security::disable();
                performWipe(WipeBoot::UNLOCKED);
                break;
            }
            if (uiPhoneDone() && uiPhonePinReady()) {
                switch (Security::check(uiPhonePinDigits(), now)) {
                    case Security::Check::OK:
                        uiAlertSetRedacted(false);
                        enterClear();
                        break;
                    case Security::Check::DURESS: performWipe(WipeBoot::UNLOCKED); break;
                    case Security::Check::WIPED:  performWipe(WipeBoot::LOCKED);   break;
                    default:                      uiPhonePinReject();              break;
                }
            }
            break;
        }
        case AppState::POWER_SAVER: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiPowerTick(t, now, engine); });
            // Same drag-to-scroll, commit-on-release gesture the Settings and
            // type-filter lists use, and for the same reason: committing on
            // press turns a swipe that happens to start on a row into a
            // toggle before the drag is ever recognised as a scroll.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiPowerScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    gestureActive = false;
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    PowerRow hit = uiPowerHitTest(*canvas, gestureStartX, gestureStartY,
                                                  tft.width(), tft.height());
                    switch (hit) {
                        case PowerRow::ENABLED:
                            Settings::togglePowerSaver();
                            // Turning it off has to undo whatever it was
                            // doing, immediately and on this frame -- leaving
                            // a dimmed screen behind after switching the
                            // feature off would read as a bug.
                            applyCpuClock();
                            if (s_screenDimmed) { s_screenDimmed = false; applyBrightness(); }
                            break;
                        case PowerRow::SCREEN_TIMEOUT: Settings::cycleScreenTimeout(); break;
                        case PowerRow::DIM_LEVEL:
                            // Left half darker, right half brighter, the same
                            // split the BRIGHTNESS row uses.
                            Settings::adjustDimLevel(gestureStartX < tft.width() / 2 ? -8 : 8);
                            if (s_screenDimmed) applyBrightness();   // show it live
                            break;
                        case PowerRow::IDLE_FPS:      Settings::cycleIdleFps(); break;
                        case PowerRow::IDLE_AFTER:    Settings::cycleIdleAfter(); break;
                        case PowerRow::CPU_CLOCK:
                            Settings::cycleCpuMhz();
                            applyCpuClock();
                            break;
                        case PowerRow::WAKE_ON_ALERT: Settings::toggleWakeOnAlert(); break;
#if defined(TWATCH_S3)
                        case PowerRow::RADIO_DUTY:    Settings::cycleRadioDuty(); break;
#endif
#if defined(TWATCH_S3)
#endif
                        default: break;
                    }
                }
                gestureActive = false;
            }
            break;
        }
#if MULESKIN_LORA
        case AppState::LORA_CHAT: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiLoraChatTick(t, now); });
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                const int dy = tp.y - lastY;
                if (abs(dy) > 24) {
                    gestureMoved = true;
                    uiLoraChatScroll(dy > 0 ? -1 : 1);   // drag down: newer
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                gestureActive = false;
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    if (Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                        enterSettings();
                        uiSettingsOpenPage(SettingsPage::WATCH);
                        break;
                    }
                    const LoraChatHit hit = uiLoraChatHit(*canvas, gestureStartX, gestureStartY);
                    if (hit == LoraChatHit::TAB_MESHTASTIC) uiLoraChatSelect(0);
                    else if (hit == LoraChatHit::TAB_MESHCORE) uiLoraChatSelect(1);
                }
            }
            break;
        }
#endif
        case AppState::STATUS_LIGHT: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiLightTick(t, now, engine); });
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiLightScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    gestureActive = false;
                    lastTouch = now;
                    enterSettings();
                    uiSettingsOpenAppearance(true);
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    LightRow hit = uiLightHitTest(*canvas, gestureStartX, gestureStartY,
                                                  tft.width(), tft.height());
                    switch (hit) {
                        case LightRow::ENABLED:    Settings::toggleLight(); break;
                        case LightRow::ALERTS:     Settings::toggleLightAlerts(); break;
                        case LightRow::IDLE:       Settings::cycleLightIdle(); break;
                        case LightRow::IDLE_COLOR: Settings::cycleLightColor(); break;
                        case LightRow::BRIGHTNESS: Settings::cycleLightBrightness(); break;
                        case LightRow::TEST:       StatusLight::test(now); break;
                        default: break;
                    }
                }
                gestureActive = false;
            }
            break;
        }
        case AppState::DIAGNOSTICS: {
            DiagnosticsInfo info;
            {
                int16_t a = 0, b = 0;
                info.hasRaw = true;
                info.rawTouching = readTouchRaw(a, b);
                info.rawA = a;
                info.rawB = b;
                info.usingSavedCal = s_calSource == CalSource::SAVED;
                info.calSource = s_calSource == CalSource::SAVED     ? "saved"
                               : s_calSource == CalSource::OLD_SAVED ? "older firmware's"
                                                                     : "compiled-in default";
                // What the panel's top-left and bottom-right corners read
                // as, raw -- the old min/max pairs, for a Fit.
                float a0 = 0, b0 = 0, a1 = 0, b1 = 0;
                TouchFit::toRaw(s_touchFit, 0, 0, a0, b0);
                TouchFit::toRaw(s_touchFit, s_touchFit.w0, s_touchFit.h0, a1, b1);
                info.calA0 = (int16_t)lroundf(a0); info.calA1 = (int16_t)lroundf(a1);
                info.calB0 = (int16_t)lroundf(b0); info.calB1 = (int16_t)lroundf(b1);
            }
            info.usingCapTouch = usingCapTouch;
#if defined(CROWPANEL7)
            info.boardName = "CrowPanel 7";
#elif defined(TAB5)
            info.boardName = "Tab5 BETA";
#elif defined(TOUCH_ON_DISPLAY_BUS)
            info.boardName = "AWOK";
#elif defined(FREENOVE_S3_40)
            info.boardName = "freenove-s3-40 BETA";
#elif defined(FREENOVE_S3_35)
            info.boardName = "freenove-s3-35 BETA";
#elif defined(TDECK_PLUS)
            info.boardName = "tdeck-plus BETA";
#elif defined(STICKS3)
            info.boardName = "StickS3 BETA";
#elif defined(CARDPUTER_ADV)
            info.boardName = "Cardputer ADV LAB";
#elif defined(CYD35C)
            info.boardName = "cyd35c BETA";
#elif defined(CYD35)
            info.boardName = "cyd35 BETA";
#else
            info.boardName = "cyd";
#endif
            info.touchValid = tp.valid;
            info.mappedX = tp.x;
            info.mappedY = tp.y;
            info.crash   = g_lastCrash;
            info.pushUs  = s_pushUsAvg;
            info.frameUs = s_frameUsAvg;
            info.bgUs    = Theme::backgroundUs();
            info.lastScreenName = s_lastScreenName;
            info.lastScreenUs   = s_lastScreenUs;
            info.freeHeap = ESP.getFreeHeap();
            info.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#if defined(CYD_GPS)
            {
                const Gnss::Fix& f = Gnss::fix();
                const Gnss::Sky k = Gnss::sky();
                const uint32_t nowMs = millis();
                info.gpsShown = true;
                info.gpsGood = Gnss::good(); info.gpsBad = Gnss::bad();
                info.view = k.view; info.heard = k.heard; info.used = f.used;
                info.accM = f.accM; info.lat7 = f.lat7; info.lon7 = f.lon7;
                info.fixAgeMs = !f.atMs ? UINT32_MAX : (int32_t)(nowMs - f.atMs) > 0 ? nowMs - f.atMs : 0;
                info.fresh = Gnss::fresh(nowMs); info.faked = Gnss::faked();
                info.wdOn = Wardrive::enabled();
                info.wdState = (uint8_t)Wardrive::sdState(nowMs);
                info.wdWritten = Wardrive::written(); info.wdSkipped = Wardrive::skipped();
                info.wdDropped = Wardrive::dropped(); info.wdFile = Wardrive::sdFileName();
            }
#endif
            info.nvsPercent   = nvsFullPercent();
            info.pushWarn     = pushStatusText(info.push, sizeof info.push) != 0;
            {
                uint16_t stuck = 0, saved = 0;
                OtaWifi::joinStats(stuck, saved);
                if (!stuck) snprintf(info.wifiJoin, sizeof info.wifiJoin, "boot joins never stalled");
                else        snprintf(info.wifiJoin, sizeof info.wifiJoin, "%u stalled, retry joined %u",
                                     (unsigned)stuck, (unsigned)saved);
                info.wifiWarn = stuck > saved;
            }
            {
                const char* h = engine.homeSsid();
                const uint32_t seen = engine.homeSeenMs();
                if (!h[0])      snprintf(info.home, sizeof info.home, "no network marked USE");
                else if (!seen) snprintf(info.home, sizeof info.home, "%.20s, not heard", h);
                else            snprintf(info.home, sizeof info.home, "%.20s, heard %lus ago%s", h,
                                         (unsigned long)((millis() - seen) / 1000),
                                         engine.homeInView(millis()) ? "" : " (away)");
                // How many of its routers are known: 0 = recognised by name only.
                const size_t l = strlen(info.home);
                if (h[0] && l + 8 < sizeof info.home)
                    snprintf(info.home + l, sizeof info.home - l, ", %u AP", (unsigned)engine.homeAps().count());
            }
            info.resetReason = resetReasonName();
            info.loopFree    = s_loopHeapFree;
            info.loopLargest = s_loopHeapLargest;
            info.otaSlot  = OtaCore::runningSlot();
            info.otaOther = OtaCore::otherVersion();
            info.bbReady   = BlackBox::ready();
            info.bbKept    = BlackBox::detectionsKept();
            info.bbCrashes = BlackBox::crashesKept();
            info.bbHaveLast = BlackBox::lastCrash(info.bbLast);

            drawTwoBand([&](TFT_eSPI& t, bool) { uiDiagnosticsTick(t, now, engine, info); });
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                uiDiagnosticsHitBack(tp.x, tp.y, tft.width(), tft.height())) {
                lastTouch = now;
                enterSettings();
            } else if (Theme::compact() && touchJustDown) {
                uiDiagnosticsNextPage();   // a 240x135 screen shows it a page at a time
            }
            break;
        }
        case AppState::HUNT: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiHuntTick(t, now, engine, advance); });
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                if (uiHuntHitStop(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    engine.clearHunt();
                    Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                    enterClear();
                } else if (uiHuntHitBack(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    enterClear();
                }
            }
            break;
        }
        case AppState::COLOR_CHECK: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiColorCheckTick(t, now); });
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                ColorCheckTap ctap = uiColorCheckHitTest(tp.x, tp.y, tft.width(), tft.height());
                if (ctap == ColorCheckTap::INVERT) {
                    lastTouch = now;
                    Settings::toggleInvert();
                    // XOR against the panel's own baseline, not an
                    // absolute call -- see PANEL_NEEDS_INVERSION.
                    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
                } else if (ctap == ColorCheckTap::ORDER) {
                    lastTouch = now;
                    Settings::toggleRgbSwap();
                    applyColorOrder();
                } else if (ctap == ColorCheckTap::DONE) {
                    lastTouch = now;
                    Settings::markColorChecked();
                    if (s_colorCheckFromSettings) enterSettings();
                    else                           enterClear();
                }
            }
            break;
        }
        case AppState::DIARY: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiDiaryTick(t, now, engine); });
            // Simple read-only info panel — any tap takes you back,
            // no button bar or scroll needed.
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                enterClear();
            }
            break;
        }
        case AppState::OUTFIT: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiOutfitTick(t, now, engine, advance); });
            // Arrow taps cycle the equipped outfit (already persisted
            // live, no separate "confirm" step needed); a tap anywhere
            // else jumps straight back to the main screen, same "tap to
            // dismiss" feel as the Diary screen — the settings icon
            // (handled by the global back-navigation check above, which
            // runs before this switch and already changes `state`
            // itself when it fires) remains the way back to SETTINGS
            // specifically.
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                if (!uiOutfitTapArrow(tp.x, tp.y, tft.width(), tft.height())) {
                    enterClear();
                }
            }
            break;
        }
    }

#if !defined(CYD35)
    // Skipped on cyd35: this effect reads back already-drawn pixels to
    // shift them sideways, which is instant/reliable against the sprite
    // in RAM but would mean a live SPI readback from the panel itself
    // with no sprite buffer -- pixel readback (MISO) is a known-flaky
    // path on cheap SPI panels, not worth the risk for a 220ms cosmetic
    // transition effect.
    //
    // frameBufferOk guards both: if a rotate ever failed to reallocate
    // `frame` (see the rotate handler above), canvas already points
    // straight at tft and every draw this frame already landed on the
    // real screen -- pushing `frame` here would just paint stale data
    // from the sprite we stopped using back over the top of it.
    if (frameBufferOk) {
        if (now - transitionStart < TRANSITION_MS) {
            Theme::drawTransitionGlitch(frame, now - transitionStart, TRANSITION_MS);
        }
#if SQW_KEYCURSOR
        stickDrawCursor(frame, now);
#endif
        FrameProf::lap(FrameProf::POST);
        pushFrame(0, 0);
        FrameProf::lap(FrameProf::PUSH);
    }
#endif

    s_pushUsAvg  = emaUpdate(s_pushUsAvg, s_pushAccumUs);
    const uint32_t frameUs = micros() - frameStartUs;
    s_frameUsAvg = emaUpdate(s_frameUsAvg, frameUs);
    FrameProf::endFrame();
    if (const char* nm = timedScreenName(state)) {
        if (now - transitionStart >= TRANSITION_MS) {
            if (s_lastScreenAt != transitionStart) {
                s_lastScreenAt   = transitionStart;
                s_lastScreenName = nm;
                s_lastScreenUs   = 0;
            }
            s_lastScreenUs = emaUpdate(s_lastScreenUs, frameUs);
        }
    }
    // The same two numbers DIAGNOSTICS shows, once every ten seconds on
    // serial, so a frame-rate change can be read off a capture rather than
    // off a screen somebody has to navigate to and photograph. Ten seconds
    // is the [scan] restart cadence; the log stays readable.
    {
        static uint32_t lastFrameSay = 0;
        if (s_frameUsAvg && now - lastFrameSay >= 10000) {
            lastFrameSay = now;
            const uint32_t loopsPerS = s_loopsSinceSay / 10;
            s_loopsSinceSay = 0;
            FrameProf::print();
#if defined(CYD35)
            // Measurement, not a feature: where the 3.5"'s frame really goes.
            Serial.printf("[bands] draw %lu / %lu us   hash %lu us   wire %lu us   rows %ld\n",
                          (unsigned long)s_bandUs[0], (unsigned long)s_bandUs[1],
                          (unsigned long)FramePush::hashUs(), (unsigned long)FramePush::wireUs(),
                          (long)SQW_PUSH_ROWS());
#endif
            const volatile uint32_t* ak = advertKinds();
            Serial.printf("[frame] avg %lu.%lu ms (%lu fps, loop %lu/s)  push %lu.%lu ms (%ld rows)  screen %u  bg %u  heap %lu/%lu  wifi %lu  ble %lu/s  adv %lu  kinds %lu/%lu/%lu/%lu/%lu  det %lu\n",
                          (unsigned long)(s_frameUsAvg / 1000), (unsigned long)((s_frameUsAvg / 100) % 10),
                          (unsigned long)(1000000UL / s_frameUsAvg), (unsigned long)loopsPerS,
                          (unsigned long)(s_pushUsAvg / 1000), (unsigned long)((s_pushUsAvg / 100) % 10), (long)SQW_PUSH_ROWS(),
                          (unsigned)state, (unsigned)Settings::background(), (unsigned long)ESP.getFreeHeap(),
                          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                          (unsigned long)wifiFramesSeen(), (unsigned long)advertRate(), (unsigned long)advertsSeen(),
                          (unsigned long)ak[0], (unsigned long)ak[1], (unsigned long)ak[2], (unsigned long)ak[3], (unsigned long)ak[4],
                          (unsigned long)engine.lifetimeTotal());
        }
    }

    // ---- power saver ------------------------------------------------------
    // Both timers hang off lastTouch, which every screen already maintains.
    // They are separate settings because they are very different impositions:
    // slowing the animation down is barely noticeable, and blanking the screen
    // is not, so most people will want them on different clocks.
    {
        const uint32_t idleMs = now - lastTouch;

        // An alert has to be visible. A detector that dims itself and then
        // hides the thing it just found is worse than one with no saver at all.
        const bool alerting = (state == AppState::ALERT || state == AppState::WATCH_ALERT);
        const uint16_t timeoutSec = Settings::screenTimeoutSec();
        // The meeting sign is there to be read; it never dims.
        bool wantDim = timeoutSec && idleMs > (uint32_t)timeoutSec * 1000UL &&
                       !(Settings::wakeOnAlert() && alerting) && state != AppState::MEETING;
        // NIGHT POWER (night_mode.h): through the night hours, the screen
        // goes dark half a minute after the last touch, whatever SCREEN
        // TIMEOUT says -- and dark is off, not DIM LEVEL. A tap wakes it the
        // way it wakes the saver (swallowed, not a press); an alert moves
        // lastTouch, so it lights the card.
        {
            const bool nightDark = Settings::nightPower() && Clock::trusted() &&
                                   NightMode::nightHours(Settings::nightMode(), Clock::hour()) &&
                                   idleMs > NightMode::POWER_IDLE_MS && !alerting &&
                                   state != AppState::MEETING;
            if (nightDark != s_nightDark) {
                s_nightDark = nightDark;
                Serial.printf("[night] %s\n", nightDark ? "screen off for the night" : "screen back on");
                if (s_screenDimmed) applyBrightness();   // already dimmed: the level changes now
            }
            if (nightDark) wantDim = true;
        }
#if defined(TWATCH_S3)
        // On the cable the watch stays lit; on battery the timeout always runs
        // (see Settings::screenTimeoutSec), unless it is set to NEVER.
        if (s_onUsb) wantDim = false;
#endif
#if SQW_POCKET
        // POCKET MODE, which beats the cable, the desk and a timeout of NEVER.
        // Only the button ends it (pocketPress); a touch never does.
        //
        // Things that need to be seen borrow the screen without ending it:
        // an alert card for as long as it is up on its own (ten seconds; a
        // WATCH card, which waits for a tap, goes dark with the card still
        // on it), and the first-time note. Then it is dark again, not the
        // main screen after.
        {
            const uint32_t shownAt = state == AppState::WATCH_ALERT ? watchAlertStart : alertStart;
            const bool alertLit = alerting && Settings::wakeOnAlert() &&
                                  (int32_t)(now - shownAt) < (int32_t)ALERT_AUTO_DISMISS_MS &&
                                  (int32_t)(shownAt - s_crownPressAt) > 0;
            const bool noteLit = s_pocketNoteUntil && (int32_t)(s_pocketNoteUntil - now) > 0;
            if (!noteLit) s_pocketNoteUntil = 0;
            if (s_crownDark) wantDim = !alertLit && !noteLit;
        }
#endif
        if (wantDim != s_screenDimmed) {
            s_screenDimmed = wantDim;
            applyBrightness();
#if SQW_POCKET
            if (s_crownDark) Serial.println(wantDim ? "[pocket] screen dark" : "[pocket] screen lit for an alert");
#endif
        }

        // Auto-lock, on the saver's idle clock: after N idle minutes, or the
        // moment the saver dims the screen. Never out of the boot, a PIN being
        // typed, or an alert that is still up.
        if (Security::enabled() && !Security::locked() &&
            state != AppState::BOOT && state != AppState::COLOR_CHECK && state != AppState::PIN_ENTRY &&
            state != AppState::ALERT && state != AppState::WATCH_ALERT) {
            const uint32_t lockMs = Security::autoLockIdleMs();
            if ((lockMs && idleMs >= lockMs) || (Security::autoLockOnSleep() && s_screenDimmed)) {
                if (onRawScanScreen()) engine.stopRawScan();
                Security::lock();
                enterLocked();
            }
        }

        // Hold the loop to the chosen rate once idle. delay() hands the core
        // to the RTOS idle task, which parks it in WAITI -- a real saving
        // because the clock gates, though not the same order as a true light
        // sleep, which this firmware cannot take while the radio is scanning.
        //
        // Note which way this points: the slack being given back only exists
        // because the panel push is fast. On the 40 MHz build there is no idle
        // time left to hand over, so the faster SPI clock is what makes this
        // saving possible rather than something to trade against it.
#if defined(TWATCH_S3)
        // Asleep, the loop only needs to hear the radios and feel a tap:
        // ten passes a second is plenty, and the rest of the time is idle.
        if (s_panelAsleep) {
            const uint32_t spentUs = micros() - frameStartUs;
            if (spentUs < 100000UL) delay((100000UL - spentUs) / 1000UL);
        }
#endif
        const uint8_t fps = Settings::idleFps();
        uint32_t budgetUs = 0;
        if (fps && idleMs > (uint32_t)Settings::idleAfterSec() * 1000UL) budgetUs = 1000000UL / fps;
        // TERMINAL MODE at rest: whichever is slower, its pace or the saver's.
        const uint32_t restUs = terminalRestBudgetUs(now, idleMs);
        if (restUs > budgetUs) budgetUs = restUs;
        if (budgetUs) {
            const uint32_t spentUs = micros() - frameStartUs;
            if (spentUs < budgetUs) {
                if (restUs) terminalRestWait((budgetUs - spentUs) / 1000UL);
                else        delay((budgetUs - spentUs) / 1000UL);
            }
        }
    }

    // The light on the back reads the state machine rather than being told
    // about transitions, so there is no exit path it can miss.
    {
        StatusLight::Context lc;
        lc.alert      = (state == AppState::ALERT);
        lc.alertColor = Theme::colorFor(lastAlertType);
        // A fox caught on the HUNT gauge flashes the light green, the same
        // three flashes a detection gets in its own colour.
        if (state == AppState::HUNT && uiHuntCaught()) { lc.alert = true; lc.alertColor = Theme::GREEN; }
#if defined(SENSE_CYD_APP)
        // A movement HUMAN SENSING announced, card or no card: the same
        // flashes, in MOTION's colour, for a few seconds.
        if (s_motionLightUntil && (int32_t)(now - s_motionLightUntil) < 0) {
            lc.alert = true;
            lc.alertColor = Theme::colorFor(DetectionType::MOTION);
        } else {
            s_motionLightUntil = 0;
        }
#endif
        lc.update = 0;
        {
            const OtaBle::State  b = OtaBle::state();
            const OtaWifi::State w = OtaWifi::state();
            if (b == OtaBle::State::DONE || w == OtaWifi::State::DONE) lc.update = 2;
            else if (b == OtaBle::State::FAILED || w == OtaWifi::State::FAILED) lc.update = 3;
            else if (b == OtaBle::State::RECEIVING || b == OtaBle::State::VERIFYING ||
                     w == OtaWifi::State::CONNECTING || w == OtaWifi::State::CHECKING ||
                     w == OtaWifi::State::DOWNLOADING || w == OtaWifi::State::VERIFYING) lc.update = 1;
        }
        // The meeting sign means it: no flashing light behind it either.
        lc.quiet        = (state == AppState::LOCKED || state == AppState::PIN_ENTRY ||
                           state == AppState::MEETING);
        lc.screenDimmed = s_screenDimmed;
        lc.screenDark   = s_screenDimmed && Settings::dimLevel() == 0;
        StatusLight::tick(now, lc);
#if SQW_HAS_BUZZER
        // Every pass in every state, so the chirp's OFF lands whatever
        // screen the alert opened.
        Buzzer::tick();
#endif
    }
    prevTouchValid = tp.valid;
}
