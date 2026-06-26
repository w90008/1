/* Sonic Loader — klog-driven kstuff auto-toggle.
   When a game (CUSA/PPSA title) is detected on /dev/klog, we pause
   kstuff after a configurable delay and resume it after a configurable
   delay following the game's exit. */

#pragma once

void kmonitor_start(void);

/* Settings API used by the web UI's /api/state endpoint. All return
   functions are safe to call from any thread. */
int  kmonitor_kstuff_supported(void);
int  kmonitor_kstuff_is_enabled(void);          /* 1=on, 0=off, -1=unknown */
int  kmonitor_kstuff_set(int on);                /* 0=success, -1=failure */

/* The auto-toggle is the klog-driven pause/resume that the kmonitor
   thread performs on game launch/exit. Turning it off does NOT touch
   the kstuff state itself; kstuff stays in whatever state it is in.
   This is what the UI's "kstuff" toggle actually controls. */
int  kmonitor_auto_toggle_enabled(void);
void kmonitor_set_auto_toggle(int on);

void kmonitor_get_delays(int *pause_seconds, int *resume_seconds);
void kmonitor_set_delays(int pause_seconds, int resume_seconds);
