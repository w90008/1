/* Sonic Loader — homebrew payload + launcher-PKG installer.

   /api/homebrew/list                          list bundled homebrew assets
   /api/homebrew/install?asset=NAME.zip        download & extract to /data/homebrew/
   /api/homebrew/launcher                      download & install HOMEBREWLOADER01.pkg
   /api/homebrew/pkgs                          list staged .pkg files
   /api/homebrew/install-pkg?path=…            install a local .pkg
   /api/homebrew/install-pkg-url?url=…         install a remote .pkg via DPI
   /api/homebrew/install-pkg-upload (POST)     stream a .pkg from the browser
                                                  to /data/sonic-loader/pkgs/uploaded/
                                                  and optionally auto-install
   /api/homebrew/dpi-status                    DPI daemon health probe
   /api/homebrew/dpi-start                     spawn DPI if not running */

#pragma once

#include <microhttpd.h>
#include <stddef.h>

enum MHD_Result homebrew_request(struct MHD_Connection *conn, const char *url);

/* Boot-time hook: rm -rf everything inside /data/sonic-loader/pkgs/
   (the dir itself stays, recreated 0755). Sonic Loader does not
   retain user PKGs across payload re-sends. */
void homebrew_wipe_staged_pkgs(void);

/* Spawn a background thread that downloads + installs the
   sonic-loader-tile.pkg every loader boot. ~30 s sleep up front so
   the network stack has settled before the http_get fires. Failures
   are stderr-logged only — no UI noise. The PS5 installer accepts
   reinstalling the same contentId, so running this every boot is
   idempotent and just keeps the home-screen tile in sync with the
   latest published PKG.

   Gated by the user-controlled tile_autoinstall flag (default-on,
   persisted in /data/sonic-loader/config.ini). The thread reads the
   flag at the top and bails before any network I/O if disabled. */
void homebrew_auto_install_tile_init(void);

/* User-visible toggle that controls whether boot-time tile auto-install
   runs at all. Default 1. Setter persists to config.ini and, when
   flipped off→on, kicks off an immediate install run so the user
   doesn't have to reboot to see the tile appear. */
int  homebrew_tile_autoinstall_enabled(void);
void homebrew_tile_autoinstall_set_enabled(int on);

/* Streaming-POST handler for /api/homebrew/install-pkg-upload. The websrv
   per-connection state machine drives this — same shape as
   avatar_upload_request. Pass &req->pkg_upload_state in. */
enum MHD_Result pkg_upload_request(struct MHD_Connection *conn,
                                   const char *upload_data,
                                   size_t *upload_data_size,
                                   void **state);
void pkg_upload_free(void *state);
