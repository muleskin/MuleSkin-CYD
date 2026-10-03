// MuleSkin-CYD — a WiFi network's security, read from its beacon.
//
// Until wardriving, a beacon told this firmware one bit: Privacy, set or not.
// WiGLE wants the whole thing -- WPA2-PSK-CCMP, WPA3-SAE, an open network --
// and it is all in the beacon's tagged parameters: the RSN element (48) for
// WPA2 and WPA3, Microsoft's vendor element (221, 00:50:F2 type 1) for the
// original WPA, and the capability field for WEP (Privacy with neither) and
// for ESS/IBSS. Parsed into bits so a wardrive record carries it in two
// bytes, and written out as WiGLE's capability string only on export.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace WifiAuth {

enum : uint16_t {
    PRIV = 1u << 0,   // capability Privacy bit
    WPA  = 1u << 1,   // vendor WPA element present
    RSN  = 1u << 2,   // RSN element present
    PSK  = 1u << 3,
    EAP  = 1u << 4,
    SAE  = 1u << 5,
    OWE  = 1u << 6,
    CCMP = 1u << 7,
    TKIP = 1u << 8,
    GCMP = 1u << 9,
    ESS  = 1u << 10,
    IBSS = 1u << 11,
};

// `cap` is the beacon's 16-bit capability field; `ies` and `len` its tagged
// parameters (everything after the 12 fixed bytes). Never reads past len.
uint16_t parse(uint16_t cap, const uint8_t* ies, uint16_t len);

// WiGLE's AuthMode column, e.g. "[WPA2-PSK-CCMP][ESS]", "[WPA2-PSK-CCMP]
// [WPA3-SAE-CCMP][ESS]" for a transition network, "[WEP][ESS]", "[ESS]" for
// open. Returns the length written, always NUL-terminated.
size_t wigle(uint16_t bits, char* out, size_t n);

}  // namespace WifiAuth
