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

#include <stdint.h>
#include <stddef.h>


int sys_launch_title(const char* title_id, const char* args);
int sys_launch_homebrew(const char* cwd, const char* path, const char* args,
			const char* env);
int sys_launch_daemon(const char* cwd, const char* uri, const char* args,
		      const char* env);
int sys_launch_payload(const char* cwd, uint8_t* elf, size_t elf_size,
                       const char* argv, const char* env);

void sys_spawn_embedded_payloads(void);

/* BackPork toggle (Settings panel). on=1 spawns the bundled
   backpork.elf if it isn't already running; on=0 kills any running
   instance. Returns 1 if running afterwards, 0 if stopped, -1 on error. */
int sys_backpork_set_enabled(int on);
int sys_backpork_is_running(void);

/* nanoDNS toggle. drakmor/nanoDNS — bundled DNS forwarder +
   override engine, listens on 127.0.0.1:53. Auto-spawned at boot
   so the home-screen tile auto-install (and any other community-
   domain resolver call) works without the user changing PS5 DNS
   settings. The toggle is here so users who run their own DNS
   stack can stop nanoDNS without rebuilding. */
int sys_nanodns_set_enabled(int on);
int sys_nanodns_is_running(void);

/* Bundled Lapy JB Daemon (replaces the etaHEN-compatible jb.c IPC
   daemon entirely). Spawned at boot, listens on its own polling
   path and handles PID escalation for apps that previously needed
   etaHEN's HijackerCommand protocol. on=1 spawns the bundled
   lapyjb.elf if it isn't already running; on=0 kills any running
   instance. Returns 1 if running afterwards, 0 if stopped, -1 on
   error. Process is identified by name "lapyjb.elf". */
int sys_lapyjb_set_enabled(int on);
int sys_lapyjb_is_running(void);

/* EchoStretch/ps5-app-dumper — one-shot spawn. Dumps mounted apps
   to a connected USB drive. The user presses Run from Settings; the
   ELF runs detached, posts its own SceShellCore notifications, and
   exits when finished. */
int sys_spawn_app_dumper(void);

/* Return the userId (hex value PS5 firmware assigns to the signed-in
   account, e.g. 0x1396ECE8) of the foreground user, or 0 if no user is
   signed in / the call failed. name_out is optional and gets the
   human-readable user name (max 17 bytes incl. NUL); pass NULL to skip. */
uint32_t sys_get_foreground_user(char *name_out, size_t name_out_size);

/* Spawn one of the embedded helper payloads on demand from a Settings
   button (np-fake-signin, np-restore-account). Returns 0 on spawn
   success, -1 on failure. The payload runs detached and exits on its
   own — the caller does not wait for it. */
int sys_spawn_np_fake_signin(void);
int sys_spawn_np_restore_account(void);

/* Garlic Worker — long-running daemon that processes save-encrypt /
   decrypt jobs from garlicsaves.com. Runs in the background, does
   nothing while there are no pending jobs. on=1 spawns the bundled
   garlic-worker.elf if it isn't already running; on=0 kills any
   running instance. Returns 1 if running afterwards, 0 if stopped,
   -1 on error. */
int sys_garlic_worker_set_enabled(int on);
int sys_garlic_worker_is_running(void);

/* Garlic SaveMgr — interactive save manager that serves its own web UI
   on port 8082. Same toggle pattern. We identify "running" by probing
   the listening socket on 8082 because the upstream binary doesn't
   set a process name. */
int sys_garlic_savemgr_set_enabled(int on);
int sys_garlic_savemgr_is_running(void);

/* Seed /data/garlic/config.ini with the bundled defaults if the file
   doesn't already exist. workerKey is fixed so all instances of this
   payload share the same garlicsaves.com identity (helps the
   community-drive saves traffic). pollInterval can be overridden by
   the user via /api/garlic/poll. Called at boot. */
void sys_garlic_seed_config(void);

/* Update the pollInterval=N line in /data/garlic/config.ini in place
   (or seed the file if missing). Returns 0 on success, -1 on error.
   The worker re-reads the config on each cycle, so the change takes
   effect on the next poll without a respawn. */
int sys_garlic_set_poll_interval(int seconds);
int sys_garlic_get_poll_interval(void);

/* Look up a PID by its kernel-thread name (the same matching
   sys_backpork_is_running uses). Returns -1 if not found. */
int sys_find_pid_by_name(const char *name);

/* TCP-probe 127.0.0.1:<port>. Returns 1 if something accepts connect(),
   0 otherwise. Cheap helper used to decide whether the DPI install
   daemon (or any other localhost service) is already up. */
int sys_port_is_open(int port);

/* Spawn the embedded DPI (cy33hc/ps5-ezremote-dpi) install daemon if
   it isn't already running on 127.0.0.1:9040. Returns 1 if running
   afterwards, 0 if a spawn was issued (caller may have to wait a beat
   before connecting), -1 on error. */
int sys_dpi_ensure_running(void);

/* ftpsrv daemon — port-configurable, on/off toggle. The bundled
   ftpsrv binary only supports `-p PORT` on the command line; user/
   pass and TYPE-default aren't configurable in this build of ftpsrv
   (defaults are anonymous + Binary). Returns 1 if running afterwards,
   0 if stopped, -1 on error. */
int  sys_ftpsrv_set_enabled(int on);
int  sys_ftpsrv_is_running(void);
int  sys_ftpsrv_restart(void);
int  sys_ftpsrv_get_port(void);
void sys_ftpsrv_set_port(int port);

/* Sonic Loader fork of ftpsrv now accepts -u USER -P PASS -t TYPE.
   user="anonymous" (default) means open access (no auth gate);
   anything else triggers the auth gate. type is "auto" (default,
   client picks via TYPE I/A), "binary", or "ascii" — when forced,
   client TYPE commands are acked but env.type stays pinned. */
const char* sys_ftpsrv_get_user(void);
const char* sys_ftpsrv_get_pass(void);
const char* sys_ftpsrv_get_type(void);
void sys_ftpsrv_set_user(const char *user);
void sys_ftpsrv_set_pass(const char *pass);
void sys_ftpsrv_set_type(const char *type);


/* Firmware version helpers.

   sys_get_firmware_version() returns the system software version as a
   packed BCD uint32 — e.g. 0x10010000 for FW 10.01, 0x12000000 for
   12.00. Returns 0 if the lookup failed. The first-boot auto-installer
   uses the 0x10010000 threshold to pick between drakmor (≤ 10.01) and
   EchoStretch (> 10.01). */
unsigned int sys_get_firmware_version(void);
