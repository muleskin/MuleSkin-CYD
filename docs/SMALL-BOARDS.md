# StickS3 and Cardputer ADV

Both boards have no touch screen. A cursor moves between the buttons on each
screen, and the button it is on lights up whole and shimmers. Lists scroll
under the cursor and loop, so you never page. (Brought over from upstream
SquachWatch v1.42-v1.43, laid out for MuleSkin's own screens.)

On the main screen the cursor hides a while after the last key; the first
key only brings it back.

In TERMINAL MODE (Settings) the main screen is the LOG window: the cursor
stops on the three buttons, the clock, the menu, then each heading (select
sorts, again flips it), each row (select picks it, again opens its WATCH /
HUNT / INFO / IGNORE panel) and the scroll arrows. (Upstream v1.43.)

## M5Stack StickS3

| Do | Press |
|----|-------|
| Next | Front button, click |
| Select | Front button, hold |
| Back | Side button, click. On the main screen it opens the menu |
| Home | Side button, hold |

- **Wiggle** (or shake) wakes a dark screen. POWER SAVER is on by default
  here and the screen goes dark after a minute.
- No SECURITY menu and no typing: a PIN set by an older build never locks
  it. No WIFI NETWORKS and no UPDATE OVER WIFI either (there is no way to
  type a password); it updates over Bluetooth.
- No GPS, no SD card, no wardriving, no sound.

## M5Stack Cardputer ADV

Not the original Cardputer.

| Do | Press |
|----|-------|
| Move | Arrows (`;` `,` `.` `/`) or TAB |
| Select | ENTER or SPACE |
| Back | ESC or DEL. On the main screen, ESC opens the menu |
| Home | Hold ESC 3 seconds, from anywhere, even while typing |

Hotkeys (not while typing):

| Key | Where | What |
|-----|-------|------|
| W | main screen | WIFI TIME |
| L | main screen | LOG |
| M | main screen | IN MEETING |
| T | main screen | TODAY |
| S | main screen | the menu |
| any letter | other screens | presses the button whose label starts with it. Two matches: the key steps between them and ENTER presses |

Every screen's letters: [CARDPUTER-HOTKEYS.md](CARDPUTER-HOTKEYS.md).

- The WiFi password, MY ALERTS' pattern and label, and the PIN pad type on
  the keyboard. DEL deletes, ENTER is OK, TAB shows or hides a password.
  FN + arrow still moves.
- Wiggle and shake wake a dark screen as on the StickS3. POWER SAVER is off
  by default here.
- SD card in its own slot: the log and the wardrive files go there. Some old
  cards never answer; a new card works.
- No sound: MuleSkin has no speaker layer for its codec yet.

### GPS (M5Stack Unit GPS on the Grove port)

Plug it into the Grove port. It runs at 115200 (the v1.1) and the board
falls back to 9600 and 38400 on its own if nothing parses.

- **WARDRIVE** in Settings logs every WiFi network and Bluetooth device it
  hears, with position, to a WiGLE file on the SD card. Rows are written only
  with a real fix.
- The clock sets itself from the GPS, and positions go on the SD log rows
  and the alert, as on the other GPS boards.
- The first fix after a power-off can take a few minutes outside. Indoors it
  may never come. `GPS STATUS` on the console says what it hears.

## Not brought over

- Upstream's FOB MODE (it takes the place of DESK, which MuleSkin does not
  have), and knocking or shaking to poke or toss the mascot.
- The Cardputer's EXT SCREEN (the XL mod's second panel), WARDRIVE SOUND
  and gps-status.log, and both boards' speaker tones.
