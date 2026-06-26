/* Sonic Loader — cheat engine.

   Implements the Sonic-Loader cheat schema:

   {
     "name":    "inFAMOUS: Second Son",
     "id":      "CUSA00004",
     "version": "01.07",
     "process": "eboot.bin",
     "mods": [
       {
         "name":   "Godmode",
         "hint":   null,
         "type":   "checkbox" | "button",
         "memory": [
           { "offset": "615c17", "on": "90909090...",
             "off":    "C4C17A11..." }
         ]
       }
     ]
   }

   Files live at /data/sonic-loader/cheats/<TITLE_ID>.json. Drop them in
   via the bundled FTP server, or hit /api/cheats/download to pull from
   GoldHEN_Cheat_Repository or "etaHEN PS5_Cheats". */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <microhttpd.h>

#include <ps5/kernel.h>

#include "cheats.h"
#include "ps5/http.h"
#include "ps5/notify.h"
#include "ps5/pt.h"
#include "third_party/cJSON.h"
#include "third_party/mc4/mc4decrypter.h"
#include "titleid.h"
#include "websrv.h"


#define CHEATS_DIR  "/data/sonic-loader/cheats"

static atomic_int g_cheats_engine_enabled = 1;

int  cheats_engine_enabled(void)            { return atomic_load(&g_cheats_engine_enabled); }
void cheats_engine_set_enabled(int on)      {
  atomic_store(&g_cheats_engine_enabled, on ? 1 : 0);
  extern void config_save(void);
  config_save();
}

int sceSystemServiceGetAppIdOfRunningBigApp(void);

typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  char     title_id[14];
  char     unknown2[0x3c];
} app_info_t;

int sceKernelGetAppInfo(pid_t pid, app_info_t *info);


/* --------------------------------------------------------------------- */
/*  Helpers                                                               */
/* --------------------------------------------------------------------- */

static enum MHD_Result
serve_buffer(struct MHD_Connection *conn, unsigned int status,
             const char *mime, void *data, size_t size, int free_after) {
  enum MHD_Result ret = MHD_NO;
  struct MHD_Response *resp;
  enum MHD_ResponseMemoryMode mode = free_after ? MHD_RESPMEM_MUST_FREE
                                                : MHD_RESPMEM_PERSISTENT;
  if((resp=MHD_create_response_from_buffer(size, data, mode))) {
    if(mime) {
      MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, mime);
    }
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CACHE_CONTROL, "no-cache");
    ret = websrv_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
  } else if(free_after) {
    free(data);
  }
  return ret;
}


static enum MHD_Result
serve_json_object(struct MHD_Connection *conn, unsigned int status,
                  cJSON *obj) {
  char *txt = cJSON_PrintUnformatted(obj);
  if(!txt) {
    return serve_buffer(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                        "application/json",
                        "{\"error\":\"alloc\"}", 17, 0);
  }
  return serve_buffer(conn, status, "application/json", txt, strlen(txt), 1);
}


static enum MHD_Result
serve_error(struct MHD_Connection *conn, unsigned int status,
            const char *message) {
  cJSON *obj = cJSON_CreateObject();
  cJSON_AddBoolToObject(obj, "ok", 0);
  cJSON_AddStringToObject(obj, "error", message ? message : "error");
  enum MHD_Result ret = serve_json_object(conn, status, obj);
  cJSON_Delete(obj);
  return ret;
}


/* A bare title id is exactly CUSA##### or PPSA##### — 9 chars, all alnum.
   We use this for the running-game match. */
static int
is_safe_title_id(const char *s) {
  if(!s || !*s) return 0;
  size_t n = strlen(s);
  if(n < 9 || n > 16) return 0;
  for(size_t i=0; i<n; i++) {
    char c = s[i];
    if(!((c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') ||
         c=='-' || c=='_')) {
      return 0;
    }
  }
  return 1;
}


/* Filenames in the cheat store may carry a version suffix and a dot in
   the basename, e.g. CUSA00004_01.07.json or SLUS-00001.json. The
   base must still start with a recognised title id (CUSA/PPSA/ULUS/
   SLUS/etc., with or without a dash). The normalised separator-free
   9-char form is what gets returned in `out`, so the rest of the
   pipeline can compare it against the running-app's bare titleId. */
static int
extract_title_id_prefix(const char *filename, char *out, size_t out_size) {
  if(!filename || out_size < 10) return 0;
  return title_id_normalize(filename, out);
}


/* Recognised cheat file extensions, in priority order. */
static int
recognised_cheat_extension(const char *name) {
  size_t n = strlen(name);
  if(n > 5 && !strcasecmp(name + n - 5, ".json")) return 1;
  if(n > 4 && !strcasecmp(name + n - 4, ".shn"))  return 2;
  if(n > 4 && !strcasecmp(name + n - 4, ".mc4"))  return 3;
  return 0;
}


static int
hex_nibble(char c) {
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return 10 + c - 'a';
  if(c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}


static int
parse_hex_bytes(const char *s, uint8_t **out, size_t *out_len) {
  size_t cap = 32;
  size_t len = 0;
  uint8_t *buf = malloc(cap);
  int high = -1;

  for(const char *p=s; *p; p++) {
    if(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '-' ||
       *p == ',' || *p == ':') continue;
    int n = hex_nibble(*p);
    if(n < 0) { free(buf); return -1; }
    if(high < 0) {
      high = n;
    } else {
      if(len + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
      buf[len++] = (uint8_t)((high << 4) | n);
      high = -1;
    }
  }
  if(high >= 0) { free(buf); return -1; }
  *out = buf;
  *out_len = len;
  return 0;
}


static uint64_t
parse_offset(const char *s) {
  if(!s) return 0;
  while(*s == ' ' || *s == '\t') s++;
  /* Cheats use bare hex (no 0x). Accept either. */
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  return (uint64_t)strtoull(s, NULL, 16);
}


/* --------------------------------------------------------------------- */
/*  Running-game detection                                                */
/* --------------------------------------------------------------------- */

static pid_t
find_pid_for_app_id(uint32_t app_id) {
  int mib[4] = {1, 14, 8, 0};
  size_t buf_size = 0;
  if(sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0) return -1;
  uint8_t *buf = malloc(buf_size);
  if(!buf) return -1;
  if(sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) { free(buf); return -1; }

  pid_t result = -1;
  app_info_t info;
  for(uint8_t *ptr=buf; ptr<buf+buf_size; ) {
    int ki_structsize = *(int*)ptr;
    pid_t pid = *(pid_t*)&ptr[72];
    ptr += ki_structsize;
    memset(&info, 0, sizeof(info));
    if(sceKernelGetAppInfo(pid, &info) == 0 && info.app_id == app_id) {
      result = pid;
      break;
    }
  }
  free(buf);
  return result;
}


static int
get_running_game(pid_t *out_pid, char *out_title, size_t title_size,
                 intptr_t *out_base) {
  int app_id = sceSystemServiceGetAppIdOfRunningBigApp();
  if(app_id <= 0) return -1;
  pid_t pid = find_pid_for_app_id((uint32_t)app_id);
  if(pid <= 0) return -1;
  app_info_t info;
  memset(&info, 0, sizeof(info));
  if(sceKernelGetAppInfo(pid, &info) != 0) return -1;
  if(out_title) {
    size_t n = strnlen(info.title_id, sizeof(info.title_id));
    if(n >= title_size) n = title_size - 1;
    memcpy(out_title, info.title_id, n);
    out_title[n] = 0;
  }
  if(out_base) *out_base = kernel_dynlib_mapbase_addr(pid, 0);
  if(out_pid) *out_pid = pid;
  return 0;
}


int
cheats_game_running(void) {
  return get_running_game(NULL, NULL, 0, NULL) == 0 ? 1 : 0;
}


/* --------------------------------------------------------------------- */
/*  File I/O                                                              */
/* --------------------------------------------------------------------- */

static void
ensure_cheats_dir(void) {
  mkdir("/data/sonic-loader", 0755);
  mkdir(CHEATS_DIR, 0755);
}


void
cheats_init(void) {
  ensure_cheats_dir();
  chmod("/data/sonic-loader",       0777);
  chmod(CHEATS_DIR,                 0777);
  printf("cheats: ready, drop cheat .json files into %s via FTP\n",
         CHEATS_DIR);
}


/* --------------------------------------------------------------------- */
/*  SHN/MC4 → JSON conversion                                             */
/* --------------------------------------------------------------------- */

/* Tiny string buffer for building JSON. */
typedef struct { char *buf; size_t len; size_t cap; } cb_t;

static void cb_putc(cb_t *b, char c) {
  if(b->len + 2 > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 1024;
    b->buf = realloc(b->buf, b->cap);
  }
  b->buf[b->len++] = c;
  b->buf[b->len]   = 0;
}

static void cb_puts(cb_t *b, const char *s) {
  while(*s) cb_putc(b, *s++);
}

static void cb_puts_json(cb_t *b, const char *s, size_t n) {
  for(size_t i=0; i<n; i++) {
    char c = s[i];
    switch(c) {
      case '"':  cb_putc(b, '\\'); cb_putc(b, '"'); break;
      case '\\': cb_putc(b, '\\'); cb_putc(b, '\\'); break;
      case '\n': cb_putc(b, '\\'); cb_putc(b, 'n');  break;
      case '\r': cb_putc(b, '\\'); cb_putc(b, 'r');  break;
      case '\t': cb_putc(b, '\\'); cb_putc(b, 't');  break;
      default:
        if((unsigned char)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          cb_puts(b, buf);
        } else {
          cb_putc(b, c);
        }
    }
  }
}

/* Find an attribute or child-element value inside an XML fragment.
   These helpers are intentionally permissive — SHN files are not strict
   XML and many samples include HTML entity escapes. */
static const char*
xml_find_attr(const char *node, const char *attr, size_t *len_out) {
  char needle[64];
  snprintf(needle, sizeof(needle), "%s=\"", attr);
  const char *p = strstr(node, needle);
  if(!p) {
    snprintf(needle, sizeof(needle), "%s='", attr);
    p = strstr(node, needle);
    if(!p) return NULL;
  }
  p += strlen(needle);
  const char *end = strpbrk(p, "\"'");
  if(!end) return NULL;
  *len_out = end - p;
  return p;
}

static const char*
xml_find_child(const char *node, const char *tag, size_t *len_out) {
  char open[64], close[64];
  snprintf(open,  sizeof(open),  "<%s>",  tag);
  snprintf(close, sizeof(close), "</%s>", tag);
  const char *p = strstr(node, open);
  if(!p) return NULL;
  p += strlen(open);
  const char *end = strstr(p, close);
  if(!end) return NULL;
  *len_out = end - p;
  return p;
}

/* Decrypt an .mc4 cheat file (base64 of AES-256-CBC ciphertext, key/IV
   per etaHEN's mc4decrypter) and return the underlying SHN-shaped XML
   as a fresh malloc'd, NUL-terminated string. Caller frees. The plain
   XML still carries HTML entity escapes upstream — we replace the same
   trio etaHEN's parser does (&lt; &gt; \&quot;) so the SHN→JSON walker
   handles it without modification.

   xml_size_out (optional): length of the returned XML before the NUL.
   Returns NULL on decrypt/decode failure. The input buffer is NOT
   modified. */
static char*
mc4_decrypt_to_xml(const char *cipher, size_t cipher_len, size_t *xml_size_out) {
  if(!cipher || cipher_len == 0) return NULL;

  /* decrypt_data takes uint8_t*; it doesn't write past *size on its own,
     but we copy the input to a scratch buffer just in case. */
  uint8_t *scratch = malloc(cipher_len + 1);
  if(!scratch) return NULL;
  memcpy(scratch, cipher, cipher_len);
  scratch[cipher_len] = 0;

  size_t out_size = cipher_len;
  uint8_t *plain = decrypt_data(scratch, &out_size);
  free(scratch);
  if(!plain) return NULL;
  /* decrypt_data returns the same pointer on a base64 decode failure;
     guard against that landing right back here. */
  if(plain == (uint8_t*)scratch) return NULL;

  /* The decrypted blob ends in a PKCS#7-ish pad we don't strip — the
     XML walker stops at </Trainer> regardless. Just NUL-terminate and
     unescape the trio etaHEN documents. */
  char *xml = malloc(out_size + 1);
  if(!xml) { free(plain); return NULL; }
  memcpy(xml, plain, out_size);
  xml[out_size] = 0;
  free(plain);

  /* In-place rewrite: <, >, " entity escapes → literal characters. */
  static const struct { const char *from; char to; } repl[] = {
    {"&lt;",      '<'},
    {"&gt;",      '>'},
    {"\\&quot;",  '"'},
    {"&quot;",    '"'},
  };
  size_t out_len = strlen(xml);
  for(size_t r = 0; r < sizeof(repl)/sizeof(repl[0]); r++) {
    const char *from = repl[r].from;
    size_t flen = strlen(from);
    char  to    = repl[r].to;
    char *src = xml, *dst = xml;
    while(*src) {
      if(strncmp(src, from, flen) == 0) {
        *dst++ = to;
        src += flen;
      } else {
        *dst++ = *src++;
      }
    }
    *dst = 0;
    out_len = (size_t)(dst - xml);
  }

  if(xml_size_out) *xml_size_out = out_len;
  return xml;
}


/* Parse a SHN/MC4-style XML <Trainer>...<Cheat>...<Cheatline> document
   and produce GoldHEN-style JSON in a fresh malloc'd buffer. The output
   is the same schema that apply_cheat() already understands. */
static char*
shn_xml_to_json(const char *xml, size_t xml_len) {
  (void)xml_len;
  cb_t out = {0};
  cb_puts(&out, "{\"name\":\"\",\"id\":\"\",\"version\":\"\",\"process\":\"eboot.bin\",\"mods\":[");

  size_t alen;
  const char *trainer = strstr(xml, "<Trainer");
  if(trainer) {
    const char *aend = strchr(trainer, '>');
    if(aend) {
      char tag[1024];
      size_t tn = aend - trainer;
      if(tn < sizeof(tag)) {
        memcpy(tag, trainer, tn); tag[tn] = 0;
        const char *v;
        if((v = xml_find_attr(tag, "Game", &alen)) ||
           (v = xml_find_attr(tag, "GameName", &alen))) {
          /* Splice in name */
          /* Already wrote the leading template — replace empty name. */
          char *needle = strstr(out.buf, "\"name\":\"\"");
          if(needle) {
            cb_t tmp = {0};
            cb_puts(&tmp, "\"name\":\"");
            cb_puts_json(&tmp, v, alen);
            cb_puts(&tmp, "\"");
            size_t prefix = needle - out.buf;
            cb_t merged = {0};
            cb_puts(&merged, "");
            for(size_t i=0; i<prefix; i++) cb_putc(&merged, out.buf[i]);
            cb_puts(&merged, tmp.buf);
            cb_puts(&merged, out.buf + prefix + 9);
            free(tmp.buf);
            free(out.buf);
            out = merged;
          }
        }
      }
    }
  }

  int first = 1;
  const char *cur = xml;
  while((cur = strstr(cur, "<Cheat ")) != NULL) {
    /* Closing > of the opening Cheat tag */
    const char *close = strchr(cur, '>');
    if(!close) break;
    size_t hdr_len = close - cur;
    char hdr[2048];
    if(hdr_len >= sizeof(hdr)) { cur = close; continue; }
    memcpy(hdr, cur, hdr_len); hdr[hdr_len] = 0;

    /* The Cheat may be self-closing or have a body — find </Cheat>. */
    const char *body_end = strstr(close, "</Cheat>");
    if(!body_end) break;
    const char *body_start = close + 1;

    if(!first) cb_putc(&out, ',');
    first = 0;

    cb_puts(&out, "{");
    /* Name */
    const char *t;
    cb_puts(&out, "\"name\":\"");
    if((t = xml_find_attr(hdr, "Text", &alen)) ||
       (t = xml_find_attr(hdr, "CheatName", &alen)) ||
       (t = xml_find_attr(hdr, "Name", &alen))) {
      cb_puts_json(&out, t, alen);
    }
    cb_puts(&out, "\",");
    /* Hint */
    cb_puts(&out, "\"hint\":");
    if((t = xml_find_attr(hdr, "Description", &alen)) && alen > 0) {
      cb_puts(&out, "\"");
      cb_puts_json(&out, t, alen);
      cb_puts(&out, "\"");
    } else {
      cb_puts(&out, "null");
    }
    cb_puts(&out, ",");
    /* Type — default to checkbox */
    cb_puts(&out, "\"type\":\"checkbox\",");
    cb_puts(&out, "\"memory\":[");

    int first_mem = 1;
    const char *line_cur = body_start;
    while(line_cur < body_end &&
          (line_cur = strstr(line_cur, "<Cheatline")) != NULL &&
          line_cur < body_end) {
      const char *line_close = strstr(line_cur, "</Cheatline>");
      const char *line_self  = strstr(line_cur, "/>");
      const char *line_end   = NULL;
      if(line_close && (!line_self || line_close < line_self)) {
        line_end = line_close + strlen("</Cheatline>");
      } else if(line_self) {
        line_end = line_self + 2;
      } else {
        break;
      }

      char chunk[4096];
      size_t cl = (size_t)(line_end - line_cur);
      if(cl >= sizeof(chunk)) { line_cur = line_end; continue; }
      memcpy(chunk, line_cur, cl); chunk[cl] = 0;

      size_t off_l, on_l, off2_l, abs_l;
      const char *off  = xml_find_child(chunk, "Offset",   &off_l);
      const char *on   = xml_find_child(chunk, "ValueOn",  &on_l);
      const char *off2 = xml_find_child(chunk, "ValueOff", &off2_l);
      const char *abs_ = xml_find_child(chunk, "Absolute", &abs_l);

      if(off && on && off2) {
        if(!first_mem) cb_putc(&out, ',');
        first_mem = 0;
        cb_puts(&out, "{\"offset\":\"");
        cb_puts_json(&out, off, off_l);
        cb_puts(&out, "\",\"on\":\"");
        cb_puts_json(&out, on, on_l);
        cb_puts(&out, "\",\"off\":\"");
        cb_puts_json(&out, off2, off2_l);
        cb_puts(&out, "\"");
        if(abs_ && abs_l > 0 &&
           (!strncasecmp(abs_, "true", 4) || abs_[0] == '1')) {
          cb_puts(&out, ",\"absolute\":true");
        }
        cb_puts(&out, "}");
      }
      line_cur = line_end;
    }
    cb_puts(&out, "]}");
    cur = body_end + strlen("</Cheat>");
  }

  cb_puts(&out, "]}");
  return out.buf;
}


/* Search /data/sonic-loader/cheats/ for any file whose basename starts
   with the given title id and ends in .json/.shn/.mc4. JSON wins over
   SHN wins over MC4 (we can apply JSON natively; SHN is XML; MC4 is
   encrypted XML). Writes the full path into `out` and the detected
   format kind (1=JSON, 2=SHN, 3=MC4) into `*kind_out`.
   Returns 1 on found, 0 otherwise. */
static int
find_cheat_file_for_title(const char *title_id, char *out, size_t out_size,
                          int *kind_out) {
  if(!is_safe_title_id(title_id) || out_size < 32) return 0;
  size_t id_len = strlen(title_id);

  DIR *d = opendir(CHEATS_DIR);
  if(!d) return 0;

  char best_name[256] = {0};
  int  best_kind = 0;
  struct dirent *ent;
  while((ent = readdir(d))) {
    const char *name = ent->d_name;
    if(name[0] == '.') continue;
    if(strncasecmp(name, title_id, id_len) != 0) continue;
    /* The character right after the id must be either '.' (extension)
       or '_' (version suffix) — otherwise PPSA12345 would also match
       PPSA12345xxx. */
    char trailer = name[id_len];
    if(trailer != '.' && trailer != '_') continue;
    int k = recognised_cheat_extension(name);
    if(!k) continue;
    if(best_kind == 0 || k < best_kind) {
      strncpy(best_name, name, sizeof(best_name)-1);
      best_kind = k;
      if(k == 1) break; /* JSON found — best possible, stop. */
    }
  }
  closedir(d);
  if(!best_kind) return 0;

  int n = snprintf(out, out_size, "%s/%s", CHEATS_DIR, best_name);
  if(n <= 0 || (size_t)n >= out_size) return 0;
  if(kind_out) *kind_out = best_kind;
  return 1;
}


static char*
read_file_text(const char *path, size_t *out_len) {
  struct stat st;
  if(stat(path, &st) != 0) return NULL;
  if(st.st_size <= 0 || st.st_size > 4*1024*1024) return NULL;
  int fd = open(path, O_RDONLY);
  if(fd < 0) return NULL;
  char *buf = malloc(st.st_size + 1);
  if(!buf) { close(fd); return NULL; }
  ssize_t n = read(fd, buf, st.st_size);
  close(fd);
  if(n != st.st_size) { free(buf); return NULL; }
  buf[n] = 0;
  if(out_len) *out_len = n;
  return buf;
}


static int
write_file_bytes(const char *path, const void *data, size_t len) {
  ensure_cheats_dir();
  int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if(fd < 0) return -1;
  ssize_t n = write(fd, data, len);
  close(fd);
  return (n == (ssize_t)len) ? 0 : -1;
}


/* --------------------------------------------------------------------- */
/*  Cheat application                                                     */
/* --------------------------------------------------------------------- */

#define ROUND_PG_DOWN(x) ((uintptr_t)(x) & ~(uintptr_t)0x3fff)
#define ROUND_PG_UP(x)   (((uintptr_t)(x) + 0x3fff) & ~(uintptr_t)0x3fff)


/* Return values:
     0  -> bytes are now exactly `data` on the target side
    -1  -> pt_copyin failed (write rejected by kernel)
    -2  -> pt_copyout failed (read-back path broken)
    -3  -> readback bytes do not match `data` — write silently lost,
           usually a sign that the cheat is for a different patch
           level of the game and the page is not where we think it is
   The pattern mirrors etaHEN's CheatManager.cpp ToggleCheat: mprotect
   to RWX, write, mprotect again (defends against the kernel/AC
   re-protecting between calls), readback, byte-compare. */
static int
write_process_memory(pid_t pid, intptr_t addr, const uint8_t *data,
                     size_t len) {
  intptr_t page = (intptr_t)ROUND_PG_DOWN((uintptr_t)addr);
  size_t span = (size_t)(ROUND_PG_UP((uintptr_t)addr + len) -
                         (uintptr_t)page);

  kernel_mprotect(pid, page, span, PROT_READ | PROT_WRITE | PROT_EXEC);
  if(pt_copyin(pid, data, addr, len) < 0) return -1;
  kernel_mprotect(pid, page, span, PROT_READ | PROT_WRITE | PROT_EXEC);

  uint8_t verify[256];
  size_t off = 0;
  while(off < len) {
    size_t chunk = len - off;
    if(chunk > sizeof(verify)) chunk = sizeof(verify);
    if(pt_copyout(pid, addr + (intptr_t)off, verify, chunk) < 0) return -2;
    if(memcmp(verify, data + off, chunk) != 0) return -3;
    off += chunk;
  }
  return 0;
}


static int
apply_cheat(const char *title_id, int mod_index, int turn_on,
            char *err, size_t err_size) {
  pid_t pid = -1;
  intptr_t base = 0;
  char running_title[16];
  char path[256];

  if(!cheats_engine_enabled()) {
    snprintf(err, err_size, "cheat engine is disabled");
    return -1;
  }
  if(get_running_game(&pid, running_title, sizeof(running_title), &base) != 0) {
    snprintf(err, err_size, "no game is currently running");
    return -1;
  }
  /* Match running game ↔ cheat target after normalising both sides:
     strip any dash/underscore separator and uppercase the prefix.
     Running big-app reports the bare 9-char form (CUSA00004), but a
     cheat file may have been authored with a dash (SLUS-00001) or in
     lowercase. */
  {
    char run_norm[10], cheat_norm[10];
    if(!title_id_normalize(running_title, run_norm) ||
       !title_id_normalize(title_id, cheat_norm) ||
       strcmp(run_norm, cheat_norm) != 0) {
      snprintf(err, err_size,
               "running game (%s) does not match cheat target (%s)",
               running_title, title_id);
      return -1;
    }
  }
  int kind = 0;
  if(!find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) {
    snprintf(err, err_size,
             "no cheat file for %s. Drop a %s.json/.shn/.mc4 (or "
             "%s_<version>.json/.shn/.mc4) into %s via FTP.",
             title_id, title_id, title_id, CHEATS_DIR);
    return -1;
  }

  size_t len = 0;
  char *txt = read_file_text(path, &len);
  if(!txt) {
    snprintf(err, err_size, "could not read %s", path);
    return -1;
  }

  /* SHN is XML; MC4 is base64+AES-256-CBC of the same XML schema. Both
     get converted to the JSON shape apply_cheat() expects. */
  char *converted = NULL;
  if(kind == 2) { /* SHN */
    converted = shn_xml_to_json(txt, len);
    if(!converted) {
      free(txt);
      snprintf(err, err_size, "SHN parse failed for %s", path);
      return -1;
    }
  } else if(kind == 3) { /* MC4 */
    /* Don't free(txt) here on success — the common cleanup below does
       it. The previous code freed it twice on the MC4 happy path
       (here, then again at the end), tripping libc's heap consistency
       check, which calls abort() and reaps the loader process with
       SIGABRT — visible to the user as "Abort Trap 6" on the PS5
       screen plus a dropped HTTP server (the symptom that got
       reported as "Network Fetch Issue" the moment the toggle was
       clicked on an MC4 cheat). */
    char *xml = mc4_decrypt_to_xml(txt, len, NULL);
    if(!xml) {
      free(txt);
      snprintf(err, err_size,
               "MC4 decrypt failed for %s — file may be corrupt", path);
      return -1;
    }
    converted = shn_xml_to_json(xml, strlen(xml));
    free(xml);
    if(!converted) {
      free(txt);
      snprintf(err, err_size, "MC4 XML parse failed for %s", path);
      return -1;
    }
  }

  cJSON *root = cJSON_Parse(converted ? converted : txt);
  free(converted);
  free(txt);
  if(!root) {
    snprintf(err, err_size, "cheat parse failed for %s", path);
    return -1;
  }

  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if(!cJSON_IsArray(mods) ||
     mod_index < 0 ||
     mod_index >= cJSON_GetArraySize(mods)) {
    snprintf(err, err_size, "cheat index out of range");
    cJSON_Delete(root);
    return -1;
  }
  cJSON *mod = cJSON_GetArrayItem(mods, mod_index);
  cJSON *type_j = cJSON_GetObjectItem(mod, "type");
  const char *type = (cJSON_IsString(type_j) && type_j->valuestring)
                       ? type_j->valuestring : "checkbox";
  cJSON *memory = cJSON_GetObjectItem(mod, "memory");
  if(!cJSON_IsArray(memory) || cJSON_GetArraySize(memory) == 0) {
    snprintf(err, err_size, "mod has no memory entries");
    cJSON_Delete(root);
    return -1;
  }

  /* "button" mods are one-shot writes — always apply the "on" payload
     regardless of toggle state. */
  int effective_on = (!strcasecmp(type, "button")) ? 1 : (turn_on ? 1 : 0);

  if(pt_attach(pid) < 0) {
    snprintf(err, err_size, "pt_attach failed (errno=%d)", errno);
    cJSON_Delete(root);
    return -1;
  }

  int rc = 0;
  cJSON *m;
  cJSON_ArrayForEach(m, memory) {
    cJSON *off_j  = cJSON_GetObjectItem(m, "offset");
    cJSON *on_j   = cJSON_GetObjectItem(m, "on");
    cJSON *off2_j = cJSON_GetObjectItem(m, "off");
    cJSON *abs_j  = cJSON_GetObjectItem(m, "absolute");
    if(!cJSON_IsString(off_j) || !cJSON_IsString(on_j) ||
       !cJSON_IsString(off2_j)) {
      snprintf(err, err_size, "memory entry missing offset/on/off");
      rc = -1;
      break;
    }
    uint64_t off = parse_offset(off_j->valuestring);
    int absolute = cJSON_IsTrue(abs_j) ? 1 : 0;

    uint8_t *on_bytes = NULL, *off_bytes = NULL;
    size_t on_len = 0, off_len = 0;
    if(parse_hex_bytes(on_j->valuestring,  &on_bytes,  &on_len) != 0 ||
       parse_hex_bytes(off2_j->valuestring, &off_bytes, &off_len) != 0) {
      snprintf(err, err_size, "memory hex parse failed");
      free(on_bytes); free(off_bytes);
      rc = -1;
      break;
    }

    intptr_t addr = absolute ? (intptr_t)off : (base + (intptr_t)off);
    const uint8_t *data = effective_on ? on_bytes : off_bytes;
    size_t        wlen  = effective_on ? on_len   : off_len;

    if(wlen == 0 || !data) {
      free(on_bytes); free(off_bytes);
      continue;
    }

    {
      int wrc = write_process_memory(pid, addr, data, wlen);
      if(wrc != 0) {
        const char *what =
          (wrc == -1) ? "kernel rejected the write"
        : (wrc == -2) ? "could not read back the patched bytes"
        :               "patch did not stick — wrong cheat for this "
                        "build of the game (try a fresh download or "
                        "match the eboot patch level)";
        snprintf(err, err_size, "%s at 0x%lx (len %zu)",
                 what, (long)addr, wlen);
        free(on_bytes); free(off_bytes);
        rc = -1;
        break;
      }
    }

    free(on_bytes);
    free(off_bytes);
  }

  /* Persist enabled flag for checkbox cheats so the UI reflects state
     after a reload. Buttons are stateless. We only write back to JSON
     files — overwriting an SHN with our JSON shape would corrupt the
     original. */
  if(rc == 0 && strcasecmp(type, "button") != 0 && kind == 1) {
    cJSON_DeleteItemFromObject(mod, "_sonic_enabled");
    cJSON_AddBoolToObject(mod, "_sonic_enabled", turn_on ? 1 : 0);
    char *out = cJSON_Print(root);
    if(out) {
      write_file_bytes(path, out, strlen(out));
      free(out);
    }
  }

  pt_detach(pid, 0);
  cJSON_Delete(root);
  return rc;
}


/* --------------------------------------------------------------------- */
/*  HTTP handlers                                                         */
/* --------------------------------------------------------------------- */

/* Tiny JSON scalar-string extractor. Looks up the first occurrence of
   "key": "..." in `json` and copies the (unescaped, NUL-terminated)
   value into `out`. Returns 1 on success. Used here to peek at a
   cheat file's "name" field without parsing the whole document. */
static int
local_json_extract_string(const char *json, const char *key,
                          char *out, size_t out_size) {
  if(!json || !key || !out || out_size < 2) return 0;
  char needle[64];
  int wn = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if(wn <= 0 || (size_t)wn >= sizeof(needle)) return 0;
  const char *p = strstr(json, needle);
  if(!p) return 0;
  p += strlen(needle);
  while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if(*p != ':') return 0;
  p++;
  while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if(*p != '"') return 0;
  p++;
  size_t i = 0;
  while(*p && *p != '"' && i < out_size - 1) {
    if(*p == '\\' && p[1]) { out[i++] = p[1]; p += 2; }
    else                   { out[i++] = *p++;            }
  }
  out[i] = 0;
  return i > 0 ? 1 : 0;
}


/* Read the friendly game name out of a cheat file. Cheap version — we
   only read the first 4 KiB and look for either:
     - JSON   : "name": "..."
     - SHN/XML: <Trainer ... Game="..." or <Trainer ... GameName="..."
   On no match, returns an empty string. */
static void
read_cheat_display_name(const char *path, int kind,
                        char *out, size_t out_size) {
  out[0] = 0;
  int fd = open(path, O_RDONLY);
  if(fd < 0) return;
  char head[4096];
  ssize_t n = read(fd, head, sizeof(head)-1);
  close(fd);
  if(n <= 0) return;
  head[n] = 0;

  if(kind == 1) {
    /* JSON. */
    local_json_extract_string(head, "name", out, out_size);
  } else if(kind == 2) {
    /* SHN — pull the Trainer attribute. */
    const char *t = strstr(head, "<Trainer");
    if(t) {
      const char *end = strchr(t, '>');
      if(end) {
        char tag[1024];
        size_t tn = (size_t)(end - t);
        if(tn < sizeof(tag)) {
          memcpy(tag, t, tn); tag[tn] = 0;
          size_t alen = 0;
          const char *v = xml_find_attr(tag, "Game", &alen);
          if(!v) v = xml_find_attr(tag, "GameName", &alen);
          if(v && alen > 0 && alen < out_size) {
            memcpy(out, v, alen);
            out[alen] = 0;
          }
        }
      }
    }
  }
}


static enum MHD_Result
list_cheats(struct MHD_Connection *conn) {
  ensure_cheats_dir();

  cJSON *root = cJSON_CreateObject();
  cJSON *files = cJSON_AddArrayToObject(root, "files");

  /* Walk all .json/.shn/.mc4 files; surface the unique title-id prefix
     of each plus the friendly display name pulled from the file. The
     UI uses the name for searching and rendering. */
  DIR *d = opendir(CHEATS_DIR);
  if(d) {
    struct dirent *ent;
    char seen[256][16];
    int seen_n = 0;
    while((ent = readdir(d))) {
      const char *name = ent->d_name;
      if(name[0] == '.') continue;
      if(!recognised_cheat_extension(name)) continue;
      char id[16];
      if(!extract_title_id_prefix(name, id, sizeof(id))) continue;
      int dup = 0;
      for(int i=0; i<seen_n; i++) {
        if(!strcmp(seen[i], id)) { dup = 1; break; }
      }
      if(dup) continue;
      if(seen_n < (int)(sizeof(seen)/sizeof(seen[0]))) {
        strncpy(seen[seen_n], id, 16);
        seen[seen_n][15] = 0;
        seen_n++;
      }

      /* Find the best file for this id, read its display name. */
      char path[256];
      int  kind = 0;
      char display[256];
      display[0] = 0;
      if(find_cheat_file_for_title(id, path, sizeof(path), &kind)) {
        read_cheat_display_name(path, kind, display, sizeof(display));
      }

      cJSON *e = cJSON_CreateObject();
      cJSON_AddStringToObject(e, "titleId", id);
      cJSON_AddStringToObject(e, "name",    display[0] ? display : id);
      cJSON_AddItemToArray(files, e);
    }
    closedir(d);
  }

  pid_t pid = -1;
  intptr_t base = 0;
  char running[16] = {0};
  if(get_running_game(&pid, running, sizeof(running), &base) == 0) {
    cJSON *running_obj = cJSON_AddObjectToObject(root, "running");
    cJSON_AddStringToObject(running_obj, "titleId", running);
    cJSON_AddNumberToObject(running_obj, "pid", pid);
    char base_hex[32];
    snprintf(base_hex, sizeof(base_hex), "0x%lx", (long)base);
    cJSON_AddStringToObject(running_obj, "imageBase", base_hex);
  } else {
    cJSON_AddNullToObject(root, "running");
  }

  enum MHD_Result ret = serve_json_object(conn, MHD_HTTP_OK, root);
  cJSON_Delete(root);
  return ret;
}


static enum MHD_Result
get_cheats_for(struct MHD_Connection *conn, const char *title_id) {
  if(!is_safe_title_id(title_id)) {
    return serve_error(conn, MHD_HTTP_BAD_REQUEST, "bad titleId");
  }
  char path[256];
  int kind = 0;
  if(!find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) {
    return serve_error(conn, MHD_HTTP_NOT_FOUND, "no cheat file");
  }
  size_t len = 0;
  char *txt = read_file_text(path, &len);
  if(!txt) {
    return serve_error(conn, MHD_HTTP_NOT_FOUND, "could not read cheat file");
  }

  if(kind == 1) {
    /* Native JSON — pass through. */
    return serve_buffer(conn, MHD_HTTP_OK, "application/json", txt, len, 1);
  }
  if(kind == 2) {
    /* SHN — convert XML to JSON on the fly. */
    char *json = shn_xml_to_json(txt, len);
    free(txt);
    if(!json) {
      return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "SHN parse failed");
    }
    return serve_buffer(conn, MHD_HTTP_OK, "application/json",
                        json, strlen(json), 1);
  }
  /* MC4 — base64 + AES-256-CBC of an SHN-shaped XML. Decrypt then run
     the same XML→JSON converter. */
  {
    char *xml = mc4_decrypt_to_xml(txt, len, NULL);
    free(txt);
    if(!xml) {
      return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "MC4 decrypt failed");
    }
    char *json = shn_xml_to_json(xml, strlen(xml));
    free(xml);
    if(!json) {
      return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "MC4 XML parse failed");
    }
    return serve_buffer(conn, MHD_HTTP_OK, "application/json",
                        json, strlen(json), 1);
  }
}


static enum MHD_Result
delete_cheats_for(struct MHD_Connection *conn, const char *title_id) {
  if(!is_safe_title_id(title_id)) {
    return serve_error(conn, MHD_HTTP_BAD_REQUEST, "bad titleId");
  }
  char path[256];
  int kind = 0;
  if(find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) {
    if(unlink(path) != 0 && errno != ENOENT) {
      return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, strerror(errno));
    }
  }
  cJSON *r = cJSON_CreateObject();
  cJSON_AddBoolToObject(r, "ok", 1);
  enum MHD_Result ret = serve_json_object(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  return ret;
}


static enum MHD_Result
toggle_cheat(struct MHD_Connection *conn) {
  const char *title_id = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "titleId");
  const char *idx_s    = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "index");
  const char *on_s     = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "on");

  if(!title_id || !idx_s || !on_s) {
    return serve_error(conn, MHD_HTTP_BAD_REQUEST, "missing args");
  }
  int idx = atoi(idx_s);
  int on  = (strcmp(on_s, "0") != 0);

  char err[256] = {0};
  int rc = apply_cheat(title_id, idx, on, err, sizeof(err));
  cJSON *r = cJSON_CreateObject();
  cJSON_AddBoolToObject(r, "ok", rc == 0);
  if(rc != 0) {
    cJSON_AddStringToObject(r, "error", err[0] ? err : "apply failed");
  } else {
    cJSON_AddBoolToObject(r, "enabled", on ? 1 : 0);
  }
  enum MHD_Result ret = serve_json_object(conn,
                                          rc == 0 ? MHD_HTTP_OK
                                                  : MHD_HTTP_BAD_REQUEST, r);
  cJSON_Delete(r);
  return ret;
}


/* --------------------------------------------------------------------- */
/*  Auto-download from GoldHEN_Cheat_Repository / "etaHEN PS5_Cheats"     */
/* --------------------------------------------------------------------- */

/* Each upstream repo carries three parallel format trees rooted at
   <fmt>.txt index files and <fmt>/<file>.<ext> data dirs. We walk all
   three so .json/.shn/.mc4 cheats all land on disk. */
struct cheat_repo_fmt {
  const char *name;     /* "json" | "shn" | "mc4" */
  const char *index;    /* full URL to the .txt index */
  const char *base;     /* directory base URL (with trailing slash) */
};

static const struct cheat_repo_fmt PS5_CHEATS_FORMATS[] = {
  {"json", "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/json.txt",
           "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/json/"},
  {"shn",  "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/shn.txt",
           "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/shn/"},
  {"mc4",  "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/mc4.txt",
           "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/mc4/"},
};

static const struct cheat_repo_fmt GOLDHEN_FORMATS[] = {
  {"json", "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/json.txt",
           "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/json/"},
  {"shn",  "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/shn.txt",
           "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/shn/"},
  {"mc4",  "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/mc4.txt",
           "https://raw.githubusercontent.com/GoldHEN/GoldHEN_Cheat_Repository/main/mc4/"},
};

/* TeeKay87/HEN-Cheats-Collection — community-curated bundle that mixes
   cheats from etaHEN and GoldHEN sources. Default branch is master
   (not main) and cheats are nested one level deeper under cheats/. */
static const struct cheat_repo_fmt HEN_COLLECTION_FORMATS[] = {
  {"json", "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/json.txt",
           "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/json/"},
  {"shn",  "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/shn.txt",
           "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/shn/"},
  {"mc4",  "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/mc4.txt",
           "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/mc4/"},
};

#define CHEAT_FORMATS_PER_REPO ((int)(sizeof(PS5_CHEATS_FORMATS)/sizeof(PS5_CHEATS_FORMATS[0])))

static const struct cheat_repo_fmt *
cheat_formats_for(const char *source, int *count_out) {
  *count_out = CHEAT_FORMATS_PER_REPO;
  if(source && !strcasecmp(source, "goldhen"))    return GOLDHEN_FORMATS;
  if(source && !strcasecmp(source, "hencollection")) return HEN_COLLECTION_FORMATS;
  return PS5_CHEATS_FORMATS;
}


/* Find an entry in `json.txt` whose filename starts with `<title_id>_`.
   The first match wins. Returns a malloc'd filename on success. */
static char*
find_index_match(const char *index_text, const char *title_id) {
  size_t idlen = strlen(title_id);
  const char *p = index_text;
  while(p && *p) {
    const char *nl = strchr(p, '\n');
    size_t llen = nl ? (size_t)(nl - p) : strlen(p);
    if(llen > idlen + 1 &&
       !strncmp(p, title_id, idlen) &&
       p[idlen] == '_') {
      const char *eq = memchr(p, '=', llen);
      size_t flen = eq ? (size_t)(eq - p) : llen;
      char *out = malloc(flen + 1);
      memcpy(out, p, flen);
      out[flen] = 0;
      return out;
    }
    if(!nl) break;
    p = nl + 1;
  }
  return NULL;
}


static enum MHD_Result
download_cheats(struct MHD_Connection *conn) {
  const char *title_id = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "titleId");
  const char *source   = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "source");
  if(!title_id || !is_safe_title_id(title_id)) {
    return serve_error(conn, MHD_HTTP_BAD_REQUEST, "bad titleId");
  }
  if(!source) source = "ps5cheats";

  int n_fmts = 0;
  const struct cheat_repo_fmt *fmts = cheat_formats_for(source, &n_fmts);

  /* Walk every format index in priority order and grab the first hit.
     JSON wins over SHN wins over MC4 because the engine can apply JSON
     natively, SHN is auto-converted, and MC4 needs offline decryption. */
  char *match = NULL;
  const struct cheat_repo_fmt *hit = NULL;
  for(int i = 0; i < n_fmts && !match; i++) {
    size_t ilen = 0;
    uint8_t *index = http_get(fmts[i].index, &ilen);
    if(!index || ilen == 0) { free(index); continue; }
    uint8_t *idx_z = realloc(index, ilen + 1);
    if(!idx_z) { free(index); continue; }
    idx_z[ilen] = 0;
    match = find_index_match((const char*)idx_z, title_id);
    free(idx_z);
    if(match) hit = &fmts[i];
  }
  if(!match || !hit) {
    return serve_error(conn, MHD_HTTP_NOT_FOUND,
        "no upstream cheat for that title (checked json/shn/mc4)");
  }

  char url[512];
  snprintf(url, sizeof(url), "%s%s", hit->base, match);
  size_t flen = 0;
  uint8_t *body = http_get(url, &flen);
  if(!body || flen == 0) {
    free(match);
    return serve_error(conn, MHD_HTTP_BAD_GATEWAY, "could not fetch cheat file");
  }

  /* For JSON we keep the canonical <titleid>.json filename — this
     matches the path the previous (working) build wrote so any old
     install layout keeps resolving. For SHN/MC4 we keep the upstream
     version suffix (e.g. CUSA00004_01.07.shn) so multiple versions
     can coexist; the engine resolves them via find_cheat_file_for_title. */
  ensure_cheats_dir();
  char path[256];
  if(!strcmp(hit->name, "json")) {
    snprintf(path, sizeof(path), "%s/%s.json", CHEATS_DIR, title_id);
  } else {
    snprintf(path, sizeof(path), "%s/%s", CHEATS_DIR, match);
  }
  int wrc = write_file_bytes(path, body, flen);
  free(body);
  if(wrc != 0) {
    free(match);
    return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "write failed");
  }

  cJSON *r = cJSON_CreateObject();
  cJSON_AddBoolToObject(r, "ok", 1);
  cJSON_AddStringToObject(r, "titleId", title_id);
  cJSON_AddStringToObject(r, "source",  source);
  cJSON_AddStringToObject(r, "format",  hit->name);
  cJSON_AddStringToObject(r, "upstream", match);
  cJSON_AddStringToObject(r, "path",    path);
  cJSON_AddNumberToObject(r, "size", (double)flen);
  enum MHD_Result ret = serve_json_object(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  free(match);
  return ret;
}


/* Bulk download — fetch every cheat file in the chosen upstream repo
   and stage them under /data/sonic-loader/cheats/. The work runs on
   a background thread so the UI can poll progress and render a status
   bar. After the loop finishes, a verification pass checks every
   expected on-disk file is actually present and reports any missing
   names. The on-disk filename is preserved verbatim from the upstream
   json.txt index (e.g. CUSA00004_01.07.json) so multiple versions for
   the same title can coexist. */

#define DL_STATE_IDLE     0
#define DL_STATE_RUNNING  1
#define DL_STATE_DONE     2
#define DL_STATE_ERROR    3

#define DL_MAX_MISSING    20

typedef struct {
  pthread_mutex_t lock;
  int    state;             /* DL_STATE_* */
  char   source[16];        /* "ps5cheats" / "goldhen" */
  int    total;             /* indexed entries */
  int    downloaded;
  int    skipped;
  int    failed;
  int    verified;          /* 1 once verification ran */
  int    missing_count;     /* # of expected files not on disk afterwards */
  char   missing[DL_MAX_MISSING][96];
  char   current[128];      /* filename currently being fetched */
  char   error[256];        /* set on DL_STATE_ERROR */
  time_t started_at;
  time_t finished_at;
} dl_progress_t;

static dl_progress_t g_dl = {
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .state = DL_STATE_IDLE,
};


static void
dl_set_state_locked(int new_state) {
  g_dl.state = new_state;
  if(new_state == DL_STATE_DONE || new_state == DL_STATE_ERROR) {
    g_dl.finished_at = time(NULL);
    g_dl.current[0] = 0;
  }
}


/* The actual worker. Runs on its own thread so the calling HTTP
   handler can return immediately and the UI can poll for progress. */
static void*
download_all_thread_fn(void *arg) {
  (void)arg;
  syscall(SYS_thr_set_name, -1, "cheat-dl");

  /* Snapshot source choice. */
  char source[16];
  pthread_mutex_lock(&g_dl.lock);
  strncpy(source, g_dl.source, sizeof(source));
  source[sizeof(source)-1] = 0;
  pthread_mutex_unlock(&g_dl.lock);

  int n_fmts = 0;
  const struct cheat_repo_fmt *fmts = cheat_formats_for(source, &n_fmts);

  /* Fetch every format's index up-front so we can publish a true total
     for the progress bar before starting downloads. */
  uint8_t *idx_bufs[CHEAT_FORMATS_PER_REPO] = {0};
  for(int i = 0; i < n_fmts; i++) {
    size_t ilen = 0;
    uint8_t *index = http_get(fmts[i].index, &ilen);
    if(!index || ilen == 0) {
      free(index);
      /* Missing one index isn't fatal — keep going for the others. */
      continue;
    }
    uint8_t *idx_z = realloc(index, ilen + 1);
    if(!idx_z) { free(index); continue; }
    idx_z[ilen] = 0;
    idx_bufs[i] = idx_z;
  }

  /* Bail only if every index failed. */
  int any_index = 0;
  for(int i = 0; i < n_fmts; i++) if(idx_bufs[i]) { any_index = 1; break; }
  if(!any_index) {
    pthread_mutex_lock(&g_dl.lock);
    snprintf(g_dl.error, sizeof(g_dl.error),
             "could not fetch any index for %s repo", source);
    dl_set_state_locked(DL_STATE_ERROR);
    pthread_mutex_unlock(&g_dl.lock);
    return NULL;
  }

  ensure_cheats_dir();

  /* Pre-pass: total entries across all available indexes. */
  int planned_total = 0;
  for(int i = 0; i < n_fmts; i++) {
    if(!idx_bufs[i]) continue;
    for(const char *p = (const char*)idx_bufs[i]; p && *p; ) {
      const char *nl = strchr(p, '\n');
      size_t llen = nl ? (size_t)(nl - p) : strlen(p);
      while(llen > 0 && (p[llen-1] == '\r' || p[llen-1] == ' ' ||
                         p[llen-1] == '\t')) llen--;
      if(llen > 0) planned_total++;
      p = nl ? nl + 1 : NULL;
    }
  }
  pthread_mutex_lock(&g_dl.lock);
  g_dl.total = planned_total;
  pthread_mutex_unlock(&g_dl.lock);

  /* We collect every expected (validated) filename so the verification
     pass at the end can check the disk. */
  size_t expected_cap = 256;
  size_t expected_n   = 0;
  char (*expected)[96] = malloc(expected_cap * sizeof(*expected));

  int local_total = 0, local_downloaded = 0, local_skipped = 0, local_failed = 0;

  /* Walk every format index (json.txt → shn.txt → mc4.txt) line-by-line.
     The on-disk filename is preserved verbatim from upstream so the
     extension survives, and find_cheat_file_for_title() maps the user's
     active title back to whichever format was downloaded. */
  for(int fi = 0; fi < n_fmts; fi++) {
    if(!idx_bufs[fi]) continue;
    const char *fmt_base = fmts[fi].base;

    const char *p = (const char*)idx_bufs[fi];
    while(p && *p) {
      const char *nl = strchr(p, '\n');
      size_t llen = nl ? (size_t)(nl - p) : strlen(p);
      if(llen == 0) { p = nl ? nl + 1 : NULL; continue; }

      while(llen > 0 && (p[llen-1] == '\r' || p[llen-1] == ' ' ||
                         p[llen-1] == '\t')) llen--;
      if(llen == 0) { p = nl ? nl + 1 : NULL; continue; }

      const char *eq = memchr(p, '=', llen);
      size_t flen = eq ? (size_t)(eq - p) : llen;
      if(flen == 0 || flen > 95) {
        local_total++; local_failed++;
        pthread_mutex_lock(&g_dl.lock);
        g_dl.total = local_total > planned_total ? local_total : planned_total;
        g_dl.failed = local_failed;
        pthread_mutex_unlock(&g_dl.lock);
        p = nl ? nl + 1 : NULL;
        continue;
      }

      char fname[96];
      memcpy(fname, p, flen);
      fname[flen] = 0;

      char title[16];
      int valid = extract_title_id_prefix(fname, title, sizeof(title));

      int safe = 1;
      for(size_t i=0; i<flen && safe; i++) {
        char c = fname[i];
        if(!isalnum((unsigned char)c) && c != '.' && c != '-' &&
           c != '_') safe = 0;
      }
      if(!valid || !safe || strstr(fname, "..")) {
        local_total++; local_skipped++;
        pthread_mutex_lock(&g_dl.lock);
        g_dl.skipped = local_skipped;
        pthread_mutex_unlock(&g_dl.lock);
        p = nl ? nl + 1 : NULL;
        continue;
      }

      local_total++;

      /* Track expected file for verification. */
      if(expected_n + 1 > expected_cap) {
        expected_cap *= 2;
        expected = realloc(expected, expected_cap * sizeof(*expected));
      }
      strncpy(expected[expected_n], fname, sizeof(expected[expected_n])-1);
      expected[expected_n][sizeof(expected[expected_n])-1] = 0;
      expected_n++;

      /* Publish current filename. */
      pthread_mutex_lock(&g_dl.lock);
      strncpy(g_dl.current, fname, sizeof(g_dl.current)-1);
      g_dl.current[sizeof(g_dl.current)-1] = 0;
      pthread_mutex_unlock(&g_dl.lock);

      char url[512];
      snprintf(url, sizeof(url), "%s%s", fmt_base, fname);
      size_t blen = 0;
      uint8_t *body = http_get(url, &blen);
      if(!body || blen == 0) {
        free(body);
        local_failed++;
        pthread_mutex_lock(&g_dl.lock);
        g_dl.failed = local_failed;
        pthread_mutex_unlock(&g_dl.lock);
        p = nl ? nl + 1 : NULL;
        continue;
      }

      char path[256];
      snprintf(path, sizeof(path), "%s/%s", CHEATS_DIR, fname);
      if(write_file_bytes(path, body, blen) == 0) {
        local_downloaded++;
      } else {
        local_failed++;
      }
      free(body);

      pthread_mutex_lock(&g_dl.lock);
      g_dl.downloaded = local_downloaded;
      g_dl.failed     = local_failed;
      pthread_mutex_unlock(&g_dl.lock);

      p = nl ? nl + 1 : NULL;
    }
    free(idx_bufs[fi]);
    idx_bufs[fi] = NULL;
  }

  /* Verification: for every filename we attempted (in `expected[]`),
     stat the corresponding path on disk. Anything missing goes into
     g_dl.missing[] up to DL_MAX_MISSING for display. */
  int missing_count = 0;
  pthread_mutex_lock(&g_dl.lock);
  g_dl.missing_count = 0;
  pthread_mutex_unlock(&g_dl.lock);

  for(size_t i=0; i<expected_n; i++) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", CHEATS_DIR, expected[i]);
    struct stat st;
    if(stat(path, &st) != 0 || st.st_size <= 0) {
      pthread_mutex_lock(&g_dl.lock);
      if(g_dl.missing_count < DL_MAX_MISSING) {
        strncpy(g_dl.missing[g_dl.missing_count], expected[i],
                sizeof(g_dl.missing[g_dl.missing_count])-1);
        g_dl.missing[g_dl.missing_count][sizeof(g_dl.missing[g_dl.missing_count])-1] = 0;
      }
      g_dl.missing_count++;
      pthread_mutex_unlock(&g_dl.lock);
      missing_count++;
    }
  }
  free(expected);

  pthread_mutex_lock(&g_dl.lock);
  g_dl.verified = 1;
  dl_set_state_locked(DL_STATE_DONE);
  pthread_mutex_unlock(&g_dl.lock);

  notify("سونيك لودر: اكتمل مستودع %s — %d غش، %d فشل، %d مفقود",
         source, local_downloaded, local_failed, missing_count);

  return NULL;
}


/* Kick off the background download. */
static enum MHD_Result
download_all_start(struct MHD_Connection *conn) {
  const char *source = MHD_lookup_connection_value(conn,
                            MHD_GET_ARGUMENT_KIND, "source");
  if(!source) source = "ps5cheats";
  if(strcasecmp(source, "ps5cheats")     != 0 &&
     strcasecmp(source, "goldhen")       != 0 &&
     strcasecmp(source, "hencollection") != 0) {
    return serve_error(conn, MHD_HTTP_BAD_REQUEST,
                       "source must be 'ps5cheats', 'goldhen', or 'hencollection'");
  }

  pthread_mutex_lock(&g_dl.lock);
  if(g_dl.state == DL_STATE_RUNNING) {
    pthread_mutex_unlock(&g_dl.lock);
    return serve_error(conn, MHD_HTTP_CONFLICT,
                       "a repository download is already in progress; "
                       "wait for it to finish or check status");
  }
  /* Reset state for a new run. */
  memset(&g_dl.missing,    0, sizeof(g_dl.missing));
  memset(g_dl.error,       0, sizeof(g_dl.error));
  memset(g_dl.current,     0, sizeof(g_dl.current));
  strncpy(g_dl.source, source, sizeof(g_dl.source)-1);
  g_dl.source[sizeof(g_dl.source)-1] = 0;
  g_dl.total = 0;
  g_dl.downloaded = 0;
  g_dl.skipped = 0;
  g_dl.failed = 0;
  g_dl.verified = 0;
  g_dl.missing_count = 0;
  g_dl.started_at  = time(NULL);
  g_dl.finished_at = 0;
  g_dl.state = DL_STATE_RUNNING;
  pthread_mutex_unlock(&g_dl.lock);

  pthread_t t;
  pthread_attr_t a;
  pthread_attr_init(&a);
  pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&t, &a, download_all_thread_fn, NULL) != 0) {
    pthread_attr_destroy(&a);
    pthread_mutex_lock(&g_dl.lock);
    snprintf(g_dl.error, sizeof(g_dl.error),
             "pthread_create failed: %s", strerror(errno));
    dl_set_state_locked(DL_STATE_ERROR);
    pthread_mutex_unlock(&g_dl.lock);
    return serve_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                       "could not start download thread");
  }
  pthread_attr_destroy(&a);

  cJSON *r = cJSON_CreateObject();
  cJSON_AddBoolToObject(r,   "ok",     1);
  cJSON_AddStringToObject(r, "state",  "running");
  cJSON_AddStringToObject(r, "source", source);
  enum MHD_Result ret = serve_json_object(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  return ret;
}


/* Snapshot the current progress as JSON so the UI can render the bar. */
static enum MHD_Result
download_all_status(struct MHD_Connection *conn) {
  cJSON *r = cJSON_CreateObject();

  pthread_mutex_lock(&g_dl.lock);
  const char *state_str =
    g_dl.state == DL_STATE_RUNNING ? "running" :
    g_dl.state == DL_STATE_DONE    ? "done"    :
    g_dl.state == DL_STATE_ERROR   ? "error"   :
                                     "idle";
  cJSON_AddStringToObject(r, "state",         state_str);
  cJSON_AddStringToObject(r, "source",        g_dl.source);
  cJSON_AddNumberToObject(r, "total",         g_dl.total);
  cJSON_AddNumberToObject(r, "downloaded",    g_dl.downloaded);
  cJSON_AddNumberToObject(r, "skipped",       g_dl.skipped);
  cJSON_AddNumberToObject(r, "failed",        g_dl.failed);
  cJSON_AddBoolToObject(r,   "verified",      g_dl.verified ? 1 : 0);
  cJSON_AddNumberToObject(r, "missingCount",  g_dl.missing_count);
  cJSON_AddStringToObject(r, "current",       g_dl.current);
  cJSON_AddNumberToObject(r, "startedAt",     (double)g_dl.started_at);
  cJSON_AddNumberToObject(r, "finishedAt",    (double)g_dl.finished_at);

  /* Up to DL_MAX_MISSING names so the UI can render examples. */
  int show = g_dl.missing_count;
  if(show > DL_MAX_MISSING) show = DL_MAX_MISSING;
  cJSON *missing = cJSON_AddArrayToObject(r, "missing");
  for(int i=0; i<show; i++) {
    cJSON_AddItemToArray(missing, cJSON_CreateString(g_dl.missing[i]));
  }
  if(g_dl.error[0]) {
    cJSON_AddStringToObject(r, "error", g_dl.error);
  }
  pthread_mutex_unlock(&g_dl.lock);

  enum MHD_Result ret = serve_json_object(conn, MHD_HTTP_OK, r);
  cJSON_Delete(r);
  return ret;
}


/* --------------------------------------------------------------------- */
/*  Top-level dispatcher                                                  */
/* --------------------------------------------------------------------- */

enum MHD_Result
cheats_request(struct MHD_Connection *conn, const char *url,
               const char *method, const char *upload_data,
               size_t *upload_data_size, void **con_cls) {
  (void)upload_data;
  (void)upload_data_size;
  (void)con_cls;

  if(strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
    return serve_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED,
                       "use GET; uploads go through FTP (port 2121) into "
                       "/data/sonic-loader/cheats/");
  }

  if(!strcmp(url, "/api/cheats")) {
    const char *title_id = MHD_lookup_connection_value(conn,
                                MHD_GET_ARGUMENT_KIND, "titleId");
    if(title_id) {
      return get_cheats_for(conn, title_id);
    }
    return list_cheats(conn);
  }
  if(!strcmp(url, "/api/cheats/toggle")) {
    return toggle_cheat(conn);
  }
  if(!strcmp(url, "/api/cheats/delete")) {
    const char *title_id = MHD_lookup_connection_value(conn,
                                MHD_GET_ARGUMENT_KIND, "titleId");
    return delete_cheats_for(conn, title_id);
  }
  if(!strcmp(url, "/api/cheats/download")) {
    return download_cheats(conn);
  }
  if(!strcmp(url, "/api/cheats/download-all")) {
    return download_all_start(conn);
  }
  if(!strcmp(url, "/api/cheats/download-all/status")) {
    return download_all_status(conn);
  }
  return serve_error(conn, MHD_HTTP_NOT_FOUND, "no such endpoint");
}
