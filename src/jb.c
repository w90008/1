/* Sonic Loader (App JB Test variant) — minimal own-process
   privilege-escalation utility.

   In the upstream build this file also hosted an etaHEN-compatible
   IPC daemon (TCP listener on 127.0.0.1:9028 speaking the
   HijackerCommand protocol + a /download0/etahen_jailbreak file
   watcher) so apps shipping the universalps5 PRX could escalate
   their own PID by talking to us. This build replaces all of that
   with the bundled Lapy JB Daemon (payloads/lapyjb.elf), spawned
   from main.c at boot. The only piece kept here is the kernel-level
   ucred / rootdir / authid / caps escalation primitive, because
   main.c still uses it on Sonic Loader's own PID at boot so every
   file op (File Manager, FTP, cheat engine) runs with full kernel
   privilege regardless of source/destination mount. */

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <ps5/kernel.h>

#include "jb.h"


#define JB_AUTHID  0x4801000000000013ULL


/* Replicates Hijacker::jailbreak() from etaHEN/libhijacker. The
   ps5-payload-sdk wraps every kernel write we need:
     kernel_set_ucred_uid / _ruid / _svuid / _rgid / _svgid → cr_*
     kernel_set_ucred_authid                               → cr_sceAuthID
     kernel_set_ucred_caps                                 → cr_sceCaps[]
     kernel_set_ucred_attrs                                → cr_sceAttr[]
     kernel_set_proc_rootdir / _jaildir + kernel_get_root_vnode
                                                            → escape sandbox
   Returns 0 on full success, -1 on any failure. */
int
jb_escalate_pid(pid_t pid) {
  if(pid <= 0) return -1;

  intptr_t proc = kernel_get_proc(pid);
  if(!proc) return -1;

  int rc = 0;

  /* uid → 0 (root) on every cred slot the kernel checks. */
  if(kernel_set_ucred_uid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_ruid(pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svuid(pid, 0)!= 0) rc = -1;
  if(kernel_set_ucred_rgid(pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svgid(pid,0) != 0) rc = -1;

  /* Sandbox escape: point the proc's rootdir + jaildir at the kernel
     root vnode so the app sees the real "/" instead of its sandboxed
     /system_data/priv/appmeta/<id>/... view. */
  intptr_t rootvnode = kernel_get_root_vnode();
  if(rootvnode) {
    if(kernel_set_proc_rootdir(pid, rootvnode) != 0) rc = -1;
    if(kernel_set_proc_jaildir(pid, rootvnode) != 0) rc = -1;
  }

  /* Sony privilege bump — the caps + authid combo etaHEN uses. */
  if(kernel_set_ucred_authid(pid, JB_AUTHID) != 0) rc = -1;
  uint8_t caps[16];
  memset(caps, 0xff, sizeof(caps));
  if(kernel_set_ucred_caps(pid, caps) != 0) rc = -1;

  /* cr_sceAttr[0] = 0x80 — single byte high-attr flag. */
  if(kernel_set_ucred_attrs(pid, 0x80) != 0) rc = -1;

  return rc;
}
