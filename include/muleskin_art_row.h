// MuleSkin-CYD — the MuleSkin artwork, one row at a time.
//
// include/muleskin_art.h holds it compressed (tools/make_boot_art.py), so
// nothing reads the pixels directly any more: row() decodes row `r` (clamped
// to the picture) into `out`, MuleSkinArt::SIZE bytes of RGB332.
#pragma once
#include <stdint.h>

namespace MuleSkinArt {
void row(int r, uint8_t* out);
}
