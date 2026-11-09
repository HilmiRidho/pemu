/* linux-user/aarch64/host_syscall.inc.h - host syscall wrapper + built-in proot. SPDX-License-Identifier: Apache-2.0 */
static long hs(long nr, long a, long b, long c_, long d, long e, long f) {
    long r = syscall(nr, a, b, c_, d, e, f);
    return r == -1 ? -errno : r;
}

#include "../pemu_root.h"      /* built-in proot: rootfs, bind mounts, fake root, login */
