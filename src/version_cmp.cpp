// MuleSkin-CYD — release versions. See include/version_cmp.h.
#include "version_cmp.h"
#include <stdio.h>
#include <stdlib.h>

namespace Version {

// By hand: three numbers and two dots, nothing for sscanf to do. (Taking
// sscanf out of here does not take newlib's scanf engine out of the firmware
// -- the Arduino core's IPv6Address parser links it anyway.)
bool parse(const char* s, unsigned v[3]) {
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') return false;
        char* end = nullptr;
        v[i] = (unsigned)strtoul(s, &end, 10);
        s = end;
        if (i < 2) { if (*s != '.') return false; s++; }
    }
    return true;
}

static bool parts(const char* s, unsigned v[3]) { return parse(s, v); }

bool newer(const char* a, const char* b) {
    unsigned x[3], y[3];
    if (!parts(a, x) || !parts(b, y)) return false;
    for (int i = 0; i < 3; i++) { if (x[i] != y[i]) return x[i] > y[i]; }
    return false;
}

void label(char* out, size_t cap, const char* v) {
    if (!cap) return;
    unsigned p[3];
    if (parts(v, p)) snprintf(out, cap, "V.%u.%u.%u", p[0], p[1], p[2]);
    else {
        if (v && (*v == 'v' || *v == 'V')) v++;
        snprintf(out, cap, "%.12s", v ? v : "");
    }
}

}  // namespace Version
