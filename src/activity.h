/* Sonic Loader — local game-activity log.

   Driven by kmonitor's existing klog scanner. Every CUSA/PPSA title
   launch / exit event flows through activity_record_launch() /
   activity_record_exit() into an in-memory table that's persisted to
   /data/sonic-loader/activity.json after each change. Survives payload
   redeploys.

   Endpoints:
     GET  /api/activity                     — full map of all tracked titles
     GET  /api/activity/title?id=…          — single title
     POST /api/activity/reset[?id=…]        — wipe one title or all */

#pragma once

#include <microhttpd.h>

void activity_init(void);
void activity_record_launch(const char *title_id);
void activity_record_exit(const char *title_id);

enum MHD_Result activity_request(struct MHD_Connection *conn, const char *url);
