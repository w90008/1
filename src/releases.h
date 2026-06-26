/* Sonic Loader — GitHub releases proxy.

   /api/releases?repo=<user>/<name>
     Fetches https://api.github.com/repos/<repo>/releases?per_page=100
     through sceHttp on the PS5 side (browser-side fetch is CORS-blocked
     from sonic-loader's :6969 origin to api.github.com), parses the
     response, returns a slim JSON array suitable for the Settings UI:

     [
       {
         "tag":      "v1.04",
         "date":     "2026-05-02",
         "asset":    "kstuff.elf",
         "assetUrl": "https://github.com/.../v1.04/kstuff.elf",
         "zipOnly":  false
       }, ...
     ]

   Whitelisted repos only — this isn't a generic open proxy.
   30-minute in-memory cache so we don't hammer GitHub's rate limit. */

#pragma once

#include <microhttpd.h>

enum MHD_Result releases_request(struct MHD_Connection *conn, const char *url);

/* Start a detached background thread that auto-fetches the release
   list for every whitelisted repo that doesn't already have a cache
   file on disk. Retries each repo with a backoff (5s, 10s, 30s, 60s,
   120s thereafter) until the fetch succeeds. Once all repos are
   cached, the thread exits. Called once from main.c — subsequent
   boots see the disk cache and the thread no-ops immediately. */
void releases_init(void);
