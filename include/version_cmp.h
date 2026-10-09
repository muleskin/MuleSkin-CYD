// MuleSkin-CYD — release versions: comparing them and showing them. Pure
// string arithmetic, so test/version_test.cpp checks it on the desktop.
//
// A release is three numbers, "x.y.z", optionally after a "v". FIRMWARE_VERSION
// is git describe's output, so it may carry a tail -- "v3.1.2-3-g554330d" past
// a tag, "-dirty" with local edits -- which neither function lets get in the
// way. Anything without three numbers in front (an untagged build's bare hash,
// a two-part "03.01") is not a version at all.
#pragma once
#include <stddef.h>

namespace Version {

// a is a strictly newer release than b. False if either is not x.y.z.
bool newer(const char* a, const char* b);

// The splash screen's label: "V.3.1.2" for anything that starts with x.y.z,
// else the text itself, cut to 12 characters.
void label(char* out, size_t cap, const char* v);

}  // namespace Version
