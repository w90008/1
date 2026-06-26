/* Copyright (C) 2026 soniciso

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

#include <microhttpd.h>

/* Generic auto-updater for third-party PS5 payloads loaded by
   ps5-y2jb-autoloader. Driven by a JSON registry at
   /data/sonic-loader/managed-payloads.json. Ships with one default
   entry (itsPLK/ps5-payload-manager); add more by editing the JSON.

   Routes:
     GET  /api/payloads/list             registry + state per entry
     GET  /api/payloads/refresh-latest   re-poll GitHub for tags
     GET  /api/payloads/update?name=X    install latest for one entry
     GET  /api/payloads/auto-toggle?name=X&on=0|1   flip on-boot flag */
enum MHD_Result payload_registry_request(struct MHD_Connection *conn,
                                         const char *url);

/* Boot-time hook: bootstrap the default registry if missing, then for
   every entry whose auto_update_on_boot is true and is already
   installed somewhere on disk, fetch + install the latest release.
   Best-effort, non-blocking; no-ops if /data isn't writable yet. */
void payload_registry_boot_update(void);
