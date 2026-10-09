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
// THE ADVERTISER. There is one, and the squad's advert already owns it. So
// rather than a second advert (whose churn is exactly the heap fragmentation
// detection.cpp's setAdvertising() was rewritten to stop), the mesh advert
// itself turns connectable while PHONE ALERTS is on and no phone is
// connected, and goes back to non-connectable the moment one is. With the
// squad switched off it advertises the name alone, so a phone can still find
// it.
//
// THE COST. NimBLE's peripheral role, compiled into the CYD builds for this
// (nimble_flags_cyd in platformio.ini): about 3 KB of heap and 66 KB of
// flash, measured. The GATT service itself is registered only once PHONE
// ALERTS is switched on (off by default), so a board that never uses it pays
// no more than the role. The CYDs give up Bluetooth firmware updates for it
// (ota_ble.cpp): one GATT server, one job.
//
// WHO MAY LISTEN. Anyone in range who connects first -- there is no pairing.
// What they would learn is what the board is alerting about, which is worth
// knowing and not worth a pairing ritual on a resistive screen. The one
// connection slot is the real limit: a stranger connected is you not
// connected, and the settings row says CONNECTED whenever someone is.
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

// A phone is connected right now.
bool connected();

// The mesh advert should accept a connection now: on, registered, nobody
// connected. Read by detection.cpp's setAdvertising() on every pass.
bool wantConnectable();

// "MuleSkin-XXXX", the last two bytes of the Bluetooth address.
const char* name();

// One alert to the phone, if one is listening. followMins is 0 for an
// ordinary alert and the minutes for a WITH YOU. The same device again
// within a minute is not resent.
void alert(const Detection& d, uint16_t followMins);

// A line of text of the board's own ("TEST", the self-test).
void note(const char* text);

}  // namespace PhoneAlerts
