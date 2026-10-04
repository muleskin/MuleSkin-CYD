#!/usr/bin/env python3
"""Renders the headline font into include/bangers_font.h.

    python tools/gen_headline_font.py        # needs Pillow

The headlines (NEARBY, the ALERT card's FLOCK CAM, AIRTAG, the boot title...)
were Bangers. They are Orbitron Black now (tools/fonts/Orbitron[wght].ttf, SIL
OFL -- see tools/fonts/Orbitron-OFL.txt), narrowed. Orbitron's capitals are
nearly twice as wide as Bangers', and every layout that draws a headline was
fitted to Bangers' widths, so each size here is:

  - rendered at PT points, weight 900, 1 bpp (thresholded, as the board draws);
  - squeezed horizontally by the largest factor at which NO headline the
    firmware can show (detection types, device titles, vendor labels, the
    fixed words) comes out wider than it was in Bangers -- so nothing moves
    or overflows;
  - shifted so its capitals sit centred on Bangers' capital band, so text
    lands where it always did vertically.

The output keeps the Bangers file's names and layout (BangersFont::Glyph,
LG_/MD_ tables, ascent) so the drawing code in theme.cpp is untouched.
Same glyph set: space, ! - ' 0-9 A-Z.

XL is new and has no Bangers original: capitals and space only, for the IN A
MEETING sign (ui_meeting.cpp). Same weight, narrowed by LG's factor so it
reads as the same face, at XL_PT.
"""
import os, re

from PIL import Image, ImageDraw, ImageFont

HERE  = os.path.dirname(os.path.abspath(__file__))
ROOT  = os.path.normpath(os.path.join(HERE, ".."))
FONT  = os.path.join(HERE, "fonts", "Orbitron[wght].ttf")
OUT   = os.path.join(ROOT, "include", "bangers_font.h")
WGHT  = 900
SIZES = {"LG": 26, "MD": 23}          # points; ~3/4 of Bangers' cap height
XL_PT    = 54
XL_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ"
CHARS = " !-'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
NAMES = {" ": "SP", "!": "BANG", "-": "HYPH", "'": "APOS"}

# ---- what Bangers measured: its last generated header, kept as a reference ----
# Not include/bangers_font.h -- that is this script's own output, and measuring
# against it would narrow the font a little further on every run.
REF = os.path.join(HERE, "fonts", "bangers_reference.h")
old = open(REF, encoding="utf-8", errors="replace").read()

def old_table(size):
    blk = old[old.index("static const Glyph %s_GLYPHS[]" % size):]
    blk = blk[:blk.index("};")]
    t = {}
    for m in re.finditer(r"\{ '(\\'|.)', (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+),", blk):
        t[m.group(1).replace("\\'", "'")] = tuple(int(m.group(i)) for i in range(2, 7))
    return t   # ch -> (w, h, xoff, yoff, advance)

OLD = {s: old_table(s) for s in SIZES}
OLD_ASCENT = {s: int(re.search(r"%s_ASCENT = (\d+)" % s, old).group(1)) for s in SIZES}

def old_width(s, size):
    return sum(OLD[size][c][4] for c in s if c in OLD[size])

def cap_band(table):
    tops = [table[c][3] for c in "HEFILT"]
    bots = [table[c][3] + table[c][1] for c in "HEFILT"]
    return min(tops), max(bots)

# ---- every headline the firmware can draw ----
def corpus():
    out = set()
    st = open(os.path.join(ROOT, "include", "state.h"), encoding="utf-8").read()
    i = st.index("inline const char* detectionTypeName")
    out |= set(re.findall(r'"([A-Z0-9 !\'-]{2,})"', st[i:i + 3000]))
    di = open(os.path.join(ROOT, "src", "device_info.cpp"), encoding="utf-8").read()
    out |= set(re.findall(r'\{ *DetectionType::\w+, *"([^"]+)"', di))
    sg = open(os.path.join(ROOT, "src", "signatures.cpp"), encoding="utf-8").read()
    out |= {lab.upper() for lab in re.findall(r'"([A-Za-z0-9][A-Za-z0-9 \-]{2,13})"', sg)
            if re.search(r"[A-Za-z]", lab)}
    out |= {"NEARBY", "LOG EMPTY", "MULESKIN", "HUNT", "MORE INFO", "MESSAGE",
            "WORD 3 OF 5", "BINGO", "FIRST OF ITS KIND", "IGNORE", "SNOOZE"}
    return {s for s in out if s and all(c in CHARS for c in s)}

WORDS = corpus()

def render(font, ch, squeeze):
    """One glyph, 1 bpp: (w, h, xoff, yoff_from_ascender_top, advance, rows)."""
    adv = font.getlength(ch)
    if ch == " ":
        return 0, 0, 0, 0, max(1, round(adv * squeeze)), []
    x0, y0, x1, y1 = font.getbbox(ch)          # anchor "la": y from the ascender top
    pad = 4
    im = Image.new("L", (x1 - x0 + 2 * pad, y1 - y0 + 2 * pad), 0)
    ImageDraw.Draw(im).text((pad - x0, pad - y0), ch, font=font, fill=255)
    nw = max(1, round(im.width * squeeze))
    im = im.resize((nw, im.height), Image.LANCZOS).point(lambda v: 255 if v >= 128 else 0)
    bb = im.getbbox()
    if not bb:
        return 0, 0, 0, 0, max(1, round(adv * squeeze)), []
    im = im.crop(bb)
    xoff = round((x0 - pad) * squeeze) + bb[0]
    yoff = (y0 - pad) + bb[1]
    rows = []
    for y in range(im.height):
        bits = [1 if im.getpixel((x, y)) else 0 for x in range(im.width)]
        row = []
        for b in range(0, len(bits), 8):
            byte = 0
            for k, v in enumerate(bits[b:b + 8]):
                byte |= v << (7 - k)
            row.append(byte)
        rows.append(row)
    return im.width, im.height, xoff, yoff, round(adv * squeeze), rows

def build(size, pt):
    font = ImageFont.truetype(FONT, pt)
    font.set_variation_by_axes([WGHT])
    # The squeeze: the tightest any headline needs, then nudged down until the
    # rounded advances agree too.
    s = min(1.0, min(old_width(w, size) / font.getlength(w) for w in WORDS))
    while True:
        glyphs = {c: render(font, c, s) for c in CHARS}
        worst = max(sum(glyphs[c][4] for c in w) - old_width(w, size) for w in WORDS)
        if worst <= 0:
            break
        s -= 0.005
    # Vertical: centre the capitals on the old font's capital band.
    otop, obot = cap_band(OLD[size])
    caps = [glyphs[c] for c in "HEFILT"]
    ntop = min(g[3] for g in caps); nbot = max(g[3] + g[1] for g in caps)
    shift = round(((otop + obot) - (ntop + nbot)) / 2)
    glyphs = {c: (g[0], g[1], g[2], g[3] + shift, g[4], g[5]) for c, g in glyphs.items()}
    return glyphs, s, (ntop + shift, nbot + shift), (otop, obot)

lines = [
    "// Generated by tools/gen_headline_font.py from Orbitron[wght].ttf (SIL OFL,",
    "// tools/fonts/Orbitron-OFL.txt), weight %d, narrowed to keep every headline" % WGHT,
    "// within its old Bangers width. Names kept from the Bangers file it replaced.",
    "// 1bpp glyph bitmaps, row-major, MSB first, each row byte-padded.",
    "#pragma once",
    "#include <stdint.h>",
    "",
    "namespace BangersFont {",
    "",
    "struct Glyph {",
    "    char ch;",
    "    uint8_t w, h;",
    "    int8_t xoff, yoff;",
    "    uint8_t advance;",
    "    const uint8_t* bitmap;",
    "};",
]
for size, pt in SIZES.items():
    glyphs, s, ncap, ocap = build(size, pt)
    if size == "LG":
        LG_SQUEEZE = s
    lines += ["", "// size %s: Orbitron %dpt, squeezed to %d%%, caps rows %d-%d (Bangers' were %d-%d)"
              % (size, pt, round(s * 100), ncap[0], ncap[1], ocap[0], ocap[1])]
    for c in CHARS:
        g = glyphs[c]
        if not g[5]:
            continue
        name = NAMES.get(c, c)
        flat = [b for row in g[5] for b in row]
        lines.append("static const uint8_t g_%s_%s_bits[] = { %s };" % (
            size, name, ", ".join("0x%02X" % b for b in flat)))
    lines.append("")
    lines.append("static const Glyph %s_GLYPHS[] = {" % size)
    for c in CHARS:
        w, h, xo, yo, adv, rows = glyphs[c]
        cc = "\\'" if c == "'" else c
        bits = "g_%s_%s_bits" % (size, NAMES.get(c, c)) if rows else "nullptr"
        lines.append("    { '%s', %d, %d, %d, %d, %d, %s }," % (cc, w, h, xo, yo, adv, bits))
    lines.append("};")
    lines.append("static const uint8_t %s_GLYPH_COUNT = %d;" % (size, len(CHARS)))
    lines.append("static const uint8_t %s_ASCENT = %d;" % (size, OLD_ASCENT[size]))
    print("%s: %dpt, squeeze %.3f, caps %s (old %s)" % (size, pt, s, ncap, ocap))
# XL: no Bangers size to match -- LG's squeeze, its own ascent, capitals only.
xl = ImageFont.truetype(FONT, XL_PT)
xl.set_variation_by_axes([WGHT])
xl_s = LG_SQUEEZE
xl_glyphs = {c: render(xl, c, xl_s) for c in XL_CHARS}
xl_top = min(g[3] for c, g in xl_glyphs.items() if g[5])
xl_glyphs = {c: (g[0], g[1], g[2], g[3] - xl_top, g[4], g[5]) for c, g in xl_glyphs.items()}
xl_ascent = max(g[3] + g[1] for g in xl_glyphs.values() if g[5])
lines += ["", "// size XL: Orbitron %dpt, squeezed to %d%% (LG's), capitals only -- the IN A MEETING sign"
          % (XL_PT, round(xl_s * 100))]
for c in XL_CHARS:
    g = xl_glyphs[c]
    if g[5]:
        lines.append("static const uint8_t g_XL_%s_bits[] = { %s };" % (
            NAMES.get(c, c), ", ".join("0x%02X" % b for row in g[5] for b in row)))
lines += ["", "static const Glyph XL_GLYPHS[] = {"]
for c in XL_CHARS:
    w, h, xo, yo, adv, rows = xl_glyphs[c]
    bits = "g_XL_%s_bits" % NAMES.get(c, c) if rows else "nullptr"
    lines.append("    { '%s', %d, %d, %d, %d, %d, %s }," % (c, w, h, xo, yo, adv, bits))
lines += ["};", "static const uint8_t XL_GLYPH_COUNT = %d;" % len(XL_CHARS),
          "static const uint8_t XL_ASCENT = %d;" % xl_ascent]
print("XL: %dpt, squeeze %.3f, cap height %d" % (XL_PT, xl_s, xl_ascent))
lines += ["", "} // namespace BangersFont", ""]

with open(OUT, "w", newline="\n") as f:
    f.write("\n".join(lines))
print("->", os.path.normpath(OUT))
