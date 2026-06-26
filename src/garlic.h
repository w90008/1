/* Sonic Loader — Garlic Worker + SaveMgr Settings endpoints.

   Endpoints:
     GET /api/garlic               JSON status of both daemons + poll interval
     GET /api/garlic/worker?on=0|1 Toggle the worker daemon
     GET /api/garlic/savemgr?on=0|1 Toggle the SaveMgr daemon
     GET /api/garlic/poll?seconds=N Set worker pollInterval (writes config.ini) */

#pragma once

#include <microhttpd.h>

enum MHD_Result garlic_request(struct MHD_Connection *conn, const char *url);
