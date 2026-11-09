/* linux-user/proot/secure_walk.inc.h - race-free walks (openat2 RESOLVE_IN_ROOT). Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- race-free walks.
 * The guest walk validates a path, but the host kernel walks the resulting host path again, and a guest thread
 * could swap a directory for a symlink in between. For paths in the rootfs the final component is therefore
 * reached through openat2(rootfd, guest-path, RESOLVE_IN_ROOT), which the kernel itself keeps inside the rootfs:
 *   - operations that follow the last component get an O_PATH fd of the target ("/proc/self/fd/N"),
 *   - the others get an O_PATH fd of the parent directory ("/proc/self/fd/N/name").
 * Used by open* and the mutating calls (mkdir, unlink, rename, link, symlink, mknod, chmod, chown, utimes,
 * truncate, setxattr, removexattr, chdir); read-only calls (stat, access, readlink, getxattr) stay on the fast
 * path. Needs Linux 5.6+ (openat2) and /proc. PEMU_SECURE_OPEN=0/1 overrides the default (off on Android,
 * whose seccomp filter may reject openat2). */
#ifndef SYS_openat2
#define SYS_openat2 437
#endif
static int pemu_o2 = -1;                                   /* -1 unknown, 0 off, 1 on */
static int pemu_secure(void) {
    if (pemu_o2 < 0) { const char *e = getenv("PEMU_SECURE_OPEN"); pemu_o2 = e && *e ? *e == '1' : (getenv("ANDROID_ROOT") == NULL); }
    return pemu_o2 > 0;
}
static int pemu_best_bind(const char *g) {
    size_t n = strlen(g); int best = 0; size_t bl = 0;
    for (int i = 0; i < pemu_nb; i++) {
        struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
        if (b->gl == 1 || (n >= b->gl && !memcmp(g, b->g, b->gl) && (g[b->gl] == '/' || !g[b->gl])))
            if (b->gl >= bl) { best = i; bl = b->gl; }
    }
    return best;
}
/* openat2(rootfs, canonical guest path, RESOLVE_IN_ROOT | rf) */
static long pemu_rootat2(const char *gc, long flags, long mode, unsigned rf) {
    int rfd = open(pemu_root, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (rfd < 0) return -errno;
    uint64_t how[3] = { (uint64_t) flags, (uint64_t) mode, 0x10 | (rf & (PEMU_RS_NO_MAGICLINK | PEMU_RS_NO_SYMLINKS)) };
    long fd = hs(SYS_openat2, rfd, (long) (gc[1] ? gc + 1 : "."), (long) how, (long) sizeof how, 0, 0);
    close(rfd);
    return fd;
}
/* Like pemu_path, for the syscall helpers: *tfd >= 0 is a temporary fd that `host` refers to; the caller
 * closes it after the syscall. gout (optional) = canonical guest path. */
static long pemu_path_s(int sec, long dfd, const char *p, int follow, char *host, size_t hsz, char *gout, int *tfd) {
    char gc[PATH_MAX]; *tfd = -1;
    long r = pemu_path_r(dfd, p, follow, host, hsz, gc, 0);
    if (r < 0) return r;
    if (r == 1) return 1;
    if (gout) strcpy(gout, gc);
    if (!sec || !pemu_secure()) return 0;
    int bi = pemu_best_bind(gc);
    if (pemu_b[bi].gl != 1 || pemu_b[bi].raw) return 0;
    size_t pl = strlen(p); const char *tr = (pl > 1 && p[pl - 1] == '/') ? "/" : "";
    long fd; char par[PATH_MAX];
    if (follow || !strcmp(gc, "/")) {
        fd = pemu_rootat2(gc, O_PATH | O_CLOEXEC, 0, 0);
        if (fd >= 0) snprintf(host, hsz, "/proc/self/fd/%d%s", (int) fd, tr);
    } else {
        strcpy(par, gc); char *sl = strrchr(par, '/'); const char *name = sl + 1;
        if (sl == par) par[1] = 0; else *sl = 0;
        fd = pemu_rootat2(par, O_PATH | O_DIRECTORY | O_CLOEXEC, 0, 0);
        if (fd >= 0) snprintf(host, hsz, "/proc/self/fd/%d/%s%s", (int) fd, name, tr);
    }
    if (fd == -ENOSYS) { pemu_o2 = 0; return 0; }          /* no openat2: keep the plain host path */
    if (fd < 0) return fd;
    *tfd = (int) fd;
    return 0;
}
static int pemu_mutating(long nr) {
    return nr == SYS_mkdirat || nr == SYS_mknodat || nr == SYS_unlinkat || nr == SYS_fchmodat || nr == SYS_fchownat ||
           nr == SYS_utimensat || nr == SYS_linkat || nr == SYS_renameat || nr == SYS_renameat2 || nr == SYS_truncate ||
           nr == SYS_setxattr || nr == SYS_lsetxattr || nr == SYS_removexattr || nr == SYS_lremovexattr || nr == 452;
}
