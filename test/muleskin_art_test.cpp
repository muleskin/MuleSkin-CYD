// MuleSkin-CYD — the compressed MuleSkin artwork (src/muleskin_art.cpp).
//
// What this guards: that every row decodes back to exactly the pixels
// tools/make_boot_art.py packed -- CHECK in the header is their FNV-1a -- and
// that rows decode on their own, in any order, which is what drawArtwork()'s
// sampling relies on.
#include "test_util.h"
#include "muleskin_art.h"
#include "muleskin_art_row.h"
#include <cstring>

using namespace MuleSkinArt;

int main() {
    suite("the picture comes back exactly");
    {
        uint32_t h = 0x811C9DC5u;
        uint8_t row[SIZE];
        for (int r = 0; r < SIZE; r++) {
            MuleSkinArt::row(r, row);
            for (int c = 0; c < SIZE; c++) h = (h ^ row[c]) * 0x01000193u;
        }
        ck("every row, in order, hashes to CHECK", h == CHECK);
    }

    suite("rows stand alone");
    {
        uint8_t a[SIZE], b[SIZE], c[SIZE];
        MuleSkinArt::row(200, a);
        MuleSkinArt::row(3, b);
        MuleSkinArt::row(200, c);
        ck("row 200 is the same before and after another", memcmp(a, c, SIZE) == 0);
        MuleSkinArt::row(-5, a);
        MuleSkinArt::row(0, b);
        ck("a row before the top is the top", memcmp(a, b, SIZE) == 0);
        MuleSkinArt::row(SIZE + 9, a);
        MuleSkinArt::row(SIZE - 1, b);
        ck("a row past the bottom is the bottom", memcmp(a, b, SIZE) == 0);
        bool inPalette = true;
        for (int i = 0; i < SIZE && inPalette; i++) {
            bool found = false;
            for (int k = 0; k < COLORS; k++) if (PALETTE[k] == b[i]) found = true;
            inPalette = found;
        }
        ck("every pixel is one of the palette's", inPalette);
    }

    suite("smaller than raw");
    ck("under 40 KB against the 100 KB it was",
       sizeof(BITS) + sizeof(ROW_BIT) + sizeof(SYMS) + sizeof(SYM_BASE) + sizeof(LEN_COUNT) + sizeof(PALETTE) < 40000);
    return report();
}
