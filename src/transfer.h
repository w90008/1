/* Sonic Loader — file-manager copy/move/delete primitives + job tracker.

   Built around the same aio_read/aio_write overlap pattern that
   EchoStretch/ps5-app-dumper uses to saturate USB throughput on big
   files. All operations run on a worker thread so the UI can poll
   /api/fs/job/status while a long copy is in flight; a single global
   atomic-int lets the UI cancel mid-operation. */

#pragma once

#include <microhttpd.h>

/* Routes /api/fs/... — list / usb / copy / move / delete / mkdir /
   rename / job/status / job/cancel. */
enum MHD_Result transfer_request(struct MHD_Connection *conn, const char *url);

/* POST /api/fs/upload?path=/dest/dir&filename=foo.bin&relpath=sub/dir
   Streams the request body to <path>/<relpath>/<filename>. relpath is
   used by the folder-upload UI (webkitdirectory) to preserve the
   per-file subpath. The websrv per-connection state machine drives
   this — same shape as avatar_upload_request / pkg_upload_request. */
enum MHD_Result fs_upload_request(struct MHD_Connection *conn,
                                  const char *upload_data,
                                  size_t *upload_data_size,
                                  void **state);
void fs_upload_free(void *state);
