#pragma once

#include <microhttpd.h>

/* Reload persisted notifications from /data/sonic-loader/notifications.json
   if the file exists. Call once during boot, BEFORE any notif_inbox_push()
   so the snapshot survives reboots — only an explicit "Clear all" from the
   UI wipes the file. */
void              notif_inbox_init(void);

void              notif_inbox_push(const char *msg);
enum MHD_Result   notif_inbox_request(struct MHD_Connection *c, const char *url);
