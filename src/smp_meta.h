/* Sonic Loader — ShadowMountPlus metadata self-healer.
   Watches /user/app/<TITLE_ID>/ entries and copies icon0.png / pic*.png /
   param.json from each game's sce_sys/ into /user/appmeta/<TITLE_ID>/
   when missing. Runs as a background thread, polling at a configurable
   interval. SMP itself is closed-binary (downloaded per-release from
   GitHub) so this lives outside it — when SMP scan-mounts a new path
   without populating appmeta correctly (which is what causes the
   "missing icon on home" bug), the next sweep heals it. */

#pragma once

#include <stdint.h>

void smp_meta_init(void);

/* Stat snapshot exposed via /api/smp/meta. */
typedef struct {
  int      running;
  int      poll_seconds;
  uint64_t last_run_unix;
  int      games_scanned;
  int      icons_healed;
  int      pics_healed;
  int      json_healed;
  int      still_missing;
  char     last_missing[64];   /* TITLE_ID of most recent unfixable game */
} smp_meta_stats_t;

void smp_meta_get_stats(smp_meta_stats_t *out);

/* Web UI handles. */
int  smp_meta_run_now(void);                /* trigger an immediate sweep */
int  smp_meta_set_poll_seconds(int seconds);
int  smp_meta_get_poll_seconds(void);
