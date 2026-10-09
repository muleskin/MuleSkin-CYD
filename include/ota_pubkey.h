// MuleSkin-CYD — the public half of the firmware signing key.
//
// Over-the-air updates are only installed when their signature verifies
// against this key (see ota_core.h). The private half never lives in this
// repository: it is the OTA_SIGNING_KEY secret the release workflow signs with,
// plus the owner's offline backup.
//
// A public key is safe to publish. What matters is that this one and that
// secret are a PAIR: sign with anything else and every board refuses the image.
//
// The real key, generated 2026-10-07 (ECDSA P-256). It replaces the
// 2026-09-12 key, whose private half was lost: boards built before this
// change trust only that old key, so they take this one through a USB
// install, not over the air. Replacing it is not a quick change: every board
// in the field only trusts THIS key, so a new one reaches them only through a
// USB install -- or through an over-the-air update signed with the old key
// that carries the new one.
//
// It is kept here as the RAW CURVE POINT rather than as the PEM text it came
// from. Handing mbedtls a PEM means handing it the general-purpose key parser,
// which drags in base64, ASN.1 key structures and RSA -- 15 KB of flash, for
// one fixed key on one curve that this firmware has known since it was built.
// The point below is exactly what that parser would have produced.
//
// The PEM it was taken from, for anyone checking it against the private half:
//
//     -----BEGIN PUBLIC KEY-----
//     MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEt5nm4mO+DZ5KcvpJ5LT/IIFXPkqj
//     ufXoHvv0IdTuM+6b32/gduYAydRlSjkYRPMmjox3/NwpamwNOXpJ7211ig==
//     -----END PUBLIC KEY-----
//
// To regenerate the array after a key change (the last 65 bytes of the DER
// are the point; 0x04 marks it uncompressed, then X and Y, 32 bytes each):
//
//     openssl ec -pubin -in key.pub.pem -outform DER | tail -c 65 | xxd -i
#pragma once

#define OTA_PUBKEY_IS_TEST 0

// Every key the boards accept, as uncompressed P-256 points (0x04 || X || Y),
// newest first. Normally one. To ROTATE (deploy/README.md, "Rotating the
// signing key"): add the new key here next to the old one and release that
// build signed with the OLD key -- boards take it over WiFi and from then on
// trust both -- then sign with the new key, and drop the old one from this
// list in a later release. A signature that matches any key here is accepted.
// Keep the PEM comment above in step: the build checks each signature against
// the PEM blocks in it.
static const unsigned char OTA_PUBKEYS[][65] = { {
    0x04, 0xB7, 0x99, 0xE6, 0xE2, 0x63, 0xBE, 0x0D, 0x9E, 0x4A, 0x72, 0xFA,
    0x49, 0xE4, 0xB4, 0xFF, 0x20, 0x81, 0x57, 0x3E, 0x4A, 0xA3, 0xB9, 0xF5,
    0xE8, 0x1E, 0xFB, 0xF4, 0x21, 0xD4, 0xEE, 0x33, 0xEE, 0x9B, 0xDF, 0x6F,
    0xE0, 0x76, 0xE6, 0x00, 0xC9, 0xD4, 0x65, 0x4A, 0x39, 0x18, 0x44, 0xF3,
    0x26, 0x8E, 0x8C, 0x77, 0xFC, 0xDC, 0x29, 0x6A, 0x6C, 0x0D, 0x39, 0x7A,
    0x49, 0xEF, 0x6D, 0x75, 0x8A,
} };
static const unsigned OTA_PUBKEY_N = sizeof(OTA_PUBKEYS) / sizeof(OTA_PUBKEYS[0]);
