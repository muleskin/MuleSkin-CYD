// MuleSkin-CYD — decoding the MuleSkin artwork a row at a time.
// The format is tools/make_boot_art.py's; see the top of that file.
#include "muleskin_art_row.h"
#include "muleskin_art.h"

namespace MuleSkinArt {

void row(int r, uint8_t* out) {
    if (r < 0) r = 0;
    if (r >= SIZE) r = SIZE - 1;
    uint32_t bit = ROW_BIT[r];
    int ctx = COLORS;                     // a row's first pixel has its own table
    for (int c = 0; c < SIZE; c++) {
        // Canonical Huffman: walk the lengths, one bit at a time.
        const uint8_t* lc = LEN_COUNT + ctx * (MAXLEN + 1);
        int code = 0, first = 0, idx = 0, sym = 0;
        for (int len = 1; len <= MAXLEN; len++) {
            code |= (BITS[bit >> 3] >> (7 - (bit & 7))) & 1;
            bit++;
            const int n = lc[len];
            if (code - first < n) { sym = SYMS[SYM_BASE[ctx] + idx + code - first]; break; }
            idx   += n;
            first  = (first + n) << 1;
            code <<= 1;
        }
        out[c] = PALETTE[sym];
        ctx = sym;
    }
}

}  // namespace MuleSkinArt
