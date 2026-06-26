/* Copyright (C) 2026 soniciso

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

#include <microhttpd.h>


/**
 * Serve /api/tmdb/<TITLEID_00> or /api/tmdb/<CONTENTID>.
 *
 * Fetches store.playstation.com's product page over HTTPS, parses the
 * embedded schema.org JSON-LD block, and returns a TMDB-shaped JSON
 * envelope (names[], contentId, npTitleId, description, category, icon).
 * Results are cached on /data/sonic-loader/tmdb/<TITLEID>.json with a
 * 30-day TTL; refresh by deleting the cache file or appending ?refresh=1.
 *
 * For bare title ids, walks a small region-prefix × trailing-label
 * matrix to discover the contentId — same approach the host-side
 * ps5_store_lookup.py uses. Retail PS5 titles whose 16-char trailing
 * slug isn't one of the templates won't resolve from a bare title id;
 * pass the full contentId in that case.
 **/
enum MHD_Result tmdb_request(struct MHD_Connection *conn, const char *url);
