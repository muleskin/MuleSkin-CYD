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
const char* INFO_UUID  = "9d1f0103-2a4b-4c8e-9b1e-5357574f5441";   // read: env=..;ver=..;proto=1

// Written by the NimBLE host task, read by the loop.
volatile bool     s_connected  = false;
volatile uint16_t s_connHandle = 0xFFFF;

NimBLEServer*         s_server = nullptr;
NimBLECharacteristic* s_alert  = nullptr;
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
        Serial.printf("[phone] connected: %s\n", d.getAddress().toString().c_str());
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        s_connected  = false;
        s_connHandle = 0xFFFF;
        Serial.printf("[phone] disconnected: reason 0x%02x\n", (unsigned)reason);
    }
} s_serverCb;

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
    // The mesh advert comes back on its own (wantConnectable()); the
    // library's own re-advertising would replace it with an empty one.
    s_server->advertiseOnDisconnect(false);
    NimBLEService* svc = s_server->createService(SVC_UUID);
    s_alert = svc->createCharacteristic(ALERT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 120);
    NimBLECharacteristic* info = svc->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ, 96);
    char buf[96];
    snprintf(buf, sizeof buf, "env=%s;ver=%s;proto=1",
#ifdef SQW_ENV
             SQW_ENV,
#else
             "unknown",
#endif
#ifdef FIRMWARE_VERSION
             FIRMWARE_VERSION
#else
             "unknown"
#endif
    );
    info->setValue((const uint8_t*)buf, strlen(buf));
    s_alert->setValue((const uint8_t*)"N,READY", 7);
    svc->start();
    s_server->start();

    const NimBLEAddress a = NimBLEDevice::getAddress();
    const uint8_t* m = a.getBase()->val;
    snprintf(s_name, sizeof s_name, "MuleSkin-%02X%02X", m[1], m[0]);
    Serial.printf("[phone] service registered as %s, heap %lu\n", s_name, (unsigned long)ESP.getFreeHeap());
    return true;
}

void setEnabled(bool on) {
    if (!on && s_server && s_connected && s_connHandle != 0xFFFF) s_server->disconnect(s_connHandle);
}

void alert(const Detection& d, uint16_t followMins) {
    if (!s_connected || !Settings::phoneAlerts()) return;
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
    if (!s_connected) return;
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
bool wantConnectable()  { return false; }
const char* name()      { return ""; }
void alert(const Detection&, uint16_t) {}
void note(const char*)  {}
}

#endif
