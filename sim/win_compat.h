// MuleSkin-CYD — host-build shims for MinGW on Windows.
//
// The emulator and the host tests are written against a POSIX libc. MinGW's
// runtime has most of it, a few pieces under other names, and a few not at
// all. The Makefiles force-include this only for a MinGW compiler, so
// nothing here reaches a Linux or WSL build, the wasm build or the firmware.
#pragma once
#if defined(_WIN32)

// M_PI and friends: hidden by -std=c++17 (strict ANSI) unless asked for, and
// it has to be asked for before the first include of <math.h>.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
// localtime_r()/gmtime_r(): MinGW's <time.h> provides them behind this.
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L
#endif

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <direct.h>

inline char* strcasestr(const char* hay, const char* needle) {
    if (!*needle) return const_cast<char*>(hay);
    for (; *hay; hay++) {
        const char* h = hay;
        const char* n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
        if (!*n) return const_cast<char*>(hay);
    }
    return nullptr;
}

inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value);
}
inline int unsetenv(const char* name) { return _putenv_s(name, ""); }

// POSIX mkdir takes a mode; Windows has no such thing.
inline int mkdir(const char* path, int /*mode*/) { return _mkdir(path); }

#endif // _WIN32
