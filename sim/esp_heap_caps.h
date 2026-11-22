// MuleSkin-Sim — heap-caps shim (diagnostics screen only).
#pragma once
#include <cstddef>
#define MALLOC_CAP_8BIT  (1 << 2)
#define MALLOC_CAP_DMA   (1 << 3)
// One heap in the emulator, so the mask changes nothing here; the firmware's
// pressure checks name it (src/detection.cpp). The real value.
#define MALLOC_CAP_INTERNAL (1 << 11)
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 110 * 1024; }
inline size_t heap_caps_get_free_size(uint32_t) { return 180 * 1024; }
