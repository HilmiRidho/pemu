/* pemu_root.h - built-in "proot" for pemu / pemu_x64.  SPDX-License-Identifier: Apache-2.0
 *
 * proot needs ptrace to catch the guest's syscalls. pemu already *is* the thing that executes
 * every guest syscall, so no ptrace is needed: the path arguments are translated right where
 * the syscall is forwarded to the host. What is provided:
 *
 *   - a guest root file system ("chroot"): every guest path is resolved component by
 *     component inside the rootfs. Symlinks (absolute ones too) and ".." stay inside it.
 *   - bind mounts (-b host[:guest]); /dev /proc /sys are bound by default and are "raw"
 *     (the kernel resolves them itself, so /proc/self/fd/N and friends keep working).
 *   - the guest working directory (chdir/fchdir/getcwd), kept as a guest path.
 *   - #! scripts are expanded here (the host kernel would look the interpreter up on the host).
 *   - /proc/self/exe, cwd and fd links are shown as guest paths.
 *   - fake root (--root-id): uid/gid 0, chown/setuid succeed, files owned by the host user
 *     appear as owned by root.
 *   - AF_UNIX socket paths of bind()/connect() are translated.
 *   - sub-commands:  pemu login [opts] NAME|DIR [cmd...]   pemu install NAME tarball   pemu list
 *
 * Not a security boundary: a guest can still reach the host through raw binds, races on
 * symlinks and syscalls that are not translated (listed in README). Include after hs(). */
#ifndef PEMU_ROOT_H
#define PEMU_ROOT_H
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/utsname.h>

/* The implementation is split by topic; the parts depend on each other in this order. */
#include "proot/state.inc.h"
#include "proot/bind.inc.h"
#include "proot/link2symlink.inc.h"
#include "proot/path.inc.h"
#include "proot/secure_walk.inc.h"
#include "proot/syscall_helpers.inc.h"
#include "proot/fake_proc.inc.h"
#include "proot/open.inc.h"
#include "proot/socket.inc.h"
#include "proot/exec.inc.h"
#include "proot/mount.inc.h"
#include "proot/fake_id.inc.h"
#include "proot/kompat.inc.h"
#include "proot/exit.inc.h"
#include "proot/setup.inc.h"
#include "proot/cmdline.inc.h"
#include "proot/subcmd.inc.h"
#endif /* PEMU_ROOT_H */
