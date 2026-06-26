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

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <microhttpd.h>

#include "activity.h"
#include "activitydb.h"
#include "appdb.h"
#include "tmdb.h"
#include "asset.h"
#include "avatar.h"
#include "cheats.h"
#include "dashboards.h"
#include "fan.h"
#include "fs.h"
#include "homebrew.h"
#include "kmonitor.h"
#include "kstuff_updater.h"
#include "smp_updater.h"
#include "np.h"
#include "notif_inbox.h"
#include "garlic.h"
#include "offact.h"
#include "y2jb_updater.h"
#include "dumper.h"
#include "transfer.h"
#include "mdns.h"
#include "smb.h"
#include "sys.h"
#include "version.h"
#include "websrv.h"


typedef struct post_data {
  char *key;
  uint8_t *val;
  size_t len;
  struct post_data *next;
} post_data_t;


typedef struct post_request {
  struct MHD_PostProcessor* pp;
  post_data_t* data;
  void* cheats_state;       /* opaque per-connection state for /api/cheats/upload */
  void* avatar_upload_state; /* opaque per-connection state for /api/avatar/upload */
  void* pkg_upload_state;    /* opaque per-connection state for /api/homebrew/install-pkg-upload */
  void* fs_upload_state;     /* opaque per-connection state for /api/fs/upload */
  void* payload_upload_state;/* opaque per-connection state for /api/payloads/upload */
} post_request_t;


static post_data_t*
post_data_get(post_data_t* data, const char* key) {
  if(!data) {
    return 0;
  }

  if(!strcmp(key, data->key)) {
    return data;
  }

  return post_data_get(data->next, key);
}


static const char*
post_data_val(post_data_t* data, const char* key) {
  data = post_data_get(data, key);
  return data ? (const char*)data->val : 0;
}


static enum MHD_Result
post_iterator(void *cls, enum MHD_ValueKind kind, const char *key,
               const char *filename, const char *mime, const char *encoding,
               const char *value, uint64_t off, size_t size) {
  post_request_t *req = cls;
  post_data_t *data = post_data_get(req->data, key);

  if(data) {
    data->val = realloc(data->val, off+size+1);
  } else {
    data = malloc(sizeof(post_data_t));
    data->next = req->data;
    data->key = strdup(key);
    data->val = malloc(off+size+1);
    data->len = 0;
    req->data = data;
  }

  memcpy(data->val+off, value, size);
  data->val[off+size] = 0;
  data->len += size;

  return MHD_YES;
}


enum MHD_Result
websrv_queue_response(struct MHD_Connection *conn, unsigned int status,
		      struct MHD_Response *resp) {
  MHD_add_response_header(resp, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
  			  "*");

  return MHD_queue_response(conn, status, resp);
}



/**
 * Respond to a version request.
 **/
static enum MHD_Result
version_request(struct MHD_Connection *conn) {
  size_t size = strlen(PAGE_VERSION);
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  void* data = PAGE_VERSION;

  if((resp=MHD_create_response_from_buffer(size, data,
					   MHD_RESPMEM_PERSISTENT))) {
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                            "application/json");
    ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
  }

  return ret;
}


/**
 * Respond to a settings-state request.
 *
 *   GET /api/state                              -> read current state
 *   GET /api/state?auto=0|1                     -> turn klog auto-toggle on/off
 *   GET /api/state?kstuff=0|1                   -> directly flip kstuff state
 *   GET /api/state?pause=N&resume=M             -> set pause/resume delays
 *   GET /api/state?cheats=0|1                   -> master cheat-engine on/off
 *
 * All combinations are accepted in the same request. Always returns the
 * fresh JSON state so the UI can re-render off the response.
 **/
extern int  cheats_engine_enabled(void);
extern void cheats_engine_set_enabled(int on);
extern int  cheats_game_running(void);

static enum MHD_Result
state_request(struct MHD_Connection *conn) {
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  const char *auto_arg;
  const char *kstuff_arg;
  const char *pause_arg;
  const char *resume_arg;
  const char *cheats_arg;
  const char *backpork_arg;
  const char *lapyjb_arg;
  const char *nanodns_arg;
  const char *tile_autoinstall_arg;
  int pause_secs = 25;
  int resume_secs = 10;
  int kstuff_supported;
  int kstuff_state;
  int auto_state;
  int cheats_state;
  int game_running;
  int backpork_state;
  int lapyjb_state;
  int nanodns_state;
  int tile_autoinstall_state;
  char body[1024];
  size_t body_len;

  auto_arg     = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "auto");
  kstuff_arg   = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "kstuff");
  pause_arg    = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "pause");
  resume_arg   = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "resume");
  cheats_arg   = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "cheats");
  backpork_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "backpork");
  lapyjb_arg   = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "lapyjb");
  nanodns_arg  = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "nanodns");
  tile_autoinstall_arg =
                 MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "tileAutoinstall");

  if(auto_arg) {
    kmonitor_set_auto_toggle(strcmp(auto_arg, "0") != 0);
  }
  if(kstuff_arg) {
    kmonitor_kstuff_set(strcmp(kstuff_arg, "0") != 0);
  }
  if(cheats_arg) {
    /* Refuse to flip the master cheat switch ON from outside a running
       game. Persisted "on" from config_load() goes through the setter
       directly and bypasses this check (config.c → cheats.c), which is
       what we want — last-session state restores even before the user
       has launched a title yet. The UI mirrors this gate by disabling
       the toggle when /api/state reports gameRunning=false. */
    int want_cheats = strcmp(cheats_arg, "0") != 0;
    if(!want_cheats || cheats_game_running()) {
      cheats_engine_set_enabled(want_cheats);
    }
  }
  if(backpork_arg) {
    sys_backpork_set_enabled(strcmp(backpork_arg, "0") != 0);
  }
  if(lapyjb_arg) {
    sys_lapyjb_set_enabled(strcmp(lapyjb_arg, "0") != 0);
  }
  if(nanodns_arg) {
    sys_nanodns_set_enabled(strcmp(nanodns_arg, "0") != 0);
  }
  if(tile_autoinstall_arg) {
    homebrew_tile_autoinstall_set_enabled(strcmp(tile_autoinstall_arg, "0") != 0);
  }

  if(pause_arg || resume_arg) {
    int cur_pause = 25, cur_resume = 10;
    kmonitor_get_delays(&cur_pause, &cur_resume);
    int new_pause  = pause_arg  ? atoi(pause_arg)  : cur_pause;
    int new_resume = resume_arg ? atoi(resume_arg) : cur_resume;
    kmonitor_set_delays(new_pause, new_resume);
  }

  kmonitor_get_delays(&pause_secs, &resume_secs);
  kstuff_supported = kmonitor_kstuff_supported();
  kstuff_state     = kmonitor_kstuff_is_enabled();
  auto_state       = kmonitor_auto_toggle_enabled();
  cheats_state     = cheats_engine_enabled();
  game_running     = cheats_game_running();
  backpork_state   = sys_backpork_is_running();
  lapyjb_state     = sys_lapyjb_is_running();
  nanodns_state    = sys_nanodns_is_running();
  tile_autoinstall_state = homebrew_tile_autoinstall_enabled();

  body_len = (size_t)snprintf(body, sizeof(body),
    "{\"kstuffSupported\":%s,"
    "\"kstuffEnabled\":%s,"
    "\"autoToggleEnabled\":%s,"
    "\"cheatsEnabled\":%s,"
    "\"gameRunning\":%s,"
    "\"backporkRunning\":%s,"
    "\"lapyjbRunning\":%s,"
    "\"lapyjbSupported\":%s,"
    "\"nanodnsRunning\":%s,"
    "\"tileAutoinstallEnabled\":%s,"
    "\"pauseAfterSeconds\":%d,"
    "\"resumeAfterSeconds\":%d}",
    kstuff_supported ? "true" : "false",
    (kstuff_state == 1) ? "true" : "false",
    auto_state ? "true" : "false",
    cheats_state ? "true" : "false",
    game_running ? "true" : "false",
    backpork_state ? "true" : "false",
    lapyjb_state ? "true" : "false",
    "true",   /* lapyjbSupported — always bundled in this build */
    nanodns_state ? "true" : "false",
    tile_autoinstall_state ? "true" : "false",
    pause_secs, resume_secs);

  if((resp=MHD_create_response_from_buffer(body_len, body,
                                           MHD_RESPMEM_MUST_COPY))) {
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                            "application/json");
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CACHE_CONTROL, "no-cache");
    ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
  }

  return ret;
}


/**
 * Respond to a launch request.
 **/
static enum MHD_Result
launch_request(struct MHD_Connection *conn) {
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  const char* title_id;
  unsigned int status;
  const char *args;

  title_id = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "titleId");
  args = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "args");

  if(!title_id) {
    status = MHD_HTTP_BAD_REQUEST;
  } else if(sys_launch_title(title_id, args)) {
    status = MHD_HTTP_SERVICE_UNAVAILABLE;
  } else {
    status = MHD_HTTP_OK;
  }

  if((resp=MHD_create_response_from_buffer(0, "",
					   MHD_RESPMEM_PERSISTENT))) {
    ret = websrv_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
  }

  return ret;
}


/**
 * Respond to a homebrew loading request.
 **/
static enum MHD_Result
hbldr_request(struct MHD_Connection *conn) {
  int (*sys_launch)(const char*, const char*, const char*, const char*) = 0;
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  const char* daemon;
  const char* path;
  const char *args;
  const char *pipe;
  const char *env;
  const char *cwd;
  int fd;

  path = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "path");
  args = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "args");
  env = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "env");
  pipe = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "pipe");
  cwd = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "cwd");
  daemon = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "daemon");

  if(daemon && strcmp(daemon, "0")) {
    sys_launch = sys_launch_daemon;
  } else {
    sys_launch = sys_launch_homebrew;
  }

  if(!path) {
    if((resp=MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT))) {
      ret = websrv_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
      MHD_destroy_response(resp);
    }
  } else if((fd=sys_launch(cwd, path, args, env)) < 0) {
    /* Surface the specific failure stage from hbldr.c so the web UI
       can show "no signed-in user" / "ELF missing" / etc. instead of
       a bare 503 with no body. */
    extern const char *hbldr_last_error(void);
    const char *why = hbldr_last_error();
    char json[512];
    int n = snprintf(json, sizeof(json),
        "{\"ok\":false,\"error\":\"%s\",\"path\":\"%s\"}",
        (why && *why) ? why : "launch failed (no diagnostic)",
        path ? path : "");
    char *out = malloc(n + 1);
    if(!out) {
      if((resp=MHD_create_response_from_buffer(0, "",
                                               MHD_RESPMEM_PERSISTENT))) {
        ret = websrv_queue_response(conn, MHD_HTTP_SERVICE_UNAVAILABLE, resp);
        MHD_destroy_response(resp);
      }
    } else {
      memcpy(out, json, n + 1);
      if((resp=MHD_create_response_from_buffer(n, out,
                                               MHD_RESPMEM_MUST_FREE))) {
        MHD_add_response_header(resp, "Content-Type", "application/json");
        ret = websrv_queue_response(conn, MHD_HTTP_SERVICE_UNAVAILABLE, resp);
        MHD_destroy_response(resp);
      } else {
        free(out);
      }
    }
  } else if(pipe && strcmp(pipe, "0")) {
    if((resp=MHD_create_response_from_pipe(fd))) {
      MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, "text/x-log; charset=utf-8");
      MHD_add_response_header(resp, MHD_HTTP_HEADER_CACHE_CONTROL, "no-cache");
      ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
      MHD_destroy_response(resp);
    }
  } else {
    close(fd);
    if((resp=MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT))) {
      ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
      MHD_destroy_response(resp);
    }
  }

  return ret;
}



/**
 * Respond to a ELF payload loading request.
 **/
static enum MHD_Result
elfldr_request(struct MHD_Connection *conn, post_data_t *data) {
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  const char *args;
  const char *pipe;
  const char *env;
  const char *cwd;
  const char *uri;
  int fd = -1;

  if(!(args=MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "args"))) {
    args = post_data_val(data, "args");
  }
  if(!(env=MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "env"))) {
    env = post_data_val(data, "env");
  }
  if(!(pipe=MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "pipe"))) {
    pipe = post_data_val(data, "pipe");
  }
  if(!(cwd=MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "cwd"))) {
    cwd = post_data_val(data, "cwd");
  }

  if((uri=MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "elf"))) {
    fd = sys_launch_daemon(cwd, uri, args, env);
  } else if((data=post_data_get(data, "elf"))) {
    fd = sys_launch_payload(cwd, data->val, data->len, args, env);
  } else {
    return asset_request(conn, "/elfldr.html");
  }

  if(fd < 0) {
    if((resp=MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT))) {
      ret = websrv_queue_response(conn, MHD_HTTP_BAD_REQUEST, resp);
      MHD_destroy_response(resp);
    }
    return ret;
  }

  if(pipe && strcmp(pipe, "0")) {
    if((resp=MHD_create_response_from_pipe(fd))) {
      MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, "text/x-log; charset=utf-8");
      MHD_add_response_header(resp, MHD_HTTP_HEADER_CACHE_CONTROL, "no-cache");
      ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
      MHD_destroy_response(resp);
    }
  } else {
    close(fd);
    if((resp=MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT))) {
      ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
      MHD_destroy_response(resp);
    }
  }

  return ret;
}


/**
 *
 **/
static enum MHD_Result
websrv_on_request(void *cls, struct MHD_Connection *conn,
                  const char *url, const char *method,
                  const char *version, const char *upload_data,
                  size_t *upload_data_size, void **con_cls) {
  post_request_t *req = *con_cls;
  enum MHD_Result ret = MHD_NO;

  if(strcmp(method, MHD_HTTP_METHOD_GET) &&
     strcmp(method, MHD_HTTP_METHOD_POST) &&
     strcmp(method, MHD_HTTP_METHOD_HEAD)) {
    return MHD_NO;
  }

  if(!req) {
    req = *con_cls = malloc(sizeof(post_request_t));
    req->pp = MHD_create_post_processor(conn, 0x1000, &post_iterator, req);
    req->data = 0;
    req->cheats_state = NULL;
    req->avatar_upload_state = NULL;
    req->pkg_upload_state = NULL;
    req->fs_upload_state = NULL;
    req->payload_upload_state = NULL;
    return MHD_YES;
  }

  if(!strcmp(method, MHD_HTTP_METHOD_GET)) {
    if(!strcmp("/fs", url)) {
      return fs_request(conn, url);
    }
    if(!strncmp("/fs/", url, 4)) {
      return fs_request(conn, url);
    }
#ifdef __SCE__
    if(!strcmp("/mdns", url)) {
      return mdns_request(conn, url);
    }
    if(!strncmp("/smb", url, 4)) {
      return smb_request(conn, url);
    }
#endif
    if(!strcmp("/launch", url)) {
      return launch_request(conn);
    }
    if(!strcmp("/api/state", url)) {
      return state_request(conn);
    }
    if(!strncmp("/api/cheats", url, 11)) {
      return cheats_request(conn, url, method, NULL, NULL, &req->cheats_state);
    }
    if(!strncmp("/api/homebrew", url, 13)) {
      return homebrew_request(conn, url);
    }
    if(!strncmp("/api/avatar", url, 11)) {
      return avatar_request(conn, url);
    }
    if(!strncmp("/api/kstuff", url, 11)) {
      return kstuff_updater_request(conn, url);
    }
    if(!strncmp("/api/smp", url, 8)) {
      return smp_updater_request(conn, url);
    }
    if(!strncmp("/api/y2jb", url, 9)) {
      return y2jb_request(conn, url);
    }
    if(!strncmp("/api/np", url, 7)) {
      return np_request(conn, url);
    }
    if(!strncmp("/api/garlic", url, 11)) {
      return garlic_request(conn, url);
    }
    if(!strncmp("/api/dumper/", url, 12)) {
      return dumper_request(conn, url);
    }
    if(!strncmp("/api/fs/", url, 8)) {
      return transfer_request(conn, url);
    }
    if(!strncmp("/api/fan", url, 8)) {
      return fan_request(conn, url);
    }
    if(!strncmp("/api/notifications", url, 18)) {
      return notif_inbox_request(conn, url);
    }
    if(!strncmp("/api/klogs", url, 10)) {
      return dashboards_klogs_request(conn, url);
    }
    if(!strcmp("/api/stats", url)) {
      return dashboards_stats_request(conn);
    }
    /* /api/activitydb MUST be matched before /api/activity (length-13
       prefix would otherwise catch both and route activitydb hits to
       the wrong handler). */
    if(!strncmp("/api/activitydb", url, 15)) {
      return activitydb_request(conn, url);
    }
    if(!strncmp("/api/activity", url, 13)) {
      return activity_request(conn, url);
    }
    if(!strncmp("/api/releases", url, 13)) {
      extern enum MHD_Result releases_request(struct MHD_Connection*, const char*);
      return releases_request(conn, url);
    }
    if(!strncmp("/api/pkgzone/", url, 13)) {
      extern enum MHD_Result pkgzone_request(struct MHD_Connection*, const char*);
      return pkgzone_request(conn, url);
    }
    if(!strncmp("/api/payloads/", url, 14)) {
      extern enum MHD_Result payload_registry_request(struct MHD_Connection*, const char*);
      return payload_registry_request(conn, url);
    }
    if(!strncmp("/api/ftpsrv", url, 11)) {
      /* /api/ftpsrv          { running, port, user, type, ... }
         /api/ftpsrv/toggle?on=0|1
         /api/ftpsrv/restart
         /api/ftpsrv/port?value=N            (set + restart)
         /api/ftpsrv/auth?user=&pass=         (set + restart)
         /api/ftpsrv/type?value=auto|binary|ascii (set + restart)
         user="anonymous" + empty pass = legacy open access. */
      char body[512];
      int len = 0;
      if(!strcmp(url, "/api/ftpsrv")) {
        const char *u = sys_ftpsrv_get_user();
        const char *p = sys_ftpsrv_get_pass();
        const char *t = sys_ftpsrv_get_type();
        int has_auth = (u && *u && strcasecmp(u, "anonymous") != 0);
        len = snprintf(body, sizeof(body),
            "{\"ok\":true,\"running\":%s,\"port\":%d,"
            "\"user\":\"%s\",\"pass\":\"%s\",\"hasPassword\":%s,"
            "\"transferDefault\":\"%s\","
            "\"authMode\":\"%s\","
            "\"authConfigurable\":true,\"transferConfigurable\":true}",
            sys_ftpsrv_is_running() ? "true" : "false",
            sys_ftpsrv_get_port(),
            u ? u : "anonymous",
            p ? p : "",
            p && p[0] ? "true" : "false",
            t ? t : "auto",
            has_auth ? "userpass" : "anonymous");
      } else if(!strcmp(url, "/api/ftpsrv/toggle")) {
        const char *on = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "on");
        int want = (on && (!strcmp(on, "1") || !strcasecmp(on, "true"))) ? 1 : 0;
        int rc = sys_ftpsrv_set_enabled(want);
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"running\":%s,\"port\":%d}",
            rc >= 0 ? "true" : "false",
            sys_ftpsrv_is_running() ? "true" : "false",
            sys_ftpsrv_get_port());
      } else if(!strcmp(url, "/api/ftpsrv/restart")) {
        int rc = sys_ftpsrv_restart();
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"running\":%s,\"port\":%d}",
            rc == 0 ? "true" : "false",
            sys_ftpsrv_is_running() ? "true" : "false",
            sys_ftpsrv_get_port());
      } else if(!strcmp(url, "/api/ftpsrv/port")) {
        const char *v = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "value");
        int port = v ? atoi(v) : 0;
        if(port < 1 || port > 65535) {
          const char *err = "{\"ok\":false,\"error\":\"port must be 1..65535\"}";
          struct MHD_Response *r =
              MHD_create_response_from_buffer(strlen(err), (void*)err,
                                              MHD_RESPMEM_PERSISTENT);
          MHD_add_response_header(r, "Content-Type", "application/json");
          enum MHD_Result rc = websrv_queue_response(conn, 400, r);
          MHD_destroy_response(r);
          return rc;
        }
        sys_ftpsrv_set_port(port);
        extern void config_save(void);
        config_save();
        int rc = sys_ftpsrv_restart();
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"running\":%s,\"port\":%d,\"restarted\":%s}",
            rc == 0 ? "true" : "false",
            sys_ftpsrv_is_running() ? "true" : "false",
            port,
            rc == 0 ? "true" : "false");
      } else if(!strcmp(url, "/api/ftpsrv/auth")) {
        const char *user = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "user");
        const char *pass = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "pass");
        sys_ftpsrv_set_user(user);  /* empty / "anonymous" -> open */
        sys_ftpsrv_set_pass(pass);
        extern void config_save(void); config_save();
        int rc = sys_ftpsrv_restart();
        const char *u = sys_ftpsrv_get_user();
        const char *p = sys_ftpsrv_get_pass();
        int has_auth = (u && *u && strcasecmp(u, "anonymous") != 0);
        int has_pass = p && p[0] != '\0';
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"running\":%s,\"user\":\"%s\","
            "\"pass\":\"%s\",\"hasPassword\":%s,"
            "\"authMode\":\"%s\",\"restarted\":%s}",
            rc == 0 ? "true" : "false",
            sys_ftpsrv_is_running() ? "true" : "false",
            u ? u : "anonymous",
            p ? p : "",
            has_pass ? "true" : "false",
            has_auth ? "userpass" : "anonymous",
            rc == 0 ? "true" : "false");
      } else if(!strcmp(url, "/api/ftpsrv/type")) {
        const char *v = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "value");
        sys_ftpsrv_set_type(v ? v : "auto");
        extern void config_save(void); config_save();
        int rc = sys_ftpsrv_restart();
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"running\":%s,\"transferDefault\":\"%s\","
            "\"restarted\":%s}",
            rc == 0 ? "true" : "false",
            sys_ftpsrv_is_running() ? "true" : "false",
            sys_ftpsrv_get_type(),
            rc == 0 ? "true" : "false");
      } else {
        const char *err = "{\"ok\":false,\"error\":\"no such endpoint\"}";
        struct MHD_Response *r =
            MHD_create_response_from_buffer(strlen(err), (void*)err,
                                            MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(r, "Content-Type", "application/json");
        enum MHD_Result rc = websrv_queue_response(conn, 404, r);
        MHD_destroy_response(r);
        return rc;
      }
      char *out = malloc(len + 1);
      if(!out) return MHD_NO;
      memcpy(out, body, len + 1);
      struct MHD_Response *r =
          MHD_create_response_from_buffer(len, out, MHD_RESPMEM_MUST_FREE);
      MHD_add_response_header(r, "Content-Type", "application/json");
      enum MHD_Result rc = websrv_queue_response(conn, 200, r);
      MHD_destroy_response(r);
      return rc;
    }
    if(!strncmp("/api/offact", url, 11)) {
      /* /api/offact                  -> array of all 16 user slots
         /api/offact/activate?slot=N&id=0xHEX  -> write id/type/flags
         /api/offact/clear?slot=N     -> zero id+flags
         /api/offact/rename?slot=N&name=... -> overwrite display name */
      char body[4096];
      int len = 0;
      if(!strcmp(url, "/api/offact")) {
        len = snprintf(body, sizeof(body), "{\"ok\":true,\"slots\":[");
        int first = 1;
        for(int s = 1; s <= OFFACT_SLOT_COUNT; s++) {
          char name[OFFACT_NAME_MAX] = {0};
          char type[OFFACT_TYPE_MAX] = {0};
          uint64_t id = 0;
          int flags = 0;
          if(offact_get_name(s, name) || !name[0]) continue;
          offact_get_id(s, &id);
          offact_get_type(s, type);
          offact_get_flags(s, &flags);
          /* Escape any double quotes in the user name (PS5 allows
             them; we don't want to break our JSON). */
          char esc[OFFACT_NAME_MAX * 2];
          int eo = 0;
          for(int i = 0; name[i] && eo < (int)sizeof(esc) - 2; i++) {
            if(name[i] == '"' || name[i] == '\\') esc[eo++] = '\\';
            esc[eo++] = name[i];
          }
          esc[eo] = 0;
          int activated = (id != 0 && flags == OFFACT_DEFAULT_FLAGS);
          int n = snprintf(body + len, sizeof(body) - len,
              "%s{\"slot\":%d,\"name\":\"%s\",\"type\":\"%s\","
              "\"flags\":%d,\"id\":\"0x%016lx\",\"activated\":%s}",
              first ? "" : ",", s, esc, type, flags,
              (unsigned long)id, activated ? "true" : "false");
          if(n <= 0 || n >= (int)(sizeof(body) - len)) break;
          len += n;
          first = 0;
        }
        len += snprintf(body + len, sizeof(body) - len, "]}");
      } else if(!strcmp(url, "/api/offact/activate")) {
        const char *slot_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "slot");
        const char *id_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "id");
        int slot = slot_s ? atoi(slot_s) : 0;
        uint64_t id = 0;
        if(id_s && *id_s) {
          /* Accept "0xHEX" or plain hex/decimal. */
          if(id_s[0] == '0' && (id_s[1] == 'x' || id_s[1] == 'X'))
            id = strtoull(id_s + 2, NULL, 16);
          else
            id = strtoull(id_s, NULL, 0);
        }
        int rc = offact_activate(slot, id);
        uint64_t actual = 0;
        offact_get_id(slot, &actual);
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"slot\":%d,\"id\":\"0x%016lx\"}",
            rc == 0 ? "true" : "false", slot, (unsigned long)actual);
      } else if(!strcmp(url, "/api/offact/id")) {
        /* Set just the account id without touching type/flags. */
        const char *slot_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "slot");
        const char *id_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "id");
        int slot = slot_s ? atoi(slot_s) : 0;
        uint64_t id = 0;
        int parsed = 0;
        if(id_s && *id_s) {
          if(id_s[0] == '0' && (id_s[1] == 'x' || id_s[1] == 'X'))
            id = strtoull(id_s + 2, NULL, 16);
          else
            id = strtoull(id_s, NULL, 0);
          parsed = 1;
        }
        int rc = parsed ? offact_set_id(slot, id) : -1;
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"slot\":%d,\"id\":\"0x%016lx\"}",
            rc == 0 ? "true" : "false", slot, (unsigned long)id);
      } else if(!strcmp(url, "/api/offact/type")) {
        /* Set just the type string (e.g. "np", "psn", or any short label). */
        const char *slot_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "slot");
        const char *type = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "type");
        int slot = slot_s ? atoi(slot_s) : 0;
        int rc = (type && *type) ? offact_set_type(slot, type) : -1;
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"slot\":%d,\"type\":\"%s\"}",
            rc == 0 ? "true" : "false", slot, type ? type : "");
      } else if(!strcmp(url, "/api/offact/flags")) {
        /* Set just the flags integer (decimal or 0xHEX). */
        const char *slot_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "slot");
        const char *flags_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "flags");
        int slot = slot_s ? atoi(slot_s) : 0;
        int flags = 0, parsed = 0;
        if(flags_s && *flags_s) {
          flags = (int)strtol(flags_s, NULL, 0);
          parsed = 1;
        }
        int rc = parsed ? offact_set_flags(slot, flags) : -1;
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"slot\":%d,\"flags\":%d}",
            rc == 0 ? "true" : "false", slot, flags);
      } else if(!strcmp(url, "/api/offact/clear")) {
        const char *slot_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "slot");
        int slot = slot_s ? atoi(slot_s) : 0;
        int rc = offact_clear(slot);
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"slot\":%d}",
            rc == 0 ? "true" : "false", slot);
      } else if(!strcmp(url, "/api/offact/rename")) {
        const char *slot_s = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "slot");
        const char *name = MHD_lookup_connection_value(conn,
                              MHD_GET_ARGUMENT_KIND, "name");
        int slot = slot_s ? atoi(slot_s) : 0;
        int rc = (name && *name) ? offact_set_name(slot, name) : -1;
        len = snprintf(body, sizeof(body),
            "{\"ok\":%s,\"slot\":%d,\"name\":\"%s\"}",
            rc == 0 ? "true" : "false", slot, name ? name : "");
      } else {
        const char *err = "{\"ok\":false,\"error\":\"no such endpoint\"}";
        struct MHD_Response *r =
            MHD_create_response_from_buffer(strlen(err), (void*)err,
                                            MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(r, "Content-Type", "application/json");
        enum MHD_Result rc = websrv_queue_response(conn, 404, r);
        MHD_destroy_response(r);
        return rc;
      }
      char *out = malloc(len + 1);
      if(!out) return MHD_NO;
      memcpy(out, body, len + 1);
      struct MHD_Response *r =
          MHD_create_response_from_buffer(len, out, MHD_RESPMEM_MUST_FREE);
      MHD_add_response_header(r, "Content-Type", "application/json");
      enum MHD_Result rc = websrv_queue_response(conn, 200, r);
      MHD_destroy_response(r);
      return rc;
    }
    if(!strncmp("/appdb", url, 6) && (url[6] == 0 || url[6] == '/')) {
      return appdb_request(conn, url);
    }
    if(!strncmp("/api/tmdb/", url, 10)) {
      return tmdb_request(conn, url);
    }
    if(!strcmp("/hbldr", url)) {
      return hbldr_request(conn);
    }
    if(!strcmp("/elfldr", url)) {
      return elfldr_request(conn, 0);
    }
    if(!strcmp("/version", url)) {
      return version_request(conn);
    }
    if(!strcmp("/", url) || !url[0] ||
       !strcmp("/launcher.html", url) || !strcmp("/index.html", url)) {
      /* First-boot redirect: if Sonic Loader just booted for the first
         time, /data/sonic-loader/.first_boot_redirect_pending exists.
         Send the user straight to the kstuff install card so they can
         complete setup without hunting through Settings. The marker is
         consumed on the first redirect — subsequent loads behave
         normally. */
      struct stat st_redir;
      if(stat("/data/sonic-loader/.first_boot_redirect_pending", &st_redir)
         == 0) {
        unlink("/data/sonic-loader/.first_boot_redirect_pending");
        struct MHD_Response *r = MHD_create_response_from_buffer(
            0, "", MHD_RESPMEM_PERSISTENT);
        if(r) {
          MHD_add_response_header(r, "Location",
              "/launcher.html#kstuff-update-card");
          MHD_add_response_header(r, "Cache-Control", "no-store");
          enum MHD_Result rc = websrv_queue_response(conn,
              MHD_HTTP_FOUND, r);
          MHD_destroy_response(r);
          return rc;
        }
      }
      return asset_request(conn, "/launcher.html");
    }
    /* The Homebrew launcher is a SPA whose router uses /homebrew as
       its base path. Any /homebrew or /homebrew/<subroute> should
       serve the same index.html so refresh and deep links work. */
    if(!strcmp("/homebrew", url) ||
       !strncmp("/homebrew/", url, 10)) {
      return asset_request(conn, "/index.html");
    }
    if(!strcmp("/files", url)) {
      return asset_request(conn, "/files.html");
    }
    if(!strcmp("/klog", url)) {
      return asset_request(conn, "/klog.html");
    }
    if(!strcmp("/stats", url)) {
      return asset_request(conn, "/stats.html");
    }
    if(!strcmp("/pkgzone", url)) {
      return asset_request(conn, "/pkgzone.html");
    }
    return asset_request(conn, url);
  }

  if(!strcmp(method, MHD_HTTP_METHOD_POST)) {
    if(!strcmp("/api/avatar/upload", url)) {
      return avatar_upload_request(conn, upload_data, upload_data_size,
                                   &req->avatar_upload_state);
    }
    if(!strcmp("/api/homebrew/install-pkg-upload", url)) {
      return pkg_upload_request(conn, upload_data, upload_data_size,
                                &req->pkg_upload_state);
    }
    if(!strcmp("/api/fs/upload", url)) {
      return fs_upload_request(conn, upload_data, upload_data_size,
                               &req->fs_upload_state);
    }
    if(!strcmp("/api/payloads/upload", url)) {
      extern enum MHD_Result payload_upload_request(struct MHD_Connection*,
                                                    const char*, size_t*, void**);
      return payload_upload_request(conn, upload_data, upload_data_size,
                                    &req->payload_upload_state);
    }
    if(*upload_data_size) {
      ret = MHD_post_process(req->pp, upload_data, *upload_data_size);
      *upload_data_size = 0;
      return ret;
    }
    if(!strcmp("/elfldr", url)) {
      return elfldr_request(conn, req->data);
    }
    /* Notifications inbox actions ride POST so they're side-effecting
       (mark all read / clear). The handler validates the URL itself. */
    if(!strncmp("/api/notifications", url, 18)) {
      return notif_inbox_request(conn, url);
    }
    if(!strcmp("/api/fan/curve/set", url)) {
      return fan_request(conn, url);
    }
    if(!strncmp("/api/klogs", url, 10)) {
      return dashboards_klogs_request(conn, url);
    }
    if(!strncmp("/api/dumper/", url, 12)) {
      return dumper_request(conn, url);
    }
  }

  return MHD_NO;
}



static void
websrv_on_completed(void *cls, struct MHD_Connection *connection,
                    void **con_cls, enum MHD_RequestTerminationCode toe) {
  post_request_t *req = *con_cls;
  post_data_t *data;

  if(!req) {
    return;
  }

  while((data=req->data)) {
    req->data = data->next;
    free(data->key);
    free(data->val);
    free(data);
  }

  /* If the upload was aborted before the cheats handler reached its
     terminal call, cheats_state still holds an upload_ctx_t. The cheats
     module's upload_ctx_t starts with {char* buf; size_t cap; size_t len;}
     so freeing buf + the struct is enough. */
  if(req->cheats_state) {
    struct { char *buf; size_t cap; size_t len; } *u = req->cheats_state;
    free(u->buf);
    free(u);
  }

  if(req->avatar_upload_state) {
    avatar_upload_free(req->avatar_upload_state);
  }
  if(req->pkg_upload_state) {
    pkg_upload_free(req->pkg_upload_state);
  }
  if(req->fs_upload_state) {
    fs_upload_free(req->fs_upload_state);
  }
  if(req->payload_upload_state) {
    extern void payload_upload_free(void*);
    payload_upload_free(req->payload_upload_state);
  }

  MHD_destroy_post_processor(req->pp);
  free(req);
}


int
websrv_listen(unsigned short port) {
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  struct MHD_Daemon *httpd;
  socklen_t addr_len;
  int connfd;
  int srvfd;

  signal(SIGPIPE, SIG_IGN);

  if((srvfd=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return -1;
  }

  if(setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    perror("setsockopt");
    close(srvfd);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);

  if(bind(srvfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
    perror("bind");
    close(srvfd);
    return -1;
  }

  if(listen(srvfd, 5) != 0) {
    perror("listen");
    close(srvfd);
    return -1;
  }

  if(!(httpd=MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_ITC |
			      MHD_USE_NO_LISTEN_SOCKET | MHD_USE_DEBUG |
			      MHD_USE_INTERNAL_POLLING_THREAD,
			      0, NULL, NULL, &websrv_on_request, NULL,
                              MHD_OPTION_NOTIFY_COMPLETED, &websrv_on_completed,
                              NULL, MHD_OPTION_END))) {
    perror("MHD_start_daemon");
    close(srvfd);
    return -1;
  }

  while(1) {
    addr_len = sizeof(client_addr);
    if((connfd=accept(srvfd, (struct sockaddr*)&client_addr, &addr_len)) < 0) {
      perror("accept");
      break;
    }

    if(MHD_add_connection(httpd, connfd, (struct sockaddr*)&client_addr,
			  addr_len) != MHD_YES) {
      perror("MHD_add_connection");
      break;
    }
  }

  MHD_stop_daemon(httpd);

  return close(srvfd);
}


