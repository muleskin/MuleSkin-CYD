// MuleSkin-CYD — moving settings from the old 20 KB store. See include/nvs_move.h.
#include "nvs_move.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace NvsMove {
namespace {

const uint32_t OLD_BASE  = 0x9000;   // where every table up to v3.1.8 put it
const uint32_t OLD_SIZE  = 0x5000;   // 20 KB: five pages
const char*    OLD_LABEL = "nvsold";

// Any page of the old region in a state NVS itself writes (active, full,
// freeing) -- an erased region reads all 0xFF and holds nothing to move.
bool oldRegionHoldsAStore() {
    for (uint32_t p = 0; p < OLD_SIZE; p += 4096) {
        uint32_t state = 0;
        if (esp_flash_read(esp_flash_default_chip, &state, OLD_BASE + p, sizeof state) != ESP_OK) return false;
        if (state == 0xFFFFFFFEu || state == 0xFFFFFFFCu || state == 0xFFFFFFF8u) return true;
    }
    return false;
}

// The new table is in: "nvs" is somewhere else, and nothing claims 0x9000.
bool tableMovedTheStore() {
    const esp_partition_t* nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!nvs || nvs->address == OLD_BASE) return false;
    for (int type = 0; type < 2; type++) {
        esp_partition_iterator_t it = esp_partition_find(type ? ESP_PARTITION_TYPE_DATA : ESP_PARTITION_TYPE_APP,
                                                         ESP_PARTITION_SUBTYPE_ANY, nullptr);
        for (; it; it = esp_partition_next(it)) {
            const esp_partition_t* p = esp_partition_get(it);
            if (p->address < OLD_BASE + OLD_SIZE && OLD_BASE < p->address + p->size) {
                esp_partition_iterator_release(it);
                return false;
            }
        }
    }
    return true;
}

bool copyOne(const char* ns, const char* key, nvs_type_t type) {
    nvs_handle_t src, dst;
    if (nvs_open_from_partition(OLD_LABEL, ns, NVS_READONLY, &src) != ESP_OK) return false;
    if (nvs_open(ns, NVS_READWRITE, &dst) != ESP_OK) { nvs_close(src); return false; }
    esp_err_t e = ESP_FAIL;
    switch (type) {
        case NVS_TYPE_U8:  { uint8_t  v; e = nvs_get_u8 (src, key, &v); if (!e) e = nvs_set_u8 (dst, key, v); break; }
        case NVS_TYPE_I8:  { int8_t   v; e = nvs_get_i8 (src, key, &v); if (!e) e = nvs_set_i8 (dst, key, v); break; }
        case NVS_TYPE_U16: { uint16_t v; e = nvs_get_u16(src, key, &v); if (!e) e = nvs_set_u16(dst, key, v); break; }
        case NVS_TYPE_I16: { int16_t  v; e = nvs_get_i16(src, key, &v); if (!e) e = nvs_set_i16(dst, key, v); break; }
        case NVS_TYPE_U32: { uint32_t v; e = nvs_get_u32(src, key, &v); if (!e) e = nvs_set_u32(dst, key, v); break; }
        case NVS_TYPE_I32: { int32_t  v; e = nvs_get_i32(src, key, &v); if (!e) e = nvs_set_i32(dst, key, v); break; }
        case NVS_TYPE_U64: { uint64_t v; e = nvs_get_u64(src, key, &v); if (!e) e = nvs_set_u64(dst, key, v); break; }
        case NVS_TYPE_I64: { int64_t  v; e = nvs_get_i64(src, key, &v); if (!e) e = nvs_set_i64(dst, key, v); break; }
        case NVS_TYPE_STR: {
            size_t len = 0;
            e = nvs_get_str(src, key, nullptr, &len);
            char* buf = e ? nullptr : (char*)malloc(len ? len : 1);
            if (buf) { e = nvs_get_str(src, key, buf, &len); if (!e) e = nvs_set_str(dst, key, buf); free(buf); }
            else if (!e) e = ESP_ERR_NO_MEM;
            break;
        }
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            e = nvs_get_blob(src, key, nullptr, &len);
            void* buf = e ? nullptr : malloc(len ? len : 1);
            if (buf) { e = nvs_get_blob(src, key, buf, &len); if (!e) e = nvs_set_blob(dst, key, buf, len); free(buf); }
            else if (!e) e = ESP_ERR_NO_MEM;
            break;
        }
        default: break;
    }
    if (!e) e = nvs_commit(dst);
    nvs_close(dst);
    nvs_close(src);
    return e == ESP_OK;
}

// Every key of the old store into the new one; -1 on the first that will not go.
int copyAll() {
    int moved = 0;
#if ESP_IDF_VERSION_MAJOR >= 5
    nvs_iterator_t it = nullptr;
    esp_err_t e = nvs_entry_find(OLD_LABEL, nullptr, NVS_TYPE_ANY, &it);
    while (e == ESP_OK && it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (!copyOne(info.namespace_name, info.key, info.type)) { nvs_release_iterator(it); return -1; }
        moved++;
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
#else
    nvs_iterator_t it = nvs_entry_find(OLD_LABEL, nullptr, NVS_TYPE_ANY);
    while (it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (!copyOne(info.namespace_name, info.key, info.type)) { nvs_release_iterator(it); return -1; }
        moved++;
        it = nvs_entry_next(it);
    }
#endif
    return moved;
}

}   // namespace

int run() {
    if (!tableMovedTheStore() || !oldRegionHoldsAStore()) return 0;

    const esp_partition_t* old = nullptr;
    if (esp_partition_register_external(esp_flash_default_chip, OLD_BASE, OLD_SIZE, OLD_LABEL,
                                        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, &old) != ESP_OK)
        return -1;
    if (nvs_flash_init_partition(OLD_LABEL) != ESP_OK) {
        esp_partition_deregister_external(old);
        return -1;
    }

    // The new region held whatever was there before the table moved -- on a
    // board that ever updated into app1, that is app code -- and the core
    // may have opened it as a store. Start it clean so only the old keys land.
    const esp_partition_t* nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    nvs_flash_deinit();
    const bool clean = esp_partition_erase_range(nvs, 0, nvs->size) == ESP_OK;
    int moved = -1;
    if (nvs_flash_init() == ESP_OK) {
        if (clean) moved = copyAll();
    } else {
        // As the core does at boot: a store that will not open is erased.
        esp_partition_erase_range(nvs, 0, nvs->size);
        nvs_flash_init();
    }

    nvs_flash_deinit_partition(OLD_LABEL);
    // Moved: erase the old copy, so it never moves twice and no WiFi password
    // is left lying in flash nothing reads. Failed: leave it for the next boot.
    if (moved >= 0) esp_partition_erase_range(old, 0, OLD_SIZE);
    esp_partition_deregister_external(old);
    return moved;
}

}   // namespace NvsMove

#else
namespace NvsMove { int run() { return 0; } }
#endif
