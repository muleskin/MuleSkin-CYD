// MuleSkin-CYD — release versions. See include/version_cmp.h.
#include "version_cmp.h"
#include <stdio.h>

namespace Version {

// "1.7.8" or "v1.7.8" (and any tail after it) into three numbers.
static bool parts(const char* s, unsigned v[3]) {
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    return sscanf(s, "%u.%u.%u", &v[0], &v[1], &v[2]) == 3;
}

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
