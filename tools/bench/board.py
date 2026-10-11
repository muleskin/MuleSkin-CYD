"""MuleSkin-CYD bench scripts: talking to a plugged-in board.

Shared by the scripts beside it (see README.md). pyserial at 2 Mbaud, the
board's console speed. Opening the port can reset the board, so a script
opens it once and lets the boot finish before sending anything.
"""
import os
import socket
import time

import serial

DEFAULT_PORT = os.environ.get("MULESKIN_PORT", "COM6")


class Board:
    def __init__(self, port=DEFAULT_PORT, boot_s=15):
        self.s = serial.Serial(port, 2000000, timeout=0.2)
        self.buf = b""
        if boot_s:
            self.read(boot_s)

    def read(self, sec, until=None):
        """Read for `sec` seconds, or until `until` (bytes) shows up -- then a
        few seconds more for the lines that follow it."""
        t0, start = time.time(), len(self.buf)
        while time.time() - t0 < sec:
            self.buf += self.s.read(4096)
            if until and until in self.buf[start:]:
                t1 = time.time()
                while time.time() - t1 < 4:
                    self.buf += self.s.read(4096)
                break
        return self.buf[start:].decode("utf-8", "replace")

    def cmd(self, line, sec=2, until=None):
        self.s.write((line + "\n").encode())
        return self.read(sec, until)

    def lines(self, *keys):
        """Every line seen so far that contains one of `keys`."""
        out = self.buf.decode("utf-8", "replace").splitlines()
        return [l for l in out if any(k in l for k in keys)] if keys else out

    def nvs(self):
        """NVS EXPORT as {(namespace, key): (type, hex)}. Holds secrets (WiFi
        passwords, the push token): never print it whole."""
        text = self.cmd("NVS EXPORT", 8, b"NVS END")
        if "NVS END" not in text:
            # Half an export is worse than none: a script that "restores"
            # what it did not read would wipe it.
            raise RuntimeError("no complete NVS EXPORT from the board (still booting, or locked?)")
        out = {}
        for l in text.splitlines():
            p = l.split(" ")
            if len(p) == 5 and p[0] == "NVS" and p[1] not in ("END", "SKIP"):
                out[(p[1], p[2])] = (p[3], p[4])
        return out

    def close(self):
        self.s.close()


def nvs_str(entry):
    """An NVS EXPORT string value (type 21) as text."""
    return bytes.fromhex(entry[1]).decode("utf-8", "replace") if entry else ""


def lan_ip():
    """This computer's address on the LAN the board can reach."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))   # nothing is sent; this only picks the route
        return s.getsockname()[0]
    finally:
        s.close()
