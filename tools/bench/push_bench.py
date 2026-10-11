#!/usr/bin/env python3
"""PUSH ALERTS on the bench, against a stand-in ntfy on this computer.

    python tools/bench/push_bench.py [--port COM6] [--alerts]

Runs an HTTP server here that takes the board's pushes and answers its
command-topic poll with one IGNORE, points the board at it, then:

  - the ignore list over the console (IGNORE LIST / ADD / DEL, a bad MAC);
  - PUSH TEST -> one push, and the poll -> the IGNORE lands on the list;
  - with --alerts (needs a BENCH_TOOLS build: PLATFORMIO_BUILD_FLAGS=
    -DBENCH_TOOLS=1), two made-up Flock alerts (ALERTPUSH): the first pushed
    with its Ignore / Snooze buttons, the second held by the hour's cooldown.
    PUSH WHAT and PUSH AT NIGHT are set so a Flock goes, and put back.

Everything the board had is put back at the end: its push URL, PUSH WHAT,
PUSH AT NIGHT, and the ignore list. Nothing goes to the real server, and the
token the board sends is redacted in everything printed.
"""
import argparse
import http.server
import json
import random
import re
import sys
import threading

from board import Board, DEFAULT_PORT, lan_ip, nvs_str

CMD_MAC = "a1b2c3d4e5f6"     # the stand-in's IGNORE (the MAC ALERTPUSH makes up)
SNZ_MAC = "a1b2c3d4e5f7"     # ...and its SNOOZE, on the poll after


def colons(m):
    return ":".join(m[i:i + 2] for i in range(0, 12, 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--alerts", action="store_true")
    ap.add_argument("--listen", type=int, default=8790)
    a = ap.parse_args()

    posts, gets = [], []
    run = "%06d" % random.randrange(10 ** 6)
    ids = ("Bn%sa001" % run, "Bn%sa002" % run)

    class H(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            act = re.sub(r"Bearer [^,;\s]+", "Bearer <redacted>", self.headers.get("Actions") or "")
            posts.append((self.headers.get("Title"), act))
            self.send_response(200); self.end_headers(); self.wfile.write(b"{}")

        def do_GET(self):
            gets.append(self.path)
            # Like ntfy: only what came after `since`. An IGNORE first, then a
            # SNOOZE, then nothing. Fresh ids each run, so an id the board kept
            # from an earlier one reads as unknown and starts from the top.
            body = b""
            if "-cmd/json" in self.path:
                if "since=" + ids[1] in self.path:
                    msg = None
                elif "since=" + ids[0] in self.path:
                    msg = (ids[1], "SNOOZE %s 3" % SNZ_MAC)
                else:
                    msg = (ids[0], "IGNORE %s 3" % CMD_MAC)
                if msg:
                    body = (json.dumps({"id": msg[0], "time": 1, "event": "message", "message": msg[1]},
                                       separators=(",", ":")) + "\n").encode()
            self.send_response(200); self.end_headers(); self.wfile.write(body)

        def log_message(self, *_):
            pass

    srv = http.server.HTTPServer(("0.0.0.0", a.listen), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    here = "http://%s:%d/muleskin-bench" % (lan_ip(), a.listen)

    b = Board(a.port)
    nv = b.nvs()
    url = nvs_str(nv.get(("settings", "pushUrl")))
    what = nv.get(("settings", "pushWhat"), ("01", "01"))[1]
    night = nv.get(("settings", "pushNight"), ("01", "01"))[1]
    had = set(re.findall(r"^IGN ([0-9a-f]{12}) ", b.cmd("IGNORE LIST", 2), re.M))
    try:
        b.cmd("IGNORE ADD 11:22:33:44:55:66 5")
        b.cmd("IGNORE LIST")
        b.cmd("IGNORE DEL 112233445566")
        b.cmd("IGNORE ADD xyz")
        if a.alerts:
            b.cmd("NVS SET settings pushWhat 01 01")
            b.cmd("NVS SET settings pushNight 01 00")
            b.cmd("NVS RESTART", 16)
        b.cmd("PUSH URL " + here, 1)
        b.cmd("PUSH TEST", 150, b"sent in")
        if a.alerts:
            b.cmd("ALERTPUSH", 150, b"sent in")
            b.cmd("ALERTPUSH", 4)
        b.cmd("IGNORE LIST")
        b.cmd("SELFTEST", 8)
    finally:
        # Put it all back.
        for m in set(re.findall(r"^IGN ([0-9a-f]{12}) ", b.cmd("IGNORE LIST", 2), re.M)) - had:
            b.cmd("IGNORE DEL " + m)
        b.cmd("PUSH URL " + (url or "-"), 1)
        if a.alerts:
            b.cmd("NVS SET settings pushWhat 01 " + what)
            b.cmd("NVS SET settings pushNight 01 " + night)
            b.cmd("NVS RESTART", 3)
        b.close()
        srv.shutdown()

    for l in b.lines("IGN", "[ignore]", "[push]", "[bench]", "push alerts"):
        if "token" not in l:
            print("SER ", l[:170])
    for p in posts:
        print("POST", p)
    for g in gets:
        print("GET ", g)
    seen = b.lines("[ignore]")
    ign = any(colons(CMD_MAC) in l and "from the phone" in l and "snoozed" not in l for l in seen)
    snz = any(colons(SNZ_MAC) in l and "snoozed a day" in l for l in seen)
    print("the polled IGNORE landed" if ign else "the polled IGNORE did NOT land")
    # The SNOOZE comes on the second poll, which only --alerts' second join makes.
    if a.alerts:
        print("the polled SNOOZE landed" if snz else "the polled SNOOZE did NOT land")
    return 0 if ign and (snz or not a.alerts) else 1


if __name__ == "__main__":
    sys.exit(main())
