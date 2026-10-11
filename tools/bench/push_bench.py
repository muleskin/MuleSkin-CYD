#!/usr/bin/env python3
"""PUSH ALERTS on the bench, against a stand-in ntfy on this computer.

    python tools/bench/push_bench.py [--port COM6] [--alerts] [--limit]

Runs an HTTP server here that takes the board's pushes and answers its
command-topic polls, points the board at it, then:

  - the ignore list over the console (IGNORE LIST / ADD / DEL, a bad MAC);
  - PUSH TEST -> one push, and the poll -> an IGNORE lands on the list;
  - with --limit, that first push is answered 429 with Retry-After: 60, and
    the board has to wait the minute out and send it again;
  - with --alerts (needs a BENCH_TOOLS build: PLATFORMIO_BUILD_FLAGS=
    -DBENCH_TOOLS=1), two made-up Flock alerts (ALERTPUSH): the first pushed
    with its Ignore / Snooze buttons, the second held by the hour's cooldown;
    the later polls bring a SNOOZE and then an UNIGNORE (the confirmation's
    Undo), each answered by a confirmation push. PUSH WHAT and PUSH AT NIGHT
    are set so a Flock goes, and put back.

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

CMD_MAC = "a1b2c3d4e5f6"     # the stand-in's IGNORE (the MAC ALERTPUSH makes up), then its UNIGNORE
SNZ_MAC = "a1b2c3d4e5f7"     # ...and its SNOOZE, on the poll between


def colons(m):
    return ":".join(m[i:i + 2] for i in range(0, 12, 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--alerts", action="store_true")
    ap.add_argument("--limit", action="store_true", help="answer the first push 429, Retry-After: 60")
    ap.add_argument("--listen", type=int, default=8790)
    a = ap.parse_args()

    posts, gets = [], []
    run = "%06d" % random.randrange(10 ** 6)
    ids = ["Bn%sa00%d" % (run, i) for i in (1, 2, 3)]
    # Like ntfy: only what came after `since`. Fresh ids each run, so an id
    # the board kept from an earlier one reads as unknown and starts the list.
    script = [(ids[0], "IGNORE %s 3" % CMD_MAC), (ids[1], "SNOOZE %s 3" % SNZ_MAC),
              (ids[2], "UNIGNORE %s" % CMD_MAC)]
    limited = [a.limit]

    class H(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            act = re.sub(r"Bearer [^,;\s]+", "Bearer <redacted>", self.headers.get("Actions") or "")
            if limited[0]:
                limited[0] = False
                posts.append((self.headers.get("Title"), "-> 429"))
                self.send_response(429); self.send_header("Retry-After", "60"); self.end_headers()
                return
            posts.append((self.headers.get("Title"), act))
            self.send_response(200); self.end_headers(); self.wfile.write(b"{}")

        def do_GET(self):
            gets.append(self.path)
            body = b""
            if "-cmd/json" in self.path:
                at = next((i + 1 for i, (mid, _) in enumerate(script) if "since=" + mid in self.path), 0)
                if at < len(script):
                    body = (json.dumps({"id": script[at][0], "time": 1, "event": "message",
                                        "message": script[at][1]}, separators=(",", ":")) + "\n").encode()
            self.send_response(200); self.end_headers(); self.wfile.write(body)

        def log_message(self, *_):
            pass

    srv = http.server.HTTPServer(("0.0.0.0", a.listen), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    here = "http://%s:%d/muleskin-bench" % (lan_ip(), a.listen)

    b = Board(a.port)
    # What the board has now, read in full before anything changes -- and
    # nothing goes ahead if it cannot be (board.nvs() raises), since the
    # clean-up below puts back exactly this.
    nv = b.nvs()
    url = nvs_str(nv.get(("settings", "pushUrl")))
    was_on = nv.get(("settings", "pushOn"), ("01", "00"))[1] == "01"
    what = nv.get(("settings", "pushWhat"), ("01", "01"))[1]
    night = nv.get(("settings", "pushNight"), ("01", "01"))[1]
    listed = b.cmd("IGNORE LIST", 3, b"IGN END")
    if "IGN END" not in listed:
        b.close(); srv.shutdown()
        sys.exit("push_bench: no IGNORE LIST answer -- not touching the board")
    had = set(re.findall(r"^IGN ([0-9a-f]{12}) ", listed, re.M))
    print("board now: push %s, %s, PUSH WHAT %s, %d ignored" %
          ("on" if was_on else "off", "a server set" if url else "no server", what, len(had)))
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
        if not was_on:
            b.cmd("PUSH ON", 2)                  # off on this board: on for the test, off again after
        b.cmd("PUSH TEST", 150, b"sent in")
        if a.limit:
            b.read(120, b"1 of 1 sent")         # the retry, a minute on
        if a.alerts:
            b.cmd("ALERTPUSH", 150, b"sent in")
            b.cmd("ALERTPUSH", 4)
            # The confirmations make joins of their own; each join polls.
            b.read(180, b"undone from the phone")
            b.read(60, b"sent in")               # ...and the "un-ignored" goes out
        b.cmd("IGNORE LIST")
        b.cmd("SELFTEST", 8)
    finally:
        # Put it all back.
        for m in set(re.findall(r"^IGN ([0-9a-f]{12}) ", b.cmd("IGNORE LIST", 2), re.M)) - had:
            b.cmd("IGNORE DEL " + m)
        b.cmd("PUSH URL " + (url or "-"), 1)
        if not was_on and url:
            b.cmd("PUSH OFF", 2)                 # ("PUSH URL -" already turns it off)
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
    checks = {"the polled IGNORE landed":
              any(colons(CMD_MAC) in l and "from the phone:" in l and "snoozed" not in l for l in seen)}
    if a.limit:
        checks["a 429 waited out its minute"] = bool(b.lines("rate limited: next try in 1 min")) and \
            sum(1 for t, _ in posts if t and t.endswith("MuleSkin test")) >= 2
    if a.alerts:
        checks["the polled SNOOZE landed"] = any(colons(SNZ_MAC) in l and "snoozed a day" in l for l in seen)
        checks["the Undo (UNIGNORE) landed"] = any(colons(CMD_MAC) in l and "undone" in l for l in seen)
        checks["alerts carry Ignore + Snooze"] = any("Snooze 1 day" in act for _, act in posts)
        checks["confirmations carry Undo"] = any(t and t.endswith(": ignored") and "Undo" in act and "UNIGNORE" in act
                                                 for t, act in posts)
    for k, v in checks.items():
        print("%-32s %s" % (k, "ok" if v else "FAILED"))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
