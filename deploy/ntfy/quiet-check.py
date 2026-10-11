#!/usr/bin/env python3
"""Tells you when a board has gone quiet: no push from it in a day and a bit.

A detector that has died, lost power or lost its WiFi says nothing -- which
looks exactly like a quiet day. Every board with DAILY SUMMARY on pushes at
least once a day (the 8 PM summary), so silence for longer than that means
something is wrong. Run hourly from cron on the ntfy server:

    0 * * * * /usr/bin/python3 /root/MuleSkin-CYD/deploy/ntfy/quiet-check.py

Reads /root/.config/muleskin/quiet-check.env (KEY=value lines):

    NTFY_URL=https://ntfy.oillie.cloud
    TOPIC=muleskin-...                 the boards' alert topic
    TOKEN=tk_...                       an account that can read AND write it
                                       (yours -- not a board's write-only one)
    BOARDS=Car,MuleSkin-565E           the names the boards push under
    HOURS=26                           how long is too quiet (default 26)
    STATUS_FILE=/path/status.html      optional: a page of every board's last
                                       push, last summary and firmware

The status page is written each run when STATUS_FILE is set. It names your
boards and when they were last heard: put it somewhere only you can read
(behind a login, or read it over ssh), not on the public flasher site.

Each board gets one "<name>: quiet" push when it goes over HOURS, and one
"<name>: back" when it is heard again. State lives in
/var/lib/muleskin/quiet-check.json. Python 3 standard library only.
"""
import html
import json
import os
import re
import sys
import time
import urllib.request

CONF = os.environ.get("QUIET_CHECK_CONF", "/root/.config/muleskin/quiet-check.env")
STATE = os.environ.get("QUIET_CHECK_STATE", "/var/lib/muleskin/quiet-check.json")


def load_conf(path):
    conf = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                conf[k.strip()] = v.strip()
    for k in ("NTFY_URL", "TOPIC", "TOKEN", "BOARDS"):
        if not conf.get(k):
            sys.exit("quiet-check: %s missing from %s" % (k, path))
    return conf


def request(url, token, data=None, headers=None):
    req = urllib.request.Request(url, data=data, method="POST" if data is not None else "GET")
    req.add_header("Authorization", "Bearer " + token)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read().decode("utf-8", "replace")


def heard(titles, board):
    """A board's pushes are titled "<name>: ..." or, the summary, "<name> today"
    -- not counting this script's own "<name>: quiet" and "<name>: back"."""
    own = (board + ": quiet", board + ": back")
    return any((t.startswith(board + ":") and t not in own) or t == board + " today" for t in titles)


def main():
    conf = load_conf(CONF)
    base = conf["NTFY_URL"].rstrip("/") + "/" + conf["TOPIC"]
    hours = int(conf.get("HOURS", "26"))
    boards = [b.strip() for b in conf["BOARDS"].split(",") if b.strip()]

    body = request("%s/json?poll=1&since=%dh" % (base, hours), conf["TOKEN"])
    titles, msgs = [], []
    for line in body.splitlines():
        try:
            m = json.loads(line)
        except ValueError:
            continue
        if m.get("event") == "message":
            titles.append(m.get("title", ""))
            msgs.append(m)

    try:
        with open(STATE) as f:
            state = json.load(f)
    except (OSError, ValueError):
        state = {}

    for b in boards:
        quiet = not heard(titles, b)
        was = state.get(b, {}).get("quiet", False)
        if quiet and not was:
            request(base, conf["TOKEN"],
                    ("No push from %s in %d hours. Unplugged, out of WiFi range, or stuck? "
                     "With DAILY SUMMARY on, every board pushes at least once a day." % (b, hours)).encode(),
                    {"Title": b + ": quiet", "Priority": "4", "Tags": "warning", "X-Sequence-ID": "quiet-" + b})
            state[b] = {"quiet": True, "since": int(time.time())}
            print("quiet-check: %s quiet, told" % b)
        elif not quiet and was:
            request(base, conf["TOKEN"], ("%s is pushing again." % b).encode(),
                    {"Title": b + ": back", "Priority": "2", "Tags": "white_check_mark", "X-Sequence-ID": "quiet-" + b})
            state[b] = {"quiet": False}
            print("quiet-check: %s back, told" % b)
        elif b not in state:
            state[b] = {"quiet": quiet}

    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE, "w") as f:
        json.dump(state, f)
    if conf.get("STATUS_FILE"):
        write_status(conf["STATUS_FILE"], boards, msgs, state, hours)


def ago(t):
    s = max(0, int(time.time()) - int(t))
    return "%d min ago" % (s // 60) if s < 7200 else "%.1f h ago" % (s / 3600)


def write_status(path, boards, msgs, state, hours):
    """One row a board: when it was last heard and with what, its last 8 PM
    summary, and the firmware and settings store that summary names."""
    rows = []
    for b in boards:
        own = (b + ": quiet", b + ": back")
        mine = [m for m in msgs if (m.get("title", "").startswith(b + ":") and m.get("title") not in own)
                or m.get("title") == b + " today"]
        last = max(mine, key=lambda m: m.get("time", 0), default=None)
        summ = max((m for m in mine if m.get("title") == b + " today"), key=lambda m: m.get("time", 0), default=None)
        fw = re.search(r"\. (v?\d+\.\d+\.\d+\S*), (\d+) KB store", summ.get("message", "")) if summ else None
        quiet = state.get(b, {}).get("quiet", False)
        rows.append("<tr class='%s'><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
            "quiet" if quiet else "ok", html.escape(b),
            "QUIET" if quiet else "ok",
            html.escape("%s: %s" % (ago(last["time"]), last.get("title", "")[len(b):].lstrip(": "))) if last else "nothing in %d h" % hours,
            html.escape("%s: %s" % (ago(summ["time"]), summ.get("message", ""))) if summ else "-",
            html.escape("%s, %s KB" % (fw.group(1), fw.group(2))) if fw else "-"))
    page = """<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="refresh" content="300">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>MuleSkin boards</title>
<style>body{font-family:system-ui,sans-serif;background:#111;color:#ddd;margin:16px}
table{border-collapse:collapse;width:100%%}td,th{border-bottom:1px solid #333;padding:6px;text-align:left;vertical-align:top}
tr.quiet td{color:#f90}th{color:#0cf}</style></head><body>
<h1>MuleSkin boards</h1><p>Updated %s, from the last %d hours of pushes.</p>
<table><tr><th>Board</th><th>State</th><th>Last push</th><th>Last summary</th><th>Firmware</th></tr>
%s
</table></body></html>
""" % (time.strftime("%Y-%m-%d %H:%M"), hours, "\n".join(rows))
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(page)
    os.replace(tmp, path)


if __name__ == "__main__":
    main()
