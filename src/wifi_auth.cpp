// MuleSkin-CYD — WiFi security from a beacon. See wifi_auth.h.
#include "wifi_auth.h"
#include <string.h>
#include <stdio.h>

namespace WifiAuth {

namespace {

// A cipher suite selector's type byte (the OUI is 00:0F:AC for RSN and
// 00:50:F2 for WPA; the type numbers agree for the ones read here).
uint16_t cipherBit(uint8_t t) {
    switch (t) {
        case 2:  return TKIP;
        case 4:  return CCMP;
        case 8: case 9: return GCMP;   // GCMP-128, GCMP-256
        default: return 0;
    }
}

uint16_t akmBit(uint8_t t, bool rsn) {
    switch (t) {
        case 1: case 3: case 5: case 11: case 12: case 13: return EAP;  // 802.1X, FT, SHA256, Suite B
        case 2: case 4: case 6: return PSK;                            // PSK, FT-PSK, PSK-SHA256
        case 8: case 9: return rsn ? SAE : 0;                          // SAE, FT-SAE
        case 18: return rsn ? OWE : 0;
        default: return 0;
    }
}

// The body shared by RSN (after its 2-byte version) and WPA (after OUI, type
// and version): group cipher, pairwise ciphers, AKMs. Stops at whatever the
// element is too short to hold.
uint16_t suites(const uint8_t* p, int n, bool rsn) {
    uint16_t b = 0;
    if (n < 4) return b;
    p += 4; n -= 4;                          // group cipher: not reported
    if (n < 2) return b;
    int count = p[0] | (p[1] << 8); p += 2; n -= 2;
    for (int i = 0; i < count && n >= 4; i++, p += 4, n -= 4) b |= cipherBit(p[3]);
    if (n < 2) return b;
    count = p[0] | (p[1] << 8); p += 2; n -= 2;
    for (int i = 0; i < count && n >= 4; i++, p += 4, n -= 4) b |= akmBit(p[3], rsn);
    return b;
}

size_t put(char* out, size_t n, size_t at, const char* s) {
    const size_t l = strlen(s);
    if (at + l >= n) return at;
    memcpy(out + at, s, l + 1);
    return at + l;
}

const char* ciphers(uint16_t b) {
    if ((b & TKIP) && (b & CCMP)) return "TKIP+CCMP";
    if (b & CCMP) return "CCMP";
    if (b & GCMP) return "GCMP";
    if (b & TKIP) return "TKIP";
    return "";
}

}  // namespace

uint16_t parse(uint16_t cap, const uint8_t* ies, uint16_t len) {
    uint16_t b = 0;
    if (cap & 0x0001) b |= ESS;
    if (cap & 0x0002) b |= IBSS;
    if (cap & 0x0010) b |= PRIV;
    uint16_t i = 0;
    while (ies && (uint32_t)i + 2u <= len) {
        const uint8_t id = ies[i], l = ies[i + 1];
        if ((uint32_t)i + 2u + l > len) break;
        const uint8_t* body = ies + i + 2;
        if (id == 48 && l >= 2) {
            b |= RSN;
            b |= suites(body + 2, l - 2, true);
        } else if (id == 221 && l >= 6 && body[0] == 0x00 && body[1] == 0x50 && body[2] == 0xF2 && body[3] == 0x01) {
            b |= WPA;
            b |= suites(body + 6, l - 6, false);   // OUI, type, 2-byte version
        }
        i = (uint16_t)(i + 2 + l);
    }
    return b;
}

size_t wigle(uint16_t b, char* out, size_t n) {
    if (!out || !n) return 0;
    out[0] = 0;
    size_t at = 0;
    char tok[40];
    const char* c = ciphers(b);
    if (b & WPA) {
        snprintf(tok, sizeof tok, "[WPA-%s%s%s]", (b & EAP) && !(b & RSN) ? "EAP" : "PSK", c[0] ? "-" : "", c);
        at = put(out, n, at, tok);
    }
    if (b & RSN) {
        if (b & PSK) { snprintf(tok, sizeof tok, "[WPA2-PSK%s%s]", c[0] ? "-" : "", c); at = put(out, n, at, tok); }
        if (b & EAP) { snprintf(tok, sizeof tok, "[WPA2-EAP%s%s]", c[0] ? "-" : "", c); at = put(out, n, at, tok); }
        if (b & SAE) { snprintf(tok, sizeof tok, "[WPA3-SAE%s%s]", c[0] ? "-" : "", c); at = put(out, n, at, tok); }
        if (b & OWE) { snprintf(tok, sizeof tok, "[WPA3-OWE%s%s]", c[0] ? "-" : "", c); at = put(out, n, at, tok); }
        if (!(b & (PSK | EAP | SAE | OWE))) { snprintf(tok, sizeof tok, "[WPA2%s%s]", c[0] ? "-" : "", c); at = put(out, n, at, tok); }
    }
    if ((b & PRIV) && !(b & (WPA | RSN))) at = put(out, n, at, "[WEP]");
    if (b & ESS)  at = put(out, n, at, "[ESS]");
    if (b & IBSS) at = put(out, n, at, "[IBSS]");
    return at;
}

}  // namespace WifiAuth
