/* Sonic Loader — sl2_log.db activity reader.

   Reads /system_data/priv/system_logger2/nobackup/database/sl2_log.db
   and surfaces session counts per title.

   Endpoint:
     GET /api/activitydb        — session count for hardcoded test title */

#pragma once

#include <microhttpd.h>

enum MHD_Result activitydb_request(struct MHD_Connection *conn, const char *url);
