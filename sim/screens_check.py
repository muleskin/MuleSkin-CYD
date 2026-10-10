"""Screenshot regression check: render the main screens in the emulator and
compare them with the reference images in sim/golden/.

    python sim/screens_check.py            # check (CI runs this)
    python sim/screens_check.py --update   # re-render the references, after
                                           # a change that MEANT to move pixels

Needs Pillow and a built ./muleskinsim (make -C sim). Each screen is rendered
from fresh settings -- an empty NVS directory -- with a fixed number of
warm-up frames, so the picture depends on the code and nothing else.

The comparison is not byte-for-byte, on purpose: the references are made on
one machine and checked on another (a Windows MinGW build against CI's
Linux), and a stray pixel of difference between two C libraries is not a
regression. A screen fails when more than MAX_BAD of its pixels differ by
more than PIXEL_TOL in any channel -- which a moved row, a clipped label or
a wrong colour always does. On a failure the differing pixels are written,
in red over the new render, to sim/out/diff/ (CI keeps that folder).

Screens are chosen to hold still between machines: none shows the build's
own version string (the splash does) or anything drawn from rand().
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "muleskinsim" + (".exe" if os.name == "nt" else ""))
GOLDEN = os.path.join(HERE, "golden")
DIFF = os.path.join(HERE, "out", "diff")

PIXEL_TOL = 24       # per channel, out of 255
MAX_BAD = 0.005      # of the screen's pixels

# name, screen, extra args, extra environment
SCREENS = [
    ("settings",      "settings",   [], {}),
    ("settings-system", "settings", ["--scroll", "6"], {"MULESKINSIM_PAGE": "2"}),
    ("security",      "security",   [], {}),
    ("light",         "light",      [], {}),
    ("detfilter",     "detfilter",  [], {}),
    ("update",        "update",     [], {}),
    ("sysprops",      "sysprops",   [], {}),
    ("autoupdate",    "autoupdate", [], {}),
    ("pin",           "pin",        ["--type", "12"], {}),
    ("wifipass",      "wifipass",   [], {}),
    ("alert-flock",   "alert",      ["--alert", "1"], {}),
    ("log",           "log",        [], {}),
]


def render(name, screen, args, env, out_dir):
    nvs = tempfile.mkdtemp(prefix="msk-nvs-")
    try:
        out = os.path.join(out_dir, name + ".png")
        # A fixed wall clock: the LOG and the title bar print the time, and
        # the computer's own would change the picture from run to run.
        e = dict(os.environ, MULESKINSIM_NVS=nvs, MULESKIN_EPOCH="1791500000", **env)
        subprocess.run([EXE, screen, out, "--frames", "8"] + args, env=e, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=HERE)
        return out
    finally:
        shutil.rmtree(nvs, ignore_errors=True)


def compare(a_path, b_path, diff_path):
    from PIL import Image
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    if a.size != b.size:
        return 1.0, "size %s vs %s" % (a.size, b.size)
    pa, pb = a.load(), b.load()
    w, h = a.size
    bad = 0
    mark = b.copy()
    pm = mark.load()
    for y in range(h):
        for x in range(w):
            ra, ga, ba = pa[x, y]
            rb, gb, bb = pb[x, y]
            if abs(ra - rb) > PIXEL_TOL or abs(ga - gb) > PIXEL_TOL or abs(ba - bb) > PIXEL_TOL:
                bad += 1
                pm[x, y] = (255, 0, 0)
    frac = bad / float(w * h)
    if frac > MAX_BAD:
        os.makedirs(os.path.dirname(diff_path), exist_ok=True)
        mark.save(diff_path)
    return frac, ""


def main():
    if not os.path.exists(EXE):
        sys.exit("no %s -- build it first: make -C sim" % EXE)
    update = "--update" in sys.argv
    if update:
        os.makedirs(GOLDEN, exist_ok=True)
        from PIL import Image
        for name, screen, args, env in SCREENS:
            out = render(name, screen, args, env, GOLDEN)
            # The emulator writes its PNGs uncompressed; the references live
            # in the repo, so they are squeezed first.
            Image.open(out).convert("RGB").save(out, optimize=True)
            print("updated", name)
        return 0
    work = tempfile.mkdtemp(prefix="msk-shots-")
    failed = []
    try:
        for name, screen, args, env in SCREENS:
            ref = os.path.join(GOLDEN, name + ".png")
            if not os.path.exists(ref):
                print("%-16s NO REFERENCE (run --update)" % name)
                failed.append(name)
                continue
            got = render(name, screen, args, env, work)
            frac, why = compare(ref, got, os.path.join(DIFF, name + ".png"))
            ok = frac <= MAX_BAD
            print("%-16s %s  %.3f%% of pixels differ%s" % (name, "ok  " if ok else "FAIL", frac * 100,
                                                          (" (" + why + ")") if why else ""))
            if not ok:
                failed.append(name)
                shutil.copy(got, os.path.join(DIFF, name + ".new.png"))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if failed:
        print("\n%d screen(s) changed: %s. If that was the point, run --update and commit sim/golden/."
              % (len(failed), ", ".join(failed)))
        return 1
    print("\nall %d screens match" % len(SCREENS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
