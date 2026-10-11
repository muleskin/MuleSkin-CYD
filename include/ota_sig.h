// MuleSkin-CYD — is this firmware image ours? The signature check, alone.
//
// Split out of ota_core.cpp (which needs the ESP32's OTA partitions and
// FreeRTOS) so the one function that decides whether a board installs an
// image can be tested on a computer against real mbedtls: test/ota_sig_test.cpp,
// with vectors from Python's `cryptography` (test/gen_ota_sig_vectors.py),
// which shares no code with mbedtls. CI runs it on every push.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace OtaSig {

// 0 when `sig` (DER, as tools/sign_firmware.py writes it) is a valid
// ECDSA P-256 signature of `hash` (the image's SHA-256) by the key whose raw
// uncompressed point (0x04 || X || Y) is `point`; an mbedtls error if not.
int checkWith(const unsigned char point[65], const uint8_t hash[32], const uint8_t* sig, size_t sigLen);

// The same against each of `n` keys in turn (more than one only while a key
// is being rotated): 0 on the first that matches, else the last error.
int check(const unsigned char (*points)[65], unsigned n, const uint8_t hash[32], const uint8_t* sig, size_t sigLen);

}  // namespace OtaSig
