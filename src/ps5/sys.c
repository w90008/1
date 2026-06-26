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

#include <arpa/inet.h>
#include <execinfo.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>

#include <ps5/kernel.h>

#include "elfldr.h"
#include "fs.h"
#include "hbldr.h"
#include "http.h"
#include "notify.h"
#include "pt.h"
#include "sys.h"
#include "websrv.h"
#include "kstuff_updater.h"
#include "smp_updater.h"


#define INCASSET(name, file)			\
  __asm__(".section .rodata\n"			\
	  ".global " #name "\n"			\
	  ".global " #name "_end\n"		\
	  ".global " #name "_size\n"		\
	  ".align 16\n"				\
	  #name ":\n"				\
	  ".incbin \"" file "\"\n"		\
	  #name "_end:\n"			\
	  #name "_size:\n"			\
	  ".quad " #name "_end - " #name "\n"	\
	  ".previous\n");			\
  extern const uint8_t name[];			\
  extern const size_t name##_size;


INCASSET(ftpsrv_elf,            "payloads/ftpsrv.elf");
INCASSET(klogsrv_elf,           "payloads/klogsrv.elf");
INCASSET(backpork_elf,          "payloads/backpork.elf");
/* np-fake-signin is no longer embedded — see sys_spawn_np_fake_signin
   below for the disk-cached lazy-download path. */
INCASSET(np_restore_account_elf,"payloads/np-restore-account.elf");
INCASSET(garlic_worker_elf,     "payloads/garlic-worker.elf");
INCASSET(garlic_savemgr_elf,    "payloads/garlic-savemgr.elf");
INCASSET(nanodns_elf,           "payloads/nanodns.elf");
INCASSET(ps5_app_dumper_elf,    "payloads/ps5-app-dumper.elf");
INCASSET(dpi_elf,               "payloads/dpi.elf");
INCASSET(smp_icon_png,          "payloads/smp_icon.png");
INCASSET(lapyjb_elf,            "payloads/lapyjb.elf");

#define GARLIC_WORKER_PROC_NAME "garlic-worker.elf"
#define NANODNS_PROC_NAME       "nanodns.elf"
#define LAPYJB_PROC_NAME        "lapyjb.elf"

/* BackPork lives behind a Settings toggle — it sets its own process
   name to "backpork.elf" (see PAYLOAD_NAME in BackPork-master/main.c)
   so we can find/kill it later without spawning a tracker thread. */
#define BACKPORK_PROC_NAME "backpork.elf"

/* SMP daemon lives behind a Settings toggle. SMP sets its own thread
   name to PAYLOAD_NAME ("shadowmountplus.elf") via SYS_thr_set_name in
   ShadowMountPlus-main/src/main.c — the proc-name we match against
   has to include the .elf suffix or every sys_find_pid() lookup
   misses it (and the toggle UI can't tell the daemon is running). */
#define SHADOWMOUNT_PROC_NAME "shadowmountplus.elf"

/* ftpsrv toggle / port config. ftpsrv sets its own thread name to
   "ftpsrv.elf" via syscall; the spawn argv[0] only matters for the
   `-p PORT` flag we hand it. */
#define FTPSRV_PROC_NAME "ftpsrv.elf"
#define FTPSRV_DEFAULT_PORT 2121

/* kstuff is no longer baked into Sonic Loader. The user installs it via
   Settings → "Install kstuff-lite + ShadowMountPlus". Path stays the same
   so existing tools (and the updater endpoint) keep finding it. */
#define KSTUFF_INSTALL_PATH "/data/kstuff.elf"

/* Optional override SMP — written by /api/smp/install. If present, this
   wins over the embedded copy on next boot. Lives in the SMP state
   directory alongside config.ini / daemon.lock / smp_icon.png. */
#define SHADOWMOUNT_DIR          "/data/shadowmount"
#define SHADOWMOUNT_INSTALL_PATH "/data/shadowmount/shadowmountplus.elf"

/* SMP's per-notification icon. We drop our own copy here BEFORE spawning
   SMP so the per-game notification toast uses the Sonic Loader art.
   We never overwrite an existing file so the user's customizations
   (and the SMP config / daemon.lock / debug.log siblings) survive. */
#define SHADOWMOUNT_ICON_PATH    "/data/shadowmount/smp_icon.png"

/* First-boot marker. Created the first time sys_spawn_embedded_payloads()
   completes a boot. Used to gate the "go install kstuff" notification and
   the optional Homebrew Launcher autolaunch. */
#define SONIC_FIRST_BOOT_MARKER "/data/sonic-loader/.first_boot_done"


typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;


int  sceUserServiceInitialize(void*);
int  sceUserServiceGetForegroundUser(uint32_t *user_id);
int  sceUserServiceGetUserName(int32_t user_id, char *name, size_t max_size);
void sceUserServiceTerminate(void);

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceSystemServiceLaunchApp(const char* title_id, char** argv,
			      app_launch_ctx_t* ctx);

/* PS5 kernel firmware-version probe. Argument is a 0x18-byte struct:
   uint32_t size at offset 0, BCD-packed version uint32 at offset 0x14
   (e.g. 0x10010000 = 10.01, 0x12000000 = 12.00). */
int sceKernelGetProsperoSystemSwVersion(void *buf);


/**
 * Decode an escaped argument.
 **/
static char*
args_decode(const char* s) {
  size_t length = strlen(s);
  char *arg = malloc(length+1);
  size_t off = 0;
  int escape = 0;

  for(size_t i=0; i<length; i++) {
    if(s[i] == '\\' && !escape) {
      escape = 1;
    } else {
      arg[off++] = s[i];
      escape = 0;
    }
  }

  arg[off] = 0;
  return arg;
}


static int
args_split(const char* args, char** argv, size_t size) {
  char* buf = strdup(args);
  size_t len = strlen(buf);
  int escape = 0;
  int argc = 0;

  memset(argv, 0, size*sizeof(char*));
  for(int i=0; i<len && argc<size; i++) {
    if(escape) {
      escape = 0;
      continue;
    }

    if(buf[i] == '\\') {
      escape = 1;
      continue;
    }

    if(buf[i] == ' ') {
      buf[i] = 0;
      continue;
    }

    if(buf[i] && !i) {
      argv[argc++] = buf+i;
      continue;
    }

    if(buf[i] && !buf[i-1]) {
      argv[argc++] = buf+i;
    }
  }

  for(int i=0; i<argc; i++) {
    argv[i] = args_decode(argv[i]);
  }

  free(buf);

  return argc;
}


/**
 * Fint the pid of a process with the given name.
 **/
static pid_t sys_find_pid(const char *name);

/* Public wrapper around the static helper so other translation units
   (e.g. playgo.c) can find a process by its kernel-thread name. */
int
sys_find_pid_by_name(const char *name) {
  return (int)sys_find_pid(name);
}


/* Check whether something is listening on 127.0.0.1:<port>. Used to
   probe whether the DPI install daemon (or any other sub-payload that
   binds a fixed port) is already up before re-spawning it. */
int
sys_port_is_open(int port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) return 0;
  struct sockaddr_in sa = {0};
  sa.sin_family      = AF_INET;
  sa.sin_port        = htons((uint16_t)port);
  sa.sin_addr.s_addr = inet_addr("127.0.0.1");
  /* Short blocking connect — DPI binds to loopback so this resolves in
     under a millisecond when it's up. */
  int rc = connect(s, (struct sockaddr*)&sa, sizeof(sa));
  close(s);
  return rc == 0 ? 1 : 0;
}


static pid_t
sys_find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    perror("sysctl");
    return -1;
  }

  if(!(buf=malloc(buf_size))) {
    perror("malloc");
    return -1;
  }

  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    perror("sysctl");
    free(buf);
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && ki_pid != mypid) {
      pid = ki_pid;
    }
  }

  free(buf);

  return pid;
}


int
sys_launch_homebrew(const char* cwd, const char* path, const char* args,
		    const char* env) {
  char* argv[255];
  char* envp[255];
  int optval = 1;
  int fds[2];
  pid_t pid;

  if(!cwd) {
    cwd = "/";
  }

  if(!args) {
    args = "";
  }

  if(!env) {
    env = "";
  }

  printf("launch homebrew: CWD=%s %s %s %s\n", cwd, env, path, args);

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
    perror("socketpair");
    return 1;
  }

  if(setsockopt(fds[1], SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval)) < 0) {
    perror("setsockopt");
    close(fds[0]);
    close(fds[1]);
    return -11;
  }

  args_split(args, argv, 255);
  args_split(env, envp, 255);
  pid = hbldr_launch(cwd, path, fds[1], argv, envp);

  for(int i=0; argv[i]; i++) {
    free(argv[i]);
  }
  for(int i=0; envp[i]; i++) {
    free(envp[i]);
  }

  close(fds[1]);
  if(pid < 0) {
    close(fds[0]);
    return -1;
  }

  return fds[0];
}


int
sys_launch_daemon(const char* cwd, const char* uri, const char* args,
		  const char* env) {
  uint8_t* elf = 0;
  char* argv[255];
  char* envp[255];
  int fds[2];
  pid_t pid;

  if(!cwd) {
    cwd = "/";
  }
  if(!args) {
    args = "";
  }
  if(!env) {
    env = "";
  }

  if(uri[0] == '/') {
    if(!(elf=fs_readfile(uri, 0))) {
      return -1;
    }

  } else if(!strncmp(uri, "file:", 5)) {
    if(!(elf=fs_readfile(uri+5, 0))) {
      return -1;
    }

  } else if(!strncmp(uri, "http:", 5) ||
	    !strncmp(uri, "https:", 6)) {
    if(!(elf=http_get(uri, 0))) {
      return -1;
    }
  }

  if(!elf) {
    return -1;
  }

  printf("launch daemon: CWD=%s %s %s %s\n", cwd, env, uri, args);

  if(pipe(fds) == -1) {
    perror("pipe");
    return 1;
  }

  args_split(args, argv, 255);
  args_split(env, envp, 255);
  pid = elfldr_spawn(cwd, fds[1], elf, argv, envp);

  free(elf);
  for(int i=0; argv[i]; i++) {
    free(argv[i]);
  }
  for(int i=0; envp[i]; i++) {
    free(envp[i]);
  }

  close(fds[1]);
  if(pid < 0) {
    close(fds[0]);
    return -1;
  }

  return fds[0];
}


int
sys_launch_payload(const char* cwd, uint8_t* elf, size_t elf_size,
                   const char* args, const char* env) {
  char* argv[255];
  char* envp[255];

  int fds[2];
  pid_t pid;

  if(!cwd) {
    cwd = "/";
  }

  if(!args) {
    args = "";
  }

  if(!env) {
    env = "";
  }

  printf("launch payload: CWD=%s %s %s\n", cwd, env, args);

  if(pipe(fds) == -1) {
    perror("pipe");
    return 1;
  }

  args_split(args, argv, 255);
  args_split(env, envp, 255);
  pid = elfldr_spawn(cwd, fds[1], elf, argv, envp);

  for(int i=0; argv[i]; i++) {
    free(argv[i]);
  }
  for(int i=0; envp[i]; i++) {
    free(envp[i]);
  }

  close(fds[1]);
  if(pid < 0) {
    close(fds[0]);
    return -1;
  }

  return fds[0];
}


int
sys_launch_title(const char* title_id, const char* args) {
  app_launch_ctx_t ctx = {0};
  char* argv[255];
  int argc = 0;
  int app_id;
  int err;
  int have_ctx = 0;

  if(!args) {
    args = "";
  }

  printf("launch title: %s %s\n", title_id, args);

  /* Try to capture the foreground user. If the call succeeds AND we
     get a real user-id back, build a ctx with the structsize header
     Sony's launcher expects and pass it in. If the lookup fails (no
     user signed in yet, sign-out race, disc-launch from cold boot,
     etc.) DON'T abort — fall through and call the launcher with a
     NULL ctx, same way Sony's own VSH does for early-boot launches. */
  uint32_t fg_uid = 0xFFFFFFFFu;
  if(sceUserServiceGetForegroundUser(&fg_uid) == 0 &&
     fg_uid != 0xFFFFFFFFu && (int32_t)fg_uid != -1) {
    ctx.structsize = sizeof(ctx);
    ctx.user_id    = fg_uid;
    have_ctx = 1;
  }

  if((app_id=sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    if((err=sceSystemServiceKillApp(app_id, -1, 0, 0))) {
      perror("sceSystemServiceKillApp");
      return err;
    }
  }

  argc = args_split(args, argv, 255);
  err = sceSystemServiceLaunchApp(title_id, argv, have_ctx ? &ctx : NULL);
  /* sceSystemServiceLaunchApp returns the new app-id on success (a
     positive int like 0x4016) and a negative SCE error code on
     failure. The old code propagated `err` as-is and the websrv
     caller treats any non-zero return as failure — so a successful
     launch (positive app-id) was being surfaced as a bogus 503 in
     the UI. Normalize: 0 on success, negative on failure. */
  if(err < 0) {
    perror("sceSystemServiceLaunchApp");
  } else {
    err = 0;
  }

  for(int i=0; i<argc; i++) {
    free(argv[i]);
  }

  return err;
}

/**
 *
 **/
static int
sys_notify_address(const char* prefix, int port) {
  char ip[INET_ADDRSTRLEN] = "127.0.0.1";
  struct ifaddrs *ifaddr;

  if(getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    return -1;
  }

  // Enumerate all AF_INET IPs
  for(struct ifaddrs *ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
    if(ifa->ifa_addr == NULL) {
      continue;
    }

    if(ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    // skip localhost
    if(!strncmp("lo", ifa->ifa_name, 2)) {
      continue;
    }

    struct sockaddr_in *in = (struct sockaddr_in*)ifa->ifa_addr;
    inet_ntop(AF_INET, &(in->sin_addr), ip, sizeof(ip));

    // skip interfaces without an ip
    if(!strncmp("0.", ip, 2)) {
      continue;
    }
  }

  freeifaddrs(ifaddr);

  notify("%s %s:%d", prefix, ip, port);
  printf("%s %s:%d\n", prefix, ip, port);

  return 0;
}


static void
on_fatal_signal(int sig) {
  void *buf[0x1000];
  int nptrs;

  notify("sonic-loader.elf: %s", strsignal(sig));

  nptrs = backtrace(buf, sizeof(buf));
  backtrace_symbols_fd(buf, nptrs, open("/dev/console", O_WRONLY));

  _exit(EXIT_FAILURE);
}


/**
 * Write `data` to `path` if the file is missing or its contents differ.
 * Returns 0 on success, -1 on failure.
 **/
static int
install_payload_file(const char *path, const uint8_t *data, size_t size) {
  struct stat st;
  int fd;
  ssize_t n;
  int need_write = 1;

  /* Skip rewriting if the file already exists with the same size and the
     bytes match — saves a few hundred ms on repeat boots. */
  if(stat(path, &st) == 0 && (size_t)st.st_size == size) {
    if((fd=open(path, O_RDONLY)) >= 0) {
      uint8_t *buf = malloc(size);
      if(buf) {
        n = read(fd, buf, size);
        if(n == (ssize_t)size && memcmp(buf, data, size) == 0) {
          need_write = 0;
        }
        free(buf);
      }
      close(fd);
    }
  }

  if(!need_write) {
    return 0;
  }

  if((fd=open(path, O_WRONLY|O_CREAT|O_TRUNC, 0755)) < 0) {
    perror("install_payload_file: open");
    return -1;
  }
  if((n=write(fd, data, size)) != (ssize_t)size) {
    perror("install_payload_file: write");
    close(fd);
    unlink(path);
    return -1;
  }
  close(fd);
  chmod(path, 0755);
  return 0;
}


/**
 * Read an ELF file from disk.
 **/
static uint8_t*
read_elf_file(const char *path, size_t *size_out) {
  struct stat st;
  int fd;
  uint8_t *buf;
  ssize_t n;

  if(stat(path, &st) != 0) {
    return NULL;
  }
  if((fd=open(path, O_RDONLY)) < 0) {
    return NULL;
  }
  if(!(buf=malloc(st.st_size))) {
    close(fd);
    return NULL;
  }
  n = read(fd, buf, st.st_size);
  close(fd);
  if(n != st.st_size) {
    free(buf);
    return NULL;
  }
  *size_out = st.st_size;
  return buf;
}


/**
 * Spawn an embedded ELF as a detached daemon.
 **/
static int
spawn_embedded(const char *label, const uint8_t* elf, size_t elf_size) {
  char* argv[2] = {(char*)label, 0};
  char* envp[1] = {0};
  int devnull;
  pid_t pid;

  if((devnull=open("/dev/null", O_WRONLY)) < 0) {
    devnull = -1;
  }

  pid = elfldr_spawn("/", devnull, (uint8_t*)elf, argv, envp);

  if(devnull >= 0) {
    close(devnull);
  }

  if(pid < 0) {
    /* Failures are still loud — they matter. */
    notify("sonic-loader: فشل تشغيل %s", label);
    fprintf(stderr, "sonic-loader: فشل تشغيل %s\n", label);
    return -1;
  }

  /* Successful spawns are silent on-screen; the consolidated boot toast
     is emitted by sys_spawn_embedded_payloads() at the end. */
  printf("sonic-loader: spawned %s (size=%zu pid=%d)\n",
         label, elf_size, (int)pid);
  return 0;
}


/* Spawn variant that takes a full argv. For sub-payloads that accept
   command-line flags (ftpsrv accepts -p PORT). */
static int
spawn_embedded_argv(const uint8_t *elf, size_t elf_size, char **argv) {
  char *envp[1] = {0};
  int devnull = open("/dev/null", O_WRONLY);
  pid_t pid = elfldr_spawn("/", devnull, (uint8_t*)elf, argv, envp);
  if(devnull >= 0) close(devnull);
  if(pid < 0) {
    notify("sonic-loader: فشل تشغيل %s", argv[0] ? argv[0] : "(elf)");
    fprintf(stderr, "sonic-loader: فشل تشغيل %s\n",
            argv[0] ? argv[0] : "(elf)");
    return -1;
  }
  printf("sonic-loader: spawned %s (size=%zu pid=%d)\n",
         argv[0] ? argv[0] : "(elf)", elf_size, (int)pid);
  return 0;
}


/* ─────── ftpsrv toggle + port + auth + transfer-type config ─────── */

static atomic_int g_ftpsrv_port = FTPSRV_DEFAULT_PORT;

/* User/pass/type are mostly read-mostly + small; using a static buffer
   guarded by config_save() rewrites is plenty. The main thread reads
   them when building argv at spawn time; no concurrent writes. */
static char g_ftpsrv_user[64] = "anonymous";
static char g_ftpsrv_pass[64] = "";
static char g_ftpsrv_type[8]  = "auto";   /* "auto" | "binary" | "ascii" */


int
sys_ftpsrv_get_port(void) {
  int p = atomic_load(&g_ftpsrv_port);
  if(p < 1 || p > 65535) p = FTPSRV_DEFAULT_PORT;
  return p;
}

void
sys_ftpsrv_set_port(int port) {
  if(port < 1 || port > 65535) port = FTPSRV_DEFAULT_PORT;
  atomic_store(&g_ftpsrv_port, port);
}

const char* sys_ftpsrv_get_user(void) { return g_ftpsrv_user; }
const char* sys_ftpsrv_get_pass(void) { return g_ftpsrv_pass; }
const char* sys_ftpsrv_get_type(void) { return g_ftpsrv_type; }

void
sys_ftpsrv_set_user(const char *user) {
  if(!user || !*user) {
    strcpy(g_ftpsrv_user, "anonymous");
  } else {
    strncpy(g_ftpsrv_user, user, sizeof(g_ftpsrv_user) - 1);
    g_ftpsrv_user[sizeof(g_ftpsrv_user) - 1] = '\0';
  }
}

void
sys_ftpsrv_set_pass(const char *pass) {
  if(!pass) pass = "";
  strncpy(g_ftpsrv_pass, pass, sizeof(g_ftpsrv_pass) - 1);
  g_ftpsrv_pass[sizeof(g_ftpsrv_pass) - 1] = '\0';
}

void
sys_ftpsrv_set_type(const char *type) {
  if(!type || !*type) type = "auto";
  if(!strcasecmp(type, "binary") || !strcasecmp(type, "i") ||
     !strcasecmp(type, "image"))
    strcpy(g_ftpsrv_type, "binary");
  else if(!strcasecmp(type, "ascii") || !strcasecmp(type, "a"))
    strcpy(g_ftpsrv_type, "ascii");
  else
    strcpy(g_ftpsrv_type, "auto");
}


int
sys_ftpsrv_is_running(void) {
  return sys_find_pid(FTPSRV_PROC_NAME) > 0 ? 1 : 0;
}


static int
spawn_ftpsrv(void) {
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%d", sys_ftpsrv_get_port());
  /* Build argv. We always pass -p PORT and -t TYPE (defaults are
     auto/anon-equivalent if user hasn't customised). User+pass are
     only added when a non-anonymous user is configured. */
  char *argv[16];
  int n = 0;
  argv[n++] = "ftpsrv";
  argv[n++] = "-p"; argv[n++] = port_str;
  argv[n++] = "-t"; argv[n++] = (char*)sys_ftpsrv_get_type();
  if(g_ftpsrv_user[0] && strcasecmp(g_ftpsrv_user, "anonymous") != 0) {
    argv[n++] = "-u"; argv[n++] = g_ftpsrv_user;
    argv[n++] = "-P"; argv[n++] = g_ftpsrv_pass;
  }
  argv[n] = NULL;
  return spawn_embedded_argv(ftpsrv_elf, ftpsrv_elf_size, argv);
}

int
sys_ftpsrv_set_enabled(int on) {
  pid_t existing;
  int rc;

  if(on) {
    if(sys_find_pid(FTPSRV_PROC_NAME) > 0) { rc = 1; goto persist; }
    if(spawn_ftpsrv() != 0) { rc = -1; goto persist; }
    rc = 1;  /* spawned — assume PID will register shortly */
    for(int i = 0; i < 30; i++) {
      if(sys_find_pid(FTPSRV_PROC_NAME) > 0) break;
      usleep(100000);
    }
    goto persist;
  }

  /* Off — TERM running instances. */
  while((existing = sys_find_pid(FTPSRV_PROC_NAME)) > 0) {
    if(kill(existing, SIGTERM) != 0) {
      perror("kill ftpsrv");
      rc = -1;
      goto persist;
    }
    int exited = 0;
    for(int i = 0; i < 30; i++) {
      usleep(100000);
      if(sys_find_pid(FTPSRV_PROC_NAME) <= 0) { exited = 1; break; }
    }
    if(!exited) kill(existing, SIGKILL);
  }
  rc = 0;

persist:
  {
    extern void config_save(void);
    config_save();
  }
  return rc;
}

int
sys_ftpsrv_restart(void) {
  pid_t pid = sys_find_pid(FTPSRV_PROC_NAME);
  if(pid > 0) {
    kill(pid, SIGTERM);
    for(int i = 0; i < 30; i++) {
      usleep(100000);
      if(sys_find_pid(FTPSRV_PROC_NAME) <= 0) break;
    }
    if((pid = sys_find_pid(FTPSRV_PROC_NAME)) > 0) kill(pid, SIGKILL);
  }
  return spawn_ftpsrv();
}


unsigned int
sys_get_firmware_version(void) {
  /* Prefer the SDK's kernel-side reader — kmonitor already uses it
     successfully against the same kernel-R/W primitive that's in
     play here, and it works on firmwares where the libkernel call
     below silently returns failure. Falls back to the userspace
     sceKernelGetProsperoSystemSwVersion if kernel_get_fw_version
     reports zero (unsupported FW range, primitive not warm yet). */
  uint32_t fw = kernel_get_fw_version();
  if(fw != 0) return (unsigned int)fw;

  uint8_t buf[0x18] = {0};
  *((uint32_t*)buf) = sizeof(buf);
  if(sceKernelGetProsperoSystemSwVersion(buf) != 0) return 0;
  return *(uint32_t*)&buf[0x14];
}


/* Spawn the embedded DPI elf if 127.0.0.1:9040 isn't accepting yet.
   Used by /api/homebrew/install-pkg as a self-healing safety net if
   the boot-time spawn lost the race or the daemon was killed. */
int
sys_dpi_ensure_running(void) {
  if(sys_port_is_open(9040)) return 1;
  if(spawn_embedded("dpi", dpi_elf, dpi_elf_size) != 0) return -1;
  for(int i = 0; i < 30; i++) {
    usleep(100000);
    if(sys_port_is_open(9040)) return 1;
  }
  return 0;
}


/* Drop our SMP icon to /data/shadowmount/smp_icon.png if the file isn't
   already there. SMP's ensure_notification_icon_present() only writes
   its compiled-in default when the file is absent — so if we beat it
   to the punch the per-game notify toasts inherit our art regardless
   of which upstream SMP build the user installed. We never overwrite
   an existing copy: that preserves user customizations and the
   sibling state files (config.ini, daemon.lock, debug.log) that SMP
   itself owns in the same directory. */
static void
seed_shadowmount_icon(void) {
  struct stat st;
  mkdir(SHADOWMOUNT_DIR, 0755);
  if(stat(SHADOWMOUNT_ICON_PATH, &st) == 0 && st.st_size > 0) {
    return;  /* Already present — leave it. */
  }
  install_payload_file(SHADOWMOUNT_ICON_PATH,
                       smp_icon_png, smp_icon_png_size);
}


static int
first_boot_marker_present(void) {
  struct stat st;
  return stat(SONIC_FIRST_BOOT_MARKER, &st) == 0;
}


static void
write_first_boot_marker(void) {
  mkdir("/data/sonic-loader", 0755);
  int fd = open(SONIC_FIRST_BOOT_MARKER, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if(fd >= 0) {
    const char *msg = "sonic-loader first boot complete\n";
    (void)write(fd, msg, strlen(msg));
    close(fd);
  }
}


/**
 * Spawn the bundled klogsrv/ftpsrv/shadowmount/nanodns payloads in order,
 * plus the user-installed kstuff-lite at /data/kstuff.elf (if present —
 * we no longer ship an embedded copy). On the very first boot, fire a
 * notification telling the user to install kstuff via Settings.
 *
 * If SONIC_AUTOLAUNCH_HBL is defined at build time, the first boot also
 * launches the homebrew loader (FAKE00000) so the user lands directly in
 * the launcher's Settings → Install kstuff section.
 **/
void
sys_spawn_embedded_payloads(void) {
  uint8_t *kstuff_buf = NULL;
  size_t   kstuff_buf_size = 0;
  uint8_t *smp_buf    = NULL;
  size_t   smp_buf_size = 0;
  int      first_boot = !first_boot_marker_present();
  struct stat st;

  /* Wipe the PKG staging dir on every boot — Sonic Loader does not
     retain user PKGs across payload re-sends. Anything FTPed in or
     uploaded last session is gone now. The dir is recreated 0755 so
     subsequent uploads land cleanly. */
  {
    extern void homebrew_wipe_staged_pkgs(void);
    homebrew_wipe_staged_pkgs();
  }

  /* Make sure SMP's notification icon is our Sonic Loader art before we
     spawn SMP — once SMP runs and sees a non-empty file it leaves it
     alone. */
  seed_shadowmount_icon();

  /* No more embedded kstuff — load whatever the user installed. If the
     file is missing AND this is the first boot, attempt to auto-install
     a combo matching the firmware version (drakmor for ≤ 10.01,
     EchoStretch otherwise). If auto-install fails (no network, GitHub
     down, sceHttp blocked, etc.), fall back to the notify-and-prompt
     flow that points the user at Settings. */
  if(stat(KSTUFF_INSTALL_PATH, &st) != 0 || st.st_size <= 4) {
    unsigned int fw = sys_get_firmware_version();
    int use_drakmor = (fw != 0 && fw <= 0x10010000u);
    const char *smp_v = use_drakmor ? "104" : "103";
    const char *combo_label = use_drakmor
        ? "drakmor (firmware ≤ 10.01)"
        : "EchoStretch (firmware > 10.01)";

    if(fw == 0) {
      notify("سونيك لودر: تعذر قراءة إصدار النظام — سيتم الرجوع "
             "back to EchoStretch combo.");
    } else {
      notify("أول تشغيل لسونيك لودر: تم اكتشاف إصدار النظام %u.%02u — "
             "auto-installing %s combo from GitHub…",
             (unsigned)((fw >> 24) & 0xff),
             (unsigned)((fw >> 16) & 0xff),
             combo_label);
    }

    int kr = kstuff_install_direct(use_drakmor);
    int sr = smp_install_direct(smp_v);

    if(kr == 0 && sr == 0) {
      notify("سونيك لودر: تم التثبيت التلقائي بنجاح (kstuff %s + SMP %s). "
             "Loading kstuff now.",
             use_drakmor ? "drakmor" : "EchoStretch", smp_v);
    } else {
      notify("سونيك لودر: فشل التثبيت التلقائي (kstuff=%d, SMP=%d). "
             "Open Settings → Install kstuff-lite to retry manually.",
             kr, sr);
    }
  }

  if(stat(KSTUFF_INSTALL_PATH, &st) == 0 && st.st_size > 4) {
    kstuff_buf = read_elf_file(KSTUFF_INSTALL_PATH, &kstuff_buf_size);
    if(kstuff_buf) {
      spawn_embedded("kstuff-lite", kstuff_buf, kstuff_buf_size);
      free(kstuff_buf);
      /* Give kstuff-lite a moment to apply its kernel patches before the
         rest of the daemons try to touch privileged state. */
      sleep(2);
    }
  } else {
    notify("سونيك لودر: لم يتم تثبيت kstuff بعد. افتح مشغّل الهومبرو "
           "or http://<your-ps5-ip>:6969/ → Settings → Install kstuff-lite "
           "to enable kernel patches.");
  }

  spawn_embedded("klogsrv",          klogsrv_elf,      klogsrv_elf_size);
  /* ftpsrv: spawn with the configured port (default 2121). The port
     is loaded from /data/sonic-loader/config.ini before this runs. */
  spawn_ftpsrv();

  /* SMP is no longer baked in — it's installed by the user via
     Settings → ⬇ Install SMP. If the on-disk copy at
     /data/shadowmount/shadowmountplus.elf is missing, skip the spawn
     and fire a notification pointing the user at Settings. */
  if(stat(SHADOWMOUNT_INSTALL_PATH, &st) == 0 && st.st_size > 64) {
    smp_buf = read_elf_file(SHADOWMOUNT_INSTALL_PATH, &smp_buf_size);
  }
  if(smp_buf) {
    spawn_embedded("shadowmountplus", smp_buf, smp_buf_size);
    free(smp_buf);
  } else {
    notify("سونيك لودر: ShadowMountPlus غير مثبت بعد. افتح "
           "Settings → ShadowMountPlus and pick a release to install.");
  }

  /* nanoDNS — local DNS forwarder + override engine (drakmor/nanoDNS).
     Self-terminates any previous instance and reads /data/nanodns/
     nanodns.ini for overrides; runs with sane defaults if the config
     is missing. Listens on 127.0.0.1:53 by default. */
  spawn_embedded("nanodns",          nanodns_elf,      nanodns_elf_size);

  /* DPI (cy33hc/ps5-ezremote-dpi) — long-lived PKG-install daemon
     listening on 127.0.0.1:9040. The PS5 install state machine needs
     a permanent owning process for sceAppInstUtilAppInstallPkg() to
     actually run to completion; calling it from the websrv request
     thread (which dies as soon as we return) silently no-ops on
     RemotePKG-style fakepkgs. DPI fixes that by being the owning
     process — Sonic Loader connects to :9040 and sends a URL or
     local path; DPI handles the rest of the lifecycle. */
  if(!sys_port_is_open(9040)) {
    spawn_embedded("dpi",            dpi_elf,          dpi_elf_size);
  }

  /* BackPork is intentionally NOT auto-started — it conflicts with
     ShadowMountPlus's built-in fakelib watcher. The user opts in
     via the Settings toggle which calls sys_backpork_set_enabled(1). */

  if(first_boot) {
    /* Drop the redirect-pending marker so the very first request to /
       or /launcher.html gets a 302 to the kstuff install card. The
       websrv consumes the marker on the first hit. */
    mkdir("/data/sonic-loader", 0755);
    int fd = open("/data/sonic-loader/.first_boot_redirect_pending",
                  O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(fd >= 0) close(fd);

    /* Fire the "go install kstuff" toast a second time after a small
       delay so it lands AFTER the websrv address toast — otherwise the
       address toast wins the screen real-estate and the user misses
       the install prompt. */
    sleep(3);
    notify("أول تشغيل لسونيك لودر: افتح الإعدادات ← ثبّت kstuff-lite "
           "+ ShadowMountPlus to finish setup.");

#ifdef SONIC_AUTOLAUNCH_HBL
    /* Auto-launch the homebrew loader (FAKE00000) on first boot so the
       user lands inside the launcher's webkit view without manually
       opening it. The websrv-side first-boot redirect sends them to
       the kstuff install card on the first GET. */
    sleep(1);
    sys_launch_title("FAKE00000", "");
#endif

    write_first_boot_marker();
  }

  /* No boot toast here — sys_notify_address() already fired the single
     "Sonic Loader serving HTTP on …" notification. */
}


/**
 * Return 1 if a process named "backpork.elf" is currently running on
 * the system (other than ourselves).
 */
int
sys_backpork_is_running(void) {
  return sys_find_pid(BACKPORK_PROC_NAME) > 0 ? 1 : 0;
}


int
sys_nanodns_is_running(void) {
  return sys_find_pid(NANODNS_PROC_NAME) > 0 ? 1 : 0;
}


int
sys_nanodns_set_enabled(int on) {
  pid_t existing;
  int rc;

  if(on) {
    if(sys_find_pid(NANODNS_PROC_NAME) > 0) {
      rc = 1;
      goto nanodns_persist;
    }
    if(spawn_embedded("nanodns", nanodns_elf, nanodns_elf_size) != 0) {
      rc = -1;
      goto nanodns_persist;
    }
    /* Wait for the proc to register so the UI toggle doesn't snap
       back off — same backstop pattern the etaHEN toggle uses. */
    rc = -1;
    for(int i = 0; i < 30; i++) {
      if(sys_find_pid(NANODNS_PROC_NAME) > 0) { rc = 1; break; }
      usleep(100000);
    }
    goto nanodns_persist;
  }

  while((existing = sys_find_pid(NANODNS_PROC_NAME)) > 0) {
    if(kill(existing, SIGKILL) != 0) {
      perror("kill nanodns");
      rc = -1;
      goto nanodns_persist;
    }
    sleep(1);
  }
  rc = 0;

nanodns_persist:
  {
    extern void config_save(void);
    config_save();
  }
  return rc;
}


/**
 * Toggle BackPork on/off. When called with on=1 we spawn the embedded
 * ELF (no-op if it's already running). When called with on=0 we kill
 * the existing instance, if any. Returns the resulting state (1=running,
 * 0=stopped, -1=error).
 */
int
sys_backpork_set_enabled(int on) {
  pid_t existing;
  int rc;

  if(on) {
    if(sys_find_pid(BACKPORK_PROC_NAME) > 0) {
      rc = 1;
      goto persist;
    }
    if(spawn_embedded("backpork", backpork_elf, backpork_elf_size) != 0) {
      rc = -1;
      goto persist;
    }
    /* Wait up to ~3s for the new process to register in the proc table.
       Without this, sys_find_pid() can race the spawn and the response
       JSON comes back with backporkRunning=false, which makes the UI
       slider snap back off — so the user has to click it again. */
    rc = -1;
    for(int i = 0; i < 30; i++) {
      if(sys_find_pid(BACKPORK_PROC_NAME) > 0) { rc = 1; break; }
      usleep(100000);  /* 100 ms */
    }
    if(rc != 1) {
      /* Spawn returned 0 but the proc never showed up — surface the
         failure to the UI rather than silently flipping back to off. */
      rc = -1;
    }
    goto persist;
  }

  /* Off — kill any running instance(s). */
  while((existing = sys_find_pid(BACKPORK_PROC_NAME)) > 0) {
    if(kill(existing, SIGKILL) != 0) {
      perror("kill backpork");
      rc = -1;
      goto persist;
    }
    sleep(1);
  }
  rc = 0;

persist:
  /* Persist the desired state so it survives a redeploy. */
  {
    extern void config_save(void);
    config_save();
  }
  return rc;
}


/* ───── SMP daemon toggle ───── */

int
sys_smp_is_running(void) {
  return sys_find_pid(SHADOWMOUNT_PROC_NAME) > 0 ? 1 : 0;
}


/* Spawn SMP from the user-installed copy at
   /data/shadowmount/shadowmountplus.elf. Returns -1 if no on-disk
   copy is present (Sonic Loader no longer ships an embedded fallback
   — install via Settings → ⬇ Install SMP). */
static int
spawn_smp(void) {
  struct stat st;
  if(stat(SHADOWMOUNT_INSTALL_PATH, &st) != 0 || st.st_size <= 64) {
    notify("ShadowMountPlus غير مثبت — اختر إصدارًا من الإعدادات.");
    return -1;
  }
  size_t sz = 0;
  uint8_t *buf = read_elf_file(SHADOWMOUNT_INSTALL_PATH, &sz);
  if(!buf) return -1;
  int rc = spawn_embedded("shadowmountplus", buf, sz);
  free(buf);
  return rc;
}


int
sys_smp_set_enabled(int on) {
  pid_t existing;
  int rc;

  if(on) {
    if(sys_find_pid(SHADOWMOUNT_PROC_NAME) > 0) { rc = 1; goto persist; }
    if(spawn_smp() != 0) { rc = -1; goto persist; }
    /* Optimistic: spawn returned 0, SMP will register in the proc
       table within a few hundred ms. Don't fail the toggle if the
       PID hasn't materialised inside our short wait window — that's
       what made the slider snap back to off and forced the user to
       click twice. The UI polls state on a 8 s tick, so the slider
       converges on its own. */
    rc = 1;
    for(int i = 0; i < 30; i++) {
      if(sys_find_pid(SHADOWMOUNT_PROC_NAME) > 0) break;
      usleep(100000);
    }
    goto persist;
  }

  /* Off — let SMP shut down cleanly first (TERM), then SIGKILL stragglers.
     SMP writes a STOP marker file at /data/shadowmount/STOP that some
     code paths watch for, but SIGTERM is the universal escape hatch. */
  while((existing = sys_find_pid(SHADOWMOUNT_PROC_NAME)) > 0) {
    if(kill(existing, SIGTERM) != 0) {
      perror("kill shadowmountplus");
      rc = -1;
      goto persist;
    }
    /* Give SMP up to ~5s to exit cleanly before escalating. */
    int exited = 0;
    for(int i = 0; i < 50; i++) {
      usleep(100000);
      if(sys_find_pid(SHADOWMOUNT_PROC_NAME) <= 0) { exited = 1; break; }
    }
    if(!exited) kill(existing, SIGKILL);
  }
  rc = 0;

persist:
  {
    extern void config_save(void);
    config_save();
  }
  return rc;
}


int
sys_smp_restart(void) {
  /* TERM the running daemon, wait briefly, respawn. Used after the
     scan-path config changes so the new paths are picked up without
     re-sending the whole Sonic Loader payload, and by the boot-kick
     thread that runs ~2 s after main() init.

     The SIGTERM path is given 1 s (10 × 100 ms) for SMP to flush and
     exit cleanly. If it's still alive at that point, SIGKILL — which
     the kernel delivers immediately. We then poll briefly (5 × 50 ms)
     for the proc to disappear before respawning, so the respawn
     doesn't race a stale PID. */
  pid_t pid = sys_find_pid(SHADOWMOUNT_PROC_NAME);
  if(pid > 0) {
    kill(pid, SIGTERM);
    for(int i = 0; i < 10; i++) {
      usleep(100000);
      if(sys_find_pid(SHADOWMOUNT_PROC_NAME) <= 0) break;
    }
    if((pid = sys_find_pid(SHADOWMOUNT_PROC_NAME)) > 0) {
      kill(pid, SIGKILL);
      for(int i = 0; i < 5; i++) {
        usleep(50000);
        if(sys_find_pid(SHADOWMOUNT_PROC_NAME) <= 0) break;
      }
    }
  }

  int rc = spawn_smp();
  if(rc != 0) return rc;

  /* Wait for the new child to register its proc name via SYS_thr_set_
     name. spawn_embedded() returns the moment elfldr_spawn() comes
     back from the parent's fork — before the child has executed far
     enough to be findable by name. Without this wait, restart_request
     in smp_updater.c immediately reports running:false, the UI lights
     up "Stopped", and the user clicks Restart again — killing the
     daemon that was about to come up. The toggle setter uses the same
     30 × 100 ms loop for the same reason. */
  for(int i = 0; i < 30; i++) {
    if(sys_find_pid(SHADOWMOUNT_PROC_NAME) > 0) break;
    usleep(100000);
  }
  return 0;
}


/* ───── Lapy JB Daemon toggle ───── */

int
sys_lapyjb_is_running(void) {
  return sys_find_pid(LAPYJB_PROC_NAME) > 0 ? 1 : 0;
}


int
sys_lapyjb_set_enabled(int on) {
  pid_t existing;
  int rc;

  if(on) {
    if(sys_find_pid(LAPYJB_PROC_NAME) > 0) {
      rc = 1;
      goto lapyjb_persist;
    }
    if(spawn_embedded("LapyJB", lapyjb_elf, lapyjb_elf_size) != 0) {
      rc = -1;
      goto lapyjb_persist;
    }
    /* Wait for the proc to register so the UI toggle (and any
       /api/state poll) reports "running" reliably. */
    rc = -1;
    for(int i = 0; i < 30; i++) {
      if(sys_find_pid(LAPYJB_PROC_NAME) > 0) { rc = 1; break; }
      usleep(100000);
    }
    goto lapyjb_persist;
  }

  while((existing = sys_find_pid(LAPYJB_PROC_NAME)) > 0) {
    if(kill(existing, SIGKILL) != 0) {
      perror("kill lapyjb");
      rc = -1;
      goto lapyjb_persist;
    }
    sleep(1);
  }
  rc = 0;

lapyjb_persist:
  {
    extern void config_save(void);
    config_save();
  }
  return rc;
}


/* ───── ps5-app-dumper (EchoStretch) — one-shot spawn ───── */

int
sys_spawn_app_dumper(void) {
  return spawn_embedded("app-dumper",
                        ps5_app_dumper_elf, ps5_app_dumper_elf_size);
}


/**
 * Look up the userId of the foreground (signed-in) user. Returns 0
 * when the user-service call fails — callers treat 0 as "no user".
 * name_out (optional) receives the human-readable user name.
 */
uint32_t
sys_get_foreground_user(char *name_out, size_t name_out_size) {
  uint32_t uid = 0;
  if(sceUserServiceGetForegroundUser(&uid) != 0 || uid == 0) {
    if(name_out && name_out_size > 0) name_out[0] = 0;
    return 0;
  }
  if(name_out && name_out_size > 0) {
    char tmp[17] = {0};
    if(sceUserServiceGetUserName((int32_t)uid, tmp, sizeof(tmp)) != 0) {
      tmp[0] = 0;
    }
    size_t n = strlen(tmp);
    if(n >= name_out_size) n = name_out_size - 1;
    memcpy(name_out, tmp, n);
    name_out[n] = 0;
  }
  return uid;
}


/**
 * Spawn np-fake-signin.elf on demand. Unlike the rest of the bundled
 * helpers, np-fake-signin is NOT embedded into the loader — it's
 * lazy-downloaded from this repo's gitea-raw URL into
 * /data/sonic-loader/np-fake-signin.elf the first time it's needed,
 * then read from disk and spawned via spawn_embedded() (which just
 * wants the bytes, not the source path). This keeps the loader
 * ~120 KB smaller and lets us swap out the payload by repushing
 * payloads/np-fake-signin.elf without having to rebuild + redeploy
 * the whole loader.
 *
 * Failure modes — all surface as -1 to the caller (e.g. /api/np/
 * fake-signin returns 503):
 *   - file missing AND http_get failed (no internet / DNS / gitea
 *     unreachable). Fix: ensure DNS 1 = 127.0.0.1 / 62.210.38.117.
 *   - downloaded payload doesn't sniff as an ELF (HTML error page
 *     from a misbehaving CDN, etc.). Fix: rare, retry.
 */
#define NP_FAKE_SIGNIN_PATH "/data/sonic-loader/np-fake-signin.elf"
#define NP_FAKE_SIGNIN_URL  \
    "https://git.etawen.dev/soniciso/sonicloader/raw/branch/main/" \
    "payloads/np-fake-signin.elf"

static int
np_fake_signin_ensure_on_disk(void) {
  struct stat st;
  if(stat(NP_FAKE_SIGNIN_PATH, &st) == 0 && st.st_size > 1024) {
    return 0;  /* already cached */
  }

  /* Lazy fetch. */
  fprintf(stderr,
          "np-fake-signin: cache miss, fetching from %s\n",
          NP_FAKE_SIGNIN_URL);
  size_t len = 0;
  uint8_t *body = http_get(NP_FAKE_SIGNIN_URL, &len);
  if(!body || len < 1024) {
    free(body);
    fprintf(stderr,
            "np-fake-signin: download failed (got %zu bytes)\n", len);
    return -1;
  }
  /* ELF magic sniff so a CDN HTML page doesn't get spawned. */
  if(body[0] != 0x7f || body[1] != 'E' ||
     body[2] != 'L'  || body[3] != 'F') {
    free(body);
    fprintf(stderr,
            "np-fake-signin: payload from gitea raw is not an ELF\n");
    return -1;
  }

  mkdir("/data/sonic-loader", 0755);
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s.tmp", NP_FAKE_SIGNIN_PATH);
  int fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0755);
  if(fd < 0) {
    free(body);
    perror("np-fake-signin: open .tmp");
    return -1;
  }
  size_t off = 0;
  while(off < len) {
    ssize_t w = write(fd, body + off, len - off);
    if(w <= 0) { close(fd); unlink(tmp); free(body); return -1; }
    off += (size_t)w;
  }
  fsync(fd);
  close(fd);
  if(rename(tmp, NP_FAKE_SIGNIN_PATH) != 0) {
    unlink(tmp);
    free(body);
    perror("np-fake-signin: rename");
    return -1;
  }
  free(body);
  fprintf(stderr, "np-fake-signin: cached %zu bytes → %s\n",
          len, NP_FAKE_SIGNIN_PATH);
  return 0;
}

int
sys_spawn_np_fake_signin(void) {
  if(np_fake_signin_ensure_on_disk() != 0) return -1;

  size_t buf_sz = 0;
  uint8_t *buf = fs_readfile(NP_FAKE_SIGNIN_PATH, &buf_sz);
  if(!buf || buf_sz < 1024) {
    free(buf);
    fprintf(stderr, "np-fake-signin: fs_readfile failed\n");
    return -1;
  }
  int rc = spawn_embedded("np-fake-signin", buf, buf_sz);
  free(buf);
  return rc;
}


/**
 * Spawn np-restore-account.elf on demand. Reads
 * /system_data/priv/home/<uid>/config.dat for the foreground user and
 * pushes every field into the system registry verbatim. The user has
 * to copy the config.dat (and auth.dat, account.dat, token.dat) onto
 * the console themselves before clicking Run — see the Settings UI
 * description for the exact paths.
 */
int
sys_spawn_np_restore_account(void) {
  return spawn_embedded("np-restore-account",
                        np_restore_account_elf, np_restore_account_elf_size);
}


/* ───── Garlic — community save-processing worker + interactive savemgr ───── */

#define GARLIC_DIR         "/data/garlic"
#define GARLIC_CONFIG_PATH "/data/garlic/config.ini"

/* Fixed defaults that ship with this payload. workerKey is shared across
   all Sonic-Loader installs so jobs from garlicsaves.com fan out across
   every active console — that's the whole point of the worker network.
   The user can edit /data/garlic/config.ini by hand if they want a
   different key. */
static const char *GARLIC_DEFAULT_HOST     = "garlicsaves.com";
static const int   GARLIC_DEFAULT_PORT     = 80;
static const char *GARLIC_DEFAULT_KEY      =
  "9f1378109a83aa79a9e10e8f5523c4aad3fa6880c8f7da8d749c58a25c522f34";
static const int   GARLIC_DEFAULT_POLL     = 30;


static int
garlic_write_config(int poll_interval) {
  mkdir("/data", 0755);
  mkdir(GARLIC_DIR, 0777);
  FILE *f = fopen(GARLIC_CONFIG_PATH ".tmp", "w");
  if(!f) return -1;
  fprintf(f,
    "serverHost=%s\n"
    "serverPort=%d\n"
    "workerKey=%s\n"
    "pollInterval=%d\n",
    GARLIC_DEFAULT_HOST, GARLIC_DEFAULT_PORT,
    GARLIC_DEFAULT_KEY, poll_interval);
  fclose(f);
  return rename(GARLIC_CONFIG_PATH ".tmp", GARLIC_CONFIG_PATH);
}


/* Force the workerKey line in /data/garlic/config.ini to the canonical
   community key. Preserves serverHost, serverPort, pollInterval and any
   other lines. Returns 0 if the file was correct or successfully fixed,
   -1 on I/O failure. The community pool only fans jobs across consoles
   that share this key, so all Sonic-Loader installs MUST use it. */
static int
garlic_force_canonical_key(void) {
  FILE *f = fopen(GARLIC_CONFIG_PATH, "r");
  if(!f) return -1;

  /* Slurp the file. Cap at 4 KB — config.ini is tiny. */
  char  buf[4096];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';

  /* Walk lines: keep everything except a workerKey= line, replace that
     with the canonical key. If no workerKey= line exists, append one. */
  char out[4200];
  size_t outlen = 0;
  int    saw_key = 0;
  char  *line = buf;
  while(line && *line) {
    char *eol = strpbrk(line, "\r\n");
    size_t len = eol ? (size_t)(eol - line) : strlen(line);
    if(len >= 10 && strncmp(line, "workerKey=", 10) == 0) {
      saw_key = 1;
      int wrote = snprintf(out + outlen, sizeof(out) - outlen,
                           "workerKey=%s\n", GARLIC_DEFAULT_KEY);
      if(wrote < 0 || (size_t)wrote >= sizeof(out) - outlen) return -1;
      outlen += (size_t)wrote;
    } else if(len > 0) {
      if(outlen + len + 1 >= sizeof(out)) return -1;
      memcpy(out + outlen, line, len);
      outlen += len;
      out[outlen++] = '\n';
    }
    if(!eol) break;
    line = eol + 1;
    while(*line == '\r' || *line == '\n') line++;
  }
  if(!saw_key) {
    int wrote = snprintf(out + outlen, sizeof(out) - outlen,
                         "workerKey=%s\n", GARLIC_DEFAULT_KEY);
    if(wrote < 0 || (size_t)wrote >= sizeof(out) - outlen) return -1;
    outlen += (size_t)wrote;
  }

  FILE *o = fopen(GARLIC_CONFIG_PATH ".tmp", "w");
  if(!o) return -1;
  if(fwrite(out, 1, outlen, o) != outlen) { fclose(o); return -1; }
  fclose(o);
  return rename(GARLIC_CONFIG_PATH ".tmp", GARLIC_CONFIG_PATH);
}


void
sys_garlic_seed_config(void) {
  struct stat st;
  if(stat(GARLIC_CONFIG_PATH, &st) != 0 || st.st_size <= 0) {
    /* No config yet — write the full default. */
    if(garlic_write_config(GARLIC_DEFAULT_POLL) == 0) {
      printf("garlic: seeded %s\n", GARLIC_CONFIG_PATH);
    }
    return;
  }
  /* Config exists: keep the user's serverHost/port/pollInterval, but
     pin the workerKey to the canonical community value on every boot.
     Without this, hand-edited or copy-pasted configs end up with a
     stale/wrong key and the worker silently disappears from the pool. */
  if(garlic_force_canonical_key() == 0) {
    printf("garlic: pinned canonical workerKey in %s\n", GARLIC_CONFIG_PATH);
  }
}


int
sys_garlic_get_poll_interval(void) {
  FILE *f = fopen(GARLIC_CONFIG_PATH, "r");
  if(!f) return GARLIC_DEFAULT_POLL;
  char line[256];
  int v = GARLIC_DEFAULT_POLL;
  while(fgets(line, sizeof(line), f)) {
    int n;
    if(sscanf(line, "pollInterval=%d", &n) == 1 && n > 0) { v = n; break; }
  }
  fclose(f);
  return v;
}


int
sys_garlic_set_poll_interval(int seconds) {
  if(seconds < 5)    seconds = 5;
  if(seconds > 3600) seconds = 3600;
  /* Read current config, rewrite with new pollInterval, preserve other keys. */
  char host[128] = {0};
  int port = GARLIC_DEFAULT_PORT;
  char key[128] = {0};
  FILE *f = fopen(GARLIC_CONFIG_PATH, "r");
  if(f) {
    char line[256];
    while(fgets(line, sizeof(line), f)) {
      sscanf(line, "serverHost=%127[^\r\n]", host);
      sscanf(line, "serverPort=%d", &port);
      sscanf(line, "workerKey=%127[^\r\n]", key);
    }
    fclose(f);
  }
  if(!host[0]) strncpy(host, GARLIC_DEFAULT_HOST, sizeof(host)-1);
  /* workerKey is always forced to the canonical community value — never
     trust whatever was previously on disk. */
  (void)key;
  strncpy(key, GARLIC_DEFAULT_KEY, sizeof(key)-1);
  key[sizeof(key)-1] = '\0';

  mkdir("/data", 0755);
  mkdir(GARLIC_DIR, 0777);
  FILE *o = fopen(GARLIC_CONFIG_PATH ".tmp", "w");
  if(!o) return -1;
  fprintf(o,
    "serverHost=%s\nserverPort=%d\nworkerKey=%s\npollInterval=%d\n",
    host, port, key, seconds);
  fclose(o);
  return rename(GARLIC_CONFIG_PATH ".tmp", GARLIC_CONFIG_PATH);
}


int
sys_garlic_worker_is_running(void) {
  return sys_find_pid(GARLIC_WORKER_PROC_NAME) > 0 ? 1 : 0;
}


int
sys_garlic_worker_set_enabled(int on) {
  pid_t existing;
  if(on) {
    if(sys_find_pid(GARLIC_WORKER_PROC_NAME) > 0) return 1;
    sys_garlic_seed_config();
    if(spawn_embedded("garlic-worker",
                      garlic_worker_elf, garlic_worker_elf_size) != 0) {
      return -1;
    }
    /* Same race-mitigation as BackPork — wait for the proc to register. */
    for(int i = 0; i < 30; i++) {
      if(sys_find_pid(GARLIC_WORKER_PROC_NAME) > 0) return 1;
      usleep(100000);
    }
    return -1;
  }
  while((existing = sys_find_pid(GARLIC_WORKER_PROC_NAME)) > 0) {
    if(kill(existing, SIGKILL) != 0) {
      perror("kill garlic-worker");
      return -1;
    }
    sleep(1);
  }
  return 0;
}


/* SaveMgr probe — TCP connect to 127.0.0.1:8082 with a 250 ms timeout. */
static int
garlic_savemgr_port_open(void) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0) return 0;
  int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port   = htons(8082);
  a.sin_addr.s_addr = htonl(0x7f000001);
  int rc = connect(s, (struct sockaddr*)&a, sizeof(a));
  int ok = 0;
  if(rc == 0) {
    ok = 1;
  } else if(errno == EINPROGRESS) {
    fd_set wset; FD_ZERO(&wset); FD_SET(s, &wset);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 };
    if(select(s + 1, NULL, &wset, NULL, &tv) > 0) {
      int err = 0; socklen_t el = sizeof(err);
      if(getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0) ok = 1;
    }
  }
  close(s);
  return ok;
}


int
sys_garlic_savemgr_is_running(void) {
  return garlic_savemgr_port_open();
}


/* Track our own savemgr child so we can kill it. The bundled binary
   doesn't set a thread name, so PID lookup by name doesn't work. */
static pid_t g_savemgr_pid = 0;

int
sys_garlic_savemgr_set_enabled(int on) {
  if(on) {
    if(garlic_savemgr_port_open()) return 1;
    char* argv[2] = {"garlic-savemgr", 0};
    char* envp[1] = {0};
    int devnull = open("/dev/null", O_WRONLY);
    pid_t pid = elfldr_spawn("/", devnull, (uint8_t*)garlic_savemgr_elf,
                             argv, envp);
    if(devnull >= 0) close(devnull);
    if(pid < 0) return -1;
    g_savemgr_pid = pid;
    /* Wait up to ~3s for the listener to bind. */
    for(int i = 0; i < 30; i++) {
      if(garlic_savemgr_port_open()) return 1;
      usleep(100000);
    }
    return -1;
  }
  if(g_savemgr_pid > 0) {
    if(kill(g_savemgr_pid, SIGKILL) == 0) {
      g_savemgr_pid = 0;
    }
  }
  /* SaveMgr's port may take a beat to release. */
  for(int i = 0; i < 10; i++) {
    if(!garlic_savemgr_port_open()) break;
    usleep(100000);
  }
  return 0;
}


__attribute__((constructor)) static void
sys_init(void) {
  pid_t pid;
  int err;

  signal(SIGSEGV, on_fatal_signal);
  signal(SIGABRT, on_fatal_signal);
  signal(SIGFPE, on_fatal_signal);
  signal(SIGILL, on_fatal_signal);
  signal(SIGBUS, on_fatal_signal);
  signal(SIGTRAP, on_fatal_signal);
  signal(SIGSYS, on_fatal_signal);

  /* Escalate ucred FIRST so we can SIGKILL a prior ShellCore-class
     Sonic Loader instance. The previous order (kill → escalate)
     silently failed: the old loader had already raised its authid to
     0x48010..0013, and our pre-escalation kill() returned EPERM, so
     the new instance left the old one alive and just looped on
     EADDRINUSE for port 6969. The kstuff-provided primitive runs
     before sceUserServiceInitialize because it talks straight to the
     kernel patch, no userland service needed. */
  kernel_set_ucred_authid(-1, 0x4801000000000013L);

  /* Self-name so any future Sonic Loader can find + kill us too. */
  syscall(SYS_thr_set_name, -1, "sonic-loader.elf");

  /* Kill any prior instance. Cap the wait so a stuck old instance
     can't deadlock our boot. */
  for(int attempts = 0; attempts < 10; attempts++) {
    pid = sys_find_pid("sonic-loader.elf");
    if(pid <= 0) break;
    if(kill(pid, SIGKILL) != 0) {
      perror("kill old sonic-loader.elf");
      kill(pid, SIGTERM);
      break;
    }
    sleep(1);
  }

  /* sceUserServiceInitialize is best-effort — if a prior instance
     already initialised it process-wide and the call returns
     "already initialised", we don't want to exit(): the userland
     handles still work and the new launcher keeps booting. The old
     hard-exit path is what kept us hung when redeploys raced the
     teardown of a prior instance. */
  if((err=sceUserServiceInitialize(0)) && err != 0x80960003 /* SCE_USER_SERVICE_ERROR_NOT_TERMINATED */) {
    perror("sceUserServiceInitialize");
    /* fall through — we can still serve the web UI without it. */
  }

  sys_notify_address("Sonic Loader serving HTTP on", 6969);
}


__attribute__((destructor)) static void
sys_fini(void) {
  sceUserServiceTerminate();
}
