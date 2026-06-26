/* Copyright (C) 2024 John Törnblom

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

#include <unistd.h>


pid_t hbldr_launch(const char* cwd, const char* path, int stdio, char** argv,
		   char** envp);

/* Diagnostic accessors — populated when hbldr_launch returns < 0. The
   /hbldr endpoint reads these on 503 and includes them in the JSON
   response body so the web UI can show "fakeapp_create_if_missing:
   Permission denied" instead of a bare HTTP code. */
const char *hbldr_last_error(void);
int         hbldr_last_errno(void);

