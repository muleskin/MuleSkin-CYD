#!/usr/bin/env python3
"""How full a build's app slot is, from the build itself.

    tools/check_size.py <env> [--previous BYTES] [--tag vX.Y.Z]

Reads .pio/build/<env>/firmware.bin and the partition table the same build
wrote (.pio/build/<env>/partitions.bin), so the limit is the slot this board
really flashes into -- 1.9 MB on the 4 MB boards, more on the bigger ones --
and never a number copied out of a CSV by hand.

Prints one Markdown table row (CI appends it to the step summary). Above
WARN_AT it prints a GitHub warning on stderr; above FAIL_AT it exits 1, so a release
that would not fit (or would leave no room for the next fix) fails in CI
instead of failing to flash. --previous is the same board's firmware in the
last release, for the growth column.
"""
import argparse
import os
import struct
import sys

WARN_AT = 0.90
FAIL_AT = 0.95

ENTRY = struct.Struct("<HBBII16sI")   # magic, type, subtype, offset, size, label, flags
MAGIC = 0x50AA
TYPE_APP = 0x00
SUB_FACTORY, SUB_OTA_0 = 0x00, 0x10


def app_slot(table: bytes):
    """(label, size) of the slot the app runs from: ota_0, else factory."""
    apps = []
    for off in range(0, len(table) - ENTRY.size + 1, ENTRY.size):
        magic, ptype, sub, _, size, label, _ = ENTRY.unpack_from(table, off)
        if magic != MAGIC:
            break                      # 0xEBEB (the MD5 row) or 0xFFFF ends the table
        if ptype == TYPE_APP:
            apps.append((sub, label.split(b"\0")[0].decode(), size))
    for want in (SUB_OTA_0, SUB_FACTORY):
        for sub, label, size in apps:
            if sub == want:
                return label, size
    if apps:
        return apps[0][1], apps[0][2]
    raise ValueError("no app partition in the table")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("env")
    ap.add_argument("--build-dir", default=".pio/build")
    ap.add_argument("--previous", type=int, help="bytes of this board's last released firmware")
    ap.add_argument("--tag", default="last release", help="what --previous was")
    a = ap.parse_args()

    d = os.path.join(a.build_dir, a.env)
    used = os.path.getsize(os.path.join(d, "firmware.bin"))
    with open(os.path.join(d, "partitions.bin"), "rb") as f:
        label, limit = app_slot(f.read())

    frac = used / limit
    growth = ""
    if a.previous:
        delta = used - a.previous
        growth = f"{delta:+,} since {a.tag}"
    print(f"| {a.env} | {used:,} | {limit:,} ({label}) | {frac:.1%} | {limit - used:,} | {growth} |")

    if frac > FAIL_AT:
        print(f"::error::{a.env}: firmware is {frac:.1%} of its {label} slot "
              f"({used:,} of {limit:,} bytes); the limit is {FAIL_AT:.0%}", file=sys.stderr)
        return 1
    if frac > WARN_AT:
        print(f"::warning::{a.env}: firmware is {frac:.1%} of its {label} slot "
              f"({limit - used:,} bytes left)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
