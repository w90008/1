// Sonic Loader — itsPLK/ps5-y2jb-autoloader integration.
//
// Finds every ps5_autoloader/ directory the autoloader scans
// (USB / /data / per-title), reports which Sonic Loader variant
// lives in each one, and replaces the ELF in place with the
// latest release pulled from git.etawen.dev.
//
// Scan order matches autoload.js:224-233 in itsPLK/y2jb master:
//   /mnt/usb[0-7]/ps5_autoloader[_TITLE]/
//   /data/ps5_autoloader[_TITLE]/
// YT-sandbox style paths are intentionally NOT scanned.

#pragma once

#include <microhttpd.h>

// Web endpoints. Routed by /api/y2jb prefix in websrv.c.
enum MHD_Result y2jb_request(struct MHD_Connection *conn, const char *url);

// Spawn the silent-verify background thread. Sleeps ~30 s after
// boot (so the network stack and other init phases have settled),
// then compares every detected ps5_autoloader/ ELF against the
// latest release on git.etawen.dev and overwrites only the
// stale ones. Failures log to stderr and are otherwise ignored.
void y2jb_startup_init(void);
