// MuleSkin-CYD — settings backup and restore over the console.
//
// The web flasher's BACK UP SETTINGS and RESTORE SETTINGS buttons speak this,
// so a reinstall that erases the board (or a second board) can have every
// saved setting put back: WiFi networks, colour order, zone, the mesh, the PIN.
//
//   NVS EXPORT                  one line per setting, then "NVS END <n>"
//       NVS <ns> <key> <tt> <hex>     tt = the nvs_type_t in hex, the value
//                                     as hex bytes (integers little-endian)
//       NVS SKIP <ns> <key> <bytes>   too big for a line (a log, a table)
//   NVS SET <ns> <key> <tt> <hex>   writes one back; answers "[nvs] ok" or
//                                   "[nvs] bad ..."
//   NVS RESTART                     restarts so the restored settings load
//
// A sender must pace long lines (the flasher sends 64 bytes every 4 ms): the
// UART's 128-byte hardware FIFO overflows at 2 Mbaud while the board draws,
// and what is lost is the START of the line. Measured: unpaced, every line
// over ~128 characters arrived without its "NVS " and was refused.
//
// The ESP-IDF's own namespaces (nvs.net80211, phy calibration) are left out:
// they belong to the radio of the board they came from. A locked board takes
// none of this -- the console refuses every command until it is unlocked.
#pragma once

namespace NvsBackup {
// True when `line` was one of the NVS commands (handled, answered).
bool handle(const char* line);
}
