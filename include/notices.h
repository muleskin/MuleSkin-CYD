// MuleSkin-CYD — what MuleSkin notices: idle lines built from what the
// board already knows.
//
// The DEX's lore recycled as "did you know", nudges toward cards still
// blank, today against yesterday from the black box, and the calendar --
// Friday the 13th, Halloween week, 3:33 in the morning. No art, no screens:
// a line at a time, handed to MuleSkin's idle roll through setIdleProvider().
#pragma once
#include <stdint.h>

class DetectionEngine;

namespace Notices {

// A line for the idle roll, or null when it has nothing this time. The
// returned text lives in a static buffer until the next call.
const char* idleLine(const DetectionEngine& eng);

}  // namespace Notices
