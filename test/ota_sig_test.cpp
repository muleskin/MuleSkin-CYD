// MuleSkin-CYD — the firmware update's signature check (src/ota_sig.cpp).
//
// What this guards: the one decision that lets a board install an image sent
// over the air. Against vectors from Python's `cryptography` and a throwaway
// key (test/gen_ota_sig_vectors.py): the image's SHA-256 as mbedtls computes
// it matches, the good signature passes, and nothing near it does -- other
// bytes, another key, a signature cut short or with a byte added, an empty
// one, and a key list that has the right key second (a key rotation).
//
// Needs mbedtls on the computer (libmbedtls-dev; CI installs it): the Makefile
// leaves this test out where there is none.
#include "test_util.h"
#include "ota_sig.h"
#include "ota_sig_vectors.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"
#include <cstring>

int main() {
    suite("the image's hash, by mbedtls");
    {
        uint8_t image[IMAGE_LEN];
        for (unsigned i = 0; i < IMAGE_LEN; i++) image[i] = (uint8_t)((i * 7 + 3) & 0xFF);
        uint8_t h[32];
#if MBEDTLS_VERSION_MAJOR >= 3
        mbedtls_sha256(image, IMAGE_LEN, h, 0);
#else
        mbedtls_sha256_ret(image, IMAGE_LEN, h, 0);
#endif
        ck("matches Python's", memcmp(h, IMAGE_SHA, 32) == 0);
    }

    suite("what passes");
    ck("the good signature", OtaSig::check(KEY, 1, IMAGE_SHA, SIG, sizeof SIG) == 0);
    {
        unsigned char keys[2][65];
        memcpy(keys[0], WRONG_KEY[0], 65);
        memcpy(keys[1], KEY[0], 65);
        ck("with the right key second (a rotation)", OtaSig::check(keys, 2, IMAGE_SHA, SIG, sizeof SIG) == 0);
    }

    suite("what does not");
    ck("a signature over other bytes", OtaSig::check(KEY, 1, IMAGE_SHA, SIG_OTHER, sizeof SIG_OTHER) != 0);
    ck("the right signature, the wrong image", OtaSig::check(KEY, 1, OTHER_SHA, SIG, sizeof SIG) != 0);
    ck("another key", OtaSig::check(WRONG_KEY, 1, IMAGE_SHA, SIG, sizeof SIG) != 0);
    ck("cut short", OtaSig::check(KEY, 1, IMAGE_SHA, SIG, sizeof SIG - 1) != 0);
    {
        uint8_t longer[sizeof SIG + 1];
        memcpy(longer, SIG, sizeof SIG);
        longer[sizeof SIG] = 0;
        ck("a byte added", OtaSig::check(KEY, 1, IMAGE_SHA, longer, sizeof longer) != 0);
        uint8_t flipped[sizeof SIG];
        memcpy(flipped, SIG, sizeof SIG);
        flipped[sizeof SIG - 3] ^= 0x01;
        ck("one bit of it changed", OtaSig::check(KEY, 1, IMAGE_SHA, flipped, sizeof flipped) != 0);
    }
    ck("empty", OtaSig::check(KEY, 1, IMAGE_SHA, SIG, 0) != 0);
    ck("no keys at all", OtaSig::check(KEY, 0, IMAGE_SHA, SIG, sizeof SIG) != 0);
    {
        unsigned char bad[1][65];
        memcpy(bad[0], KEY[0], 65);
        bad[0][64] ^= 0x01;                     // not a point on the curve
        ck("a key that is not on the curve", OtaSig::check(bad, 1, IMAGE_SHA, SIG, sizeof SIG) != 0);
    }
    return report();
}
