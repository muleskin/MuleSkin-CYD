#!/usr/bin/env python3
"""Snapshot a board's settings, and compare against an earlier one.

    python tools/bench/nvs_snapshot.py save before.json
    ... flash, update, migrate ...
    python tools/bench/nvs_snapshot.py compare before.json

Values are stored and compared as SHA-256 hashes, never printed or kept in
the clear: an export holds WiFi passwords and the push token. Take the
"save" right before the change under test -- a snapshot from hours earlier
also catches every setting changed on the board since.
"""
import argparse
import hashlib
import json
import sys

from board import Board, DEFAULT_PORT

VOLATILE = {"clock/last", "settings/runs", "muleskinpet/boots", "dhcp_state/WIFI_STA_DEF"}


def snapshot(b):
    return {"%s/%s" % k: hashlib.sha256(("%s %s" % v).encode()).hexdigest() for k, v in b.nvs().items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["save", "compare"])
    ap.add_argument("file")
    ap.add_argument("--port", default=DEFAULT_PORT)
    a = ap.parse_args()
    b = Board(a.port)
    now = snapshot(b)
    for l in b.cmd("SELFTEST", 8).splitlines():
        if "settings store" in l:
            print(l.strip())
    b.close()
    print("%d keys" % len(now))
    if a.mode == "save":
        json.dump(now, open(a.file, "w"), indent=1)
        return 0
    old = json.load(open(a.file))
    missing = sorted(set(old) - set(now))
    for k in missing:
        print("MISSING", k)
    for k in sorted(set(now) - set(old)):
        print("NEW    ", k)
    for k in sorted(set(old) & set(now)):
        if old[k] != now[k]:
            print("CHANGED", k, "(expected)" if k in VOLATILE or k.startswith("ota/") else "")
    print("no keys lost" if not missing else "%d keys lost" % len(missing))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
