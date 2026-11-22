# M5Stack Tab5 (ESP32-P4 + ESP32-C6) — BETA

A 5" tablet: an **ESP32-P4** application processor with a 1280×720 MIPI-DSI
panel and capacitive touch, and an **ESP32-C6** beside it for the radios. Its
main chip has **no radio at all**: every WiFi frame and every Bluetooth advert
the detector sees comes across an SDIO link from the C6.

Brought over from upstream SquachWatch v1.47, where skizzophrenic brought it
up on a real Tab5 and ships it as a BETA. **Here it is build-it-yourself and
untried**: nobody has run MuleSkin on a Tab5 yet. It is not on the web
flasher, and it is not in the release or CI board lists.

| | Classic CYD (ESP32-2432S028R) | M5Stack Tab5 |
|---|---|---|
| Main chip | Xtensa LX6, dual-core 240 MHz | **ESP32-P4**, RISC-V, dual-core 360 MHz |
| Radio | on the chip | **none on the P4**: an ESP32-C6 over SDIO |
| WiFi | 2.4 GHz | 2.4 GHz (the C6's) |
| Bluetooth | BLE 4.2 + Classic | BLE 5 (the C6's), no Classic |
| Screen | 2.8" 320×240 SPI | **5" 1280×720 MIPI-DSI** (two lanes) |
| Touch | resistive XPT2046 | capacitive, in the panel chip |
| Flash / PSRAM | 4 MB / none | **16 MB / 32 MB** |
| USB | CH340 | the P4's own USB-serial-JTAG (USB-C) |

## What is in and what is not

| | State here |
|---|---|
| Screen, landscape 1280×720 | In (upstream: works, 60 fps on a still background) |
| Touch | In, no calibration (upstream: works) |
| WiFi detection | In, through the C6 (upstream: every WiFi category of its emitter walk caught) |
| Bluetooth detection | In, through the C6 (upstream: every tracker caught) |
| RING | Built in: the C6's controller carries connections and the core's NimBLE host has the central role. **Not tried anywhere**: upstream's Tab5 has no RING |
| Radio update | In: the app writes the C6 itself on first boot (upstream: proven from Espressif's stock 2.12.13) |
| WIFI NETWORKS scan | In: the C6's own scan, passive, over ESP-Hosted |
| WiFi updates, WIFI TIME, PUSH ALERTS, extra rules | **No**: the C6's radio sniffs, it does not join a network yet. `src/tab5_stubs.cpp` answers for them |
| Bluetooth firmware updates | No (`ota_ble.cpp` is out of the build) |
| The karma check | No: the C6 extension does not forward probe responses |
| HUMAN SENSING | **No**. Upstream senses on the Tab5 through its C6 with an engine and screens of their own (`sense_tab5.cpp`, `ui_sensing_tab5.cpp`), not MuleSkin's `src/sense_cyd/`; they were left out. The C6 image does carry upstream's sensing extension (see below); it stays idle |
| Portrait | No: the picture is locked landscape |
| SD card, speaker, battery, power button | Not driven |
| Status light, BOOT button | None |
| Web flasher | No: build it yourself |

## Build and flash

The P4 does not exist in the Arduino core every Xtensa board is pinned to, so
`[env:tab5]` carries its own `platform` line: the same pioarduino release as
the ESP32-C5 boards (Arduino 3.3.12 / IDF 5.5.5).

```bash
pio run -e tab5 -t upload
```

Upstream builds it from a separate PlatformIO home (`PLATFORMIO_CORE_DIR`)
because the pioarduino installer once broke `pio` for every other board; here
it builds from the normal one beside the C5s.

Upstream's notes on the console, which is the P4's USB-serial-JTAG at 115200:

- **DTR and RTS cannot reset the P4.** Opening the port does nothing to the
  board. Open the port first, then power-cycle or flash, to see a boot log.
- **Prints never wait for a reader.** With the cable in and nothing reading,
  the core's USB serial blocked on every print and the UI froze only while
  plugged in. `Serial.setTxTimeoutMs(0)` drops output nobody is reading.
- **Use a motherboard USB port on the back of the PC.** On a front-panel port
  upstream's Tab5 browned out after about 70 seconds with the radio sniffing.

Two bench builds sit beside it: `tab5-paneltest` (M5GFX alone: colour bars,
the panel's size, every touch as x,y) and `tab5-radiotest` (the C6 link alone:
power, SDIO, the hello, then 30 seconds of sniffing on channels 1, 6 and 11
with one line per frame). Flash `tab5-paneltest` first on a Tab5 that has never
run this firmware.

## The screen

The app draws a **640×360** frame and the board doubles it to 1280×720, the
same arrangement as the CrowPanel 7. 640×360 at 8 bits is 225 KB, which fits
in the P4's internal SRAM; the full 1280×720 would be a 900 KB frame in PSRAM
and four times the drawing for the same layouts. MuleSkin's screens lay out
from the canvas size, so they take the 640×360 the way they take the 3.5"'s
480×320.

- **M5GFX owns the panel.** It probes the two I²C expanders, tells this unit's
  ST7123 apart from the earlier ILI9881C panel, brings up DSI, and hands over a
  720×1280 RGB565 framebuffer in PSRAM that the DSI engine scans out by itself.
- **The glass is portrait as wired**, so `src/tab5_blit.cpp` turns the picture
  a quarter turn as it copies: logical (x, y) lands at native (719 − y, x). It
  writes one logical column as two native rows, in order (the fast direction
  for PSRAM), and only the columns that changed since the last frame, with a
  full frame every so often so a stale line cannot stick.
- **TFT_eSPI is kept as a type, not a driver.** Every UI function takes a
  `TFT_eSPI&`, so the class stays; `include/tab5_display.h` makes its bus
  calls inert and `extra_script.py` lets it compile for a chip it has never
  heard of (the library's Generic processor).
- **The frame lives in internal SRAM** on purpose (`TAB5_FRAME_INTERNAL`): the
  blit reads it column by column, which is cheap in SRAM and slow in PSRAM.

Upstream's other 640×360 work (backgrounds sized for the wider band, the FONTS
page's starting sizes for the Tab5) is about screens MuleSkin does not have:
there is no FONTS page here, and MuleSkin's backgrounds already size from the
canvas.

## The radio: an ESP32-C6 over SDIO

The P4 reaches the C6 through Espressif's **ESP-Hosted**: the host half is
built into the Arduino core, and the C6 runs the slave half. Stock ESP-Hosted
carries WiFi *connections* and Bluetooth HCI. It has **no promiscuous mode**,
and that is what the detector needs.

```
 ESP32-P4 (the app)                               ESP32-C6 (the radio)
 ───────────────────                              ────────────────────
 detection.cpp  ── WiFi frames ◄── custom msgs ◄── squach_sniff.c
                                     (SDIO)          (upstream's extension)
 esp-nimble-cpp ◄─ NimBLE host ◄── HCI over SDIO ◄─ BT controller (stock)
```

### WiFi: upstream's sniff extension

`tools/tab5-c6/squach_sniff.c` is upstream's addition to the stock ESP-Hosted
2.12.13 slave. It boots exactly like the stock image and does nothing until
the P4 asks. Commands and events travel on ESP-Hosted's custom-message
channel:

| ID | Direction | What |
|---|---|---|
| `0x5100` SNIFF | P4 → C6 | promiscuous on/off |
| `0x5101` CHANNEL | P4 → C6 | park on one channel |
| `0x5102` HOP | P4 → C6 | channel list and dwell; the C6 hops on its own clock |
| `0x5103` HELLO | P4 → C6 | is the extension in this image? |
| `0x5180` ACK | C6 → P4 | command and its result |
| `0x5181` HELLO | C6 → P4 | `squach-sniff 2` (1 on older images) and whether it is sniffing |
| `0x5200` FRAME | C6 → P4 | RSSI, channel, type, then the frame |
| `0x5201` STATS | C6 → P4 | seen / forwarded / dropped, once a second |

What gets forwarded:

- **Management frames** the detector reads (beacon, probe request, deauth,
  disassociation) go over **whole**, up to 600 bytes, so the P4 parses SSIDs,
  Remote ID, pwnagotchi payloads and RSN exactly as every other board does.
  Probe responses are not among them, so the karma check never fires here.
- **Data frames** go over as their **24-byte header only**, at most one per
  sender every two seconds.

On the P4, `src/tab5_radio.cpp` owns the link and queues frames, and
`detection.cpp`'s `onWifiFrame()` (the same parser every other board's
promiscuous callback now calls) reads them from the loop. MuleSkin's engine
drives the hop clock, one CMD_CHANNEL a hop.

### Bluetooth: the C6's controller, NimBLE on the P4

Bluetooth needs no extension. The stock slave already runs the C6's Bluetooth
controller with its HCI on the same SDIO link, and the Arduino core for the P4
already includes the NimBLE host, built to talk to it.

- **NimBLE-Arduino cannot be used here.** It brings its own copy of the NimBLE
  host and expects a Bluetooth controller on the same chip, and the P4 has none.
- **esp-nimble-cpp 2.5.0 is used instead.** It has the same C++ API (same
  author, same version line) and runs on the host the core already has, so
  `detection.cpp`'s scanner and `tag_ring.cpp` are the code every other board
  runs. It needs four `CONFIG_NIMBLE_CPP_*` defines in `platformio.ini`,
  because an Arduino build never reads its Kconfig. The host's roles and sizes
  are the core's own (central on, three connections), not MuleSkin's
  `nimble_flags_*`.
- **The order matters:** link up (`Tab5Radio::begin()`), then the C6's
  controller on (`Tab5Radio::bleBegin()`), then `NimBLEDevice::init()`.
  Without the controller, `init()` waits for a sync that never comes, forever.
  So a boot whose link is down skips Bluetooth entirely, and every tick that
  would post to NimBLE's host task checks first (`bleLive()`).

### The C6 needs upstream's firmware

A Tab5 from the box has M5Stack's factory C6 firmware (ESP-Hosted 1.4.1). This
build needs **ESP-Hosted 2.12.13 plus the sniff extension** on the C6. Without
the extension the P4's hello goes unanswered, and the board runs with WiFi and
Bluetooth both off and says so on the console.

The C6 is written **from the P4, over the SDIO link**, by the app itself. The
firmware carries upstream's C6 image (`tools/tab5-c6/c6_radio.bin`, linked in
with `board_build.embed_files`) and, when the link comes up but the hello goes
unanswered, shows a **RADIO UPDATE** screen. UPDATE streams the image with
ESP-Hosted's own slave OTA, then restarts; the panel init power-cycles the C6,
which comes up on the new image. Keep the Tab5 plugged in; it takes about a
minute. LATER boots on without WiFi and Bluetooth and asks again next boot.
The image is checked before a byte is sent, each try is counted first (NVS
`tab5c6/tries`), and after two writes that never brought a hello the app stops
offering. No wires are needed.

That image is upstream's own build: ESP-Hosted 2.12.13 with both of
upstream's extensions, the sniff extension and its HUMAN SENSING extension
(hello tag `squach-sniff 2`). MuleSkin never asks the sensing half for
anything. How it is built is in [tools/tab5-c6/README.md](../tools/tab5-c6/README.md).

> **There is no rollback on the C6.** An image that boots and then crashes
> leaves the radio dead until it is rewritten over wires: six pads beside the
> C6 module (GND, G9, RST, RXD, TXD, 3V3) and a 3.3 V USB-serial adapter.
> M5Burner has the factory C6 image. This is why the extension does nothing at
> boot.

## Facts that each cost upstream an evening

- **The versions must match.** The core's ESP-Hosted host is **2.12.13**, so
  the C6 slave must be 2.12.13 too, not the newest release.
- **The C6's reset is active-high on this unit.** GPIO15 resets it.
  ESP-Hosted's own Tab5 preset says active-low, and with that the C6 never
  answers. The app sets the Tab5's SDIO pins at run time.
- **The link sometimes comes up on the second try.** The app retries once.
- **The C6's WiFi driver does not start itself.** Sniffing is refused until
  the P4 calls the remote `esp_wifi_init()`, `set_mode()` and `start()`.
- **An empty custom message is refused** by the host library before it is
  ever sent. The hello carries one byte.
- **The core's host has room for three custom-message handlers.** ACK and
  FRAME stay registered; HELLO is registered for the handshake and then
  swapped for STATS.
- **The C6's power** is bit 0 of output register 0x05 on the I²C expander at
  0x44. M5GFX already sets it at panel init; the app checks it took.

## Pinout

From M5GFX and M5Stack's reference board support. The display, touch and
expanders are all M5GFX's; the firmware only names them.

| What | Pins |
|---|---|
| I²C (touch, expanders) | SDA 31, SCL 32 |
| Touch interrupt | 23 (touch controller at 0x55, inside the ST7123) |
| Backlight | 22 |
| C6 SDIO | CLK 12, CMD 13, D0 11, D1 10, D2 9, D3 8 |
| C6 reset | 15, **active-high** |
| Expander 0x43 | P4 LCD reset, P5 touch reset, P1 speaker enable, P2 5 V out |
| Expander 0x44 | P0 C6 power, P3 USB 5 V, P7 charge enable |

## Memory and flash

- **Partitions:** the 16 MB two-slot table (`partitions_ota_16mb.csv`) shared
  with the C5 boards: two 6 MB app slots, a core dump, MuleSkin's 128 KB
  settings store at 0xC20000 and the black box after it at 0xC40000. A Tab5
  coming from another firmware may have a settings store at 0x9000; the first
  boot copies it across like any other board's (`src/nvs_move.cpp`).
- **The app** is about 2.7 MB with the 1.15 MB C6 image inside it, under half
  its slot. Static RAM is about 71 KB of 512 KB.

## Before calling it more than BETA

Upstream ticked these on its board (2026-10-19): boots and runs the UI at
1280×720, touch lands where pressed, no lag on the cable, the C6 link answers,
WiFi and Bluetooth detection through the C6 with no brownout on a back USB
port. **None of it has been seen on MuleSkin.** To check on a board:

- [ ] Boots to the radar at 1280×720; touch lands where it is pressed
- [ ] A Tab5 with M5Stack's C6 firmware shows RADIO UPDATE, takes it, and
      comes back with `[c6] hello: "squach-sniff 2"` on the console
- [ ] WiFi detection (`RADIO` on the console shows frames from the C6)
- [ ] Bluetooth detection, and a boot with the link down skips it cleanly
- [ ] RING on a tracker
- [ ] WIFI NETWORKS lists networks from the C6's scan
- [ ] An hour or more with both radios: heap flat, no resets
- [ ] Settings persist across a power cycle
