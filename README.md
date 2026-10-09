# MuleSkin-CYD

> Surveillance-device detector for the ESP32-2432S028R ("Cheap Yellow Display").

MuleSkin-CYD sniffs the 2.4 GHz airwaves for known wireless signatures
of Flock Safety cameras, Axon body cameras, recording glasses, card
skimmers, AirTags, drones, proximity beacons and pentest hardware. It runs
standalone on a bare CYD board — no PC, no extras, just plug it into USB.

The UI is a neon radar: the hooded-MuleSkin artwork on the splash, a
sweeping radar scope behind the main screen, full-screen ALERT cards, and
headlines in Orbitron.

> **A fork of [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD)
> by [skizzophrenic](https://github.com/skizzophrenic).** The detection engine,
> the board ports, the mesh and most of what follows are their work; this fork
> rebrands it as MuleSkin and reworks the look and the main screen. Like the
> original, it is GPL-3.0.

<p align="center">
  <img src="docs/demo.gif" width="280"
       alt="MuleSkin seen from behind: a hooded figure with mule ears sitting at a computer in a dark room, ears twitching">
</p>

## What it detects

| Type | What | How |
|---|---|---|
| `FLOCK` | Flock Safety ALPR cameras | 29 WiFi OUI prefixes + BLE name + company ID `0x09C8` |
| `AXON` | Axon body cameras, TASERs, LE equipment | 3 WiFi OUI + SSID prefixes `AB2-`/`AB3-`/`AB4-`/`AXON-` |
| `META` | Camera glasses — Ray-Ban Meta, Snap Spectacles | BLE service UUID `0xFD5F` + Meta / Luxottica / Snap company IDs |
| `SKIMMER` | Bluetooth card skimmers (HC-05/06/03, RN42, BT04-A) | BT Classic name match + SPP UUID `0x1101` + 3 OUI |
| `RAVEN` | Raven gunshot detector | Service UUIDs `0x3100`–`0x3500` |
| `AIRTAG` | Apple AirTag / Find My trackers | Company ID `0x004C` + Find My payload check |
| `DRONE` | Remote ID drones | Service UUID `0xFFFA`, then the ASTM F3411 message **decoded** — aircraft position, altitude, serial, and the operator's location |
| `ALPR` | Motorola Solutions / Genetec plate readers | 6 WiFi OUI |
| `CAMERA` | Generic / covert IP cameras | 17 WiFi OUI (Wyze, Amazon, Tuya, Verkada, Avigilon, Axis, …) |
| `SAMSUNG_TAG` | Samsung Galaxy SmartTag / SmartTag+ | BLE service UUID `0xFD5A` |
| `GOOGLE_TAG` | Google Find My Device trackers (Chipolo, Pebblebee, Moto Tag) | BLE service UUID `0xFEAA` |
| `TILE` | Tile BLE trackers | BLE service UUID `0xFEED` / `0xFEEC` |
| `RING` | Ring doorbells / cameras | 15 WiFi OUI (Ring LLC's registered block + Amazon's) |
| `DEAUTH` | WiFi deauthentication floods | Rate-detected burst, not a signature |
| `EVILTWIN` | Rogue / spoofed access points | One SSID beaconing from two BSSIDs that disagree about encryption |
| `IBEACON` | Retail proximity beacons | Exact Apple header `4C 00 02 15` — **off by default**, see below |
| `HACKER` | Flipper Zero, Pwnagotchi, WiFi Pineapple, ESP deauthers | Flipper's service UUIDs `0x3081`–`0x3083`, company ID `0x0E29` and OUI `0C:FA:22`; the Pwnagotchi's own beacon payload; `Pineapple_` and `pwned` SSIDs |

### Confidence is per signature, not per type

Every hardware prefix in the firmware was checked against the IEEE registry
rather than against other detectors. Of 76 rows: **32 High, 4 Medium, 40
Low**.

That grading matters most on `FLOCK`, where exactly **one** of 29 prefixes is
registered to Flock Safety and the rest are the generic Espressif and Liteon
parts they build on — real evidence, shared with every dev board on earth.
`ALERT FILTER` is a minimum-confidence gate, so setting it to High keeps a
passing ESP32 in the log without taking over the screen.

The audit also removed `00:0E:58`, which sat here for eleven releases
labelled "Vigilant" and is registered to **Sonos**. Every speaker in range
was being logged as a plate reader.

`IBEACON` ships switched off — not a judgement about importance, one about
volume. One shop can put more beacons in range than this device would
otherwise see all week. It is one tap away in `DETECTION FILTER`.

## Hardware

- **ESP32-2432S028R** ("Cheap Yellow Display" / CYD) — about $15.
  Built-in 320×240 ILI9341 TFT, XPT2046 resistive touch, and an
  onboard microSD card slot.
- **Elecrow CrowPanel Advance 7.0** — experimental: an ESP32-S3 with an
  800×480 RGB panel and GT911 touch, via the `crowpanel7` build. It renders
  at 400×240 doubled, on purpose. See [board setup and testing](docs/CROWPANEL7.md).

That's it. No GPS, no extra modules. The CYD is the whole device, and the
one board that happens to carry a buzzer keeps it silent unless you switch
it on.

Other boards have builds of their own -- `platformio.ini` has one
`[env:...]` each, with what is and is not confirmed on it. The newest is the
**RockBase NM-CYD-C5** (`[env:nm-cyd-c5]`): the classic 2.8" CYD's glass on
an **ESP32-C5**, the first RISC-V chip here, with dual-band Wi-Fi 6, 16 MB of
flash and 8 MB of PSRAM. It runs the whole firmware and is on the web flasher
as a BETA; [docs/NM-CYD-C5.md](docs/NM-CYD-C5.md) has the esptool route and
every pin. Before it, the **Freenove ESP32-S3
Display 2.8"** (FNK0104B, `[env:freenove-s3]`): an S3 with capacitive touch, an
SDMMC card slot, a WS2812 status light and a battery connector. Pins in
[docs/PINOUT.md](docs/PINOUT.md).

## Web Flash

No build tools, no IDE, no cloning anything — flash a board straight
from your browser:

**[https://flasher.oillie.cloud/](https://flasher.oillie.cloud/)**

Works in Firefox, Chrome, Edge, or Brave on desktop. Pick your board (2.8" CYD,
AWOK 2.4", RL Phantom 2.4", Freenove 3.2", or in beta the 3.5", the LilyGo
T-Watch S3, the Freenove ESP32-S3 2.8", the Elecrow CrowPanel 7" and the
RockBase NM-CYD-C5), plug in,
click Connect & Install, done. A T-Watch has its clock set for it once the install
finishes.

## Build

Three steps:

1. Install [PlatformIO](https://platformio.org/) (CLI or VS Code extension).
2. Clone the repo:
   ```sh
   git clone https://github.com/muleskin/MuleSkin-CYD
   cd MuleSkin-CYD
   ```
3. Build and flash:
   ```sh
   pio run -t upload
   ```
   For a board that is not the default three, name its build, e.g.
   `pio run -e freenove-s3 -t upload`.

The first build pulls the TFT_eSPI, XPT2046, and NimBLE-Arduino
libraries; after that it's incremental.

A full beginner-friendly walkthrough is in [docs/BUILD.md](docs/BUILD.md).

## Usage

<p align="center">
  <img src="docs/demo1.gif" width="320"
       alt="The emulator: the splash with the version under the wordmark, the radar main screen with the time at the top centre, a FLOCK CAM alert card, then the radar again with that device's blip lighting up as the sweep passes over it">
</p>

1. Plug the CYD into USB-C.
2. The splash runs for about three seconds: the MuleSkin artwork, ears
   twitching, with the release under the wordmark and the build's own
   version on the INITIALIZING line (from `git describe`, so a working-tree
   build says so).
3. The main screen appears: the radar sweeping, the time at the top centre
   once the clock is set, and live per-type counters in alphabetical order.
   Everything heard in the last minute is a blip on the scope in its type's
   colour -- nearer the centre the stronger its signal, lighting up as the
   arm passes and fading a minute after it was last heard. (The bearing is
   only a fixed place per device: the board can't tell direction.) Tap a
   blip for that device's panel -- WATCH, IGNORE, HUNT or what it is.
4. The soft buttons at the bottom:
   - **`< WIFI TIME >`** — join your saved WiFi network just long enough to
     set the clock (see [The clock](#the-clock)).
   - **`< LOG >`** — open the rolling 200-entry detection log; tap it again
     to return to the main screen. On the LOG screen a second button,
     **`[ CLR ]`**, wipes the log and returns.
   - **`< IN MEETING >`** — a red do-not-disturb sign, **IN A MEETING** in
     big white letters, with BACK to return. It never dims, no alert takes
     it over, and the status light and buzzers stay quiet; detection keeps
     running and logging behind it. Tap the sign for a timer -- **BACK IN 15,
     30 or 60 MIN** -- and it takes itself down when the time is up.
5. When something is detected, the device **flashes a full-screen ALERT**:
   a header strip in the detection's own colour with the type in the
   headline face, a data plate with the vendor, the device's own name where
   it broadcasts one, its MAC and a signal meter, and a gauge showing what
   was found with the instrument grid over it. Tap anywhere to dismiss
   early, or it clears itself after 60 seconds.
6. **A tracker travelling with you** gets its own alert. An AirTag, Tile,
   SmartTag or Find My Device tag that has stayed in range for twenty
   minutes while the devices around you kept changing -- you are moving and
   it is not being left behind -- raises the alert card with **WITH YOU
   N MIN** across the top, once per tag. A tag near its owner changes its
   address every quarter of an hour, so it never gets that far; one planted
   on you keeps its address for about a day. Ignore a tag to stop it.

If a microSD card is present, every detection is also appended to
`MuleSkin-<day>.log` (CSV: `ts,type,rssi,mac,channel,vendor,ssid`).

### The clock

There is no GPS, and the board never joins a network to scan. It joins one
to tell the time: **`< WIFI TIME >`** on the main screen joins your saved
network (the one marked USE, or the strongest saved one in range), asks a time
server, and lets go, pausing detection only while the radio is busy; with no
network saved it opens WIFI NETWORKS to add one. The boot-time update check
and UPDATE OVER WIFI set the clock the same way while the radio is up anyway.
**AUTO TIME** (on the SYSTEM page, on by default) keeps it right on its own:
with a network saved, the board rejoins it once a day from the main screen --
and once a couple of minutes after a boot that didn't set the clock -- for the
few seconds a time server takes. Once the time is real it shows at the top
centre of the main screen, 12-hour with AM/PM. Tap it for **TODAY**: a bar
for every hour since midnight, the types seen most, and the first and last
sighting -- counted from the black box, so a restart doesn't lose the morning. The zone is yours to pick, and there are three ways: the web
flasher's **Set Time & Zone** button sends this computer's clock and zone
down the same cable right after flashing; the first time the clock is set
with no zone chosen, a card on the main screen asks, with the live time in
the zone it shows so you can see when it's right; and **TIME ZONE** on the
SYSTEM page changes it later. Daylight saving takes care of itself. Without
a saved network the clock can still be set over serial with a `TIME <epoch>`
line at 2,000,000 baud, and `ZONE US EASTERN` sets the zone the same way.
And every squad hello carries the sender's clock and zone, so a board with
neither takes them from the first member it hears: update one board by USB
and the rest of the squad know the time within a minute of meeting it.
Until the clock is set, timestamps count from boot. The board keeps a
note of the time in flash every ten minutes, and a cold boot with no clock
starts from that note: not the right time, since nobody knows how long the
power was off, but never earlier than the note, which keeps the day count
honest. Such a clock is used for the date only; the LOG times, the night
tag and the hour lines wait for a real answer.

Once it is set, the LOG shows the real time of each catch (or the date, for
one from another day), and the alert card says **AT NIGHT** for anything
caught between eleven and five. **NIGHT DIM** on the APPEARANCE page
(10PM-6AM, 11PM-7AM or 12AM-7AM) runs the screen at a quarter of its
brightness through those hours, full again for half a minute after a touch
or an alert; scanning carries on at full rate.

### Detection pauses while it's on WiFi

Nothing is detected while the board is joined to a network, attacks
included. To stay associated the radio has to sit on the access point's
channel, so WIFI TIME, AUTO TIME, the boot-time update check and UPDATE OVER WIFI turn
the WiFi sniffer off first (no DEAUTH, EVIL TWIN, pentest-gear or other WiFi
catches) and stop BLE scanning too (no trackers). The board never stays
connected: WIFI TIME gives the join 20 seconds and the time server about 6,
then lets go, and detection resumes the moment the radio is free, so the
blind spot is a few seconds and at most about 26. An update over WiFi is
blind for as long as the download and flash take. While the main screen is
up, an amber **PAUSED** pill in the title bar shows when scanning is off.

One consequence: an evil twin or a deauth flood aimed at the board's own
join isn't flagged while it happens. A flood still shows up as a join that
fails ("Could not join" on the WIFI TIME screen), and it's caught as soon as
scanning resumes if it's still going.

## The status light

The RGB LED on the back of the 2.8" CYD (on the front of the RL Phantom, and
a WS2812 on the Freenove ESP32-S3 2.8")
tells you what the screen is doing without the screen. A slow breathe in the
theme's colour when nothing is happening; three flashes and a hold in the
detection's own colour when something is, for as long as the alert card is
up; a double-blink for an unread message; a blip when a squad member walks
on; cyan while an update downloads and green or red for how it went. It goes
dark on the lock screen and through a wipe, so a duress restart looks like any
other restart from the back too.

**Settings → APPEARANCE → STATUS LIGHT**: the master switch, alerts and
messages on or off, idle breathe or solid or off, an idle colour that follows
the theme, the background, or one of nine fixed colours, brightness in seven
steps, and a TEST row that plays the lot in six seconds. Boards whose LED pins
have not been checked (the AWOK and the 3.5") compile it out and say so on
that screen.

## MuleSkinMesh

> **Work in progress.** It is in this release because it works — two boards
> find each other — but it has had days of testing, not months. Both halves are **off** until you turn them on, and one
> of them costs you something; the device asks before it lets you near the
> switch.

Two MuleSkines in range of each other notice: each hears the other's
twenty-byte BLE advert, which carries its name and version. (This build
does not draw the other board as a visitor -- the mascot is switched off --
so there are no on-screen visits.) The name is one row, **NAME** under
MULESKINMESH: a curated one until somebody types one on the payphone, where
**SHUFFLE** steps through the curated list for anyone who would rather not
type.

It is deliberately not a network. No pairing, no connection, no
acknowledgement, no retry — a broadcast that says who is here, and anybody in
earshot may or may not catch it. A peer is recognised inside the scan callback
and returns before the signature tables ever see it, so two of these can never
set each other off.

**Settings → MULESKINMESH**, and it asks first. `DETECT` is receive-only: your
board hears other boards and broadcasts nothing at all. `TRANSMIT` is the
half that makes you visible, and a full-screen warning stands in front of that
menu spelling out what goes out, how often, and what somebody with a scanner
can reconstruct from it — a fixed address that never changes is a trail of
where you have been. Nothing is transmitted until you have read that and
chosen YES.

That warning is not a formality. Broadcasting a stable identifier at strangers
is the exact behaviour this device exists to catch other people's hardware
doing. Offering it is defensible; switching it on quietly would not be.

### Messages

Two MuleSkines that share a five-word phrase can message each other: one
of 24 ready-made lines, or up to 48 characters typed on the payphone or the
QWERTY board, and nothing is sent until you have confirmed it. The message
screen -- **Settings → MULESKINMESH → SQUAD → REPLY** -- shows the latest one
in **red** with the sender's name, and the status light double-blinks while
one is unread.

**Settings → MULESKINMESH → MESSAGES**, then **PHRASE**: one of you ROLLs five
words and reads them out, the other ENTERs the same five. Setting a phrase
freezes the screen for about three seconds on purpose — it is 20,000 rounds of
PBKDF2, which every guess at your phrase has to pay too. A seven-card tutorial
runs the first time MESSAGES is switched on, and the **?** on the message
screen replays it; it never transmits anything.

Messages are AES-128-CCM, keyed from the phrase, with a nonce that cannot
repeat even across a crash, and every board checks its cipher against frames
made by an independent implementation at each boot. What stays visible is
that you sent something, and when: the contents are encrypted, the fact of a
message is not.

### Joining without typing

<p align="center">
  <img src="docs/squad-invite.gif" width="640"
       alt="Two boards side by side: one taps ADD TO SQUAD, the other's board asks and accepts, both show the same four digits, the phrase goes over, and the second board is in without typing anything.">
</p>

The typed phrase is the reliable way in and always will be. The convenient
way is **ADD**, beside INVITE and HUNT on the SQUAD screen. Pick a board in
range and tap it; their board asks them whether
they want in. Both screens then show the same four digits, which the two of
you compare out loud, and the phrase goes across sealed under a key that
exists for that one exchange and no other. The digits are derived from both
boards' keys, so a third board in the middle pretending to be each of you to
the other leaves the two screens disagreeing — say NO and nothing was sent.
The new member's board answers with a sealed hello the moment it has the
phrase, the inviter's shows **ADDED**, and both drop back to the main screen
on their own. If nothing comes back, the inviter's screen says so and offers
to show the phrase for typing.

Boards that have shown they hold your phrase read **MEMBER** on that screen,
and ADD only offers itself to strangers. Anyone with the phrase can invite
anyone; the phrase is the membership, and leaving somebody out means a new
phrase on every board.

### Your squad

**Settings → MULESKINMESH → SQUAD** is the roster: everybody who has ever been
heard holding your phrase, here or not, up to sixteen, kept across restarts.
Each member shows with how many separate times you have met, those in range
first. INVITE works when they
are here, AWAY says when they are not, and FORGET drops them after asking
once; they come back the next time they are heard with the phrase. A new
phrase clears the roster, because a new phrase is a new squad.

### Fox hunt

**HUNT** on either SQUAD screen aims HUNT MODE's signal gauge at that board.
It is the same meter the detector uses for a tag: no compass, so you turn
your body and walk toward where the needle does not fall. Two readings in a
row at arm's length and the gauge says **CAUGHT!** and the light on the back
flashes green. The fox needs TRANSMIT on; the hunters need
DETECT on, which they have if they can see the SQUAD screen at all.

**SHOW PHRASE** on the PHRASE screen is on by default. Off, the five words
become dashes, the board never prints them, and the only way into the squad
from that board's side is ADD TO SQUAD, in person.

### Knowing there is an update

Two ways, neither of which installs anything. At boot, a board with a saved
WiFi network joins it for about a second, asks flasher.oillie.cloud for the latest
version of its own build, and lets go again, all before Bluetooth starts;
**UPDATE CHECK** on the SYSTEM page turns that off. And every board's hello
to its squad carries its version, so a board that hears a member running
something newer knows without touching WiFi. Either way the SYSTEM row reads
UPDATE, and UPDATE FIRMWARE names the version until you install it.

**WIFI NETWORKS** on the SYSTEM page is where the board keeps the networks it
knows: up to six, with USE marking the one it tries first. ADD picks one from
a scan and takes the password on the board's keyboard; it is not checked by
joining, since joining means giving Bluetooth up until a restart, but at the
next boot check, and each row then says how that went: joined, wrong
password, or not found. At boot the board scans, joins the USE network if it
is there and otherwise the strongest saved one that is, so home and work both
just work. The update flow does the same, and only shows its own list when
none of the saved networks is in range. REMOVE takes one off the list.

### Smaller things

- **Arrows on NEARBY.** Each device shows a green up-arrow when it has come
  closer since its last reading and a red down-arrow when it has moved away;
  under four dB of change shows nothing, which is what a still device does.
- **First of its kind.** The first time this board ever catches a type, the
  card says so.
- **FILL on the message screen.** Eight openings that end in a blank, MEET AT,
  I'M AT, BACK IN and the rest; pick one and the keyboard opens with it typed.
- **Read receipts.** When a squad member opens your message their board says
  so, and yours shows a READ toast with their name. A reader with TRANSMIT off
  can't send one, so you see sent and never read, which is the truth.
- **SNOOZE on an alert.** Quiets that one device until the board restarts. It
  is still scanned, counted and logged; only the alert stops. IGNORE is the
  same thing kept for good.

### Updating the squad

**Settings → SYSTEM → UPDATE FIRMWARE → UPDATE SQUAD** tells every board in
range with your phrase to install the version this one is running. Each of
them shows a thirty-second countdown with SKIP, joins WiFi, installs the
signed release from flasher.oillie.cloud, restarts, and reports back by name to
the board that asked. The sender can share its own saved network with the
nudge, sealed with the phrase; the receiving boards use it once and forget it.

A board listens because it holds your phrase, which is the same trust it
already gives you for messages and the invite; **REMOTE UPDATE** on its
SECURITY screen turns that off for anyone who wants it off. A locked board
ignores the whole thing regardless. So the order on release day is: update
one board by hand, then UPDATE SQUAD from it.

## Project layout

```
MuleSkin-CYD/
├── platformio.ini
├── README.md
├── LICENSE
├── docs/
│   ├── FAQ.md                    (what it does, hardware, legality)
│   ├── DESIGN.md                 (the contract — single source of truth)
│   ├── BUILD.md                  (friendly walkthrough)
│   ├── PINOUT.md                 (CYD pin map)
│   ├── DETECTIONS.md             (per-signature provenance)
│   ├── MULESKINWARE-AESTHETIC.md   (CSS → RGB565 mapping)
│   ├── demo.gif                  (the artwork at the top of this file)
│   └── radar.gif                 (the radar background's source; see tools/)
├── include/
│   ├── state.h                   (DetectionType, Detection, Confidence)
│   ├── theme.h                   (palette, backgrounds, icons, chrome)
│   ├── signatures.h              (the tables and their lookups)
│   ├── detection.h
│   ├── remote_id.h               (ASTM F3411 decoder)
│   ├── clock.h                   (wall clock: NTP at the boot check, zones, the calendar)
│   ├── ignore_list.h             (per-device alert suppression)
│   ├── status_light.h            (the RGB LED and its rules)
│   ├── meshmsg.h                 (sealed frames: messages, emotes, nudges, invites)
│   ├── muleskinmesh.h              (the MuleSkinMesh wire format -- read first)
│   ├── settings.h
│   ├── muleskin.h                 (the mascot -- switched off: MASCOT_SHOWN)
│   ├── bangers_font.h            (generated 1bpp headline face: Orbitron)
│   ├── muleskin_art.h            (generated: the splash artwork)
│   ├── radar_art.h               (generated: the radar background's model)
│   ├── cyd_user_setup.h          (TFT_eSPI config for the CYD)
│   └── ui_*.h
├── src/
│   ├── main.cpp                  (setup/loop, state machine, touch)
│   ├── theme.cpp                 (backgrounds, per-type icons, chrome)
│   ├── muleskin.cpp               (the mascot's state; not drawn)
│   ├── signatures.cpp
│   ├── detection.cpp             (WiFi promiscuous + NimBLE scan)
│   ├── remote_id.cpp
│   ├── clock.cpp
│   ├── ignore_list.cpp
│   ├── pet.cpp
│   ├── muleskinmesh.cpp            (MuleSkinMesh encode/decode, no radio)
│   ├── meshtalk.cpp              (messages, the squad update, the invite)
│   ├── meshcrypto.cpp            (AES-CCM, PBKDF2, and X25519 for invites)
│   ├── status_light.cpp
│   ├── sd_log.cpp
│   └── ui_*.cpp
├── tools/
│   ├── make_boot_art.py          (docs/input.png -> muleskin_art.h)
│   ├── make_radar.py             (docs/radar.gif -> radar_art.h)
│   └── gen_headline_font.py      (Orbitron -> bangers_font.h)
├── test/                         (host tests -- `make -C test`, no framework)
└── sim/                          (PC simulator — compiles src/ natively)
    ├── Makefile                  (`make` builds the simulator)
    ├── *.h                       (Arduino/TFT_eSPI/NVS shims)
    ├── make_readme_demo.py       (renders a simulator clip, docs/demo1.gif)
    ├── make_invite_demo.py       (renders the ADD TO SQUAD clip above)
    └── make_social.py            (renders the repo's social preview card)
```

## License

**GNU General Public License v3.0 (GPL-3.0).** See [LICENSE](LICENSE).

## Credits

- **[skizzophrenic](https://github.com/skizzophrenic)**, author of
  [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD), the
  project MuleSkin-CYD is forked from. The firmware, the detection
  signatures, the web flasher, the simulator and the tests all started
  there.
- Flock Safety OUI research: [@NitekryDPaul](https://x.com/NitekryDPaul),
  DeFlockJoplin, [`colonelpanichacks/flock-you`](https://github.com/colonelpanichacks/flock-you)
  (MIT).
- Axon / skimmer / SSID prefix data: compiled with assistance from
  Gemini (Google), expanded against public sources.
- AirTag manufacturer-data format: public Apple FindMy spec.
- AWOK 2.4" board port (ESP32-Marauder V6.1 hardware): **bkbroiler**,
  who did the actual pin-mapping and shared-bus touch-calibration work
  that made this board possible.

## Status

**Shipping.** CI (`.github/workflows/ci.yml`) builds every flasher board and
runs the host tests on each push, but nothing is published or deployed from
it. `tools/build_flasher_bins.sh` builds every board the web
flasher lists (in Docker, so nothing needs installing) into
`.pio/flasher-bins`, and `web-flasher/Dockerfile` serves the flasher on port
8000 with those bins mounted at `/firmware`. `tools/release.sh 3.1.2 --push`
stamps the manifests, tags and pushes a release; getting it onto
flasher.oillie.cloud, which the boards also check for updates, is a manual
step on the server -- see [deploy/README.md](deploy/README.md).

Detection is reliable for the high-priority targets (Flock, Axon, skimmer,
camera glasses). Remote ID and iBeacon are exact-format matches. Raven,
generic ALPR and the Google tracker network are best-effort — see
[docs/DETECTIONS.md](docs/DETECTIONS.md) for per-signature provenance and
the confidence each one earns.

Verified on real hardware. There is also a PC simulator in `sim/` that
compiles the actual `src/` against shims, and a host test suite in `test/`
(`make -C test`) covering the decoders, the signature tables and the
simulator's own fidelity to the display library.
