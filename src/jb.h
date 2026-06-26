/* Sonic Loader (App JB Test variant) — own-process privilege
   escalation primitive. The full etaHEN-compatible IPC daemon
   (jb_start + cmd/file threads) is gone in this build; PID
   escalation requests now flow through the bundled Lapy JB
   Daemon (payloads/lapyjb.elf). The only call from main.c is
   jb_escalate_pid(getpid()) at boot. */

#pragma once

#include <sys/types.h>

int jb_escalate_pid(pid_t pid);
