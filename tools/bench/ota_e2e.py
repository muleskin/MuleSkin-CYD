#!/usr/bin/env python3
"""A whole WiFi update on the bench: download, signature, reboot, probation.

    SQW_VERSION=3.2.0 PLATFORMIO_BUILD_FLAGS=-DBENCH_TOOLS=1 \\
        pio run -e cyd-ili9341 -t upload --upload-port COM6
    python tools/bench/ota_e2e.py --expect 3.2.1 [--port COM6]

The build claims an older version (SQW_VERSION), so the board will take the
release the site serves; BENCH_TOOLS adds the console's UPDATE NOW, which
runs AUTO UPDATE's whole path with nobody at the board. Then this watches:
the join, the download into the other slot, "signature good", the restart
into --expect, and the probation that confirms it -- and checks, after a
further reboot, that it stayed. Take an nvs_snapshot.py "save" first and a
"compare" after to see that no setting was lost.
"""
import argparse
import sys
import time

from board import Board, DEFAULT_PORT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--expect", required=True, help="the version the site serves, e.g. 3.2.1")
    ap.add_argument("--port", default=DEFAULT_PORT)
    a = ap.parse_args()
    mark = ("SQWVER:v%s;" % a.expect).encode()

    b = Board(a.port)
    b.cmd("UPDATE NOW", 300, mark)
    b.read(40)                              # past the 30 s probation
    b.close()
    keep = ("[ota]", "[bench]", "SQWVER")
    for l in b.lines(*keep):
        if "%" not in l[-5:]:
            print(l[:170])
    text = "\n".join(b.lines())
    steps = {
        "downloaded": "receiving " in text,
        "signature good": "signature good" in text,
        "restarted into %s" % a.expect: mark.decode() in text,
        "on probation": "on probation" in text,
    }
    time.sleep(2)
    again = Board(a.port, boot_s=0)
    again.s.dtr = False; again.s.rts = True; time.sleep(0.1); again.s.rts = False
    after = again.read(20)
    again.close()
    steps["still %s after a reboot" % a.expect] = mark.decode() in after and "on probation" not in after
    for k, v in steps.items():
        print("%-28s %s" % (k, "ok" if v else "FAILED"))
    return 0 if all(steps.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
