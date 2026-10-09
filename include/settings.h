// MuleSkin-CYD — persisted user settings (theme, background style,
// display invert/brightness, alert confidence filter). Everything here
// is a plain value store: it owns NVS persistence and, where it makes
// sense (palette), applies the change itself. Hardware side effects
// that need the TFT/backlight objects (tft.invertDisplay, ledcWrite)
// stay in main.cpp, which reads these getters after a change.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "signatures.h"   // Confidence

namespace Settings {
    enum class Background : uint8_t {
        DIGITAL = 0, STARFIELD = 1, TOASTERS = 2,
        AQUARIUM = 3, TERMINAL = 4, FIREFLIES = 5,
        FIRE = 6, SNOWFALL = 7, SPECTRUM = 8,
        // RETIRED. WIREFRAME TUNNEL was the one background that never came
        // good: a tunnel's whole picture is its vanishing point, and MuleSkin
        // stands exactly there, so it only ever showed its corners. Ten
        // reworks were built and rendered before it was dropped.
        //
        // The VALUE stays. It is saved to NVS as a raw byte, so renumbering
        // would move every board that has SYNTHWAVE or BLACK saved onto a
        // different background. backgroundSelectable() keeps it out of the
        // picker and load() moves anyone who was sitting on it.
        TUNNEL = 9,
        SYNTHWAVE = 10,
        // No animation at all -- the band is filled flat and nothing moves.
        // Only reachable while BORING MODE is on, which is the mode that
        // already strips MuleSkin, the pet and the idle flourishes. It did
        // not strip the backdrop, so "boring" still meant a screen full of
        // falling glyphs; this is the rest of that thought.
        BLACK = 11,
        // RETIRED as a main-screen background (replaced by RADAR; load()
        // moves anyone saved on it): the hooded MuleSkin at his computer,
        // ears twitching. The artwork lives on as the boot splash.
        MULESKIN = 12,
        // docs/radar.gif: a cyan scope, the arm sweeping round once every six
        // seconds and the weather it finds fading out behind it. Drawn from a
        // model of the GIF (tools/make_radar.py), not its frames. The default
        // on a fresh board.
        RADAR = 13
    };
    static const uint8_t BACKGROUND_COUNT = 14;

    // Whether a background can be reached from the BACKGROUND row. Only
    // RADAR; load() moves a board saved on any other to it.
    bool backgroundSelectable(Background b);
    const char* backgroundName(Background b);

    // Reads all fields from NVS (namespace "settings"), falling back to
    // defaults for anything never saved. Call once at boot, before
    // Theme::applyPalette()/tft.invertDisplay()/backlight setup so the
    // very first frame already reflects a saved choice.
    void load();

    uint8_t    paletteIndex();
    void       cyclePalette();       // advances+wraps, persists, applies to Theme

    Background background();
    void       cycleBackground();    // advances+wraps, persists
    // For the bench: this session only, NOT saved, so the owner's own pick
    // is back at the next boot. Refuses a background the menu cannot reach.
    bool       previewBackground(Background b);

    void       cyclePrevBackground(); // same, but backward

    // Disables the two CLEAR-screen gestures that cycle background
    // (the left/right edge-zone tap, and boring mode's "tap anywhere"
    // stand-in for it) without touching the Settings screen's own
    // BACKGROUND row -- once you've found a favorite, an accidental
    // edge tap during normal use shouldn't change it, but deliberately
    // opening Settings and picking a new one always should. Same
    // "disable the accidental-tap surface, not the deliberate menu
    // path" reasoning as rotationLocked(), just split across two call
    // sites instead of rotation's one (see main.cpp's CLEAR case).
    bool       backgroundLocked();
    void       toggleBackgroundLocked();

    bool       inverted();
    void       toggleInvert();       // persists only — caller applies tft.invertDisplay()

    // "Wrong RGB/BGR order" fix for CYD boards whose panel batch
    // disagrees with the header's TFT_RGB_ORDER guess (colors read
    // swapped -- red shows as blue, etc.) -- unrelated to inverted()
    // above, which flips light/dark, not color channels. Persists
    // only — caller reissues the panel's MADCTL byte (see main.cpp's
    // applyColorOrder()), same XOR-against-a-compile-time-baseline
    // convention toggleInvert() already uses.
    bool       rgbSwapped();
    void       toggleRgbSwap();

    // Whether the first-boot color-check screen (see ui_colorcheck.h)
    // has ever been completed -- gates whether it auto-shows right
    // after the boot splash. Settings' own "CHECK COLORS" row can
    // re-enter that screen on demand afterward regardless of this.
    bool       colorChecked();
    void       markColorChecked();

    // Whether the RSSI/confidence primer (see DetectionInfo::
    // rssiConfidencePrimer()) has ever been shown -- gates it to
    // appear exactly once, the first time anyone taps MORE INFO on a
    // LOG entry, before that entry's own explanation.
    bool       infoPrimerShown();
    void       markInfoPrimerShown();

    // Disables the title-bar rotate button (and the ROTATED gesture it
    // triggers) without touching its icon -- an accidental tap during
    // BLE/WiFi scanning restarts the frame buffer for the new shape,
    // which some users would rather just not risk once they've settled
    // on an orientation.
    bool       rotationLocked();
    void       toggleRotationLock();

    // The Legend's aura: lit unless put out on the APPEARANCE page.
    bool       auraShown();
    void       toggleAura();

    // Last rotation (0..3, TFT_eSPI's setRotation() values) the rotate
    // button left the screen on -- so it comes back up the same way
    // after a power cycle instead of always resetting to the board's
    // compile-time default. AWOK has no rotate button and never touches
    // this (see main.cpp's setup()/rotate handler, both guarded
    // #if !defined(AWOK)).
    uint8_t    rotation();
    void       saveRotation(uint8_t r);

    // "Boring mode" — all detection features stay exactly as they are,
    // this only turns off MuleSkin's on-screen presence (the CLEAR-
    // screen mascot/animations/speech bubbles and the boot-splash
    // cameo) for anyone who just wants a plain detector.
    bool       boringMode();
    void       toggleBoringMode();

    // 32..255 — floor keeps the backlight from going fully dark and
    // unreadable via the settings screen itself.
    uint8_t    brightness();
    void       adjustBrightness(int8_t delta);   // clamps, persists

    // ---- POWER SAVER -----------------------------------------------------
    // Every one of these is an independent, manually chosen setting; the
    // master switch below only gates whether any of them are acted on, so
    // turning it off restores stock behaviour without losing the choices.
    //
    // Nothing here changes the display's SPI clock. That was the obvious
    // idea and it is the wrong one: the panel push is a blocking, polled
    // transfer, so halving the clock does not idle the chip, it keeps the
    // core awake twice as long for the same frame. The savings that are
    // real are the backlight, the idle frame rate, and the core clock.
    bool       powerSaver();
    void       togglePowerSaver();

    // Seconds of no touch before the backlight drops to dimLevel(). 0 = never.
    uint16_t   screenTimeoutSec();
    void       cycleScreenTimeout();

    // Duty the backlight falls to when it times out, 0..255. Allowed to go
    // to 0 (fully off) unlike brightness(), which has a floor of 32: this
    // one always comes back on the next touch, so it cannot strand anyone
    // in front of a black screen the way a dark brightness() could.
    uint8_t    dimLevel();
    void       adjustDimLevel(int8_t delta);

    // Frames per second the main loop is held to once idle. 0 = uncapped.
    uint8_t    idleFps();
    void       cycleIdleFps();

    // Seconds of no touch before the idle frame cap applies. Kept separate
    // from screenTimeoutSec() on purpose -- slowing the animation down is a
    // much smaller imposition than dimming the screen, so most people will
    // want it to happen sooner.
    uint16_t   idleAfterSec();
    void       cycleIdleAfter();

    // Core clock in MHz: 240, 160 or 80. Never below 80 -- the radio needs
    // an 80 MHz APB clock, and the display's SPI divisor and the UART's
    // baud divisor are both derived from it, so dropping under that would
    // take out scanning, the panel and the console together.
    uint16_t   cpuMhz();
    void       cycleCpuMhz();

    // The watch's two battery knobs, both only while POWER SAVER is on.
    // BLE LISTEN: how much of each 100 ms the Bluetooth receiver is open,
    // 25, 50 or 75 percent. The receiver is the watch's whole battery story
    // (about 100 mA whenever it is open), and a tracker announcing itself
    // every second or two is still caught within seconds at 25. Off the
    // watch this reads 75, the window every board has always run.
    uint8_t    bleListen();
    uint8_t    bleListenRaw();
    void       cycleBleListen();
    // SLEEP CPU: the core clock while the screen is asleep, 80, 160 or 240.
    // The screen-on clock is CPU CLOCK above; this one never goes above it.
    uint16_t   idleCpuMhz();
    uint16_t   idleCpuMhzRaw();
    void       cycleIdleCpu();

    // Whether an alert pulls the backlight back up. On by default: a
    // detector that dims itself and then hides the alert it just found is
    // worse than useless.
    bool       wakeOnAlert();
    void       toggleWakeOnAlert();
    // TAGS + RINGS on the watch: AirTags, Tiles, Samsung and Google tags and
    // Ring cameras are logged but never interrupt -- no card, no buzz, no
    // screen waking. On by default: a commute is fifty AirTags an hour.
    // How long each of the last few boots ran, in minutes, newest first --
    // the runtime log for boards that cannot read their battery. main.cpp
    // notes the running boot every ten minutes; the entry for a boot that
    // ended is how long it lasted (to within ten minutes).
    void       noteRunMinutes(uint16_t boot, uint16_t minutes);
    uint8_t    runHistory(uint16_t* boots, uint16_t* minutes, uint8_t cap);
    // PRIVACY MODE (SYSTEM): the screen hides the device half of every
    // address and all but three characters of every device and network
    // name. See include/privacy.h. Off by default.
    bool       privacyMode();
    void       togglePrivacyMode();
    bool       quietTrackers();
    void       toggleQuietTrackers();
    // Set the first time this watch's GPS answers: it is an S3 Plus, whose
    // bigger cell takes a faster charge. Never cleared by a GPS that is
    // merely switched off.
    bool       watchPlus();
    void       setWatchPlus();
    // The T-Watch's buzz on an alert (the DRV2605 haptic motor). Kept on
    // every board so the settings file reads the same; only the watch has
    // a motor to use it.
    // One row: OFF, HIGH, MED, LOW. buzz() is "not OFF"; buzzStrength() is
    // 0 LOW, 1 MED, 2 HIGH, which picks the motor driver's effect library
    // (1 is meant for a 1.3 V motor, 2 for a 3 V one -- the watch's is 3 V),
    // the patterns, and at HIGH lifts the drive clamp a little too.
    bool       buzz();
    uint8_t    buzzStrength();
    const char* buzzModeName();
    void       cycleBuzz();

    // The watch's radio duty cycle; see RADIO_DUTY_NAMES in settings.cpp.
    // 0 when the saver is off. Watch only.
    uint8_t     radioDuty();
    uint8_t     radioDutyRaw();
    const char* radioDutyName(uint8_t ix);
    void        cycleRadioDuty();

    // ---- BUZZER ----------------------------------------------------------
    // The CrowPanel 7's buzzer, behind the helper MCU that runs its backlight.
    // OFF by default, and ON means one chirp for a device this board has not
    // logged before, at the moment the alert goes up -- never for one coming
    // back, never with the screen dimmed, never at night. The rules live with
    // the hook in main.cpp (alertMayInterrupt); this is only the switch.
    // Boards without a buzzer never show the row; the value is kept on all
    // of them so a saved preference travels, the way the light's knobs do.
    bool        buzzerOn();
    void        toggleBuzzer();

    // ---- LORA ------------------------------------------------------------
    // The wireless slot: OFF, FOCUS on one profile, or SURVEY over the band
    // (Lora::Mode's values), and which profile FOCUS parks on (an index into
    // lora_profiles.cpp's table). Only the CrowPanel 7 shows the rows; the
    // values are kept everywhere, like the buzzer's.
    uint8_t     loraMode();
    void        cycleLoraMode();
    // Which band plan the sniffer listens to: 0 AUTO (the Americas' time
    // zones get US 915, everywhere else EU 868), 1 EU, 2 US. Read at boot.
    // The watch's LORA row: 0 OFF, 1 MESHTASTIC, 2 MESHCORE, 3 BOTH (the
    // default: the radio takes turns between the two). Applied by
    // Lora::applyListen().
    uint8_t     loraListen();
    void        cycleLoraListen();
    const char* loraListenName();
    uint8_t     loraRegion();
    void        setLoraRegion(uint8_t r);
    uint8_t     loraFocus();
    void        setLoraFocus(uint8_t ix);

    // ONLINE LOOKUPS: whether this board may ask a public service about a node
    // it heard. Four switches, not one, because a master alone cannot say
    // "aircraft yes, callsigns no" -- which is the setting a careful operator
    // actually wants. All four default OFF, and that is the one place this
    // differs in shape from updateCheck() below: that switch talks to
    // first-party infrastructure about THIS board and defaults ON, these talk
    // to third parties about OTHER PEOPLE. What each one sends, and the
    // measured harm behind the default, is in include/lora_enrich.h.
    //
    // The fourth is a different KIND of request and still defaults off. The
    // MeshCore adverts feed sends a row count and nothing else -- no key, no
    // prefix, no name, nothing about what this board heard (include/lora_feed.h)
    // -- so it is the one source here that cannot betray anything even in
    // principle. It is off by default anyway: this board does not join a
    // network unasked, whatever the request would have said.
    bool        loraLookups();           // the master
    void        toggleLoraLookups();
    bool        loraLookupCall();        // hamrig.com, for APRS and MeshCom callsigns
    void        toggleLoraLookupCall();
    bool        loraLookupOgn();         // the OGN device database, for FANET
    void        toggleLoraLookupOgn();
    bool        loraLookupFeed();        // meshcore.df0x.de recent adverts, to name MeshCore rows
    void        toggleLoraLookupFeed();

    // The channel keys the two mesh decoders hold, as one opaque blob. The
    // record format is Lora::Chan's business (include/lora_channels.h); this
    // is only the NVS entry, so that the store stays the one place that talks
    // to Preferences.
    //
    // ONE entry for the whole list, for the reason the egg-hunt progress is
    // packed into one: thirty channels would be thirty entries in a store
    // with a few hundred free, and a channel list is not what that budget is
    // for. Key "loraChans" -- 9 characters, inside NVS's limit of 15, and no
    // other key in settings.cpp begins with those letters.
    size_t      loraChannels(uint8_t* out, size_t cap);        // bytes read; 0 = nothing stored
    // n == 0 removes the entry. False when the store would not take it -- a
    // full NVS partition, say. That has to be reported rather than swallowed:
    // a write that silently fails is a board that silently forgets its keys
    // again, which is the whole bug this entry exists to fix.
    bool        setLoraChannels(const uint8_t* rec, size_t n);

    // ---- STATUS LIGHT ----------------------------------------------------
    // The RGB LED on the back of the 2.8" CYD. See status_light.h for the
    // rules it follows; these are only the knobs. Every one of them is
    // independent: LIGHT is the master switch and the others keep their
    // values while it is off, the same shape as the power saver.
    bool        lightOn();
    void        toggleLight();
    bool        lightAlerts();           // detection flashes
    void        toggleLightAlerts();
    uint8_t     lightIdle();             // 0 OFF, 1 BREATHE, 2 SOLID
    void        cycleLightIdle();

    // BANTER, on the APPEARANCE page: how much MuleSkin talks when nothing is
    // happening. 0 IMPORTANT (idle chatter off; he still speaks for a catch,
    // a message, a release, the daily hello), 1 LESS, 2 NORMAL, 3 MORE.
    // banterScale() is the multiplier on the idle roll's gaps.
    uint8_t     banter();
    const char* banterName();
    void        cycleBanter();
    float       banterScale();
    const char* lightIdleName();
    uint8_t     lightColor();            // 0 THEME, 1..9 fixed colours, 10 BACKGROUND
    void        cycleLightColor();
    const char* lightColorName();
    uint8_t     lightBrightness();       // 1..7, caps everything
    void        cycleLightBrightness();


    // UPDATE CHECK: whether the board joins its saved WiFi for a few seconds
    // at boot to ask the site for a newer release. Tells, never installs.
    bool        updateCheck();
    void        toggleUpdateCheck();

    // AUTO TIME: with a saved network, rejoin it once a day (and once soon
    // after a boot that didn't set the clock) for the few seconds a time
    // server takes, from the main screen. Detection pauses while it runs.
    bool        autoTime();
    void        toggleAutoTime();

    // NIGHT DIM: an index into NightMode::PRESETS (include/night_mode.h);
    // 0 = OFF. Needs the clock set to do anything.
    uint8_t     nightMode();
    void        cycleNightMode();

    // UPDATES: STABLE (the releases everyone gets) or LAB (test builds
    // published ahead of a release; see ota_wifi.cpp manifestName()).
    bool        labChannel();
    void        toggleLabChannel();

    // AUTO UPDATE: install a newer release on its own, at night (main.cpp
    // serviceAutoUpdate()). Off by default.
    bool        autoUpdate();
    void        toggleAutoUpdate();

    // PHONE ALERTS: alerts to a phone over Bluetooth (phone_alerts.h). Off
    // by default; only the boards that can (PhoneAlerts::available()) list it.
    bool        phoneAlerts();
    void        togglePhoneAlerts();

    // TIME ZONE: an index into Clock's zone table, applied at load and on
    // every change. UTC until somebody picks one; timeZoneChosen() says
    // whether anybody has, so MuleSkin can ask once.
    uint8_t     timeZone();
    const char* timeZoneName();
    void        cycleTimeZone();
    void        stepTimeZone(int dir);      // the card's arrows: steps without choosing
    void        setTimeZone(uint8_t i);     // from the ZONE serial command
    void        markTimeZoneChosen();       // THIS IS RIGHT on the card
    bool        timeZoneChosen();


    // What is CONFIGURED, ignoring the master switch. Only the power menu
    // wants these: it has to show you what you have chosen while the feature
    // is still switched off, which is the order most people will set it up in.
    uint16_t   screenTimeoutSecRaw();
    uint8_t    idleFpsRaw();
    uint16_t   cpuMhzRaw();

    // Minimum confidence an alert needs to interrupt with the ALERT
    // screen. LOW_CONF = no filtering (every match alerts, the
    // original behavior).
    Confidence  minConfidence();
    void        cycleMinConfidence();
    const char* minConfidenceLabel();

    // AUTO SNOOZE: how many times one device may interrupt with the full
    // ALERT screen before it has to earn the next one by coming CLOSER.
    // 0 is off. Nothing is actually silenced -- see Detection::quietBar --
    // which matters on a device whose job is telling you what is near you.
    // On the watch, POWER SAVER makes it five at most.
    uint8_t     autoQuietAfter();
    void        cycleAutoQuiet();
    const char* autoQuietLabel();

    // Per-type detection on/off (Settings > DETECTION FILTER). A
    // disabled type is dropped at the point it's first classified --
    // never logged, counted, or alerted on -- not just hidden after
    // the fact, and it isn't a scan-side filter (the radios can't be
    // told "ignore AirTags"; every advertisement is still received,
    // it's just discarded once matched against a disabled type). Off
    // by default nothing changes: every type starts enabled, so a
    // fresh install behaves exactly as it always has. UNKNOWN isn't
    // included -- it's the "matched a signature but not a specific
    // brand" fallback, not a type someone would want to blanket-mute.
    bool     typeEnabled(DetectionType t);

    // How big MuleSkin is drawn, as a percentage of the size the layout
    // would otherwise give him. SMALL 70, MEDIUM 85, LARGE 100.
    //
    // Only ever at or below 100. The two guards that size him -- one
    // keeping his crest on screen, one keeping a tall costume's overflow
    // inside a tenth of his height -- are closed-form solutions for the
    // full-size case, and every value below it is strictly more
    // conservative than what they solved for. Above 100 would invalidate
    // both, which is a different and much larger job.

    // The mascot's pace, chosen by eye on a real board (PACE N / TEMPO P on
    // the console) and kept. Pace: milliseconds between his steps. Tempo: a
    // percentage on every duration of his -- moods, bubbles, idle gags, the
    // length of a drop or a stretch -- 100 as written, 70 slower.
    uint16_t    mascotPaceMs();
    void        setMascotPaceMs(uint16_t ms);
    uint8_t     mascotTempoPct();
    void        setMascotTempoPct(uint8_t pct);

    uint8_t     muleskinSizePct();
    const char* muleskinSizeLabel();
    void        cycleMuleSkinSize();
    void     toggleType(DetectionType t);
    uint8_t  enabledTypeCount();   // for a Settings-row "12/14" summary

    // How far along an unfinished easter-egg hunt is, kept across restarts.
    // The unlocks themselves have always been saved; this is the PROGRESS
    // towards one, which used to live in memory only -- so a restart in the
    // middle of a hunt that takes minutes threw the hunt away.
    //
    // One entry, four bytes, one counter each. Only hunts that span minutes
    // belong here: the lodge knocks and the moon taps are drum rolls with a
    // 2.5 second window between taps, so they cannot survive a restart by
    // design and are deliberately NOT in it.
    enum class Hunt : uint8_t { EYE_STREAK = 0, COUNT };
    uint8_t  huntProgress(Hunt h);
    void     setHuntProgress(Hunt h, uint8_t v);   // writes only on a change
}
