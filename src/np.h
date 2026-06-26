/* Sonic Loader — np-fake-signin / np-account-restore launchers.

   Two on-demand sub-payloads bundled by Sonic Loader. Each gets a
   button in the Settings UI; clicking it spawns the embedded ELF.
   Both are read-only with respect to Sonic Loader itself — they touch
   the system registry / per-user dat files directly.

   Endpoints:
     GET /api/np/info           current foreground user + paths
     GET /api/np/fake-signin    spawn np-fake-signin.elf
     GET /api/np/restore        spawn np-restore-account.elf */

#pragma once

#include <microhttpd.h>

enum MHD_Result np_request(struct MHD_Connection *conn, const char *url);
