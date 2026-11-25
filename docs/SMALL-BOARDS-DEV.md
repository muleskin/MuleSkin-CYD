# Developing on the StickS3 and Cardputer ADV

Neither board has a touch screen, so you cannot tap your way to a screen from
the desk. The dev builds let the serial console press the buttons and dump the
screen, and three scripts in `tools/` turn that into PNGs. (Upstream
SquachWatch v1.42-v1.43.)

## Dev builds

```
pio run -e sticks3-dev -t upload
pio run -e cardputer-adv-dev -t upload
```

The release build plus `-DSQW_DEVTOOLS=1`. Never ship these: the console can
press anything. Release builds ignore every command below.

The console is the USB serial port at 115200. Close any monitor before you
upload or run a script: one program at a time holds the port. The Cardputer
only shows up on USB with its power switch on.

## Console commands

Dev builds only:

| Command | What it does |
|---|---|
| `KEY NEXT` `KEY PREV` `KEY BACK` `KEY HOME` `KEY SELECT` | The real buttons. Each NEXT/PREV prints `[keys] stop i of n at x,y (screen s)` |
| `KEY PRESS [ms]` | A press of that length at the cursor (default 120) |
| `TYPE text` | Cardputer: types it (`~` is ENTER, `^` is DEL). Off a text screen, letters are hotkeys |
| `SHOT` | Dumps the frame as text for `tools/shot.py` |
| `IMU` | Toggle: prints the accelerometer's rate and excursions, for tuning `shake.cpp` |

Every build, useful here: `GPS STATUS`, `GPS FAKE lat lon` / `GPS OFF`,
`WARDRIVE ON` / `WARDRIVE OFF` (the Cardputer with a Grove GPS).

## tools/

All three need pyserial. PlatformIO's Python has it:
`~/.platformio/penv/Scripts/python tools/shot.py ...` (`penv/bin` off Windows).

**shot.py**: keys, then a PNG of the screen.

```
tools/shot.py PORT home.png                     # what is on screen now
tools/shot.py PORT log.png home next next press # home, two stops on, press
tools/shot.py PORT - back                       # keys only, no picture
```

It prints `out.png: 240x135 state N`. N is the AppState (include/state.h).

**goto.py**: step the cursor until it is on a point, then press.

```
tools/goto.py PORT 13 11 press    # the menu, wherever it is in the stop order
```

Stop numbers change with what is on screen. Positions do not.

**sheet.py**: tile captures into one image.

```
tools/sheet.py settings.png 4 s1.png s2.png s3.png s4.png
```

## Recipes

**Every row of a list.** Open it, then loop `tools/shot.py PORT sN.png next`,
and tile the results with sheet.py. Lists loop, so stop when the first row
comes back.

**Check a new screen's stops.** Open it and send `KEY NEXT` a few times. Each
stop prints where it is. A stop on empty space means the screen needs its own
stop source: see `stickStopsBuild()` in main_keycursor.inc (a list's hit test in
`stickListId()`, a screen of buttons in `stickSweepId()`).

**Check the highlight.** `tools/shot.py PORT a.png next`. The whole button
should be lit. If only part of it is, the screen's hit test changes id inside
the button (`stickRegionId()`).

**Where BACK goes.** `stickBackPoint()`: the screen's own BACK, found with a
hit test that only answers. A screen with none goes home. Never a guessed
point.

## Gotchas

- On the main screen the cursor hides after 8 s idle. The first NEXT or PREV
  then only shows it. Read the `[keys] stop` line before you send a PRESS: a
  blind press on the wrong button changes real settings.
- Scripted presses are real presses. Check what you changed and put it back.
- A dump is about 65 KB of text at 115200. Give it a few seconds; shot.py
  retries.

## How SHOT and KEY work

Behind `SQW_DEVTOOLS`:
- `SHOT` sets a flag. Just before the frame is pushed, `devShotDump()` prints
  `[shot] BEGIN w h state`, one `R<y>:<hex>` line per row of the 8-bit
  (RGB332) frame, then `[shot] END`. Every row is labelled because other tasks
  print in between, and writes are retried because USB CDC drops what does
  not fit.
- `KEY ...` sets `g_devKey`, which `devKeysMerge()` folds into the real keys
  before `stickPoll()` sees them. The T-Deck's trackball build reads them too
  with `-DSQW_DEVTOOLS=1`.
