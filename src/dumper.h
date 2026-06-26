/* Sonic Loader — EchoStretch/ps5-app-dumper bridge.

   GET /api/dumper/run            spawn ps5-app-dumper.elf detached.
   GET /api/dumper/seed-config    seed config.ini on every writable USB. */

#pragma once

#include <microhttpd.h>

enum MHD_Result dumper_request(struct MHD_Connection *conn, const char *url);

/* Idempotent: walk every /mnt/usbN, write
   <usb>/homebrew/PS5DumpRunner/config.ini if it's missing. Called once
   at boot so the user can edit skip_existing=1 BEFORE the first run. */
void dumper_seed_configs(void);
