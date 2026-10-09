// MuleSkin-CYD — settings backup and restore. See include/nvs_backup.h.
#include "nvs_backup.h"
#include <Arduino.h>
#include <string.h>
#include <strings.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <nvs.h>
#include <esp_idf_version.h>

namespace NvsBackup {
namespace {

const size_t MAX_VALUE = 256;   // bytes; a hex line stays under the console's buffer

// The radio's own data, tied to this board's chip: never carried across.
bool ownedBySystem(const char* ns) {
    return strncmp(ns, "nvs.", 4) == 0 || strcmp(ns, "phy") == 0;
}

size_t intSize(nvs_type_t t) {
    switch (t) {
        case NVS_TYPE_U8:  case NVS_TYPE_I8:  return 1;
        case NVS_TYPE_U16: case NVS_TYPE_I16: return 2;
        case NVS_TYPE_U32: case NVS_TYPE_I32: return 4;
        case NVS_TYPE_U64: case NVS_TYPE_I64: return 8;
        default: return 0;
    }
}

void printHex(const uint8_t* b, size_t n) {
    static const char* H = "0123456789abcdef";
    char out[2 * 32 + 1];
    for (size_t i = 0; i < n; i += 32) {
        size_t k = 0;
        for (size_t j = i; j < n && j < i + 32; j++) { out[k++] = H[b[j] >> 4]; out[k++] = H[b[j] & 15]; }
        out[k] = '\0';
        Serial.print(out);
    }
}

// One entry: read it and print its line. Returns true if printed.
bool exportOne(const char* ns, const char* key, nvs_type_t type) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t buf[MAX_VALUE];
    size_t n = 0;
    esp_err_t e = ESP_FAIL;
    switch (type) {
        case NVS_TYPE_U8:  e = nvs_get_u8 (h, key, (uint8_t*)buf);  n = 1; break;
        case NVS_TYPE_I8:  e = nvs_get_i8 (h, key, (int8_t*)buf);   n = 1; break;
        case NVS_TYPE_U16: e = nvs_get_u16(h, key, (uint16_t*)buf); n = 2; break;
        case NVS_TYPE_I16: e = nvs_get_i16(h, key, (int16_t*)buf);  n = 2; break;
        case NVS_TYPE_U32: e = nvs_get_u32(h, key, (uint32_t*)buf); n = 4; break;
        case NVS_TYPE_I32: e = nvs_get_i32(h, key, (int32_t*)buf);  n = 4; break;
        case NVS_TYPE_U64: e = nvs_get_u64(h, key, (uint64_t*)buf); n = 8; break;
        case NVS_TYPE_I64: e = nvs_get_i64(h, key, (int64_t*)buf);  n = 8; break;
        case NVS_TYPE_STR: {
            size_t len = 0;
            e = nvs_get_str(h, key, nullptr, &len);           // includes the NUL
            if (e == ESP_OK && len > MAX_VALUE) { nvs_close(h); Serial.printf("NVS SKIP %s %s %u\n", ns, key, (unsigned)len); return false; }
            if (e == ESP_OK) { len = sizeof buf; e = nvs_get_str(h, key, (char*)buf, &len); n = len ? len - 1 : 0; }
            break;
        }
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            e = nvs_get_blob(h, key, nullptr, &len);
            if (e == ESP_OK && len > MAX_VALUE) { nvs_close(h); Serial.printf("NVS SKIP %s %s %u\n", ns, key, (unsigned)len); return false; }
            if (e == ESP_OK) { len = sizeof buf; e = nvs_get_blob(h, key, buf, &len); n = len; }
            break;
        }
        default: break;
    }
    nvs_close(h);
    if (e != ESP_OK) return false;
    Serial.printf("NVS %s %s %02x ", ns, key, (unsigned)type);
    printHex(buf, n);
    Serial.print('\n');
    return true;
}

void exportAll() {
    unsigned count = 0;
#if ESP_IDF_VERSION_MAJOR >= 5
    nvs_iterator_t it = nullptr;
    esp_err_t e = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &it);
    while (e == ESP_OK && it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (!ownedBySystem(info.namespace_name) && exportOne(info.namespace_name, info.key, info.type)) count++;
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
#else
    nvs_iterator_t it = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY);
    while (it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (!ownedBySystem(info.namespace_name) && exportOne(info.namespace_name, info.key, info.type)) count++;
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
#endif
    Serial.printf("NVS END %u\n", count);
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "NVS SET <ns> <key> <tt> <hex>"
void setOne(const char* args) {
    char ns[16], key[16], tt[3];
    const char* hex = nullptr;
    int consumed = 0;
    if (sscanf(args, "%15s %15s %2s %n", ns, key, tt, &consumed) != 3) { Serial.println("[nvs] bad: NVS SET <ns> <key> <type> <hex>"); return; }
    hex = args + consumed;
    if (ownedBySystem(ns)) { Serial.println("[nvs] bad: system namespace"); return; }
    const nvs_type_t type = (nvs_type_t)strtoul(tt, nullptr, 16);
    uint8_t buf[MAX_VALUE + 1];
    size_t n = 0;
    while (hex[0] && hex[1] && hex[0] != ' ' && n < MAX_VALUE) {
        const int hi = hexVal(hex[0]), lo = hexVal(hex[1]);
        if (hi < 0 || lo < 0) { Serial.println("[nvs] bad: hex"); return; }
        buf[n++] = (uint8_t)(hi * 16 + lo);
        hex += 2;
    }
    const size_t want = intSize(type);
    if (want && n != want) { Serial.println("[nvs] bad: size"); return; }

    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) { Serial.println("[nvs] bad: open"); return; }
    esp_err_t e = ESP_FAIL;
    switch (type) {
        case NVS_TYPE_U8:  e = nvs_set_u8 (h, key, buf[0]); break;
        case NVS_TYPE_I8:  e = nvs_set_i8 (h, key, (int8_t)buf[0]); break;
        case NVS_TYPE_U16: { uint16_t v; memcpy(&v, buf, 2); e = nvs_set_u16(h, key, v); break; }
        case NVS_TYPE_I16: { int16_t  v; memcpy(&v, buf, 2); e = nvs_set_i16(h, key, v); break; }
        case NVS_TYPE_U32: { uint32_t v; memcpy(&v, buf, 4); e = nvs_set_u32(h, key, v); break; }
        case NVS_TYPE_I32: { int32_t  v; memcpy(&v, buf, 4); e = nvs_set_i32(h, key, v); break; }
        case NVS_TYPE_U64: { uint64_t v; memcpy(&v, buf, 8); e = nvs_set_u64(h, key, v); break; }
        case NVS_TYPE_I64: { int64_t  v; memcpy(&v, buf, 8); e = nvs_set_i64(h, key, v); break; }
        case NVS_TYPE_STR: buf[n] = 0; e = nvs_set_str(h, key, (const char*)buf); break;
        case NVS_TYPE_BLOB: e = nvs_set_blob(h, key, buf, n); break;
        default: break;
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) Serial.printf("[nvs] ok %s %s\n", ns, key);
    else             Serial.printf("[nvs] bad: %s %s (%d)\n", ns, key, (int)e);
}

}  // namespace

bool handle(const char* line) {
    if (strncasecmp(line, "NVS ", 4) != 0) return false;
    const char* a = line + 4;
    if (strcasecmp(a, "EXPORT") == 0)        exportAll();
    else if (strncasecmp(a, "SET ", 4) == 0) setOne(a + 4);
    else if (strcasecmp(a, "RESTART") == 0) {
        Serial.println("[nvs] restarting");
        Serial.flush();
        delay(200);
        ESP.restart();
    }
    else Serial.println("[nvs] one of: NVS EXPORT, NVS SET <ns> <key> <type> <hex>, NVS RESTART");
    return true;
}

}  // namespace NvsBackup

#else   // the emulator and the host tests: no NVS partition to speak of

namespace NvsBackup {
bool handle(const char* line) { return strncasecmp(line, "NVS ", 4) == 0; }
}

#endif
