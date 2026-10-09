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
> the board ports and most of what follows are their work; this fork rebrands
> it as MuleSkin, reworks the look and the main screen, and leaves out the
> board-to-board squad mesh. Like the original, it is GPL-3.0.

<p align="center">
  <img src="docs/demo.gif" width="280"
       alt="MuleSkin seen from behind: a hooded figure with mule ears sitting at a computer in a dark room, ears twitching">
</p>

## What it detects

| Type | What | How |
|---|---|---|
| `FLOCK` | Flock Safety ALPR cameras | 29 WiFi OUI prefixes + BLE name + company ID `0x09C8` (the Penguin battery packs' radio; Medium unless the name agrees) |
| `AXON` | Axon body cameras, TASERs, LE equipment | 3 WiFi OUI + SSID prefixes `AB2-`/`AB3-`/`AB4-`/`AXON-` |
| `META` | Camera glasses — Ray-Ban Meta, Snap Spectacles | BLE service UUID `0xFD5F` + Meta / Luxottica / Snap company IDs |
| `SKIMMER` | Bluetooth card skimmers (HC-05/06/03, RN42, BT04-A) | BT Classic name match + SPP UUID `0x1101` + 3 OUI |
| `RAVEN` | Raven gunshot detector | Service UUIDs `0x3100`–`0x3500` |
| `AIRTAG` | Apple AirTag / Find My trackers | Company ID `0x004C` + Find My payload check |
| `DRONE` | Remote ID drones | Service UUID `0xFFFA`, then the ASTM F3411 message **decoded** — aircraft position, altitude, serial, and the operator's location |
| `ALPR` | Motorola Solutions / Genetec plate readers | 6 WiFi OUI |
| `CAMERA` | Generic / covert IP cameras | 17 WiFi OUI (Wyze, Amazon, Tuya, Verkada, Avigilon, Axis, …) |
| `SAMSUNG_TAG` | Samsung Galaxy SmartTag / SmartTag+ | BLE service UUID `0xFD5A` |
| `GOOGLE_TAG` | Google Find My Device trackers (Chipolo, Pebblebee, Moto Tag) | Service data under `0xFEAA` with the tracker frame type `0x40`/`0x41` -- not the plain Eddystone beacons that share the UUID |
| `TILE` | Tile BLE trackers | BLE service UUID `0xFEED` / `0xFEEC` |
| `RING` | Ring doorbells / cameras | 15 WiFi OUI (Ring LLC's registered block + Amazon's) |
| `DEAUTH` | WiFi deauthentication floods | Rate-detected burst, not a signature |
| `EVILTWIN` | Rogue / spoofed access points | One SSID beaconing from two BSSIDs that disagree about encryption |
| `IBEACON` | Retail proximity beacons | Exact Apple header `4C 00 02 15` — **off by default**, see below |
| `HACKER` | Flipper Zero, Pwnagotchi, WiFi Pineapple, ESP deauthers | Flipper's service UUIDs `0x3080`–`0x3083`, company ID `0x0E29` and OUI `0C:FA:22`; the Pwnagotchi's own beacon payload; `Pineapple_` and `pwned` SSIDs |

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

### New rules without a new release

The table above is what is built in. On top of it, a board with a saved WiFi
network fetches a **signed rule set** from flasher.oillie.cloud at every boot
check (`web-flasher/signatures.txt`): extra address prefixes and WiFi
network names, used only when the signature checks out. Rules only ever add:
a prefix the firmware already knows keeps its built-in meaning. Rule set 2
adds 44 prefixes read out of the IEEE registry -- WatchGuard, Digital Ally
and BodyWorn body cams, ShotSpotter, Neology and Kapsch plate readers, and
Axis, Hanwha, Hikvision, Dahua, Uniview, Reolink, Amcrest and other camera
makers -- and three camera setup hotspots (`davinci`, `HAP_`, `DAP-`).

## Hardware

- **ESP32-2432S028R** ("Cheap Yellow Display" / CYD) — about $15.
  Built-in 320×240 ILI9341 TFT, XPT2046 resistive touch, and an
  onboard microSD card slot.
- **Elecrow CrowPanel Advance 7.0** — experimental: an ESP32-S3 with an
  800×480 RGB panel and GT911 touch, via the `crowpanel7` build. It renders
  at 400×240 doubled, on purpose. See [board setup and testing](docs/CROWPANEL7.md).

That's it. No GPS, no extra modules. The CYD is the whole device. A small
speaker on its SPEAK connector is optional, and stays silent unless you
switch BUZZER on.

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

The same page talks to a plugged-in board over the cable:

- **Set Time & Zone** sends this computer's clock and time zone.
- **Back up / Restore settings** keeps your settings across an install that
  erases the board.
- **Download log** saves every detection the board has kept as a
  spreadsheet; **Crash report** reads what it kept about its last crash.
- **Self-test** checks the board's parts -- memory, touch, both radios, the
  black box, SD card, settings, clock, WiFi, update keys, rules -- and lists
  anything that needs a look.
- **Live view** shows what the board is detecting right now, live.

Every release is also on [GitHub Releases](https://github.com/muleskin/MuleSkin-CYD/releases)
with the bins for every board attached, and the page keeps the previous
releases in a version picker.

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
up; cyan while an update downloads and green or red for how it went. It goes
dark on the lock screen and through a wipe, so a duress restart looks like any
other restart from the back too.

**Settings → APPEARANCE → STATUS LIGHT**: the master switch, alerts on or
off, idle breathe or solid or off, an idle colour that follows the theme, the
background, or one of nine fixed colours, brightness in seven steps, and a
TEST row that plays an alert and the idle in four seconds. Boards whose LED pins
have not been checked (the AWOK and the 3.5") compile it out and say so on
that screen.

## Sound and phone alerts

**BUZZER** (Settings, off by default) chirps once for a device the board has
never logged before (not at night or with the screen dimmed), and for every
WITH YOU, whenever it comes. The CrowPanel 7 has a buzzer; on the 2.8" CYD, plug a small 8 ohm
speaker into the two-pin SPEAK connector.

**PHONE ALERTS** (Settings, 2.8" CYD builds, off by default) puts the board's
alerts on your phone over Bluetooth: no WiFi, no internet, no app. Switch it
on, open [flasher.oillie.cloud/phone](https://flasher.oillie.cloud/phone/) on
the phone (Chrome on Android; on an iPhone, the free Bluefy browser, since
Safari has no Bluetooth), tap CONNECT and pick MuleSkin-XXXX. Every alert the
board raises on its own screen, and every WITH YOU, then arrives as a
notification for as long as that page stays open. The row reads WAITING, then
CONNECTED.

The page keeps a **history** on the phone, newest first, with a **map**
(OpenStreetMap) and **CSV** and **KML** exports for a spreadsheet or Google
Earth. Tick *Record where each alert happened* and each alert is stamped with
the phone's own location: the board has no GPS, but the phone in your pocket
does, so this is where a camera was seen. The history and the locations stay
on that phone until you press Clear; the board takes nothing from the phone.

With **PHONE CODE** off (the default) the first phone to connect gets the
alerts. Switch it on and the row shows four digits -- new ones every time --
that a phone has to send before it gets anything; the page asks once and
remembers them. A phone without the code is let go after 30 seconds, or after
three wrong guesses, so a stranger cannot hold the one connection slot, and
the alert itself cannot be read off the board without it. To make room, the
CYD builds no longer offer firmware updates over Bluetooth; USB and WiFi
updates are unchanged.

## Updates

### Knowing there is an update

Nothing installs on its own unless you ask it to. At boot, a board with a
saved WiFi network joins it for about a second, asks flasher.oillie.cloud for
the latest version of its own build, and lets go again, all before Bluetooth
starts; **UPDATE CHECK** on the SYSTEM page turns that off. When there is a
newer one, the SYSTEM row reads UPDATE, and UPDATE FIRMWARE names the version
until you install it.

**AUTO UPDATE** on the SYSTEM page (off by default) installs it for you, at
night: during NIGHT DIM's hours (1 to 5 AM without them), from the main
screen, after ten minutes untouched and unlocked. It shows a 30 second
countdown with SKIP first, then joins the saved WiFi, installs, checks and
restarts; anything that fails leaves the old version running. The daily AUTO
TIME join checks for a new release too, so a board that is never restarted
still hears about one.

It has two speeds. **AFTER 3 DAYS** waits three days from when that board
first heard of the release; **EARLY** goes the first night. Set the board you
test on to EARLY and the rest to AFTER 3 DAYS: a release that turns out bad
shows itself on the test board, and once it is pulled from the site the
others never install it.

**WIFI NETWORKS** on the SYSTEM page is where the board keeps the networks it
knows: up to six, with USE marking the one it tries first. ADD picks one from
a scan and takes the password on the board's keyboard; it is not checked by
joining, since joining means giving Bluetooth up until a restart, but at the
next boot check, and each row then says how that went: joined, wrong
password, or not found. At boot the board scans, joins the USE network if it
is there and otherwise the strongest saved one that is, so home and work both
just work. The update flow does the same, and only shows its own list when
none of the saved networks is in range. REMOVE takes one off the list.

## Smaller things

- **Arrows on NEARBY.** Each device shows a green up-arrow when it has come
  closer since its last reading and a red down-arrow when it has moved away;
  under four dB of change shows nothing, which is what a still device does.
- **First of its kind.** The first time this board ever catches a type, the
  card says so.
- **SNOOZE on an alert.** Quiets that one device until the board restarts. It
  is still scanned, counted and logged; only the alert stops. IGNORE is the
  same thing kept for good.

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
│   ├── phone_alerts.cpp          (PHONE ALERTS: alerts to a phone over Bluetooth)
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
runs the host tests on each push. `tools/release.sh 3.1.5 --name ... --note
... --push` stamps the manifests with the version and the release notes the
boards show, tags and pushes it; the tag builds a GitHub Release with every
board's bins (`.github/workflows/release.yml`). Getting it onto
flasher.oillie.cloud, which the boards check for updates, is one command on
the server -- see [deploy/README.md](deploy/README.md) -- which builds the
bins, signs them and the rule set with the release key, and deploys.

Detection is reliable for the high-priority targets (Flock, Axon, skimmer,
camera glasses). Remote ID, iBeacon and Google's tracker frame are
exact-format matches. Raven and generic ALPR are best-effort — see
[docs/DETECTIONS.md](docs/DETECTIONS.md) for per-signature provenance and
the confidence each one earns.

Verified on real hardware. There is also a PC simulator in `sim/` that
compiles the actual `src/` against shims, and a host test suite in `test/`
(`make -C test`) covering the decoders, the signature tables and the
simulator's own fidelity to the display library.
