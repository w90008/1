/* Sonic Loader — PSN avatar PNG/JPG → DDS pipeline.

   Mirrors earthonion/np-fake-signin/gen_dat/png_to_dds.py: takes any
   image, generates the four DXT5 .dds files (avatar64, avatar128,
   avatar260, avatar440) the system uses for profile avatars, lets the
   user preview each size in the browser, and on Apply copies the four
   .dds files to a target directory (typically the user's profile
   avatar folder). */

#pragma once

#include <microhttpd.h>
#include <stddef.h>

enum MHD_Result avatar_request(struct MHD_Connection *conn, const char *url);

/* Streaming POST handler for /api/avatar/upload — chunks the body to
   /data/sonic-loader/avatar/in/<name> and runs the full conversion on
   the final empty-body call. */
enum MHD_Result avatar_upload_request(struct MHD_Connection *conn,
                                      const char *upload_data,
                                      size_t *upload_data_size,
                                      void **state);

/* Connection-end cleanup if the browser bails mid-upload. */
void avatar_upload_free(void *state);
