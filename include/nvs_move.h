// MuleSkin-CYD — moving settings from the old 20 KB store to the new one.
//
// Up to v3.1.8 every partition table put NVS (the settings store) at 0x9000,
// 20 KB. On 2026-10-10 a CYD's store was found erased whole at boot: full,
// the Arduino core's answer to "no free pages" is to wipe it -- every
// setting, WiFi network, push token and the touch calibration. The store now
// lives further up the flash and is 128 KB (see partitions_ota.csv).
//
// A partition table only changes over USB. An install that does NOT erase
// leaves the old store's bytes at 0x9000, unclaimed by the new table, and the
// first boot on the new table copies every key across and then erases the old
// region -- so settings survive without a backup and restore. An install WITH
// erase leaves nothing to move; restore from the web flasher's backup then.
#pragma once

namespace NvsMove {

// Call first thing in setup(), before anything opens NVS. Returns the number
// of keys moved, 0 when there was nothing to move (the usual case: an old
// table, or already moved), or -1 when the move failed (the old store is left
// in place, so the next boot tries again).
int run();

}
