/* Sonic Loader — fan threshold control. */

#pragma once

#include <microhttpd.h>

enum MHD_Result fan_request(struct MHD_Connection *conn, const char *url);

/* Spawn the background watcher thread that re-applies the pinned
   threshold every 15 s. Called once at startup. The PS5 firmware
   resets fan-threshold state on every app/game launch, so the watcher
   keeps the user's value in place without further input. */
void fan_init(void);

/* Get / set the current pinned threshold. Used by the persistent
   config (config.c) so the value survives a payload redeploy. */
int  fan_pinned_threshold(void);
void fan_pin_threshold(int temp_c);
