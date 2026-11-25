// MuleSkin-CYD -- part of src/main.cpp, included where it stood:
// The PIN lock: SECURITY, the PIN entry flows (set, change, duress), the
// LOCKED screen, and the wipe (NVS, the card, the black box) a duress PIN or
// the tenth wrong guess sets off.
//
// One translation unit with main.cpp: this uses the globals and helpers
// declared above its #include there, and what follows it uses this, so
// it is not compiled on its own and its place in main.cpp matters.

// ---- the PIN lock ------------------------------------------------------------
// SECURITY is the menu; PIN_ENTRY is the payphone asking for a PIN on its
// behalf, with BACK; LOCKED is the payphone as the lock screen, without.
enum class PinFlow : uint8_t { SET_NEW, SET_AGAIN, OFF_VERIFY, CHANGE_CUR, CHANGE_NEW, CHANGE_AGAIN,
                               DURESS_CUR, DURESS_NEW, DURESS_AGAIN, DURESS_OFF };
static PinFlow s_pinFlow = PinFlow::SET_NEW;
static char    s_pinFirst[9]  = "";   // the first entry of a confirm-by-repeating pair
static char    s_pinPromptBuf[28] = "";

static void enterSecurity() {
    state = AppState::SECURITY;
    transitionStart = millis();
    uiSecurityInit(*canvas);
}

static const char* pinFlowPrompt(PinFlow f) {
    switch (f) {
        case PinFlow::SET_NEW:
            snprintf(s_pinPromptBuf, sizeof s_pinPromptBuf, "NEW %u-DIGIT PIN", (unsigned)Security::pinLength());
            return s_pinPromptBuf;
        case PinFlow::SET_AGAIN:
        case PinFlow::CHANGE_AGAIN:
        case PinFlow::DURESS_AGAIN: return "AGAIN TO CONFIRM";
        case PinFlow::CHANGE_NEW:   return "NEW PIN";
        case PinFlow::DURESS_NEW:   return "DURESS PIN";
        default:                    return "CURRENT PIN";
    }
}

static void startPinFlow(PinFlow f, const char* prompt = nullptr) {
    s_pinFlow = f;
    state = AppState::PIN_ENTRY;
    transitionStart = millis();
    uiPhoneInitPin(*canvas, Security::pinLength(), prompt ? prompt : pinFlowPrompt(f), true);
}

static void enterLocked() {
    state = AppState::LOCKED;
    transitionStart = millis();
    uiPhoneInitPin(*canvas, Security::pinLength(), "LOCKED", false);
    uiPhonePinAllowForgot(true);
}

#if HAVE_NVS_ERASE
// The NVS erase that actually erases. Deleting a key only marks its entry
// erased -- the bytes sit in flash until that page is reused, and a USB cable
// can read them -- so the secrets are removed by erasing the WHOLE store and
// writing everything worth keeping back. Everything is kept except the two
// namespaces that hold secrets: the message phrase and key, and the ignore
// list. Settings, calibration, outfits, stats, the PIN itself: all restored.
struct KeptEntry {
    std::string ns, key;
    nvs_type_t  type;
    uint64_t    num;
    std::vector<uint8_t> bytes;
};

// NVS iteration, either side of the IDF 4 -> IDF 5 signature change.
//
// IDF 5 turned both calls inside out: nvs_entry_find now returns esp_err_t and
// hands the iterator back through an out-parameter, and nvs_entry_next takes
// the iterator BY POINTER, advances it in place, and -- this is the part worth
// knowing -- releases it and sets it to NULL itself once the walk is done.
// These two wrappers put the IDF 4 shape back so the loop in physicalNvsWipe()
// below reads the same on every board. That loop decides which entries survive
// a duress wipe, so it is deliberately not the thing being rewritten here.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static nvs_iterator_t sqwNvsEntryFind(const char* part, const char* ns, nvs_type_t type) {
    nvs_iterator_t it = nullptr;
    // Anything other than ESP_OK (including ESP_ERR_NVS_NOT_FOUND for an empty
    // store) leaves no iterator to release.
    if (nvs_entry_find(part, ns, type, &it) != ESP_OK) return nullptr;
    return it;
}
static nvs_iterator_t sqwNvsEntryNext(nvs_iterator_t it) {
    if (!it) return nullptr;
    esp_err_t e = nvs_entry_next(&it);
    if (e == ESP_OK) return it;
    // On ESP_ERR_NVS_NOT_FOUND the iterator is already freed and NULLed. On any
    // other error that is not promised, so release whatever is still held
    // rather than leak it -- this runs on the wipe path, which must not be the
    // thing that exhausts the heap.
    if (it) nvs_release_iterator(it);
    return nullptr;
}
#else
// Macros, not inline functions, deliberately. An inline wrapper is free in
// principle and was not quite free in practice: it moved [env:cyd]'s
// .flash.text by a few bytes, and a board port has no business changing the
// code generated for boards it cannot test. These expand to the original call
// exactly, so the IDF 4 builds compile the same instructions they always did.
#define sqwNvsEntryFind(part, ns, type) nvs_entry_find((part), (ns), (type))
#define sqwNvsEntryNext(it)             nvs_entry_next((it))
#endif

static bool secretNamespace(const char* ns) {
    // "otawifi" is the saved WiFi password for firmware updates; "meshtalk"
    // held the retired squad's phrase and key, erased at boot since. The
    // rest say where you live and who you are around: the devices you see
    // every day (regulars), the trackers seen on your days out (awaydays) and
    // your home routers (homeaps). Upstream's review round 1 added regulars;
    // the other two are MuleSkin's own.
    return !strcmp(ns, "meshtalk") || !strcmp(ns, "ignore") || !strcmp(ns, "otawifi") ||
           !strcmp(ns, "regulars") || !strcmp(ns, "awaydays") || !strcmp(ns, "homeaps") ||
           // The WiFi names the owner asked to be warned about, which says
           // what they are watching for (MY ALERTS).
           !strcmp(ns, "myalerts");
}

static void physicalNvsWipe() {
    std::vector<KeptEntry> kept;
    // Grown once, up front. Grown by doubling as entries arrived, the last
    // step asked for more than the heap had left and the board panicked
    // between the erase and the restart -- with the secrets already gone,
    // which is the one part that mattered, but as a crash, not a quiet reboot.
    kept.reserve(128);
    Serial.printf("[wipe] keeping settings: heap %lu\n", (unsigned long)ESP.getFreeHeap());
    nvs_iterator_t it = sqwNvsEntryFind(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY);
    while (it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        nvs_handle_t h;
        if (!secretNamespace(info.namespace_name) &&
            nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
            KeptEntry e;
            e.ns = info.namespace_name; e.key = info.key; e.type = info.type; e.num = 0;
            bool ok = true;
            switch (info.type) {
                case NVS_TYPE_U8:  { uint8_t  v; ok = nvs_get_u8 (h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I8:  { int8_t   v; ok = nvs_get_i8 (h, info.key, &v) == ESP_OK; e.num = (uint64_t)(int64_t)v; break; }
                case NVS_TYPE_U16: { uint16_t v; ok = nvs_get_u16(h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I16: { int16_t  v; ok = nvs_get_i16(h, info.key, &v) == ESP_OK; e.num = (uint64_t)(int64_t)v; break; }
                case NVS_TYPE_U32: { uint32_t v; ok = nvs_get_u32(h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I32: { int32_t  v; ok = nvs_get_i32(h, info.key, &v) == ESP_OK; e.num = (uint64_t)(int64_t)v; break; }
                case NVS_TYPE_U64: { uint64_t v; ok = nvs_get_u64(h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I64: { int64_t  v; ok = nvs_get_i64(h, info.key, &v) == ESP_OK; e.num = (uint64_t)v; break; }
                case NVS_TYPE_STR: {
                    size_t n = 0;
                    ok = nvs_get_str(h, info.key, nullptr, &n) == ESP_OK;
                    if (ok) { e.bytes.resize(n); ok = nvs_get_str(h, info.key, (char*)e.bytes.data(), &n) == ESP_OK; }
                    break;
                }
                case NVS_TYPE_BLOB: {
                    size_t n = 0;
                    ok = nvs_get_blob(h, info.key, nullptr, &n) == ESP_OK;
                    if (ok) { e.bytes.resize(n); ok = nvs_get_blob(h, info.key, e.bytes.data(), &n) == ESP_OK; }
                    break;
                }
                default: ok = false; break;
            }
            nvs_close(h);
            if (ok) kept.push_back(std::move(e));
        }
        it = sqwNvsEntryNext(it);
    }
    nvs_release_iterator(it);
    Serial.printf("[wipe] %u entries kept, heap %lu; erasing\n", (unsigned)kept.size(), (unsigned long)ESP.getFreeHeap());
    serialFlush();

    nvs_flash_erase();       // de-initialises, then erases every page
    nvs_flash_init();

    for (const KeptEntry& e : kept) {
        nvs_handle_t h;
        if (nvs_open(e.ns.c_str(), NVS_READWRITE, &h) != ESP_OK) continue;
        const char* k = e.key.c_str();
        switch (e.type) {
            case NVS_TYPE_U8:   nvs_set_u8 (h, k, (uint8_t)e.num);  break;
            case NVS_TYPE_I8:   nvs_set_i8 (h, k, (int8_t)e.num);   break;
            case NVS_TYPE_U16:  nvs_set_u16(h, k, (uint16_t)e.num); break;
            case NVS_TYPE_I16:  nvs_set_i16(h, k, (int16_t)e.num);  break;
            case NVS_TYPE_U32:  nvs_set_u32(h, k, (uint32_t)e.num); break;
            case NVS_TYPE_I32:  nvs_set_i32(h, k, (int32_t)e.num);  break;
            case NVS_TYPE_U64:  nvs_set_u64(h, k, e.num);           break;
            case NVS_TYPE_I64:  nvs_set_i64(h, k, (int64_t)e.num);  break;
            case NVS_TYPE_STR:  nvs_set_str(h, k, (const char*)e.bytes.data()); break;
            case NVS_TYPE_BLOB: nvs_set_blob(h, k, e.bytes.data(), e.bytes.size()); break;
            default: break;
        }
        nvs_commit(h);
        nvs_close(h);
    }
}
#endif

// The wipe: the secrets in NVS, the SD log, and -- on the device -- a real
// erase of the store and a restart, which also takes the RAM log, the inbox
// and every open handle to the old store with it. `after` is how the board
// comes back; see takeWipeBoot().
static void performWipe(WipeBoot after) {
    // Dark first. A duress restart has to look like any other restart, and
    // the light is the one thing visible from the back of the board.
    StatusLight::off();
#if SQW_HAS_BUZZER
    // And silent: a chirp mid-wipe would be the one sound this restart makes.
    Buzzer::quiet();
#endif
    Security::wipeSecrets();
    engine.sd().wipe();
    BlackBox::wipe();        // the log and the crash history kept in flash
#if defined(TWATCH_S3)
    Wardrive::clear();       // where the watch has been, and everything it heard there
#elif defined(CYD_GPS)
    Wardrive::sdWipe();      // the WiGLE file is a record of everywhere the board has been
#endif
#if HAVE_NVS_ERASE
    // The frame buffer is 77 KB the wipe can have: the board restarts in a
    // moment and the screen is meant to go quiet anyway. Without it the copy
    // of the kept settings ran the heap dry.
    releaseFrameBuffer("wipe");
    physicalNvsWipe();
    Serial.println("[wipe] done, restarting");
    serialFlush();
    g_wipeBoot = WIPEBOOT_MAGIC | (uint8_t)after;
    delay(20);
    esp_restart();
#else
    engine.clearLog();
    engine.clearWatch();
    engine.clearHunt();
    if (after == WipeBoot::LOCKED) Security::lock();
    enterClear();
#endif
}
