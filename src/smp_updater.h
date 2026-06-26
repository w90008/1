/* Sonic Loader — ShadowMountPlus install/update endpoint.

   GET /api/smp           info
   GET /api/smp/info      info
   GET /api/smp/install?version=103|104    fetch SMP from drakmor's GitHub
                                            and write to /user/data/
                                            shadowmount/shadowmountplus.elf
   GET /api/smp/reset     delete the user-installed copy so the embedded
                          fallback is used on next boot. */

#pragma once

#include <microhttpd.h>

enum MHD_Result smp_updater_request(struct MHD_Connection *conn,
                                    const char *url);

/* Direct (non-HTTP) install entry point, used by the boot-time auto
   installer. version="103" or "104". Returns 0 on success, -1 on
   failure. Writes /data/shadowmount/shadowmountplus.elf — sibling
   files in /data/shadowmount/ (config.ini, daemon.lock, smp_icon.png)
   are never touched. */
int smp_install_direct(const char *version);

/* SMP daemon on/off toggle. Mirrors the BackPork / etaHEN pattern:
   set_enabled(1) spawns the bundled SMP (or the user-installed override
   at /data/shadowmount/shadowmountplus.elf) if it isn't already running;
   set_enabled(0) kills any running instance via SIGTERM. Returns 1 if
   running afterwards, 0 if stopped, -1 on error. */
int sys_smp_set_enabled(int on);
int sys_smp_is_running(void);
/* Call cb(path, arg) for every active SMP scan path (defaults + manual). */
void smp_foreach_scan_path(void (*cb)(const char *, void *), void *arg);

/* SMP config.ini tunable getters/setters (persisted via config_save). */
int  smp_cfg_get_debug(void);
void smp_cfg_set_debug(int v);
int  smp_cfg_get_quiet_mode(void);
void smp_cfg_set_quiet_mode(int v);
int  smp_cfg_get_kstuff_auto_toggle(void);
void smp_cfg_set_kstuff_auto_toggle(int v);
int  smp_cfg_get_kstuff_crash_detection(void);
void smp_cfg_set_kstuff_crash_detection(int v);
int  smp_cfg_get_pause_delay_image(void);
void smp_cfg_set_pause_delay_image(int v);
int  smp_cfg_get_pause_delay_direct(void);
void smp_cfg_set_pause_delay_direct(int v);
/* Restart the daemon — used after the scan-path config changes so the
   new paths are picked up. Returns 0 on success, -1 on error. */
int sys_smp_restart(void);
