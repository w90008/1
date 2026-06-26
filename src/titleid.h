/* Sonic Loader — recognised PS-family title-ID prefixes.

   Every Sony console family uses a 4-letter region+publisher prefix
   followed by a 5-digit serial. PS5 firmware reports running big-app
   titles in the bare separator-free form (CUSA12345 / PPSA12345),
   but cheat files in the wild are often named with a dash
   (SLUS-12345) — particularly the older PS1/PS2 catalogues that long
   predate the PSN-era PSXX schema. This header centralises both the
   prefix allowlist and the normalisation step so cheats.c, appdb.c
   and kmonitor.c can't drift out of sync. */

#pragma once

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static const char * const sonic_titleid_prefixes[] = {
  "CUSA", "PPSA",                                    /* PS4, PS5 native */
  "ULUS", "ULES", "ULJS", "ULKS",                    /* PSP */
  "SLUS", "SCUS", "SLES", "SCES",                    /* PS1/PS2 */
  "SLPS", "SLPM", "SCED", "SLED", "SCPS",            /* PS1/PS2 (JP/EU/extra) */
  NULL
};


/* Returns 1 if `s` looks like a recognised title-ID, optionally
   writing the normalised 9-char form (uppercase, separator-free) into
   `norm` (must be at least 10 bytes). Accepts:

     CUSA12345
     CUSA-12345
     cusa_12345
     SLUS-00001     -> "SLUS00001"

   Trailing characters past the digits are ignored — useful for the
   klog scanner that wants to peel a title-ID out of a longer line. */
static inline int
title_id_normalize(const char *s, char norm[10]) {
  if(!s) return 0;
  while(*s == ' ' || *s == '\t') s++;
  if(strlen(s) < 9) return 0;

  char up[5];
  for(int i = 0; i < 4; i++) {
    if(!isalpha((unsigned char)s[i])) return 0;
    up[i] = (char)toupper((unsigned char)s[i]);
  }
  up[4] = 0;

  int matched = 0;
  for(const char * const *p = sonic_titleid_prefixes; *p; p++) {
    if(memcmp(up, *p, 4) == 0) { matched = 1; break; }
  }
  if(!matched) return 0;

  const char *digits = s + 4;
  if(*digits == '-' || *digits == '_') digits++;

  for(int i = 0; i < 5; i++) {
    if(!isdigit((unsigned char)digits[i])) return 0;
  }

  if(norm) {
    memcpy(norm, up, 4);
    memcpy(norm + 4, digits, 5);
    norm[9] = 0;
  }
  return 1;
}
