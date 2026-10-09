// MuleSkin-CYD — PHONE ALERTS. See include/phone_alerts.h.
#include "phone_alerts.h"
#include <string.h>
#include <stdio.h>

#if defined(ARDUINO_ARCH_ESP32) && defined(SQW_PHONE_ALERTS)
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "settings.h"

namespace PhoneAlerts {
namespace {

// Ours, and the phone page filters on it. Next to the update service's
// 9d1f0001.. so the family is recognisable, and never the same.
const char* SVC_UUID   = "9d1f0101-2a4b-4c8e-9b1e-5357574f5441";
const char* ALERT_UUID = "9d1f0102-2a4b-4c8e-9b1e-5357574f5441";   // read, notify: the latest line
const char* INFO_UUID  = "9d1f0103-2a4b-4c8e-9b1e-5357574f5441";   // read: env=..;ver=..;proto=1;code=0|1
const char* CODE_UUID  = "9d1f0104-2a4b-4c8e-9b1e-5357574f5441";   // write: the four digits, with PHONE CODE on

// PHONE CODE: how long a phone may sit connected without the code, and how
// many wrong ones it gets, before the board lets it go -- the one slot is
// then free for the owner.
const uint32_t CODE_WAIT_MS = 30000;
const uint8_t  CODE_TRIES   = 3;

// Written by the NimBLE host task, read by the loop.
volatile bool     s_connected  = false;
volatile uint16_t s_connHandle = 0xFFFF;

volatile bool     s_paused     = false;
bool              s_advOn      = false;
// The connected phone has given the code (or none is needed). Alerts go only
// to a phone that has. Written by the host task, read by the loop.
volatile bool     s_authed     = false;
volatile uint8_t  s_codeFails  = 0;
volatile uint32_t s_connAt     = 0;
volatile bool     s_kick       = false;   // let the phone go, from the loop
volatile bool     s_greet      = false;   // the code was right: say READY
volatile bool     s_leaving    = false;   // disconnect asked for; the link has not gone yet

NimBLEServer*         s_server = nullptr;
NimBLECharacteristic* s_alert  = nullptr;
NimBLECharacteristic* s_info   = nullptr;
char                  s_name[16] = "MuleSkin";

// The last few devices sent, so a device bouncing in and out of range is one
// notification a minute, not one per alert gate.
struct Sent { uint8_t mac[6]; uint32_t at; };
Sent    s_sent[4];
uint8_t s_sentNext = 0;
const uint32_t RESEND_MS = 60000;

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& d) override {
        s_connHandle = d.getConnHandle();
        s_connected  = true;
        s_authed     = (Settings::phoneCode() == 0);
        s_codeFails  = 0;
        s_connAt     = millis();
        s_leaving    = false;
        Serial.printf("[phone] connected: %s%s\n", d.getAddress().toString().c_str(),
                      s_authed ? "" : ", waiting for the code");
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        s_connected  = false;
        s_connHandle = 0xFFFF;
        Serial.printf("[phone] disconnected: reason 0x%02x\n", (unsigned)reason);
    }
} s_serverCb;

// The four digits, as ASCII. Compared whole: a phone that sends "48" and then
// "21" has sent two wrong codes, not one right one.
class CodeCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        if (s_authed || s_kick) return;
        const NimBLEAttValue v = c->getValue();
        char want[8];
        snprintf(want, sizeof want, "%04u", (unsigned)Settings::phoneCode());
        if (v.length() == 4 && memcmp(v.data(), want, 4) == 0) {
            s_authed = true;
            s_greet  = true;
        } else if (++s_codeFails >= CODE_TRIES) {
            s_kick = true;
        }
    }
} s_codeCb;

void setInfo() {
    if (!s_info) return;
    char buf[96];
    snprintf(buf, sizeof buf, "env=%s;ver=%s;proto=1;code=%u",
#ifdef SQW_ENV
             SQW_ENV,
#else
             "unknown",
#endif
#ifdef FIRMWARE_VERSION
             FIRMWARE_VERSION,
#else
             "unknown",
#endif
             Settings::phoneCode() ? 1u : 0u);
    s_info->setValue((const uint8_t*)buf, strlen(buf));
}

void send(const char* line) {
    if (!s_alert) return;
    s_alert->setValue((const uint8_t*)line, strlen(line));
    if (s_connected) s_alert->notify();
}

}  // namespace

bool available()  { return true; }
bool registered() { return s_server != nullptr; }
bool connected()  { return s_connected; }
const char* name() { return s_name; }

bool wantConnectable() {
    return s_server && !s_connected && Settings::phoneAlerts();
}

bool registerService() {
    if (s_server) return true;
    NimBLEScan* scan = NimBLEDevice::getScan();
    const uint32_t t0 = millis();
    while (scan && scan->isScanning() && millis() - t0 < 2000) delay(10);
    if (scan && scan->isScanning()) {
        Serial.println("[phone] scan did not stop: service not registered");
        return false;
    }
    // Plenty for a line of text, and a cap on what a phone may negotiate.
    NimBLEDevice::setMTU(185);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_serverCb, false);
    // tick() puts the advert back after a disconnect, in its own time.
    s_server->advertiseOnDisconnect(false);
    NimBLEService* svc = s_server->createService(SVC_UUID);
    // Notify only, not readable: the last alert is the one thing a phone
    // without the code must not be able to read off it.
    s_alert = svc->createCharacteristic(ALERT_UUID, NIMBLE_PROPERTY::NOTIFY, 120);
    s_info  = svc->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ, 96);
    NimBLECharacteristic* code = svc->createCharacteristic(CODE_UUID, NIMBLE_PROPERTY::WRITE, 8);
    code->setCallbacks(&s_codeCb);
    setInfo();
    svc->start();
    s_server->start();

    const NimBLEAddress a = NimBLEDevice::getAddress();
    const uint8_t* m = a.getBase()->val;
    snprintf(s_name, sizeof s_name, "MuleSkin-%02X%02X", m[1], m[0]);
    Serial.printf("[phone] service registered as %s, heap %lu\n", s_name, (unsigned long)ESP.getFreeHeap());
    return true;
}

void pauseRadio(bool paused) {
    s_paused = paused;
    if (paused && s_advOn) { NimBLEDevice::getAdvertising()->stop(); s_advOn = false; }
}

void tick(uint32_t now) {
    // PHONE CODE's two ways out: three wrong codes, or none in time.
    if (s_connected && s_server && s_connHandle != 0xFFFF && !s_leaving &&
        (s_kick || (!s_authed && now - s_connAt > CODE_WAIT_MS))) {
        Serial.printf("[phone] let go: %s\n", s_kick ? "wrong code three times" : "no code in time");
        s_kick = false;
        s_leaving = true;
        s_server->disconnect(s_connHandle);
    }
    if (s_greet) {
        s_greet = false;
        Serial.println("[phone] code accepted");
        send("N,READY");
    }
    const bool want = wantConnectable() && !s_paused;
    if (want == s_advOn) return;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) return;
    s_advOn = want;
    // A phone connecting ends a connectable advert in the controller, so
    // "stop" here is often a no-op; it is what makes s_advOn true again.
    if (!want) { adv->stop(); return; }
    NimBLEAdvertisementData d;
    d.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    d.setCompleteServices(NimBLEUUID(SVC_UUID));
    adv->setAdvertisementData(d);
    NimBLEAdvertisementData r;
    r.setName(s_name);
    adv->setScanResponseData(r);
    adv->enableScanResponse(true);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
    // Every half second: shared with the scan and the WiFi sniffer, a slower
    // advert was easy for a phone to miss (1.5 s: one scan in three, bench).
    adv->setMinInterval(800);
    adv->setMaxInterval(816);
    adv->start();
}

void setEnabled(bool on) {
    if (!on && s_server && s_connected && s_connHandle != 0xFFFF) s_server->disconnect(s_connHandle);
}

bool listening() { return s_connected && s_authed; }

void codeChanged() {
    setInfo();
    // A phone that got in without the code (or with the old one) reconnects
    // and is asked for the new one.
    if (s_server && s_connected && s_connHandle != 0xFFFF) s_server->disconnect(s_connHandle);
}

void alert(const Detection& d, uint16_t followMins) {
    if (!s_connected || !s_authed || !Settings::phoneAlerts()) return;
    const uint32_t now = millis();
    if (!followMins) {
        for (const Sent& s : s_sent)
            if (s.at && memcmp(s.mac, d.mac, 6) == 0 && now - s.at < RESEND_MS) return;
        memcpy(s_sent[s_sentNext].mac, d.mac, 6);
        s_sent[s_sentNext].at = now ? now : 1;
        s_sentNext = (uint8_t)((s_sentNext + 1) % 4);
    }
    // One line, commas between fields; the page splits it. Nothing in it
    // can hold a comma: the type and vendor names are this firmware's own
    // constants, and the advertised name (which could) is not sent.
    //   A|W , TYPE , vendor , rssi , mac , minutes
    char line[96];
    snprintf(line, sizeof line, "%c,%s,%s,%d,%02x:%02x:%02x:%02x:%02x:%02x,%u",
             followMins ? 'W' : 'A', detectionTypeName(d.type), vendorText(d), (int)d.rssi,
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5], (unsigned)followMins);
    send(line);
    Serial.printf("[phone] sent %s\n", line);
}

void note(const char* text) {
    if (!s_connected || !s_authed) return;
    char line[96];
    snprintf(line, sizeof line, "N,%s", text);
    send(line);
}

}  // namespace PhoneAlerts

#else   // no phone link on this board (or the emulator)

namespace PhoneAlerts {
bool available()        { return false; }
bool registerService()  { return false; }
bool registered()       { return false; }
void setEnabled(bool)   {}
bool connected()        { return false; }
bool listening()        { return false; }
void codeChanged()      {}
bool wantConnectable()  { return false; }
const char* name()      { return ""; }
void tick(uint32_t)     {}
void pauseRadio(bool)   {}
void alert(const Detection&, uint16_t) {}
void note(const char*)  {}
}

#endif
