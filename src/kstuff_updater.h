/* Sonic Loader — kstuff-lite auto-updater.

   Pulls the latest kstuff.elf asset from
   https://github.com/EchoStretch/kstuff-lite/releases and writes it to
   /data/kstuff.elf so the next boot picks it up. */

#pragma once

#include <microhttpd.h>

enum MHD_Result kstuff_updater_request(struct MHD_Connection *conn,
                                       const char *url);

/* Direct (non-HTTP) install entry point, used by the boot-time auto
   installer. use_drakmor=1 pulls drakmor's fork; 0 pulls EchoStretch's.
   Returns 0 on success, -1 on failure. Writes /data/kstuff.elf. */
int kstuff_install_direct(int use_drakmor);
