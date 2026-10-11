#!/usr/bin/env python3
"""Packs the MuleSkin artwork into include/muleskin_art.h.

    python tools/make_boot_art.py            # needs Pillow

Reads docs/input.png (the hooded MuleSkin at his computer, the same source
docs/twich.py animates) and makes it SIZE x SIZE in RGB332 -- the frame
buffer's own format, so a decoded row goes to the screen with one pushImage()
and no conversion. It is error-diffused against the RGB332 colours here, on a
PC, rather than left for the panel to round, which is what keeps the dark
gradients from banding.

Stored compressed: raw, it was 100 KB of a 1.8 MB app slot, the biggest
single thing in the firmware. The dithering that keeps it smooth defeats
run-length coding, but a pixel is very predictable from the one to its left,
so each is Huffman-coded with a table chosen by that left neighbour (order-1;
the first pixel of a row uses a table of its own). About 30 KB. Every row
starts on a known bit, so src/muleskin_art.cpp decodes any row on its own --
drawArtwork() samples rows, it never needs the whole picture in RAM.

One asset, two users: the boot splash draws the whole square, scaled down
into its frame; the MULESKIN background crops it to cover the screen. The ear
twitch is not stored at all -- Theme::drawArtwork() redraws it from this one
frame with the column shifts twich.py uses.

Run it again after changing the source image, and commit the header. CHECK
in the header is the FNV-1a of the raw pixels; test/muleskin_art_test.cpp
decodes every row and compares.
"""
import heapq
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC  = os.path.join(HERE, "..", "docs", "input.png")
OUT  = os.path.join(HERE, "..", "include", "muleskin_art.h")
SIZE = 320
MAXLEN = 15

# Palette entry i IS RGB332 byte i, so the quantised indices are the pixels.
pal = []
for i in range(256):
    pal += [((i >> 5) & 7) * 255 // 7, ((i >> 2) & 7) * 255 // 7, (i & 3) * 255 // 3]
pal_img = Image.new("P", (1, 1))
pal_img.putpalette(pal)

im = Image.open(SRC).convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)
q = im.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
px = q.tobytes()

colors = sorted(set(px))
index = {c: i for i, c in enumerate(colors)}
N = len(colors)
START = N                     # the context of a row's first pixel
CTX = N + 1


def code_lengths(freq):
    """Huffman code lengths for {symbol: count}, none longer than MAXLEN."""
    freq = dict(freq)
    while True:
        if len(freq) == 1:
            return {s: 1 for s in freq}
        heap = [(f, i, (s,)) for i, (s, f) in enumerate(sorted(freq.items()))]
        heapq.heapify(heap)
        depth = {s: 0 for s in freq}
        tie = len(heap)
        while len(heap) > 1:
            f1, _, a = heapq.heappop(heap)
            f2, _, b = heapq.heappop(heap)
            for s in a + b:
                depth[s] += 1
            heapq.heappush(heap, (f1 + f2, tie, a + b))
            tie += 1
        if max(depth.values()) <= MAXLEN:
            return depth
        freq = {s: max(1, f // 2) for s, f in freq.items()}   # flatten, try again


# Counts per context.
counts = [dict() for _ in range(CTX)]
for r in range(SIZE):
    prev = START
    for c in range(SIZE):
        s = index[px[r * SIZE + c]]
        counts[prev][s] = counts[prev].get(s, 0) + 1
        prev = s

# Canonical codes per context: symbols sorted by (length, symbol).
len_count = []     # CTX x (MAXLEN + 1)
sym_base = []
syms = []
codes = []         # per context: {symbol: (code, length)}
for k in range(CTX):
    lc = [0] * (MAXLEN + 1)
    table = {}
    sym_base.append(len(syms))
    if counts[k]:
        lens = code_lengths(counts[k])
        order = sorted(lens, key=lambda s: (lens[s], s))
        code = 0
        prev_len = lens[order[0]]
        for i, s in enumerate(order):
            L = lens[s]
            if i:
                code = (code + 1) << (L - prev_len)
            prev_len = L
            table[s] = (code, L)
            lc[L] += 1
        syms.extend(order)
    len_count.append(lc)
    codes.append(table)

# The bits, MSB first, every row starting on a recorded bit.
out = bytearray()
acc = 0
nbits = 0
total = 0
row_bit = []


def put(code, length):
    global acc, nbits, total
    for i in range(length - 1, -1, -1):
        acc = (acc << 1) | ((code >> i) & 1)
        nbits += 1
        total += 1
        if nbits == 8:
            out.append(acc)
            acc = 0
            nbits = 0


for r in range(SIZE):
    row_bit.append(total)
    prev = START
    for c in range(SIZE):
        s = index[px[r * SIZE + c]]
        put(*codes[prev][s])
        prev = s
row_bit.append(total)
if nbits:
    out.append(acc << (8 - nbits))

h = 0x811C9DC5
for b in px:
    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF


def block(ctype, name, vals, per=24):
    lines = ["static const %s %s[%d] = {" % (ctype, name, len(vals))]
    for i in range(0, len(vals), per):
        lines.append("    " + ",".join(str(v) for v in vals[i:i + per]) + ",")
    lines.append("};")
    return lines


lines = [
    "// Generated by tools/make_boot_art.py from docs/input.png -- do not edit.",
    "// The MuleSkin artwork: %dx%d RGB332, order-1 Huffman coded (see the tool)." % (SIZE, SIZE),
    "// Decoded a row at a time by MuleSkinArt::row(), src/muleskin_art.cpp.",
    "#pragma once",
    "#include <stdint.h>",
    "",
    "namespace MuleSkinArt {",
    "constexpr int SIZE   = %d;" % SIZE,
    "constexpr int COLORS = %d;     // distinct RGB332 values; context COLORS starts a row" % N,
    "constexpr int MAXLEN = %d;" % MAXLEN,
    "constexpr uint32_t CHECK = 0x%08Xu;   // FNV-1a of the raw pixels" % h,
]
lines += block("uint8_t", "PALETTE", colors)
lines += block("uint8_t", "LEN_COUNT", [v for lc in len_count for v in lc], MAXLEN + 1)
lines += block("uint16_t", "SYM_BASE", sym_base)
lines += block("uint8_t", "SYMS", syms)
lines += block("uint32_t", "ROW_BIT", row_bit, 12)
lines += block("uint8_t", "BITS", list(out))
lines.append("}  // namespace MuleSkinArt")
lines.append("")

with open(OUT, "w", newline="\n") as f:
    f.write("\n".join(lines))
size = len(out) + len(row_bit) * 4 + len(syms) + len(sym_base) * 2 + CTX * (MAXLEN + 1) + N
print("%dx%d RGB332, %d colours -> %s: %d bytes of code, %d in all (raw was %d)" % (
    SIZE, SIZE, N, os.path.normpath(OUT), len(out), size, len(px)))
