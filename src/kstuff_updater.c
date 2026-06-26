/* Sonic Loader — kstuff-lite auto-updater.

   Fetches the most recent release of EchoStretch/kstuff-lite from GitHub
   and drops the resulting kstuff.elf at /data/kstuff.elf. The boot path
   in sys_spawn_embedded_payloads() loads /data/kstuff.elf if present, so
   the new file takes effect on the next payload run. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>

#include <microhttpd.h>

#include "kstuff_updater.h"
#include "ps5/http.h"
#include "third_party/cJSON.h"
#include "websrv.h"


/* Two upstreams. EchoStretch's fork tracks current PS5 firmwares; drakmor's
   fork is older and only verified up to firmware 10.01. The Settings UI
   exposes both behind two combo buttons. */
#define KSTUFF_RELEASES_URL_ECHO \
  "https://api.github.com/repos/EchoStretch/kstuff-lite/releases/latest"
#define KSTUFF_DIRECT_URL_ECHO \
  "https://github.com/EchoStretch/kstuff-lite/releases/latest/download/" \
  "kstuff.elf"

#define KSTUFF_RELEASES_URL_DRAKMOR \
  "https://api.github.com/repos/drakmor/kstuff-lite/releases/latest"
#define KSTUFF_DIRECT_URL_DRAKMOR \
  "https://github.com/drakmor/kstuff-lite/releases/latest/download/" \
  "kstuff.elf"

/* "Super low FW" path — drakmor's kstuff v1.0.3 ELF, hosted in this
   repo at payloads/kstuff-lowfw.elf because the original release is
   only distributed as a .zip on a Discord channel. The releases-API
   probe is skipped for this source; we go straight to the direct
   URL. Targets the lowest verified-working drakmor build for PS5
   firmwares well below 10.01. */
#define KSTUFF_RELEASES_URL_LOWFW NULL
#define KSTUFF_DIRECT_URL_LOWFW \
  "https://git.etawen.dev/soniciso/sonicloader/raw/branch/main/" \
  "payloads/kstuff-lowfw.elf"

#define KSTUFF_INSTALL_PATH "/data/kstuff.elf"


static enum MHD_Result
serve_buffer(struct MHD_Connection *conn, unsigned int status,
             const char *mime, void *data, size_t size, int free_after) {
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  enum MHD_ResponseMemoryMode mode = free_after ? MHD_RESPMEM_MUST_FREE
                                                : MHD_RESPMEM_PERSISTENT;
  if((resp=MHD_create_response_from_buffer(size, data, mode))) {
    if(mime) MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, mime);
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CACHE_CONTROL, "no-cache");
    ret = websrv_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
  } else if(free_after) {
    free(data);
  }
  return ret;
}


static enum MHD_Result
serve_json(struct MHD_Connection *conn, unsigned int status, cJSON *obj) {
  char *txt = cJSON_PrintUnformatted(obj);
  if(!txt) return serve_buffer(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                               "application/json",
                               "{\"error\":\"alloc\"}", 17, 0);
  return serve_buffer(conn, status, "application/json", txt, strlen(txt), 1);
}


static enum MHD_Result
serve_error(struct MHD_Connection *conn, unsigned int status, const char *msg) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", 0);
  cJSON_AddStringToObject(o, "error", msg);
  enum MHD_Result ret = serve_json(conn, status, o);
  cJSON_Delete(o);
  return ret;
}


/* Walk the assets[] array in the GitHub releases JSON, return the
   browser_download_url of the entry matching the requested basename
   (case-insensitive). Caller frees. */
static char*
find_asset_url(const char *json_text, size_t json_len, const char *basename) {
  cJSON *root = cJSON_ParseWithLength(json_text, json_len);
  if(!root) return NULL;

  char *out = NULL;
  cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
  if(cJSON_IsArray(assets)) {
    cJSON *e;
    cJSON_ArrayForEach(e, assets) {
      cJSON *n = cJSON_GetObjectItemCaseSensitive(e, "name");
      cJSON *u = cJSON_GetObjectItemCaseSensitive(e, "browser_download_url");
      if(cJSON_IsString(n) && cJSON_IsString(u) &&
         !strcasecmp(n->valuestring, basename)) {
        out = strdup(u->valuestring);
        break;
      }
    }
  }
  cJSON_Delete(root);
  return out;
}


/* Same as find_asset_url but matches by suffix instead of exact
   basename. Used to fall back to .zip assets when the release
   doesn't ship kstuff.elf as a direct asset. Caller frees. */
static char*
find_release_asset_url_by_ext(const char *json_text, size_t json_len,
                              const char *ext) {
  cJSON *root = cJSON_ParseWithLength(json_text, json_len);
  if(!root) return NULL;
  char *out = NULL;
  size_t el = strlen(ext);
  cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
  if(cJSON_IsArray(assets)) {
    cJSON *e;
    cJSON_ArrayForEach(e, assets) {
      cJSON *n = cJSON_GetObjectItemCaseSensitive(e, "name");
      cJSON *u = cJSON_GetObjectItemCaseSensitive(e, "browser_download_url");
      if(!cJSON_IsString(n) || !cJSON_IsString(u)) continue;
      const char *name = n->valuestring;
      size_t nl = strlen(name);
      if(nl >= el && !strcasecmp(name + nl - el, ext)) {
        out = strdup(u->valuestring);
        break;
      }
    }
  }
  cJSON_Delete(root);
  return out;
}


/* Pull a single named file out of a zip-in-memory into a malloc'd
   buffer. Mirrors the helper in smp_updater.c — match by basename
   so a top-level dir prefix in the zip works. *out_buf must be
   freed by the caller; returns 0 on success. */
static int
extract_named_from_zip(const uint8_t *zip_data, size_t zip_len,
                       const char *target_basename,
                       uint8_t **out_buf, size_t *out_len,
                       char *err, size_t err_size) {
  *out_buf = NULL;
  *out_len = 0;

  struct archive *a = archive_read_new();
  archive_read_support_format_zip(a);
  archive_read_support_filter_all(a);
  if(archive_read_open_memory(a, (void*)zip_data, zip_len) != ARCHIVE_OK) {
    snprintf(err, err_size, "zip open: %s", archive_error_string(a));
    archive_read_free(a);
    return -1;
  }

  struct archive_entry *entry;
  while(archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char *name = archive_entry_pathname(entry);
    if(!name || !*name) continue;
    const char *bn = strrchr(name, '/');
    bn = bn ? bn + 1 : name;
    if(strcasecmp(bn, target_basename) != 0) continue;

    int64_t entry_size = archive_entry_size(entry);
    if(entry_size <= 0 || entry_size > 64 * 1024 * 1024) {
      snprintf(err, err_size, "zip entry %s size implausible: %lld",
               target_basename, (long long)entry_size);
      archive_read_free(a);
      return -1;
    }
    *out_buf = malloc((size_t)entry_size);
    if(!*out_buf) {
      snprintf(err, err_size, "malloc(%lld) failed",
               (long long)entry_size);
      archive_read_free(a);
      return -1;
    }
    size_t got = 0;
    const void *buf;
    size_t      sz;
    int64_t     off;
    int         rc;
    while((rc = archive_read_data_block(a, &buf, &sz, &off)) == ARCHIVE_OK) {
      if(got + sz > (size_t)entry_size) {
        free(*out_buf); *out_buf = NULL;
        snprintf(err, err_size, "zip entry %s size mismatch",
                 target_basename);
        archive_read_free(a);
        return -1;
      }
      memcpy(*out_buf + got, buf, sz);
      got += sz;
    }
    if(rc != ARCHIVE_EOF) {
      free(*out_buf); *out_buf = NULL;
      snprintf(err, err_size, "zip read: %s", archive_error_string(a));
      archive_read_free(a);
      return -1;
    }
    *out_len = got;
    archive_read_free(a);
    return 0;
  }

  snprintf(err, err_size, "zip does not contain '%s'", target_basename);
  archive_read_free(a);
  return -1;
}


/* Pull the tag_name field from the releases JSON for display. */
static char*
extract_tag(const char *json_text, size_t json_len) {
  cJSON *root = cJSON_ParseWithLength(json_text, json_len);
  if(!root) return NULL;
  char *out = NULL;
  cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
  if(cJSON_IsString(t)) out = strdup(t->valuestring);
  cJSON_Delete(root);
  return out;
}


/* Atomic write: data → /data/kstuff.elf.tmp, then rename. */
static int
atomic_write(const char *path, const uint8_t *buf, size_t len) {
  char tmp[256];
  if(snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
    return -1;
  }

  int fd = open(tmp, O_WRONLY|O_CREAT|O_TRUNC, 0755);
  if(fd < 0) return -1;
  size_t off = 0;
  while(off < len) {
    ssize_t n = write(fd, buf + off, len - off);
    if(n <= 0) { close(fd); unlink(tmp); return -1; }
    off += (size_t)n;
  }
  close(fd);

  if(rename(tmp, path) != 0) {
    unlink(tmp);
    return -1;
  }
  chmod(path, 0755);
  return 0;
}


/* GET /api/kstuff/update[?source=echostretch|drakmor][&tag=<release>]
   Fetch a specific kstuff.elf release (or latest if no tag) from the
   chosen upstream and write it to /data/kstuff.elf. */
static enum MHD_Result
update_request(struct MHD_Connection *conn) {
  /* Upstream. Default = EchoStretch (current firmware). drakmor is
     verified up to firmware 10.01 — Settings UI puts that warning
     next to its picker. */
  const char *src = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND,
                                                "source");
  const char *want_tag = MHD_lookup_connection_value(conn,
                                MHD_GET_ARGUMENT_KIND, "tag");
  const char *repo, *direct_url, *source_label;
  int from_lowfw = 0;
  if(src && (!strcasecmp(src, "lowfw")  ||
             !strcasecmp(src, "low_fw") ||
             !strcasecmp(src, "low-fw") ||
             !strcasecmp(src, "v103"))) {
    /* Vendored drakmor kstuff v1.0.3 hosted in this repo. Skip the
       GitHub releases probe entirely. */
    repo         = NULL;
    direct_url   = KSTUFF_DIRECT_URL_LOWFW;
    source_label = "lowfw";
    from_lowfw   = 1;
  } else if(src && (!strcasecmp(src, "drakmor") || !strcasecmp(src, "dr"))) {
    repo         = "drakmor/kstuff-lite";
    direct_url   = KSTUFF_DIRECT_URL_DRAKMOR;
    source_label = "drakmor";
  } else {
    repo         = "EchoStretch/kstuff-lite";
    direct_url   = KSTUFF_DIRECT_URL_ECHO;
    source_label = "echostretch";
  }

  char *asset_url = NULL;
  char *tag       = NULL;
  int   from_zip  = 0;

  if(from_lowfw) {
    /* Direct hosted ELF — no JSON probe, no tag. */
    asset_url = strdup(direct_url);
    tag       = strdup("v1.0.3");
  } else {
    /* Build the right URL. If a specific tag was requested, hit
       api.github.com/.../releases/tags/<tag> for that release's JSON;
       otherwise hit /releases/latest. */
    char releases_url[256];
    if(want_tag && *want_tag) {
      snprintf(releases_url, sizeof(releases_url),
               "https://api.github.com/repos/%s/releases/tags/%s",
               repo, want_tag);
    } else {
      snprintf(releases_url, sizeof(releases_url),
               "https://api.github.com/repos/%s/releases/latest", repo);
    }

    size_t json_len = 0;
    uint8_t *json   = http_get(releases_url, &json_len);

    if(json && json_len) {
      /* Prefer kstuff.elf as a direct asset; fall back to any .zip
         that ships kstuff.elf inside (1.0.55+ extracts from zip). */
      asset_url = find_asset_url((const char*)json, json_len, "kstuff.elf");
      if(!asset_url) {
        asset_url = find_release_asset_url_by_ext(
            (const char*)json, json_len, ".zip");
        from_zip  = (asset_url != NULL);
      }
      tag = extract_tag((const char*)json, json_len);
    }
    free(json);

    /* Fall back to the redirect URL only when no tag was requested
       (latest install). Tag-specific installs fail outright if
       GitHub didn't return a usable asset. */
    if(!asset_url && (!want_tag || !*want_tag)) {
      asset_url = strdup(direct_url);
    }
    if(!asset_url) {
      cJSON *r = cJSON_CreateObject();
      cJSON_AddBoolToObject(r,   "ok", 0);
      cJSON_AddStringToObject(r, "error",
          "release not found or has no kstuff.elf / .zip asset");
      cJSON_AddStringToObject(r, "tagRequested", want_tag);
      enum MHD_Result ret = serve_json(conn, MHD_HTTP_NOT_FOUND, r);
      cJSON_Delete(r);
      return ret;
    }
  }

  size_t blob_len = 0;
  uint8_t *blob   = http_get(asset_url, &blob_len);
  if(!blob || blob_len < 64) {
    free(blob);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r,   "ok", 0);
    cJSON_AddStringToObject(r, "error",
                            "download failed — GitHub/repo returned no payload");
    cJSON_AddStringToObject(r, "url", asset_url);
    free(asset_url); free(tag);
    enum MHD_Result ret = serve_json(conn, MHD_HTTP_BAD_GATEWAY, r);
    cJSON_Delete(r);
    return ret;
  }

  /* If we pulled a .zip, extract kstuff.elf from inside it. */
  uint8_t *elf     = blob;
  size_t   elf_len = blob_len;
  if(from_zip) {
    char zerr[256] = "";
    uint8_t *extracted = NULL;
    size_t   extracted_len = 0;
    if(extract_named_from_zip(blob, blob_len, "kstuff.elf",
                              &extracted, &extracted_len,
                              zerr, sizeof(zerr)) != 0) {
      free(blob);
      cJSON *r = cJSON_CreateObject();
      cJSON_AddBoolToObject(r,   "ok", 0);
      cJSON_AddStringToObject(r, "error", zerr);
      cJSON_AddStringToObject(r, "url", asset_url);
      free(asset_url); free(tag);
      enum MHD_Result ret = serve_json(conn, MHD_HTTP_BAD_GATEWAY, r);
      cJSON_Delete(r);
      return ret;
    }
    free(blob);
    elf     = extracted;
    elf_len = extracted_len;
  }

  /* Sanity-check the ELF magic so we never write a GitHub HTML
     error page on top of a working kstuff binary. */
  if(elf_len < 64 ||
     elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
    free(elf);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r,   "ok", 0);
    cJSON_AddStringToObject(r, "error",
                            "downloaded payload is not a valid ELF (bad magic)");
    cJSON_AddStringToObject(r, "url", asset_url);
    cJSON_AddNumberToObject(r, "size", (double)elf_len);
    if(tag) cJSON_AddStringToObject(r, "tag", tag);
    free(asset_url);
    free(tag);
    enum MHD_Result ret = serve_json(conn, MHD_HTTP_BAD_GATEWAY, r);
    cJSON_Delete(r);
    return ret;
  }

  /* /data should already exist (the SDK creates it), but mkdir is cheap
     insurance. */
  mkdir("/data", 0755);

  int rc = atomic_write(KSTUFF_INSTALL_PATH, elf, elf_len);
  free(elf);

  cJSON *r = cJSON_CreateObject();
  if(rc != 0) {
    cJSON_AddBoolToObject(r,   "ok", 0);
    cJSON_AddStringToObject(r, "error", strerror(errno));
    cJSON_AddStringToObject(r, "path", KSTUFF_INSTALL_PATH);
    cJSON_AddStringToObject(r, "url", asset_url);
    if(tag) cJSON_AddStringToObject(r, "tag", tag);
    free(asset_url);
    free(tag);
    enum MHD_Result ret = serve_json(conn,
                                     MHD_HTTP_INTERNAL_SERVER_ERROR, r);
    cJSON_Delete(r);
    return ret;
  }

  cJSON_AddBoolToObject(r,   "ok", 1);
  cJSON_AddStringToObject(r, "path", KSTUFF_INSTALL_PATH);
  cJSON_AddNumberToObject(r, "size", (double)elf_len);
  cJSON_AddStringToObject(r, "url", asset_url);
  cJSON_AddStringToObject(r, "source", source_label);
  cJSON_AddBoolToObject(r,   "fromZip", from_zip ? 1 : 0);
  if(tag) cJSON_AddStringToObject(r, "tag", tag);
  cJSON_AddStringToObject(r, "note",
      "Update applied — restart Sonic Loader (re-send the payload) to "
      "load the new kstuff.elf.");

  free(asset_url);
  free(tag);
  enum MHD_Result ret = serve_json(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  return ret;
}


/* GET /api/kstuff/info — quick status read for the UI. */
static enum MHD_Result
info_request(struct MHD_Connection *conn) {
  cJSON *r = cJSON_CreateObject();
  cJSON_AddStringToObject(r, "path", KSTUFF_INSTALL_PATH);

  cJSON *sources = cJSON_CreateObject();
  cJSON_AddStringToObject(sources, "echostretch",
      "https://github.com/EchoStretch/kstuff-lite/releases");
  cJSON_AddStringToObject(sources, "drakmor",
      "https://github.com/drakmor/kstuff-lite/releases");
  cJSON_AddItemToObject(r, "sources", sources);

  struct stat st;
  if(stat(KSTUFF_INSTALL_PATH, &st) == 0) {
    cJSON_AddBoolToObject(r,   "exists", 1);
    cJSON_AddNumberToObject(r, "size",   (double)st.st_size);
    cJSON_AddNumberToObject(r, "mtime",  (double)st.st_mtime);
  } else {
    cJSON_AddBoolToObject(r, "exists", 0);
  }
  enum MHD_Result ret = serve_json(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  return ret;
}


/* GET /api/kstuff/reset — delete /data/kstuff.elf so the next boot
   falls back to the embedded copy. Useful if the user's downloaded
   kstuff is incompatible with the firmware. */
static enum MHD_Result
reset_request(struct MHD_Connection *conn) {
  cJSON *r = cJSON_CreateObject();
  if(unlink(KSTUFF_INSTALL_PATH) == 0) {
    cJSON_AddBoolToObject(r,   "ok",   1);
    cJSON_AddStringToObject(r, "path", KSTUFF_INSTALL_PATH);
    cJSON_AddStringToObject(r, "note",
        "Deleted. Sonic Loader no longer ships an embedded kstuff — "
        "use Settings → Install kstuff-lite to put a fresh copy at "
        "/data/kstuff.elf.");
  } else if(errno == ENOENT) {
    cJSON_AddBoolToObject(r,   "ok",   1);
    cJSON_AddStringToObject(r, "note",
        "Already absent — install via Settings to enable kstuff.");
  } else {
    cJSON_AddBoolToObject(r,   "ok",    0);
    cJSON_AddStringToObject(r, "error", strerror(errno));
  }
  enum MHD_Result ret = serve_json(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  return ret;
}


/* Direct (non-HTTP) entry point used by sys.c on first boot. Mirrors
   the behavior of update_request without going through MHD. */
int
kstuff_install_direct(int use_drakmor) {
  const char *releases_url = use_drakmor ? KSTUFF_RELEASES_URL_DRAKMOR
                                         : KSTUFF_RELEASES_URL_ECHO;
  const char *direct_url   = use_drakmor ? KSTUFF_DIRECT_URL_DRAKMOR
                                         : KSTUFF_DIRECT_URL_ECHO;

  size_t json_len = 0;
  uint8_t *json   = http_get(releases_url, &json_len);

  char *asset_url = NULL;
  if(json && json_len) {
    asset_url = find_asset_url((const char*)json, json_len, "kstuff.elf");
  }
  free(json);

  if(!asset_url) asset_url = strdup(direct_url);

  size_t elf_len = 0;
  uint8_t *elf   = http_get(asset_url, &elf_len);
  free(asset_url);

  if(!elf || elf_len < 64 ||
     elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
    free(elf);
    return -1;
  }

  mkdir("/data", 0755);
  int rc = atomic_write(KSTUFF_INSTALL_PATH, elf, elf_len);
  free(elf);
  return rc;
}


enum MHD_Result
kstuff_updater_request(struct MHD_Connection *conn, const char *url) {
  if(!strcmp(url, "/api/kstuff"))         return info_request(conn);
  if(!strcmp(url, "/api/kstuff/info"))    return info_request(conn);
  if(!strcmp(url, "/api/kstuff/update"))  return update_request(conn);
  if(!strcmp(url, "/api/kstuff/reset"))   return reset_request(conn);
  return serve_error(conn, MHD_HTTP_NOT_FOUND, "no such endpoint");
}
