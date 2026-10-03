#!/usr/bin/env python3
"""Renders the README's ADD TO SQUAD clip: two boards side by side, the
inviter on the left and the invited board on the right, one caption a step.

    python3 make_invite_demo.py --render-only   # under WSL, after `make`
    python  make_invite_demo.py --encode-only   # wherever Pillow is installed

`python make_invite_demo.py` does both. Every pane is a run of the one-shot
emulator: the invite screen's --pose N is the real ui_invite.cpp drawing each
page of the exchange, and the right-hand board's first frame is its own main
screen. Only the pairing of panes and the captions are staged.

The clip used to be rendered by hand and never had a script; this one
reproduces its layout -- 320x240 panes at 1:1, an 8 px gutter, a caption band
under them -- so the README's alt text still describes it.
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
# The emulator binaries: ./muleskinsim under Linux/WSL, muleskinsim.exe from a
# MinGW build on Windows.
EXE  = ".exe" if os.name == "nt" else ""
def sim(name):
    return os.path.join(HERE, name + EXE)
OUT  = os.path.join(HERE, "out", "invitedemo")
GIF  = os.path.join(HERE, "..", "docs", "squad-invite.gif")

W, H  = 320, 240
GAP   = 8
BAND  = 38
BG    = 10                      # SYNTHWAVE, for the invited board's main screen
INK   = (12, 10, 22)            # the gutter and the caption band
PINK  = (241, 90, 167)
CYAN  = (58, 219, 209)
WHITE = (220, 215, 241)

# ui_invite's pages, as main_sim.cpp's --pose numbers them.
OFFERING, ASKED, CODE, SENDING, JOINED, WAITING, ADDED = 0, 1, 2, 3, 4, 6, 8

# (left pane, right pane, hold ms, caption). A pane is a --pose number, or
# None for the invited board's own main screen.
STEPS = [
    (OFFERING, None,    1500, "BIGFOOT taps ADD TO SQUAD on the SQUAD screen."),
    (OFFERING, ASKED,   2500, "YETI's board asks. ACCEPT."),
    (CODE,     CODE,    3200, "Same four digits on both? MATCHES, on both."),
    (SENDING,  WAITING, 1500, "The phrase goes over, sealed under a one-time key."),
    (ADDED,    JOINED,  3200, "YETI is in. Nobody typed anything."),
]


def render():
    if not os.path.exists(sim("muleskinsim")):
        sys.exit("muleskinsim not built -- run `make` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    shots = {"main": ["clear", "--noseed", "--bg", str(BG)]}
    for pose in sorted({p for s in STEPS for p in s[:2] if p is not None}):
        shots["pose%d" % pose] = ["invite", "--pose", str(pose)]
    for name, args in shots.items():
        cmd = [sim("muleskinsim"), args[0], os.path.join(OUT, name + ".png")] + args[1:]
        print(" ".join(cmd))
        if subprocess.call(cmd, cwd=HERE, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) != 0:
            sys.exit("render failed: " + name)


def encode():
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.load_default(size=11)

    def pane(p):
        f = os.path.join(OUT, ("main" if p is None else "pose%d" % p) + ".png")
        if not os.path.exists(f):
            sys.exit("no frames -- run --render-only first")
        return Image.open(f).convert("RGB")

    frames, durations = [], []
    for left, right, ms, caption in STEPS:
        im = Image.new("RGB", (2 * W + GAP, H + BAND), INK)
        im.paste(pane(left), (0, 0))
        im.paste(pane(right), (W + GAP, 0))
        d = ImageDraw.Draw(im)
        d.text((7, H + 14), "BIGFOOT, inviting", fill=PINK, font=font, anchor="ls")
        d.text((W + GAP + 7, H + 14), "YETI, invited", fill=CYAN, font=font, anchor="ls")
        d.text(((2 * W + GAP) // 2, H + 31), caption, fill=WHITE, font=font, anchor="ms")
        frames.append(im)
        durations.append(ms)

    # One palette for the whole clip: the panes are RGB332 and the captions
    # add a handful of anti-aliased greys, still well inside 256.
    strip = Image.new("RGB", (frames[0].width, frames[0].height * len(frames)))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * f.height))
    pal = strip.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    q[0].save(GIF, save_all=True, append_images=q[1:], duration=durations, loop=0,
              optimize=False, disposal=1)
    print("%d frames, %.1fs, %dx%d -> %s (%d KB)" % (
        len(q), sum(durations) / 1000.0, q[0].width, q[0].height,
        os.path.normpath(GIF), os.path.getsize(GIF) // 1024))


if __name__ == "__main__":
    if "--encode-only" not in sys.argv:
        render()
    if "--render-only" not in sys.argv:
        encode()
