#!/usr/bin/env python3
"""Packs docs/radar.gif into include/radar_art.h, as a model rather than frames.

    python tools/make_radar.py [--preview out.gif]      # needs Pillow + numpy

The GIF is 299 frames of a radar scope: static rings and ticks, a sweep arm
turning clockwise once every ~6 s, and weather-like blobs that light up as
the arm passes and fade out behind it. 299 frames will not fit in flash, but
the picture is a function of one thing -- how far behind the arm a pixel is --
so it is stored as that function:

  RING   per pixel: the part that never changes (rings, ticks), 0..15
  BLOB   per pixel: how bright the weather is there when freshly swept, 0..15
  QUAD   one quadrant of the angle table (atan2 by lookup, mirrored)
  LEVEL  [blob][d]: arm + afterglow brightness, d = angle behind the arm in
         256ths of a turn, both curves MEASURED from the GIF

and the firmware draws  level = min(15, RING + LEVEL[BLOB][d])  each frame,
in the GIF's cyan (PALETTE, 16 RGB332 steps). Geometry is in 320x240 (the
GIF scaled to the CYD); other panels scale it to cover.

--preview renders a GIF with exactly that maths, beside the original.
"""
import math, os, sys

import numpy as np
from PIL import Image, ImageSequence

HERE = os.path.dirname(os.path.abspath(__file__))
SRC  = os.path.join(HERE, "..", "docs", "radar.gif")
OUT  = os.path.join(HERE, "..", "include", "radar_art.h")
W, H = 320, 240

src = Image.open(SRC)
durations = [f.info.get("duration", 20) for f in ImageSequence.Iterator(Image.open(SRC))]
PERIOD_MS = sum(durations)
fr = np.stack([np.asarray(f.convert("RGB").resize((W, H), Image.LANCZOS), dtype=np.float32)
               for f in ImageSequence.Iterator(src)])
N = fr.shape[0]
lum = fr.max(axis=3)                                  # brightness 0..255
peak_rgb = fr.reshape(-1, 3)[np.argmax(lum)]          # the GIF's cyan at full

# ---- geometry -----------------------------------------------------------
mx = lum.max(axis=0)
ys, xs = np.nonzero(mx > 40)
X0, X1, Y0, Y1 = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
CX, CY = (X0 + X1 - 1) / 2.0, (Y0 + Y1 - 1) / 2.0
yy, xx = np.mgrid[0:H, 0:W]
ang = (np.degrees(np.arctan2(xx - CX, -(yy - CY))) + 360.0) % 360.0   # 0 = up, clockwise

# ---- the arm: when each pixel peaks gives its angle against time --------
RATE = 360.0 / N                                      # degrees a frame, one turn a loop
inside = mx > 60
pk = lum.argmax(axis=0)
phis = (ang[inside] - pk[inside] * RATE) % 360.0
a = np.radians(phis)
PHI0 = (math.degrees(math.atan2(np.sin(a).mean(), np.cos(a).mean())) + 360.0) % 360.0

ring = lum.min(axis=0)                                # what never changes
dyn = lum - ring                                      # what the arm does
k = np.arange(N)[:, None, None]
d = ((PHI0 + k * RATE - ang[None]) % 360.0)           # degrees behind the arm, per frame/pixel

BINS = 256
db = np.floor(d / 360.0 * BINS).astype(int) % BINS

# Arm alone: where there is no weather, the dynamic part is the arm's sweep.
fresh = (d >= 20) & (d < 45)
blob = np.where(fresh, dyn, 0).max(axis=0)            # weather brightness just after the arm
quiet = (blob < 12) & inside
arm = np.zeros(BINS)
for b in range(BINS):
    m = (db == b) & quiet[None]
    arm[b] = np.median(dyn[m]) if m.any() else 0.0

# Afterglow: weather brightness behind the arm, relative to its fresh value.
strong = (blob > 80) & inside
fade = np.zeros(BINS)
for b in range(BINS):
    m = (db == b) & strong[None]
    if m.any():
        r = (dyn - arm[db])[m] / np.broadcast_to(blob, dyn.shape)[m]
        fade[b] = float(np.clip(np.median(r), 0.0, 1.5))
fade = np.convolve(np.r_[fade[-4:], fade, fade[:4]], np.ones(9) / 9, mode="valid")

# ---- quantise to what the firmware stores ---------------------------------
def q15(v):
    return np.clip(np.round(v / 255.0 * 15.0), 0, 15).astype(np.uint8)

RING = q15(ring)[Y0:Y1, X0:X1]
BLOB = q15(blob)[Y0:Y1, X0:X1]
LEVEL = np.zeros((16, BINS), dtype=np.uint8)
for bl in range(16):
    LEVEL[bl] = q15(arm + (bl / 15.0 * 255.0) * fade)

R = max(int(math.ceil(max(CX - X0, X1 - 1 - CX, CY - Y0, Y1 - 1 - CY))) + 1, 1)
QUAD = np.zeros((R, R), dtype=np.uint8)                # angle of (dx, dy) in the up-right quadrant
for qy in range(R):
    for qx in range(R):
        QUAD[qy, qx] = int(round(math.degrees(math.atan2(qx, qy)) / 360.0 * BINS)) % BINS

# The GIF's own colour at each brightness level (its dim cyan is bluer than
# the bright one scaled down), snapped to the nearest RGB332 the panel shows.
q_all = q15(lum)
levels_rgb = []
for l in range(16):
    m = q_all == l
    levels_rgb.append(fr[m].mean(axis=0) if m.any() else peak_rgb * l / 15.0)
C332 = np.array([[((c >> 5) & 7) * 255 // 7, ((c >> 2) & 7) * 255 // 7, (c & 3) * 255 // 3]
                 for c in range(256)], dtype=np.float32)
PALETTE = [0] + [int(np.argmin(((C332 - levels_rgb[l]) ** 2).sum(axis=1))) for l in range(1, 16)]

def angle_lut(px, py):
    """Firmware's atan2: the quadrant table, mirrored."""
    dx, dy = px - CX, py - CY
    ax, ay = min(R - 1, int(abs(dx))), min(R - 1, int(abs(dy)))
    a = int(QUAD[ay, ax])
    if dx >= 0 and dy < 0:  return a                   # up-right
    if dx >= 0:             return (BINS // 2 - a) % BINS   # down-right
    if dy >= 0:             return (BINS // 2 + a) % BINS   # down-left
    return (BINS - a) % BINS                           # up-left

def render(t_ms):
    sweep = int((PHI0 / 360.0 * BINS + t_ms / PERIOD_MS * BINS)) % BINS
    img = np.zeros((H, W), dtype=np.uint8)
    for y in range(Y0, Y1):
        for x in range(X0, X1):
            a = angle_lut(x, y)
            dd = (sweep - a) % BINS
            lv = min(15, int(RING[y - Y0, x - X0]) + int(LEVEL[BLOB[y - Y0, x - X0], dd]))
            img[y, x] = PALETTE[lv]
    rgb = np.zeros((H, W, 3), dtype=np.uint8)
    rgb[..., 0] = (img & 0xE0); rgb[..., 1] = (img & 0x1C) << 3; rgb[..., 2] = (img & 0x03) << 6
    return Image.fromarray(rgb)

# ---- header -------------------------------------------------------------------
bw, bh = X1 - X0, Y1 - Y0
packed = (RING.astype(np.uint16) << 4 | BLOB).astype(np.uint8).ravel()
def arr(name, ctype, data, per=24):
    out = ["static const %s %s[%d] = {" % (ctype, name, len(data))]
    for i in range(0, len(data), per):
        out.append("    " + ",".join(str(int(v)) for v in data[i:i + per]) + ",")
    out.append("};")
    return out
lines = [
    "// Generated by tools/make_radar.py from docs/radar.gif -- do not edit.",
    "// The radar background as a model: see the script for what each table is.",
    "#pragma once",
    "#include <stdint.h>",
    "",
    "namespace RadarArt {",
    "constexpr int REF_W = %d, REF_H = %d;          // the space below is laid out in" % (W, H),
    "constexpr int BOX_X = %d, BOX_Y = %d, BOX_W = %d, BOX_H = %d;   // the scope's box" % (X0, Y0, bw, bh),
    "constexpr float CX = %.2ff, CY = %.2ff;          // its centre" % (CX, CY),
    "constexpr int QUAD_N = %d;" % R,
    "constexpr uint32_t PERIOD_MS = %d;             // one turn of the arm" % PERIOD_MS,
    "constexpr uint8_t PHASE = %d;                  // where the arm is at t = 0, 256ths of a turn" % int(PHI0 / 360.0 * BINS),
    "// RING in the high nibble, BLOB in the low, BOX_W x BOX_H, row-major.",
] + arr("PIXELS", "uint8_t", packed) + [""] + arr("QUAD", "uint8_t", QUAD.ravel()) + [
    "// LEVEL[blob * 256 + d]: d = how far behind the arm, 256ths of a turn.",
] + arr("LEVEL", "uint8_t", LEVEL.ravel(), 32) + [""] + arr("PALETTE", "uint8_t", PALETTE, 16) + [
    "}  // namespace RadarArt", ""]
with open(OUT, "w", newline="\n") as f:
    f.write("\n".join(lines))
size = len(packed) + QUAD.size + LEVEL.size + 16
print("box %dx%d at (%d,%d), centre (%.1f,%.1f), period %d ms, phase %.1f deg, %d bytes -> %s"
      % (bw, bh, X0, Y0, CX, CY, PERIOD_MS, PHI0, size, os.path.normpath(OUT)))

if "--preview" in sys.argv:
    out = sys.argv[sys.argv.index("--preview") + 1]
    frames, step = [], 6
    for i in range(0, N, step):
        model = render(i * PERIOD_MS / N)
        orig = Image.fromarray(fr[i].astype(np.uint8))
        both = Image.new("RGB", (W * 2, H)); both.paste(orig, (0, 0)); both.paste(model, (W, 0))
        frames.append(both)
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=20 * step, loop=0)
    print("preview ->", out)
