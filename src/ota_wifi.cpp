// MuleSkin-CYD — firmware updates over WiFi. See include/ota_wifi.h.
#include "ota_wifi.h"
#if defined(CROWPANEL7)
#include "crowpanel7_backlight.h"
#include "settings.h"
#endif
#include "security.h"
#include "settings.h"   // the update channel (STABLE / LAB)
#include "signatures.h" // setExtraRules(): detection rules from the site
#include "clock.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef OTA_WIFI_BASE
#define OTA_WIFI_BASE "http://flasher.oillie.cloud/"
#endif

using OtaCore::Fail;

namespace OtaWifi {
namespace {

// The update channel's file names. STABLE: manifest-<env>.json and
// <env>-firmware.bin/.sig, what the web flasher installs. LAB: the same with
// "lab-" in front (manifest-lab-<env>.json, lab-<env>-firmware.*), published
// by build_flasher_bins.sh LAB_VERSION=... ahead of a release, so a test board
// takes a build over WiFi before everyone else does. Settings::labChannel().
String manifestName() {
    return String(Settings::labChannel() ? "manifest-lab-" : "manifest-") + OtaCore::buildName() + ".json";
}
String firmwareName(const char* ext) {
    return String(Settings::labChannel() ? "lab-" : "") + OtaCore::buildName() + "-firmware." + ext;
}

const char* NVS_NS = "otawifi";
const uint32_t JOIN_TIMEOUT_MS  = 20000;
const uint32_t STALL_TIMEOUT_MS = 15000;
const uint32_t TASK_STACK       = 8192;    // measured 3.2 KB used over a whole plain-HTTP update (was 12 KB, sized for TLS)

volatile State s_state   = State::OFF;
volatile Fail  s_fail    = Fail::NONE;
volatile bool  s_install = false;
volatile bool  s_cancel  = false;
volatile bool  s_downloadStarted = false;
volatile uint32_t s_rx   = 0;
volatile uint32_t s_size = 0;

Net     s_nets[NET_MAX];
uint8_t s_netN = 0;

char    s_ssid[33]  = "";
char    s_pass[65]  = "";
bool    s_save      = false;
bool    s_savedRead = false;
bool    s_autoJoin  = false;    // begin() with networks saved: join the best one after the scan
struct Saved { char ssid[33]; SavedResult result; };
Saved   s_list[SAVED_MAX];
uint8_t s_n = 0, s_use = 0;
char    s_latest[24] = "";
uint8_t s_sig[80];
uint8_t s_sigLen = 0;

TaskHandle_t s_task       = nullptr;
// What the last fetch from the site ran into, in a few words: shown after the
// failure's own sentence and printed to the log, so a person reporting "it
// won't update" can say which of the ways it was.
char    s_why[28]   = "";

void key(char* out, const char* k, uint8_t i) { snprintf(out, 4, "%s%u", k, (unsigned)i); }

// Keys: n, use, s0..s5 (names), p0..p5 (passwords), r0..r5 (how the last try
// went). A board from before the list had one network under ssid/pass; it
// becomes slot 0 the first time this runs.
void readSaved() {
    if (s_savedRead) return;
    s_savedRead = true;
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    if (p.isKey("n")) {
        s_n   = p.getUChar("n", 0);   if (s_n > SAVED_MAX) s_n = SAVED_MAX;
        s_use = p.getUChar("use", 0); if (s_use >= s_n)    s_use = 0;
        for (uint8_t i = 0; i < s_n; i++) {
            char k[4];
            key(k, "s", i);
            strncpy(s_list[i].ssid, p.getString(k, "").c_str(), 32);
            s_list[i].ssid[32] = '\0';
            key(k, "r", i);
            s_list[i].result = (SavedResult)p.getUChar(k, 0);
        }
    } else if (p.isKey("ssid")) {
        strncpy(s_list[0].ssid, p.getString("ssid", "").c_str(), 32);
        s_list[0].ssid[32] = '\0';
        s_list[0].result = SavedResult::UNTRIED;
        if (s_list[0].ssid[0]) {
            s_n = 1; s_use = 0;
            p.putString("s0", s_list[0].ssid);
            p.putString("p0", p.getString("pass", ""));
            p.putUChar("n", 1);
            p.putUChar("use", 0);
            Serial.printf("[ota] wifi: %s moved to the network list\n", s_list[0].ssid);
        }
        p.remove("ssid");
        p.remove("pass");
    }
    p.end();
}

void writeList() {
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        p.putUChar("n", s_n);
        p.putUChar("use", s_use);
        for (uint8_t i = 0; i < s_n; i++) {
            char k[4];
            key(k, "s", i); p.putString(k, s_list[i].ssid);
            key(k, "r", i); p.putUChar(k, (uint8_t)s_list[i].result);
        }
        p.end();
    }
}

bool passAt(uint8_t i, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = '\0';
    if (i >= s_n) return false;
    Preferences p;
    if (!p.begin(NVS_NS, true)) return false;
    char k[4];
    key(k, "p", i);
    strncpy(out, p.getString(k, "").c_str(), cap - 1);
    out[cap - 1] = '\0';
    p.end();
    return true;
}

void setResult(int8_t i, SavedResult r) {
    if (i < 0 || i >= (int8_t)s_n || s_list[i].result == r) return;
    s_list[i].result = r;
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        char k[4];
        key(k, "r", (uint8_t)i);
        p.putUChar(k, (uint8_t)r);
        p.end();
    }
}

void fail(Fail f) {
    if (s_state == State::FAILED) return;
    // A cancel wins. The task can be several seconds inside an HTTP request
    // when end() is called, and whatever it decides on its way out must not
    // put update mode back on a screen the board has already left.
    if (s_cancel) return;
    s_fail  = f;
    s_state = State::FAILED;
    Serial.printf("[ota] wifi update stopped: %s%s%s%s\n", OtaCore::failWords(f),
                  s_why[0] ? " (" : "", s_why, s_why[0] ? ")" : "");
}

void startScan() {
    s_netN = 0;
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, false /* no hidden */);
    s_state = State::SCANNING;
}

void collectScan(int n) {
    s_netN = 0;
    for (int i = 0; i < n; i++) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue;
        const int8_t rssi = (int8_t)WiFi.RSSI(i);
        const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        // One row per name: the same network from two access points is one
        // choice, shown at its strongest.
        int found = -1;
        for (uint8_t k = 0; k < s_netN; k++) if (name == s_nets[k].ssid) { found = k; break; }
        if (found >= 0) {
            if (rssi > s_nets[found].rssi) s_nets[found].rssi = rssi;
            continue;
        }
        Net cand;
        strncpy(cand.ssid, name.c_str(), sizeof cand.ssid - 1);
        cand.ssid[sizeof cand.ssid - 1] = '\0';
        cand.rssi = rssi;
        cand.open = open;
        if (s_netN < NET_MAX) {
            s_nets[s_netN++] = cand;
        } else {
            // Full: replace the weakest if this one is stronger.
            uint8_t weakest = 0;
            for (uint8_t k = 1; k < s_netN; k++) if (s_nets[k].rssi < s_nets[weakest].rssi) weakest = k;
            if (rssi > s_nets[weakest].rssi) s_nets[weakest] = cand;
        }
    }
    // Strongest first.
    for (uint8_t i = 1; i < s_netN; i++)
        for (uint8_t j = i; j > 0 && s_nets[j].rssi > s_nets[j - 1].rssi; j--) {
            Net t = s_nets[j]; s_nets[j] = s_nets[j - 1]; s_nets[j - 1] = t;
        }
    WiFi.scanDelete();
}

// Plain HTTP, on purpose, for the firmware as well as the manifest.
//
// What made this safe was already here: every image is signed, and the board
// refuses one that is not ours or that is older than the one running (see
// OtaCore::finish). TLS was buying secrecy about WHICH public file was being
// downloaded, and charging about 45 KB of flash and a 40 KB contiguous heap
// block for it -- the block that forced the screen buffer to be freed before
// an update could start, on a board whose largest block is 34 KB.
//
// The cost is that somebody on the same network can see which release is
// being fetched, and a network that blocks plain HTTP now blocks updates too.
static WiFiClient* s_plain = nullptr;
WiFiClient* client() {
    if (!s_plain) s_plain = new WiFiClient();
    return s_plain;
}

// ---- Talking to the site ----------------------------------------------------
//
// One GET on a bare WiFiClient (ported from upstream SquachWatch v1.33.0,
// 2026-10-07). HTTPClient did this before, and every way a fetch could go
// wrong reached the screen as the same "couldn't reach the site": a hotel's
// sign-in page, a router's filter, a name lookup that failed and a slow minute
// all looked alike. This one tells them apart, knocks again when the trouble
// might pass, and reaches the site by the last address its name had when the
// router cannot look the name up. (The ntfy pushes keep HTTPClient: they need
// its headers and POST.)
//
// It understands what nginx sends here: a status line, headers with a
// Content-Length, then the body, and the connection closed when asked.

enum class Got : uint8_t { OK, NO_CONNECT, NO_ANSWER, SIGN_IN, HTTPS_ONLY, BLOCKED, MISSING, SITE_TROUBLE };

struct Reply {
    Got     got    = Got::NO_CONNECT;
    int     code   = 0;     // the HTTP status, 0 when none came back
    int32_t length = -1;    // Content-Length, -1 when the reply had none
};

// OTA_WIFI_BASE taken apart once: "http://host[:port]/prefix/".
char     s_host[48]   = "";
uint16_t s_port       = 80;
char     s_prefix[48] = "/";
bool     s_noName     = false;   // the name lookup failed this session: go straight to the remembered address

void parseBase() {
    if (s_host[0]) return;
    const char* b = OTA_WIFI_BASE;
    if (strncmp(b, "http://", 7) == 0) b += 7;
    const char* slash = strchr(b, '/');
    const char* end   = slash ? slash : b + strlen(b);
    const char* colon = (const char*)memchr(b, ':', (size_t)(end - b));
    size_t n = (size_t)((colon ? colon : end) - b);
    if (n >= sizeof s_host) n = sizeof s_host - 1;
    memcpy(s_host, b, n);
    s_host[n] = '\0';
    if (colon) s_port = (uint16_t)strtoul(colon + 1, nullptr, 10);
    if (slash) { strncpy(s_prefix, slash, sizeof s_prefix - 1); s_prefix[sizeof s_prefix - 1] = '\0'; }
}

// The site itself, with or without "www.".
bool ourHost(const char* h, size_t n) {
    if (n > 4 && strncasecmp(h, "www.", 4) == 0) { h += 4; n -= 4; }
    return n == strlen(s_host) && strncasecmp(h, s_host, n) == 0;
}

// The site's address as of the last name lookup that worked, kept in NVS: the
// way round a router whose name lookups are broken or filtered. Upstream uses
// GitHub Pages' four published addresses; this site is a server of our own,
// so it is the address the name last had. Written only when it changes.
uint32_t rememberedIp() {
    Preferences p;
    uint32_t v = 0;
    if (p.begin(NVS_NS, true)) { v = p.getUInt("siteip", 0); p.end(); }
    return v;
}
void rememberIp(const IPAddress& ip) {
    const uint32_t v = (uint32_t)ip;
    if (!v || v == rememberedIp()) return;
    Preferences p;
    if (p.begin(NVS_NS, false)) { p.putUInt("siteip", v); p.end(); }
}

// Everything inside `budgetMs`, apart from the name lookup itself, which lwIP
// gives up to 15 s of its own. The boot check passes what is left of its few
// seconds, so a bad network never holds the boot screen longer than before.
bool connectSite(WiFiClient& c, uint32_t budgetMs) {
    const uint32_t t0 = millis();
    IPAddress ip;
    const bool literal = ip.fromString(s_host);
#ifdef OTA_TEST_NO_DNS
    if (!literal) s_noName = true;   // bench: pretend the router cannot look names up
#endif
    if (literal || !s_noName) {
        if (literal || WiFi.hostByName(s_host, ip)) {
            if (!literal) rememberIp(ip);
            const uint32_t used = millis() - t0;
            const uint32_t left = used + 1000 < budgetMs ? budgetMs - used : 1000;
            if (c.connect(ip, s_port, (int32_t)(left > 8000 ? 8000 : left))) return true;
            Serial.printf("[ota] %s (%s): no connection\n", s_host, ip.toString().c_str());
            return false;
        }
        Serial.printf("[ota] name lookup for %s failed (asked %s, %s); trying its last address\n", s_host,
                      WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
        s_noName = true;
    }
    const uint32_t last = literal ? 0 : rememberedIp();
    if (!last) return false;
    const uint32_t used = millis() - t0;
    if (s_cancel || used + 1000 > budgetMs) return false;
    const uint32_t left = budgetMs - used;
    const IPAddress a(last);
    if (c.connect(a, s_port, (int32_t)(left > 8000 ? 8000 : left))) {
        Serial.printf("[ota] connected to %s by its last address\n", a.toString().c_str());
        return true;
    }
    return false;
}

// One line of the reply without its CR LF; anything past `cap` is dropped.
// False when nothing more came in time.
bool readLine(WiFiClient& c, char* out, size_t cap, uint32_t timeoutMs) {
    size_t n = 0;
    uint32_t t0 = millis();
    for (;;) {
        if (c.available() > 0) {
            const int ch = c.read();
            if (ch < 0) continue;
            t0 = millis();
            if (ch == '\n') {
                if (n && out[n - 1] == '\r') n--;
                out[n] = '\0';
                return true;
            }
            if (n < cap - 1) out[n++] = (char)ch;
        } else if (!c.connected()) {
            out[n] = '\0';
            return n > 0;
        } else if (millis() - t0 > timeoutMs || s_cancel) {
            return false;
        } else {
            delay(5);
        }
    }
}

void noteWhy(const Reply& r) {
    switch (r.got) {
        case Got::OK:         s_why[0] = '\0'; return;
        case Got::NO_CONNECT: snprintf(s_why, sizeof s_why, s_noName ? "name lookup failed" : "no connection"); return;
        case Got::NO_ANSWER:  snprintf(s_why, sizeof s_why, r.code ? "reply cut short" : "no reply"); return;
        default:
            if (r.code) snprintf(s_why, sizeof s_why, "HTTP %d", r.code);
            else        snprintf(s_why, sizeof s_why, "not a web reply");
            return;
    }
}

// Connects, asks for `file` and reads the status and headers; on OK the body
// is waiting in `c`. A redirect back to the site itself is followed once.
Reply get(WiFiClient& c, const char* file, uint32_t timeoutMs) {
    parseBase();
    Reply r;
    char path[96];
    snprintf(path, sizeof path, "%s%s", s_prefix, file);
    for (int hop = 0; hop < 2; hop++) {
        c.stop();
        r = Reply();
        if (!connectSite(c, timeoutMs > 20000 ? 20000 : timeoutMs)) { noteWhy(r); return r; }
        const size_t sent = c.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: MuleSkin/%s\r\nConnection: close\r\n\r\n",
                                     path, s_host, FIRMWARE_VERSION);
        char line[128];
        const uint32_t tw = millis();
        if (!readLine(c, line, sizeof line, timeoutMs)) {
            Serial.printf("[ota] %s: asked (%u bytes sent), nothing back in %lu ms, connected %d\n", file,
                          (unsigned)sent, (unsigned long)(millis() - tw), c.connected() ? 1 : 0);
            r.got = Got::NO_ANSWER; noteWhy(r); return r;
        }
        if (strncmp(line, "HTTP/", 5) == 0) {
            const char* sp = strchr(line, ' ');
            if (sp) r.code = atoi(sp + 1);
        }
        bool html = false, chunked = false;
        char loc[96] = "";
        while (readLine(c, line, sizeof line, timeoutMs) && line[0]) {
            if (strncasecmp(line, "Content-Length:", 15) == 0)          r.length = atol(line + 15);
            else if (strncasecmp(line, "Content-Type:", 13) == 0)       html = strstr(line + 13, "html") != nullptr;
            else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0)  chunked = strstr(line + 18, "chunked") != nullptr;
            else if (strncasecmp(line, "Location:", 9) == 0) {
                const char* v = line + 9;
                while (*v == ' ') v++;
                strncpy(loc, v, sizeof loc - 1);
                loc[sizeof loc - 1] = '\0';
            }
        }
        const int code = r.code;
        if (code == 0)            r.got = Got::BLOCKED;      // something answered, but not a web server
        else if (code == 200)     r.got = html ? Got::SIGN_IN : (chunked ? Got::BLOCKED : Got::OK);
        else if (code >= 300 && code < 400 && loc[0]) {
            // Back to the site (www -> bare name, an old path) is followed;
            // anywhere else is a network putting its own page in the way.
            const char* p = loc;
            bool https = false;
            if (strncasecmp(p, "https://", 8) == 0)     { https = true; p += 8; }
            else if (strncasecmp(p, "http://", 7) == 0) { p += 7; }
            else if (*p == '/')                         { strncpy(path, p, sizeof path - 1); path[sizeof path - 1] = '\0'; continue; }
            const size_t hn = strcspn(p, "/:");
            if (!ourHost(p, hn))  r.got = Got::SIGN_IN;
            else if (https)       r.got = Got::HTTPS_ONLY;
            else {
                const char* rest = strchr(p, '/');
                strncpy(path, rest ? rest : "/", sizeof path - 1);
                path[sizeof path - 1] = '\0';
                Serial.printf("[ota] HTTP %d, following to %s\n", code, path);
                continue;
            }
        }
        else if (code == 401 || code == 407 || code == 511 || (code >= 300 && code < 400)) r.got = Got::SIGN_IN;
        else if (code == 404)     r.got = Got::MISSING;
        else if (code >= 500)     r.got = Got::SITE_TROUBLE;
        else                      r.got = Got::BLOCKED;
        if (r.got != Got::OK) Serial.printf("[ota] %s: HTTP %d%s%s%s\n", file, code, html ? ", a web page" : "",
                                            loc[0] ? ", to " : "", loc);
        noteWhy(r);
        return r;
    }
    r.got = Got::BLOCKED;    // redirected round in a circle
    noteWhy(r);
    return r;
}

// get(), knocking again when the trouble might pass: no connection, no reply,
// or the site's own server error. Not a sign-in page or a refusal -- those
// will say the same thing a second later.
Reply getTries(WiFiClient& c, const char* file, uint32_t timeoutMs, uint8_t tries) {
    Reply r;
    for (uint8_t i = 0; i < tries; i++) {
        r = get(c, file, timeoutMs);
        const bool again = r.got == Got::NO_CONNECT || r.got == Got::NO_ANSWER || r.got == Got::SITE_TROUBLE;
        if (!again || s_cancel || i + 1 == tries) break;
        Serial.printf("[ota] %s: %s, trying again\n", file, s_why);
        c.stop();
        delay(2000u * (i + 1));
    }
    return r;
}

Fail failFor(Got g) {
    switch (g) {
        case Got::SIGN_IN:      return Fail::SIGN_IN;
        case Got::HTTPS_ONLY:   return Fail::HTTPS_ONLY;
        case Got::BLOCKED:      return Fail::NET_BLOCKED;
        case Got::NO_ANSWER:    return Fail::SITE_SLOW;
        case Got::SITE_TROUBLE: return Fail::SITE_DOWN;
        case Got::MISSING:      return Fail::NOT_SIGNED;
        default:                return Fail::NO_SITE;
    }
}

// The body of a reply already OK'd by get(), into `out` (at most `cap`):
// until Content-Length, the connection closing, or `stallMs` without a byte.
size_t readBody(WiFiClient& c, const Reply& r, uint8_t* out, size_t cap, uint32_t stallMs) {
    size_t len = 0;
    uint32_t t0 = millis();
    while (len < cap && (r.length < 0 || (int32_t)len < r.length) && millis() - t0 < stallMs) {
        const int a = c.available();
        if (a > 0) {
            const int n = c.read(out + len, (size_t)a < cap - len ? (size_t)a : cap - len);
            if (n > 0) { len += (size_t)n; t0 = millis(); }
        } else if (!c.connected()) {
            break;
        } else {
            delay(5);
        }
    }
    return len;
}

// A small file into `out`; OK only when all of it arrived.
Reply getSmall(const char* file, uint8_t* out, size_t cap, size_t& len) {
    WiFiClient& c = *client();
    Reply r = getTries(c, file, STALL_TIMEOUT_MS, 3);
    len = r.got == Got::OK ? readBody(c, r, out, cap, STALL_TIMEOUT_MS) : 0;
    if (r.got == Got::OK && r.length >= 0 && (int32_t)len < r.length && len < cap) { r.got = Got::NO_ANSWER; noteWhy(r); }
    c.stop();
    return r;
}

// Pulls "version": "1.7.2" out of a flasher manifest without a JSON library.
bool parseVersion(const char* body, char* out, size_t cap) {
    const char* k = strstr(body, "\"version\"");
    if (!k) return false;
    const char* q = strchr(k + 9, '"');
    if (!q) return false;
    const char* e = strchr(q + 1, '"');
    if (!e || e == q + 1) return false;
    size_t n = (size_t)(e - (q + 1));
    const bool addV = q[1] != 'v';
    if (n + (addV ? 1 : 0) >= cap) return false;
    size_t o = 0;
    if (addV) out[o++] = 'v';
    memcpy(out + o, q + 1, n);
    out[o + n] = '\0';
    return true;
}

// The two fields the flasher's manifest carries for the board itself, both
// optional: "release_name", and "whats_new" as an array of short strings.
// Everything else in that file is for the browser flasher, which ignores
// what it does not know, exactly as this does.
// One JSON string, from its opening quote. Escapes are read, not copied: the
// workflow writes these with json.dump, which turns a quote into \" and
// anything past ASCII into \uXXXX -- taken raw, a quote ended the line early
// and scrambled every line after it. The board's fonts are ASCII, so an
// escaped character past it comes out as '?'. Copies what fits into `out`
// and returns the character after the closing quote, or nullptr.
static const char* jsonString(const char* q, char* out, size_t cap) {
    if (!q || *q != '"' || !cap) return nullptr;
    size_t o = 0;
    for (const char* p = q + 1; *p; p++) {
        char c = *p;
        if (c == '"') { out[o] = '\0'; return p + 1; }
        if (c == '\\') {
            const char e = *++p;
            if (!e) break;
            if (e == 'u') {
                int v = 0, d = 0;
                for (; d < 4 && isxdigit((unsigned char)p[1]); d++, p++)
                    v = v * 16 + (isdigit((unsigned char)p[1]) ? p[1] - '0' : (tolower((unsigned char)p[1]) - 'a' + 10));
                c = (d == 4 && v >= 0x20 && v < 0x7F) ? (char)v : '?';
            } else {
                c = (e == 'n' || e == 't' || e == 'r') ? ' ' : e;   // \" \\ \/ are the character itself
            }
        }
        if (o + 1 < cap) out[o++] = c;
    }
    out[o] = '\0';
    return nullptr;
}

void parseRelease(const char* body) {
    char name[20] = "";
    if (const char* k = strstr(body, "\"release_name\"")) {
        const char* p = k + 14;
        while (*p == ' ' || *p == ':') p++;
        jsonString(p, name, sizeof name);
    }
    char lines[OtaCore::NEWS_MAX][40];
    const char* ptr[OtaCore::NEWS_MAX];
    uint8_t n = 0;
    if (const char* a = strstr(body, "\"whats_new\"")) {
        const char* p = strchr(a + 11, '[');
        // String by string, not "everything before the first ]": a bracket
        // inside a line of the notes is part of the line.
        while (p && n < OtaCore::NEWS_MAX) {
            p++;
            while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
            if (*p != '"') break;
            p = jsonString(p, lines[n], sizeof lines[0]);
            ptr[n] = lines[n];
            n++;
            if (p) p--;                   // the loop steps over it
        }
    }
    if (name[0] || n) OtaCore::noteRelease(name, ptr, n);
}

// joinedBssid(): written by whichever join just succeeded.
static uint8_t  s_joinedBssid[6];
static int8_t   s_joinedIdx = -1;
static volatile uint32_t s_joinedSeq = 0;
static void noteJoined(int8_t i) {
    const uint8_t* b = WiFi.BSSID();
    if (i < 0 || !b) return;
    memcpy(s_joinedBssid, b, 6);
    s_joinedIdx = i;
    s_joinedSeq = s_joinedSeq + 1;
}

bool join() {
    s_state = State::CONNECTING;
    Serial.printf("[ota] joining %s\n", s_ssid);
    WiFi.begin(s_ssid, s_pass[0] ? s_pass : nullptr);
    const uint32_t t0 = millis();
    wl_status_t st = WiFi.status();
    while (st != WL_CONNECTED && millis() - t0 < JOIN_TIMEOUT_MS && !s_cancel) {
        if (st == WL_CONNECT_FAILED) break;
        delay(100);
        st = WiFi.status();
    }
    if (s_cancel) return false;
    if (st != WL_CONNECTED) {
        setResult(savedIndexOf(s_ssid), st == WL_NO_SSID_AVAIL ? SavedResult::NOT_FOUND : SavedResult::BAD_PASSWORD);
        fail(st == WL_NO_SSID_AVAIL ? Fail::WIFI_NOT_FOUND : Fail::WIFI_PASSWORD);
        return false;
    }
    Serial.printf("[ota] joined %s as %s\n", s_ssid, WiFi.localIP().toString().c_str());
    // The clock rides along: one NTP round trip while the radio is up anyway.
    Clock::syncWait(1500);
    if (s_save && !saveNetwork(s_ssid, s_pass))
        Serial.println("[ota] wifi: the network list is full, not saved");
    setResult(savedIndexOf(s_ssid), SavedResult::JOINED);
    noteJoined(savedIndexOf(s_ssid));
    return true;
}

bool check() {
    s_state = State::CHECKING;
    Serial.printf("[ota] checking %s, largest block %lu\n", OTA_WIFI_BASE,
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    uint8_t body[1024];
    size_t  len = 0;
    Reply r = getSmall(manifestName().c_str(), body, sizeof body - 1, len);
    if (r.got != Got::OK) { fail(failFor(r.got)); return false; }
    body[len] = '\0';
    if (!parseVersion((const char*)body, s_latest, sizeof s_latest)) {
        snprintf(s_why, sizeof s_why, "unreadable manifest");
        fail(Fail::NET_BLOCKED);
        return false;
    }

    r = getSmall(firmwareName("sig").c_str(), s_sig, sizeof s_sig, len);
    if (r.got != Got::OK) { fail(failFor(r.got)); return false; }
    if (len < 8 || len >= sizeof s_sig) {
        Serial.printf("[ota] signature: %u bytes\n", (unsigned)len);
        fail(Fail::NOT_SIGNED);
        return false;
    }
    s_sigLen = (uint8_t)len;
    Serial.printf("[ota] latest is %s, running %s\n", s_latest, FIRMWARE_VERSION);
    return true;
}

void download() {
    s_state = State::DOWNLOADING;
    s_downloadStarted = true;
    s_rx = 0;
    WiFiClient& c = *client();
        Serial.printf("[ota] heap before GET: %lu free, largest %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    const Reply rp = getTries(c, firmwareName("bin").c_str(), STALL_TIMEOUT_MS, 3);
    const int size = (int)rp.length;
        Serial.printf("[ota] heap after GET: %lu free, largest %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (rp.got != Got::OK || size <= 0) {
        Serial.printf("[ota] firmware: HTTP %d, size %d\n", rp.code, size);
        c.stop();
        if (rp.got == Got::OK) snprintf(s_why, sizeof s_why, "no size given");
        fail(rp.got == Got::OK ? Fail::NET_BLOCKED : failFor(rp.got));
        return;
    }
    Fail f = OtaCore::begin((uint32_t)size, s_sig, s_sigLen);
    if (f != Fail::NONE) { c.stop(); fail(f); return; }
    s_size = (uint32_t)size;
        Serial.printf("[ota] heap after ota begin: %lu free, largest %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) {
        Serial.printf("[ota] no room for the download buffer: heap %lu largest %lu\n",
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        c.stop(); OtaCore::abort(); fail(Fail::LOW_MEMORY); return;
    }
    WiFiClient* s = &c;
    uint32_t last = millis();
    // Every 256 KB, and at the end: how far, how fast, and how much room is
    // left. "The download stopped" on its own said nothing about which of
    // the two exits below took it, or how far it had got.
    const uint32_t t0 = millis();
    uint32_t nextMark = 262144;
    const char* why = "complete";
    while (s_rx < s_size && !s_cancel && s_state == State::DOWNLOADING) {
        const int a = s->available();
        if (a <= 0) {
            if (!c.connected())                    { why = "connection closed"; break; }
            if (millis() - last > STALL_TIMEOUT_MS) { why = "no data for 15 s";  break; }
            delay(5);
            continue;
        }
        size_t want = (size_t)a;
        if (want > 4096) want = 4096;
        if (want > s_size - s_rx) want = s_size - s_rx;
        const int r = s->read(buf, want);
        if (r <= 0) continue;
        if (!OtaCore::write(buf, (size_t)r)) {
            fail(s_rx == 0 ? Fail::NOT_FIRMWARE : Fail::WRITE_ERROR);
            break;
        }
        s_rx += (uint32_t)r;
        last = millis();
        if (s_rx >= nextMark) {
            nextMark += 262144;
            const uint32_t el = millis() - t0;
            Serial.printf("[ota] rx %lu/%lu  %lu KB/s  heap %lu largest %lu\n", (unsigned long)s_rx,
                          (unsigned long)s_size, (unsigned long)(el ? s_rx / el : 0),
                          (unsigned long)ESP.getFreeHeap(),
                          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
    }
    if (s_rx < s_size) {
        const uint32_t el = millis() - t0;
        Serial.printf("[ota] download ended: %s at %lu/%lu after %lu ms (last data %lu ms ago), "
                      "connected %d, heap %lu largest %lu\n", why, (unsigned long)s_rx, (unsigned long)s_size,
                      (unsigned long)el, (unsigned long)(millis() - last), c.connected() ? 1 : 0,
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    free(buf);
    c.stop();

    if (s_cancel || s_state == State::FAILED) { OtaCore::abort(); return; }
    if (s_rx != s_size) { OtaCore::abort(); snprintf(s_why, sizeof s_why, "%s", why); fail(Fail::TIMEOUT); return; }

    s_state = State::VERIFYING;
    f = OtaCore::finish();
    if (f != Fail::NONE) { fail(f); return; }
    s_state = State::DONE;
    OtaCore::restartSoon(4000);
}

void run(void*) {
    s_why[0] = '\0';
    s_noName = false;
    WiFi.scanDelete();
    Clock::syncStop();
    WiFi.disconnect(false, false);
    delay(100);
    // Bench diagnosis: a drop mid-update names its reason (200 beacon
    // timeout, 2 auth expired, 201 no AP found, 15 handshake).
    const wifi_event_id_t dropEv = WiFi.onEvent(
        [](arduino_event_id_t, arduino_event_info_t info) {
            Serial.printf("[ota] WiFi dropped mid-update, reason %u\n", (unsigned)info.wifi_sta_disconnected.reason);
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (join()) {
        // The password has done its job; it only lives on in NVS if saved.
        memset(s_pass, 0, sizeof s_pass);
        if (check()) {
            s_state = State::READY;
            while (!s_install && !s_cancel) delay(50);
            if (!s_cancel) download();
        }
    }
    memset(s_pass, 0, sizeof s_pass);
    // Same reason as fail()'s: a cancel that arrived mid-request leaves this
    // task holding a state nobody is watching any more. DONE is left alone --
    // that install happened, and the board is already on its way to a restart.
    if (s_cancel && s_state != State::DONE) s_state = State::OFF;
    // Hand the radio back the way it was found. Detection sniffs WiFi by
    // hopping channels, which an association would fight over, and the board
    // no longer restarts when update mode ends -- so the association goes
    // rather than the board.
    WiFi.removeEvent(dropEv);
    WiFi.disconnect(false, false);
    Serial.printf("[ota] update task stack: %u of %u bytes never used\n",
                  (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), (unsigned)TASK_STACK);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

uint32_t joinedBssid(uint8_t out[6], int8_t* savedIdx) {
    const uint32_t q = s_joinedSeq;
    if (q) {
        if (out) memcpy(out, s_joinedBssid, 6);
        if (savedIdx) *savedIdx = s_joinedIdx;
    }
    return q;
}

int8_t bestSavedInScan();   // below, with the rest of the list

bool begin() {
    if (pushBusy()) return false;       // a PUSH join holds the radio
    if (s_state != State::OFF) return true;
    if (Security::locked() || !OtaCore::available()) return false;
    // A cancelled attempt's task can still be unwinding an HTTP request it is
    // waiting on. One at a time: it clears s_task on its way out.
    if (s_task) return false;
    if (timeSyncBusy()) return false;   // a WIFI TIME / AUTO TIME join holds the radio
    readSaved();
    s_fail = Fail::NONE;
    s_cancel = s_install = s_downloadStarted = false;
    s_rx = s_size = 0;
    WiFi.mode(WIFI_STA);
#if defined(CROWPANEL7)
    // The join and the download at full backlight browned this board out
    // into a POWER ON reset (black box, 2026-09-23): a 7" backlight plus
    // WiFi at 20 dBm plus BLE is more than a USB port reliably gives. The
    // boot check already dims before its join (main.cpp); this is the flow
    // the screen starts. 8.5 dBm is plenty for a network in the same flat.
    CrowBL::set(24);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
    // A saved network is used straight away. The list only appears when there
    // is nothing saved, or through TRY AGAIN when the saved one cannot be
    // joined -- which is also how somebody who has moved picks a new one.
    // With networks saved, the scan comes first and the best of them is
    // joined from tick(); the list only appears when none is in range.
    s_autoJoin = s_n > 0;
    startScan();
    Serial.println("[ota] wifi update mode: scanning");
    return true;
}

bool end() {
    if (s_state == State::OFF) return false;
    s_cancel = true;
    WiFi.scanDelete();
    s_state = State::OFF;
#if defined(CROWPANEL7)
    CrowBL::set(Settings::brightness());   // the dim above, undone
#endif
    Serial.println("[ota] wifi update mode off");
    return false;
}

void tick(uint32_t) {
    if (s_state != State::SCANNING) return;
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n > 0) collectScan(n);
    else       s_netN = 0;
    s_state = State::PICK;
    if (s_autoJoin) {
        s_autoJoin = false;
        const int8_t k = bestSavedInScan();
        if (k >= 0) {
            Serial.printf("[ota] wifi update mode: using saved network %s\n", s_list[k].ssid);
            connectSavedAt((uint8_t)k);
        } else {
            Serial.println("[ota] wifi update mode: no saved network in range, showing the list");
        }
    }
}

void rescan() {
    if (s_state == State::PICK || s_state == State::FAILED) startScan();
}

State      state()    { return s_state; }
uint8_t    netCount() { return s_netN; }
const Net* net(uint8_t i) { return i < s_netN ? &s_nets[i] : nullptr; }

bool hasSaved() { readSaved(); return s_n > 0; }

void forget() {
    Preferences p;
    if (p.begin(NVS_NS, false)) { p.clear(); p.end(); }
    s_n = 0; s_use = 0;
    s_savedRead = true;
}

uint8_t     savedCount()          { readSaved(); return s_n; }
const char* savedSsidAt(uint8_t i){ readSaved(); return i < s_n ? s_list[i].ssid : ""; }
uint8_t     savedUse()            { readSaved(); return s_use; }
SavedResult savedResult(uint8_t i){ readSaved(); return i < s_n ? s_list[i].result : SavedResult::UNTRIED; }
int8_t savedIndexOf(const char* ssid) {
    readSaved();
    if (!ssid || !ssid[0]) return -1;
    for (uint8_t i = 0; i < s_n; i++) if (!strcmp(s_list[i].ssid, ssid)) return (int8_t)i;
    return -1;
}

bool saveNetwork(const char* ssid, const char* pass) {
    readSaved();
    if (!ssid || !ssid[0]) return false;
    int8_t k = savedIndexOf(ssid);
    if (k < 0) {
        if (s_n >= SAVED_MAX) return false;
        k = (int8_t)s_n++;
        strncpy(s_list[k].ssid, ssid, 32);
        s_list[k].ssid[32] = '\0';
        if (s_n == 1) s_use = 0;
    }
    s_list[k].result = SavedResult::UNTRIED;
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        char key_[4];
        key(key_, "p", (uint8_t)k);
        p.putString(key_, pass ? pass : "");
        p.end();
    }
    writeList();
    Serial.printf("[ota] wifi: saved %s (%u of %u)\n", ssid, (unsigned)s_n, (unsigned)SAVED_MAX);
    return true;
}

void removeSaved(uint8_t i) {
    readSaved();
    if (i >= s_n) return;
    // Passwords move down with their names; the last slot's is dropped.
    Preferences p;
    const bool open = p.begin(NVS_NS, false);
    for (uint8_t j = i; j + 1 < s_n; j++) {
        s_list[j] = s_list[j + 1];
        if (open) {
            char from[4], to[4];
            key(from, "p", (uint8_t)(j + 1));
            key(to,   "p", j);
            p.putString(to, p.getString(from, ""));
        }
    }
    if (open) {
        char k[4];
        key(k, "p", (uint8_t)(s_n - 1)); p.remove(k);
        key(k, "s", (uint8_t)(s_n - 1)); p.remove(k);
        key(k, "r", (uint8_t)(s_n - 1)); p.remove(k);
        p.end();
    }
    s_n--;
    if (s_use == i) s_use = 0;
    else if (s_use > i) s_use--;
    writeList();
}

void printSaved() {
    readSaved();
    static const char* const R[] = { "not tried", "joined", "wrong password", "not found" };
    Serial.printf("[wifi] %u saved, USE is %u\n", (unsigned)s_n, (unsigned)s_use);
    for (uint8_t i = 0; i < s_n; i++)
        Serial.printf("[wifi]   %u: %s -- %s\n", (unsigned)i, s_list[i].ssid, R[(uint8_t)s_list[i].result & 3]);
}

void useSaved(uint8_t i) {
    readSaved();
    if (i >= s_n) return;
    s_use = i;
    writeList();
}

// The best saved network in the last scan: USE when it is there, else the
// strongest. -1 when none of them is.
int8_t bestSavedInScan() {
    readSaved();
    int8_t best = -1, bestRssi = -127;
    for (uint8_t j = 0; j < s_netN; j++) {
        const int8_t k = savedIndexOf(s_nets[j].ssid);
        if (k < 0) continue;
        if (k == (int8_t)s_use) return k;
        if (s_nets[j].rssi > bestRssi) { bestRssi = s_nets[j].rssi; best = k; }
    }
    return best;
}

void connect(const char* ssid, const char* pass, bool save) {
    if (s_task || (s_state != State::PICK && s_state != State::FAILED)) return;
    strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    strncpy(s_pass, pass ? pass : "", sizeof s_pass - 1);
    s_pass[sizeof s_pass - 1] = '\0';
    s_save    = save;
    s_fail    = Fail::NONE;
    s_cancel  = s_install = s_downloadStarted = false;
    s_state   = State::CONNECTING;
    if (xTaskCreatePinnedToCore(run, "otawifi", TASK_STACK, nullptr, 1, &s_task, 1) != pdPASS) {
        s_task = nullptr;
        memset(s_pass, 0, sizeof s_pass);
        Serial.printf("[ota] no room for the update task: %u wanted, heap %lu largest %lu\n", (unsigned)TASK_STACK,
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        fail(Fail::LOW_MEMORY);
    }
}

// ---- Extra detection rules (signatures.cpp setExtraRules()) -----------------
// manifest-signatures.json on the site: {"serial": N, "body": "<rules>",
// "sig": "<DER hex>"}, the body signed like firmware under the name
// "signatures" (build_flasher_bins.sh). The boot check fetches it; a newer
// serial whose signature checks out replaces the stored set in NVS ("sigx"),
// which loadExtraRules() puts back at every boot. A file that fails the
// signature is ignored and the stored set stays.
namespace {
const char* SIGX_NS = "sigx";
const size_t SIGX_BODY_MAX = 2048;

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void fetchExtraRules(uint32_t budgetMs) {
    // 5 KB, held only for the few hundred ms this takes, at boot before the
    // frame buffer exists: as statics they cost every board 5 KB of RAM for
    // the whole run. Freed on every way out by the guard.
    const size_t JSON_CAP = 3072;
    struct Bufs {
        char* json = (char*)malloc(3072);
        char* body = (char*)malloc(SIGX_BODY_MAX);
        ~Bufs() { free(json); free(body); }
    } bufs;
    if (!bufs.json || !bufs.body) { Serial.println("[rules] no memory to check"); return; }
    char* const json = bufs.json;
    char* const body = bufs.body;
    char sigHex[200];
    uint8_t sig[100];
    WiFiClient plain;
    const Reply rr = get(plain, "manifest-signatures.json", budgetMs > 60000 ? 60000 : budgetMs);
    const size_t len = rr.got == Got::OK ? readBody(plain, rr, (uint8_t*)json, JSON_CAP - 1, budgetMs) : 0;
    plain.stop();
    json[len] = '\0';
    if (rr.got != Got::OK) { Serial.printf("[rules] none from the site (%s)\n", s_why); return; }

    const char* k = strstr(json, "\"serial\"");
    if (!k) return;
    k = strchr(k, ':');
    const uint32_t serial = k ? (uint32_t)strtoul(k + 1, nullptr, 10) : 0;
    Preferences p;
    p.begin(SIGX_NS, true);
    const uint32_t have = p.getUInt("serial", 0);
    p.end();
    if (serial <= have) { Serial.printf("[rules] up to date (set %lu)\n", (unsigned long)have); return; }

    const char* b = strstr(json, "\"body\"");
    const char* g = strstr(json, "\"sig\"");
    if (!b || !g) return;
    b = strchr(b + 6, '"');
    g = strchr(g + 5, '"');
    if (!b || !g || !jsonString(b, body, SIGX_BODY_MAX) || !jsonString(g, sigHex, sizeof sigHex)) return;
    size_t sigLen = 0;
    for (const char* h = sigHex; h[0] && h[1] && sigLen < sizeof sig; h += 2) {
        const int hi = hexVal(h[0]), lo = hexVal(h[1]);
        if (hi < 0 || lo < 0) return;
        sig[sigLen++] = (uint8_t)(hi * 16 + lo);
    }
    if (!OtaCore::verifySigned("signatures", (const uint8_t*)body, strlen(body), sig, sigLen)) {
        Serial.printf("[rules] set %lu is NOT signed with our key -- ignored\n", (unsigned long)serial);
        return;
    }
    const uint16_t n = setExtraRules(body);
    p.begin(SIGX_NS, false);
    p.putString("body", body);
    p.putUInt("serial", serial);
    p.end();
    Serial.printf("[rules] set %lu: %u extra rules, signature good\n", (unsigned long)serial, (unsigned)n);
}
}  // namespace

void loadExtraRules() {
    Preferences p;
    if (!p.begin(SIGX_NS, true)) return;
    const uint32_t serial = p.getUInt("serial", 0);
    String body = p.getString("body", "");
    p.end();
    if (!serial) return;
    const uint16_t n = setExtraRules(body.c_str());
    Serial.printf("[rules] set %lu: %u extra rules loaded\n", (unsigned long)serial, (unsigned)n);
}

bool bootCheck(uint32_t budgetMs) {
    readSaved();
    if (!s_n) return false;
    const uint32_t t0 = millis();
    WiFi.mode(WIFI_STA);
#if defined(CROWPANEL7)
    WiFi.setTxPower(WIFI_POWER_8_5dBm);   // see begin(); the backlight is already down here
#endif
    // More than one network saved: a quick scan says which are here, and the
    // one marked USE wins when it is, else the strongest of the rest. One
    // network: join it blind, as before, and keep the scan's two seconds.
    uint8_t pick = s_use;
    if (s_n > 1) {
        // PASSIVE, 130 ms a channel: listen for beacons rather than ask.
        // The active scan asked for 110 ms a channel and took 5.6 seconds on
        // the soak board, twice in a row, so it was not a warm-up cost; it
        // was most of a boot check that ran to thirteen. A passive scan
        // keeps to the time it is given -- 1.5 s at 110 ms, measured -- and
        // an access point beacons about every 102 ms, so 130 ms on each
        // channel hears every network in range. Hidden networks were never
        // shown by this scan anyway.
        const uint32_t ts = millis();
        const int found = WiFi.scanNetworks(false, false, true, 130);
        int8_t best = -1, bestRssi = -127;
        bool   seen[SAVED_MAX] = { false, false, false, false, false, false };
        uint8_t inRange = 0;
        for (int j = 0; j < found; j++) {
            const int8_t k = savedIndexOf(WiFi.SSID(j).c_str());
            if (k < 0) continue;
            if (!seen[k]) inRange++;
            seen[k] = true;
            if (k == (int8_t)s_use) { best = k; bestRssi = 127; }
            else if (WiFi.RSSI(j) > bestRssi) { bestRssi = (int8_t)WiFi.RSSI(j); best = k; }
        }
        WiFi.scanDelete();
        Serial.printf("[ota] boot check: scan %lu ms, %d network(s), %u of %u saved in range\n",
                      (unsigned long)(millis() - ts), found, (unsigned)inRange, (unsigned)s_n);
        for (uint8_t i = 0; i < s_n; i++) if (!seen[i]) setResult((int8_t)i, SavedResult::NOT_FOUND);
        if (best < 0) {
            // The radio off on the way out, as the end of this does: the
            // station mode switched on above held the WiFi driver's heap
            // through the whole of the boot that followed.
            WiFi.mode(WIFI_OFF);
            return false;
        }
        pick = (uint8_t)best;
    }
    char pass[65] = "";
    passAt(pick, pass, sizeof pass);
    const char* ssid = s_list[pick].ssid;
    Serial.printf("[ota] boot check: joining %s (heap %lu, largest %lu)\n", ssid,
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    // Why the access point let go, if it did. The status alone says only
    // "disconnected"; the driver's reason code says whether that was the
    // password (15, a handshake that never finished), the signal (2, auth
    // expired), or the network vanishing (201).
    static volatile uint8_t s_dropReason;
    s_dropReason = 0;
    const wifi_event_id_t dropEv = WiFi.onEvent(
        [](arduino_event_id_t, arduino_event_info_t info) {
            s_dropReason = info.wifi_sta_disconnected.reason;
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    // The budget starts HERE, after the scan, not at t0. It used to be one
    // clock for the lot, and on the soak board the scan alone took 5.7 of
    // the 6 seconds the join was allowed: the join got 0.3 s, gave up
    // before the router had answered, and a network three access points
    // strong went unjoined boot after boot. A board with one saved network
    // skips the scan, which is why it never showed there.
    uint32_t tj = millis();
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    wl_status_t st = WiFi.status();
    // Two thirds of the budget for the join, the rest for the fetch.
    while (st != WL_CONNECTED && millis() - tj < budgetMs * 2 / 3) {
        if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
        delay(50);
        st = WiFi.status();
    }
    // One more go when the join simply never got anywhere: still idle or
    // disconnected, and the access point never said why (no drop reason).
    // Seen 2026-10-10 -- status 0 after the whole 6 s, then 0.9 s on the
    // very next boot -- and a missed boot check skips the update notice, the
    // clock and the rule set until AUTO TIME's daily join. A wrong password
    // or a missing network says so and is not retried. The retry gets half
    // the first try's window, and the fetch's clock starts again with it.
    bool retried = false;
    if (st != WL_CONNECTED && st != WL_CONNECT_FAILED && st != WL_NO_SSID_AVAIL && !s_dropReason) {
        retried = true;
        Serial.printf("[ota] boot check: join stuck (status %d) after %lu ms; once more\n",
                      (int)st, (unsigned long)(millis() - tj));
        WiFi.disconnect(false, false);
        delay(100);
        tj = millis();
        WiFi.begin(ssid, pass[0] ? pass : nullptr);
        st = WiFi.status();
        while (st != WL_CONNECTED && millis() - tj < budgetMs / 3) {
            if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
            delay(50);
            st = WiFi.status();
        }
    }
    WiFi.removeEvent(dropEv);
    if (retried) {
        Preferences jp;
        if (jp.begin(NVS_NS, false)) {
            jp.putUShort("jStuck", (uint16_t)(jp.getUShort("jStuck", 0) + 1));
            if (st == WL_CONNECTED) jp.putUShort("jSaved", (uint16_t)(jp.getUShort("jSaved", 0) + 1));
            jp.end();
        }
    }
    if (st != WL_CONNECTED)
        Serial.printf("[ota] boot check: join gave up after %lu ms, status %d, last drop reason %u\n",
                      (unsigned long)(millis() - tj), (int)st, (unsigned)s_dropReason);
    memset(pass, 0, sizeof pass);
    // How it went, for the WIFI NETWORKS screen. A join that merely ran out
    // of time says nothing about the password, so it changes nothing.
    if (st == WL_CONNECTED)           { setResult((int8_t)pick, SavedResult::JOINED); noteJoined((int8_t)pick); }
    else if (st == WL_NO_SSID_AVAIL)  setResult((int8_t)pick, SavedResult::NOT_FOUND);
    else if (st == WL_CONNECT_FAILED) setResult((int8_t)pick, SavedResult::BAD_PASSWORD);
    bool found = false;
    if (st == WL_CONNECTED) {
        Serial.printf("[ota] boot check: joined in %lu ms\n", (unsigned long)(millis() - t0));
        uint8_t body[1024];
        size_t  len = 0;
        // From the join, like the join's own wait, and never below a second:
        // this was an unsigned subtraction, and a join that ran past the
        // whole budget would have wrapped it round to about fifty days.
        const uint32_t usedJ = millis() - tj;
        const uint32_t left  = usedJ + 1000 < budgetMs ? budgetMs - usedJ : 1000;
        // Plain HTTP, on purpose. A TLS handshake wants 40 KB in one piece
        // and five to ten seconds, and one that timed out left a dead
        // connection in the middle of the heap that cost the frame buffer
        // its block -- measured, twice. The site answers the manifest over
        // plain HTTP, and nothing rides on this answer but a notice: the
        // install itself goes over HTTPS and checks the signature.
        // One try only: the boot is waiting, and the update screen knocks
        // again with words.
        WiFiClient plain;
        {
            const Reply rp = get(plain, manifestName().c_str(), left > 60000 ? 60000 : left);
            if (rp.got == Got::OK) {
                const uint32_t t1 = millis();
                len = readBody(plain, rp, body, sizeof body - 1, left);
                Serial.printf("[ota] boot check: manifest %u bytes in %lu ms\n", (unsigned)len, (unsigned long)(millis() - t1));
                body[len] = '\0';
                char latest[16];
                if (parseVersion((const char*)body, latest, sizeof latest)) {
                    Serial.printf("[ota] boot check: site has %s, running %s\n", latest, FIRMWARE_VERSION);
                    OtaCore::noteAvailable(latest);
                    // Only once it is known to be newer: noteAvailable drops
                    // anything that is not, and notes without an update are
                    // notes about the version already running.
                    if (OtaCore::availableVersion()[0]) parseRelease((const char*)body);
                    found = true;
                }
            } else {
                Serial.printf("[ota] boot check: manifest: %s\n", s_why);
            }
            plain.stop();
        }
        // The extra detection rules, while the radio is up anyway: one small
        // GET, and only a newer, correctly signed set changes anything.
        fetchExtraRules(2500);
        // The clock, asked AFTER the manifest rather than alongside it. The
        // two used to overlap to save a moment, and on two boots in three
        // the manifest's GET then took 2.8-3.0 s instead of 70 ms: two name
        // lookups at once, one of them waiting out a retry. Asked one after
        // the other, the clock costs its own couple of hundred ms and the
        // GET costs what it should.
        Clock::syncStart();
    } else {
        Serial.printf("[ota] boot check: no join (%d) in %lu ms\n", (int)st, (unsigned long)(millis() - t0));
    }
    if (st == WL_CONNECTED) {
        // A time server answers in well under a second; this is the cap on
        // a bad day, not the usual cost. Skipped once the clock is fresh.
        const uint32_t t2 = millis();
        const uint32_t used = t2 - tj;
        uint32_t left = used < budgetMs ? budgetMs - used : 0;
        if (left > 2500) left = 2500;
        const bool ok = Clock::syncWait(left);
        char clk[24];
        Clock::formatClock(clk, sizeof clk);
        Serial.printf("[clock] %s in %lu ms (%s)\n", ok ? "set" : "no answer",
                      (unsigned long)(millis() - t2), clk);
    }
    Clock::syncStop();
    // Everything back the way it was: the driver torn down, so Bluetooth
    // starts into the heap it always had.
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    Serial.printf("[ota] boot check done in %lu ms (heap %lu, largest %lu)\n", (unsigned long)(millis() - t0),
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return found;
}

void connectSavedAt(uint8_t i) {
    readSaved();
    if (i >= s_n) return;
    char pass[65] = "";
    passAt(i, pass, sizeof pass);
    connect(s_list[i].ssid, pass, false);
    memset(pass, 0, sizeof pass);
}

void connectSaved() {
    readSaved();
    if (!s_n) return;
    const int8_t k = bestSavedInScan();
    connectSavedAt(k >= 0 ? (uint8_t)k : s_use);
}

// ---- WIFI TIME ---------------------------------------------------------------
namespace {
volatile TimeSync s_ts     = TimeSync::IDLE;
TaskHandle_t      s_tsTask = nullptr;
char              s_tsSsid[33] = "";

// The best saved network in range, joined: the boot check's rule -- with one
// saved, that one; with several, a quick passive scan, the one marked USE if
// it is in range, else the strongest of the rest. Its name into `ssid`; the
// pick's SavedResult recorded either way. True once connected.
// `skip`: a saved network NOT to pick -- the one that just refused -- and
// `picked`, when given, which one was tried. With `skip` set and nothing else
// of ours in range, it gives up rather than trying the refuser again.
bool joinBest(char* ssid, size_t cap, const char* tag, int8_t skip = -1, int8_t* picked = nullptr) {
    uint8_t pick = s_use;
    if (skip >= 0 && s_n < 2) return false;
    if (s_n > 1) {
        const int found = WiFi.scanNetworks(false, false, true, 130);
        int8_t best = -1, bestRssi = -127;
        for (int j = 0; j < found; j++) {
            const int8_t k = savedIndexOf(WiFi.SSID(j).c_str());
            if (k < 0 || k == skip) continue;
            if (k == (int8_t)s_use) { best = k; bestRssi = 127; }
            else if (WiFi.RSSI(j) > bestRssi) { bestRssi = (int8_t)WiFi.RSSI(j); best = k; }
        }
        WiFi.scanDelete();
        if (best >= 0) pick = (uint8_t)best;
        else if (skip >= 0) return false;
    }
    if (picked) *picked = (int8_t)pick;
    strncpy(ssid, s_list[pick].ssid, cap - 1);
    ssid[cap - 1] = '\0';
    char pass[65] = "";
    passAt(pick, pass, sizeof pass);
    Serial.printf("[%s] joining %s\n", tag, ssid);
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    memset(pass, 0, sizeof pass);
    const uint32_t tj = millis();
    wl_status_t st = WiFi.status();
    while (st != WL_CONNECTED && millis() - tj < JOIN_TIMEOUT_MS) {
        if (st == WL_CONNECT_FAILED) break;
        delay(100);
        st = WiFi.status();
    }
    if (st != WL_CONNECTED) {
        setResult((int8_t)pick, st == WL_NO_SSID_AVAIL ? SavedResult::NOT_FOUND : SavedResult::BAD_PASSWORD);
        return false;
    }
    setResult((int8_t)pick, SavedResult::JOINED);
    noteJoined((int8_t)pick);
    return true;
}

void pollCommands();   // below: IGNORE from a notification

void tsRun(void*) {
    const uint32_t t0 = millis();
    if (!joinBest(s_tsSsid, sizeof s_tsSsid, "time")) {
        s_ts = TimeSync::NO_JOIN;
    } else {
        s_ts = TimeSync::ASKING;
        Clock::syncStart();
        s_ts = Clock::syncWait(6000) ? TimeSync::DONE : TimeSync::NO_ANSWER;
        // While joined anyway: is there a newer release? The boot check asks
        // only at boot, so a board that runs for days would never hear --
        // this is how the daily AUTO TIME join keeps it current (and what
        // AUTO UPDATE acts on). One small GET; the buffer lives only here.
        if (Settings::updateCheck()) {
            uint8_t* body = (uint8_t*)malloc(1024);
            size_t len = 0;
            if (body && getSmall(manifestName().c_str(), body, 1023, len).got == Got::OK) {
                body[len] = '\0';
                char latest[16];
                if (parseVersion((const char*)body, latest, sizeof latest)) {
                    OtaCore::noteAvailable(latest);
                    if (OtaCore::availableVersion()[0]) parseRelease((const char*)body);
                    Serial.printf("[time] release check: site has v%s\n", latest);
                }
            }
            free(body);
        }
        pollCommands();
    }
    Clock::syncStop();
    // Off the network, but the station mode stays up: detection sniffs on it.
    WiFi.disconnect(false, false);
    char clk[24];
    Clock::formatClock(clk, sizeof clk);
    Serial.printf("[time] %s in %lu ms (%s)\n",
                  s_ts == TimeSync::DONE ? "set" : s_ts == TimeSync::NO_JOIN ? "no join" : "no answer",
                  (unsigned long)(millis() - t0), clk);
    s_tsTask = nullptr;
    vTaskDelete(nullptr);
}
}  // namespace

// ---- IGNORE from a notification --------------------------------------------------
namespace {
const uint8_t     IGN_MAX = 8;
uint8_t           s_ign[IGN_MAX][9];       // mac, type, kind, mine -- waiting for the main loop
char              s_boardId[8] = "";       // "5AF6": main.cpp's setBoardId()
volatile int      s_cmdCode = 0;
volatile uint8_t  s_ignHead = 0, s_ignTail = 0;

int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "IGNORE aabbccddeeff 12" or "SNOOZE aabbccddeeff 12" -> queued for the main loop.
void noteCommand(const char* msg) {
    static const char* const KINDS[] = { "IGNORE ", "SNOOZE ", "UNIGNORE ", "UNSNOOZE ", "PAUSE ", "RESUME ",
                                         "SYNC IGNORE ", "SYNC UNIGNORE " };
    int kind = -1;
    for (int k = 0; k < 8 && kind < 0; k++)
        if (strncasecmp(msg, KINDS[k], strlen(KINDS[k])) == 0) kind = k;
    if (kind < 0) return;
    const char* p = msg + strlen(KINDS[kind]);
    uint8_t rec[9] = { 0 };
    if (kind <= 3 || kind >= 6) {                // the device's MAC, then its type (IGNORE / SNOOZE / SYNC IGNORE)
        for (int i = 0; i < 6; i++) {
            while (*p == ':' || *p == '-') p++;
            const int a = hexNib(p[0]), b = a < 0 ? -1 : hexNib(p[1]);
            if (a < 0 || b < 0) return;
            rec[i] = (uint8_t)(a << 4 | b);
            p += 2;
        }
        while (*p == ' ') p++;
        if (kind <= 1 || kind == 6) {
            rec[6] = (uint8_t)atoi(p);
            while (*p && *p != ' ') p++;
        }
    }
    // The board it came from, last; none means a notification from before
    // there was one, and counts as this board's.
    while (*p == ' ') p++;
    char id[8] = "";
    for (int n = 0; *p && *p != ' ' && n < 7; ) id[n++] = *p++, id[n] = '\0';
    rec[7] = (uint8_t)kind;
    rec[8] = (!id[0] || !s_boardId[0] || strcasecmp(id, s_boardId) == 0) ? 1 : 0;
    const uint8_t next = (uint8_t)((s_ignHead + 1) % IGN_MAX);
    if (next == s_ignTail) return;               // full: the rest wait for the next join
    memcpy(s_ign[s_ignHead], rec, 9);
    s_ignHead = next;
    Serial.printf("[push] phone says %s%02x:%02x:%02x:%02x:%02x:%02x (%s)\n", KINDS[kind],
                  rec[0], rec[1], rec[2], rec[3], rec[4], rec[5], rec[8] ? "this board" : id);
}

// What is new on the command topic since the last one read. ntfy's poll: one
// JSON object a line, each with an "id" and a "message". Plain HTTP and the
// board's token, like the pushes; the last id read is kept in NVS (written
// only when it moves) so a restart does not replay the lot.
void pollCommands() {
    if (!Settings::pushOn() || !Settings::pushUrl()[0]) return;
    Preferences p;
    char since[24] = "";
    if (p.begin(NVS_NS, true)) { strncpy(since, p.getString("cmdSince", "").c_str(), sizeof since - 1); p.end(); }
    HTTPClient http;
    int code = 0;
    // Twice at most: a server that does not know the id kept -- another
    // server's, after PUSH URL changed, or one that has since forgotten it --
    // answers 400 "invalid since parameter". Forget it and ask for the last
    // 12 hours instead, or that board would never read a command again.
    for (int attempt = 0; attempt < 2; attempt++) {
        String url = String(Settings::pushUrl()) + "-cmd/json?poll=1&since=" + (since[0] ? since : "12h");
        if (!http.begin(*client(), url)) return;
        http.setTimeout(4000);
        if (Settings::pushToken()[0]) http.addHeader("Authorization", String("Bearer ") + Settings::pushToken());
        code = http.GET();
        if (code != 400 || !since[0]) break;
        http.end();
        Serial.printf("[push] command topic forgot id %s; reading the last 12 hours\n", since);
        since[0] = '\0';
        if (p.begin(NVS_NS, false)) { p.remove("cmdSince"); p.end(); }
    }
    s_cmdCode = code > 0 ? code : -1;
    if (code != 200) {
        if (code != 404) Serial.printf("[push] command topic: HTTP %d\n", code);
        http.end();
        return;
    }
    String body = http.getString();
    http.end();
    char last[24] = "";
    int at = 0;
    while (at < (int)body.length()) {
        int nl = body.indexOf('\n', at);
        if (nl < 0) nl = body.length();
        const String line = body.substring(at, nl);
        at = nl + 1;
        if (line.indexOf("\"event\":\"message\"") < 0) continue;
        const int i = line.indexOf("\"id\":\"");
        const int m = line.indexOf("\"message\":\"");
        if (i < 0 || m < 0) continue;
        const int ie = line.indexOf('"', i + 6), me = line.indexOf('"', m + 11);
        if (ie < 0 || me < 0) continue;
        strncpy(last, line.substring(i + 6, ie).c_str(), sizeof last - 1);
        noteCommand(line.substring(m + 11, me).c_str());
    }
    if (last[0] && strcmp(last, since) != 0 && p.begin(NVS_NS, false)) {
        p.putString("cmdSince", last);
        p.end();
    }
}
}  // namespace

int cmdLastCode() { return s_cmdCode; }

void setBoardId(const char* id) { strncpy(s_boardId, id ? id : "", sizeof s_boardId - 1); }

bool takeCommand(uint8_t mac[6], uint8_t& type, uint8_t& kind, bool& mine) {
    if (s_ignTail == s_ignHead) return false;
    memcpy(mac, s_ign[s_ignTail], 6);
    type = s_ign[s_ignTail][6];
    kind = s_ign[s_ignTail][7];
    mine = s_ign[s_ignTail][8] != 0;
    s_ignTail = (uint8_t)((s_ignTail + 1) % IGN_MAX);
    return true;
}

void joinStats(uint16_t& stuck, uint16_t& saved) {
    stuck = saved = 0;
    Preferences p;
    if (!p.begin(NVS_NS, true)) return;
    stuck = p.getUShort("jStuck", 0);
    saved = p.getUShort("jSaved", 0);
    p.end();
}

// ---- PUSH ALERTS ---------------------------------------------------------------
namespace {
volatile PushResult s_push     = PushResult::NONE;
TaskHandle_t        s_psTask   = nullptr;
PushMsg*            s_psMsgs   = nullptr;   // the batch, on the heap only while it is out
uint8_t             s_psN      = 0;
volatile uint8_t    s_psSent   = 0;
volatile int        s_psCode   = 0;
volatile uint32_t   s_psRetryS = 0;

void psRun(void*) {
    const uint32_t t0 = millis();
    char ssid[33];
    // The best network in range, and if it will not take us -- a hotspot gone
    // to sleep, a password changed -- the next best, once.
    int8_t tried = -1;
    bool joined = joinBest(ssid, sizeof ssid, "push", -1, &tried);
    if (!joined && tried >= 0) {
        WiFi.disconnect(false, false);
        joined = joinBest(ssid, sizeof ssid, "push", tried, nullptr);
    }
    if (!joined) {
        s_push = PushResult::NO_JOIN;
    } else {
        // A shorter timeout than a download's: a server that does not answer
        // in five seconds is not going to, and detection is blind meanwhile.
        for (uint8_t i = 0; i < s_psN; i++) {
            HTTPClient http;
            if (s_psMsgs[i].cmd) {
                // A line for the other boards, on the command topic.
                if (!http.begin(*client(), String(Settings::pushUrl()) + "-cmd")) break;
                http.setTimeout(5000);
                if (Settings::pushToken()[0]) http.addHeader("Authorization", String("Bearer ") + Settings::pushToken());
                const int code = http.POST((uint8_t*)s_psMsgs[i].body, strlen(s_psMsgs[i].body));
                http.end();
                s_psCode = code < 0 ? 0 : code;
                if (code < 200 || code >= 300) { Serial.printf("[push] command topic said %d\n", code); break; }
                s_psSent++;
                continue;
            }
            if (!http.begin(*client(), Settings::pushUrl())) break;
            http.setTimeout(5000);
            http.addHeader("Title", s_psMsgs[i].title);
            char pr[4];
            snprintf(pr, sizeof pr, "%u", (unsigned)s_psMsgs[i].prio);
            http.addHeader("Priority", pr);
            if (s_psMsgs[i].tags[0]) http.addHeader("Tags", s_psMsgs[i].tags);
            if (s_psMsgs[i].seq[0])  http.addHeader("X-Sequence-ID", s_psMsgs[i].seq);
            if (Settings::pushToken()[0]) http.addHeader("Authorization", String("Bearer ") + Settings::pushToken());
            // The IGNORE button: the phone posts the act to the command topic
            // itself, so over HTTPS when the server has a name (a bare IP
            // is a LAN box, plain HTTP), with the same token -- the board's,
            // which can write muleskin-* topics. The notification carries
            // it; only accounts that can read the topic ever see that.
            if (s_psMsgs[i].act[0]) {
                String cmd = String(Settings::pushUrl()) + "-cmd";
                const String host = cmd.substring(7, cmd.indexOf('/', 7) < 0 ? cmd.length() : cmd.indexOf('/', 7));
                bool named = false;
                for (unsigned k = 0; k < host.length(); k++) if (isalpha((unsigned char)host[k])) named = true;
                if (cmd.startsWith("http://") && named) cmd = "https://" + cmd.substring(7);
                // Two buttons, one command topic: Ignore for good, or Snooze
                // for a day ("SNOOZE" in place of "IGNORE", the rest alike).
                // The button token when one is set (Settings::pushBtnToken:
                // command topics only), else the board's own.
                const char* btnTok = Settings::pushBtnToken()[0] ? Settings::pushBtnToken() : Settings::pushToken();
                const String auth = btnTok[0] ? String(", headers.Authorization=Bearer ") + btnTok : String();
                String act;
                if (strncmp(s_psMsgs[i].act, "IGNORE ", 7) == 0) {
                    // "IGNORE <mac> <type> <board>": Ignore and Snooze carry
                    // it all; Pause only needs the board.
                    const String rest = String(s_psMsgs[i].act).substring(6);
                    const String board = rest.substring(rest.lastIndexOf(' ') + 1);
                    act = "http, Ignore, " + cmd + ", method=POST, body=IGNORE" + rest + ", clear=true" + auth +
                          "; http, Snooze 1 day, " + cmd + ", method=POST, body=SNOOZE" + rest + ", clear=true" + auth +
                          "; http, Pause 2 h, " + cmd + ", method=POST, body=PAUSE " + board + ", clear=true" + auth;
                } else {
                    // The confirmation's one button: UNIGNORE / UNSNOOZE / RESUME.
                    act = "http, Undo, " + cmd + ", method=POST, body=" + String(s_psMsgs[i].act) + ", clear=true" + auth;
                }
                http.addHeader("Actions", act);
            }
            static const char* KEEP[] = { "Retry-After" };   // collectHeaders() wants it non-const
            http.collectHeaders(KEEP, 1);
            const int code = http.POST((uint8_t*)s_psMsgs[i].body, strlen(s_psMsgs[i].body));
            s_psRetryS = code == 429 ? (uint32_t)http.header("Retry-After").toInt() : 0;
            http.end();
            s_psCode = code < 0 ? 0 : code;
            if (code < 200 || code >= 300) {
                Serial.printf("[push] server said %d\n", code);
                break;
            }
            s_psSent++;
        }
        s_push = s_psSent == s_psN ? PushResult::SENT : PushResult::NO_ANSWER;
        pollCommands();   // joined anyway: any IGNORE from the phone
    }
    // Off the network, but the station mode stays up: detection sniffs on it.
    WiFi.disconnect(false, false);
    Serial.printf("[push] %u of %u sent in %lu ms\n", (unsigned)s_psSent, (unsigned)s_psN,
                  (unsigned long)(millis() - t0));
    free(s_psMsgs);
    s_psMsgs = nullptr;
    s_psTask = nullptr;
    vTaskDelete(nullptr);
}
}  // namespace

bool pushStart(const PushMsg* msgs, uint8_t n) {
    if (!n || s_psTask || s_tsTask || s_task || s_state != State::OFF) return false;
    readSaved();
    if (!s_n) { s_push = PushResult::NO_SAVED; return false; }
    s_psMsgs = (PushMsg*)malloc(sizeof(PushMsg) * n);
    if (!s_psMsgs) { s_push = PushResult::NO_MEMORY; return false; }
    memcpy(s_psMsgs, msgs, sizeof(PushMsg) * n);
    s_psN    = n;
    s_psSent = 0;
    s_psCode = 0;
    s_push   = PushResult::NONE;
    if (xTaskCreatePinnedToCore(psRun, "push", 8192, nullptr, 1, &s_psTask, 1) != pdPASS) {
        free(s_psMsgs);
        s_psMsgs = nullptr;
        s_psTask = nullptr;
        s_push   = PushResult::NO_MEMORY;
        return false;
    }
    return true;
}
bool       pushBusy()      { return s_psTask != nullptr; }
PushResult pushResult()    { return s_push; }
uint8_t    pushSentCount() { return s_psSent; }
int        pushLastCode()  { return s_psCode; }
uint32_t   pushRetryAfterS() { return s_psRetryS; }


bool timeSyncStart() {
    if (s_tsTask || s_psTask || s_task || s_state != State::OFF) return false;
    readSaved();
    if (!s_n) { s_ts = TimeSync::NO_SAVED; return false; }
    s_ts = TimeSync::JOINING;
    s_tsSsid[0] = '\0';
    if (xTaskCreatePinnedToCore(tsRun, "wifitime", 8192, nullptr, 1, &s_tsTask, 1) != pdPASS) {
        s_tsTask = nullptr;
        s_ts = TimeSync::NO_MEMORY;
        return false;
    }
    return true;
}
bool        timeSyncBusy()    { return s_tsTask != nullptr; }
TimeSync    timeSyncState()   { return s_ts; }
const char* timeSyncNetwork() { return s_tsSsid; }

const char* network()       { return s_ssid; }
const char* latestVersion() { return s_latest; }
bool        upToDate()      { return s_latest[0] && strcmp(s_latest, FIRMWARE_VERSION) == 0; }
void        install()       { if (s_state == State::READY) s_install = true; }

bool canTryAgain() { return s_state == State::FAILED && !s_downloadStarted && !s_task; }
void tryAgain() {
    if (!canTryAgain()) return;
    Clock::syncStop();
    WiFi.disconnect(false, false);
    startScan();
}

uint8_t     percent()       { return s_size ? (uint8_t)((uint64_t)s_rx * 100 / s_size) : 0; }
uint32_t    bytesReceived() { return s_rx; }
uint32_t    bytesExpected() { return s_size; }
// The failure's sentence, and what the fetch ran into after it in brackets
// when there is something: "(HTTP 403)", "(name lookup failed)".
const char* failureText() {
    static char buf[144];
    const char* w = OtaCore::failWords(s_fail);
    if (!s_why[0]) return w;
    snprintf(buf, sizeof buf, "%s (%s)", w, s_why);
    return buf;
}

}  // namespace OtaWifi
