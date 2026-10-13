# Bench scripts

For a board plugged into this computer: the checks a release gets on real
hardware before it goes out, which the host tests and the emulator cannot
do. Python 3 with pyserial (PlatformIO's own Python has it:
`~/.platformio/penv/Scripts/python`). The port is `--port`, or
`MULESKIN_PORT`, or COM6.

Opening the port can reset the board, and only one program can hold it: close
the web flasher's tab (or its serial connection) first, or the script gets
"Access is denied".

| Script | What it checks |
|---|---|
| `nvs_snapshot.py save F` / `compare F` | No setting lost across a flash, an update or a migration. Values are hashed, never printed: an export holds WiFi passwords and the push token. |
| `push_bench.py [--alerts]` | PUSH ALERTS against a stand-in ntfy on this computer: a push, the command-topic poll and an IGNORE landing on the list, the console's ignore list; with `--alerts` (a `BENCH_TOOLS` build) an alert's Ignore / Snooze buttons and the hour's cooldown. Puts the board's push URL, PUSH WHAT, PUSH AT NIGHT and ignore list back. |
| `ota_e2e.py --expect X.Y.Z` | A whole signed WiFi update from flasher.oillie.cloud: download, signature, restart, probation, and still there after a reboot. Needs a `BENCH_TOOLS` build claiming an older version (see the script). |

The usual round before a release:

```bash
python tools/bench/nvs_snapshot.py save before.json
SQW_VERSION=3.2.0 PLATFORMIO_BUILD_FLAGS=-DBENCH_TOOLS=1 pio run -e cyd-ili9341 -t upload --upload-port COM6
python tools/bench/push_bench.py --alerts
python tools/bench/ota_e2e.py --expect 3.2.2
python tools/bench/nvs_snapshot.py compare before.json
```

Bench builds (`PLATFORMIO_BUILD_FLAGS=-DBENCH_TOOLS=1`) also take, on the
console: `ALERTPUSH` (a made-up Flock alert through the push path),
`DAYSFAKE aa:bb:cc:dd:ee:ff` (the regulars told that device was seen on the
three days before today -- the "near you on N days" alert without waiting),
`ADVDUMP [seconds]` (prints `ADV <type> <mac> <hex>` for every tracker heard:
the source of `test/tracker_adverts.txt`), `ARTBENCH`, `UPDATE NOW`.

then flash the normal build back. Never run two `pio` builds at once: they
share `~/.platformio/packages`, and a collision once deleted a framework and
hung an upload.
