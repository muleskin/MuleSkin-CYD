// MuleSkin-CYD — PHONE ALERTS: the board's alerts on your phone, over
// Bluetooth. No WiFi, no internet, no app to install.
//
// HOW. A phone opens the PHONE ALERTS page on the flasher site (Chrome on
// Android, or Bluefy on an iPhone -- Safari has no Web Bluetooth), taps
// CONNECT and picks MuleSkin-XXXX. While that page stays open, every alert
// the board raises on its own screen -- and every WITH YOU -- arrives as a
// phone notification. The board sends; it never takes anything from the
// phone but the connection itself.
//
// THE ADVERT. Connectable, carrying the service and the name, and up only
// while PHONE ALERTS is on and no phone is connected. It is rebuilt only when
// that changes -- a phone connecting or leaving, the switch -- never on a
// timer: advert churn is heap fragmentation on these boards. An update owns
// the radio while it runs, and the advert waits it out (pauseRadio()).
//
// THE COST. NimBLE's peripheral role, compiled into the CYD builds for this
// (nimble_flags_cyd in platformio.ini): about 3 KB of heap and 66 KB of
// flash, measured. The GATT service itself is registered only once PHONE
// ALERTS is switched on (off by default), so a board that never uses it pays
// no more than the role. The CYDs give up Bluetooth firmware updates for it
// (ota_ble.cpp): one GATT server, one job.
//
// WHO MAY LISTEN. With PHONE CODE off (the default), anyone in range who
// connects first. With it on, a phone has to write the four digits shown on
// that settings row within 30 seconds of connecting, three tries, or the
// board lets it go -- so a stranger cannot hold the one connection slot, and
// gets nothing while trying: alerts go only to a phone that gave the code,
// and the alert characteristic is notify-only, never readable.
#pragma once
#include <stdint.h>
#include "state.h"

namespace PhoneAlerts {

// Compiled in on this board (the CYDs: SQW_PHONE_ALERTS).
bool available();

// Registers the service. NimBLE will not do that while a scan runs, so the
// caller pauses the radios around it (main.cpp phoneAlertsStart()). Returns
// false if the scan did not stop in time; nothing changed then.
bool registerService();
bool registered();

// The settings row turned it on or off. Off drops a connected phone.
void setEnabled(bool on);

// A phone is connected right now -- and, for listening(), has given the code
// (or PHONE CODE is off), so alerts reach it.
bool connected();
bool listening();

// PHONE CODE changed: tells phones whether to ask, and lets a connected one
// go so it comes back through the new code.
void codeChanged();

// A phone may connect now: on, registered, nobody connected.
bool wantConnectable();

// From loop(): puts the advert up or takes it down when wantConnectable()
// changes. Cheap when nothing did.
void tick(uint32_t now);

// The update radio's start and end (detection.cpp): no advert in between.
void pauseRadio(bool paused);

// "MuleSkin-XXXX", the last two bytes of the Bluetooth address.
const char* name();

// One alert to the phone, if one is listening. followMins is 0 for an
// ordinary alert and the minutes for a WITH YOU. The same device again
// within a minute is not resent.
void alert(const Detection& d, uint16_t followMins);

// A line of text of the board's own ("TEST", the self-test).
void note(const char* text);

}  // namespace PhoneAlerts
