// MuleSkin-CYD — the firmware signature check. See include/ota_sig.h.
#include "ota_sig.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls_compat.h"

namespace OtaSig {

// One curve, one hash, one key that was compiled in -- so this talks to the
// ECDSA code directly instead of going through mbedtls's general key layer.
// That layer would read the key from its PEM text every time, and reading a
// PEM pulls base64, the ASN.1 key structures and RSA into the image: 15 KB of
// flash to arrive at the same 65 bytes that sit in ota_pubkey.h already.
//
// mbedtls_ecdsa_read_signature() takes the DER signature openssl writes
// (tools/sign_firmware.py), rejects a signature with trailing bytes after it,
// and checks r and s are in range before doing any curve arithmetic.
int checkWith(const unsigned char point[65], const uint8_t hash[32], const uint8_t* sig, size_t sigLen) {
    mbedtls_ecdsa_context ctx;
    mbedtls_ecdsa_init(&ctx);
    int rc = mbedtls_ecp_group_load(&SQW_ECDSA_GRP(ctx), MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0) rc = mbedtls_ecp_point_read_binary(&SQW_ECDSA_GRP(ctx), &SQW_ECDSA_Q(ctx), point, 65);
    if (rc == 0) rc = mbedtls_ecp_check_pubkey(&SQW_ECDSA_GRP(ctx), &SQW_ECDSA_Q(ctx));
    if (rc == 0) rc = mbedtls_ecdsa_read_signature(&ctx, hash, 32, sig, sigLen);
    mbedtls_ecdsa_free(&ctx);
    return rc;
}

int check(const unsigned char (*points)[65], unsigned n, const uint8_t hash[32], const uint8_t* sig, size_t sigLen) {
    int rc = -1;
    for (unsigned i = 0; i < n; i++) {
        rc = checkWith(points[i], hash, sig, sigLen);
        if (rc == 0) return 0;
    }
    return rc;
}

}  // namespace OtaSig
