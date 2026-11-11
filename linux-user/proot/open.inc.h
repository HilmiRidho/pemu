/* linux-user/proot/open.inc.h - open / openat2 with RESOLVE_* flags. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- open with RESOLVE_* and, for paths in the rootfs, a race-free walk.
 * The guest walk above validates a path, but the host kernel walks it again; a guest thread could swap a
 * directory for a symlink in between. For the rootfs the final open is therefore done as
 * openat2(rootfd, guest-path, RESOLVE_IN_ROOT), which the kernel itself keeps inside the rootfs.
 * PEMU_SECURE_OPEN=0/1 overrides the default (off on Android, whose seccomp filter may reject openat2). */
/* Fake root (-0): the host kernel still sees the plain Termux user, but a real root opens files whose permission bits forbid it
 * (dpkg re-opens a 0440 conffile such as /etc/sudoers.d/README O_WRONLY for fsync). The file belongs to the host user, so add the
 * missing bits for the duration of the open and put the old mode back.
 * This is the one place where fake root touches the host file system, so it is kept as tight as it can be:
 *   - the inode is pinned with an O_PATH fd first; the chmod, the open and the restore all go through /proc/self/fd/N, so a
 *     rename or symlink swap of the path cannot redirect them to another file (the old code chmod'ed the path again to restore);
 *   - threads of this process are serialised (pemu_privlk), so a second thread never mistakes the temporary mode for the real
 *     one and "restores" it permanently;
 *   - O_CREAT / O_EXCL / O_NOFOLLOW are dropped for the re-open: the file exists (we just looked at it) and was reached already.
 * Other processes can still see the extra bits for the few microseconds of the open; that cannot be avoided without a real root. */
/* pin: an O_PATH fd of the file that the caller already reached through the race-free walk (RESOLVE_IN_ROOT), or -1 to open `host` by name */
static long pemu_open_priv(const char *host, long hf, long mode, int pin) {
    struct stat st; int acc = (int) (hf & O_ACCMODE);
    if (!PEMU_PRIV() || geteuid() == 0) return -EACCES;
    int pfd = pin >= 0 ? (int) fcntl(pin, F_DUPFD_CLOEXEC, 3) : open(host, O_PATH | O_CLOEXEC);
    if (pfd < 0) return -EACCES;
    char pp[48]; snprintf(pp, sizeof pp, "/proc/self/fd/%d", pfd);
    if (fstat(pfd, &st) != 0 || st.st_uid != geteuid() || !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) { close(pfd); return -EACCES; }
    mode_t need = (acc != O_RDONLY ? 0200 : 0) | (acc != O_WRONLY ? 0400 : 0);
    while (__sync_lock_test_and_set(&pemu_privlk, 1)) sched_yield();
    if (fstat(pfd, &st) != 0) { __sync_lock_release(&pemu_privlk); close(pfd); return -EACCES; }
    mode_t old = st.st_mode & 07777;                       /* the real mode: no other thread of ours has a temporary one in place now */
    long fd = -EACCES;
    if ((old & need) != need && chmod(pp, old | need) == 0) {
        fd = hs(SYS_openat, AT_FDCWD, (long) pp, hf & ~(long) (O_CREAT | O_EXCL | O_NOFOLLOW), PEMU_OPEN_HAS_MODE(hf) ? mode : 0, 0, 0);
        if (fd == -ENOENT && pin < 0) fd = hs(SYS_openat, AT_FDCWD, (long) host, hf, mode, 0, 0);       /* no /proc/self/fd: by name, as before */
        if (chmod(pp, old) != 0 && fd >= 0) fchmod((int) fd, old);
    }
    __sync_lock_release(&pemu_privlk);
    close(pfd);
    return fd;
}
static long pemu_open(long dfd, const char *p, long hf, long mode, unsigned rf) {
    char host[PEMU_HP], gc[PATH_MAX];
    int fol = !(hf & O_NOFOLLOW) && !((hf & O_CREAT) && (hf & O_EXCL));
    long r = pemu_path_r(dfd, p, fol, host, sizeof host, gc, rf);
    if (r < 0) return r;
    if (r == 1) return hs(SYS_openat, dfd, (long) p, hf, mode, 0, 0);
    static int force = -1; if (force < 0) { const char *e = getenv("PEMU_FAKE_PROC"); force = e && *e == '1'; }
    long f;
    if (force && (f = pemu_fakeproc_open(gc, hf)) != LONG_MIN) return f;
    long fd = -ENOSYS; int pin = -1;
    if (pemu_secure()) {
        if (pemu_in_rootfs(gc)) {
            char rt[PATH_MAX]; pemu_root_get(rt);
            int rfd = open(rt, O_PATH | O_DIRECTORY | O_CLOEXEC);
            if (rfd >= 0) {
                /* RESOLVE_CACHED goes to the kernel too: it answers EAGAIN when the lookup would have to block (or EINVAL before 5.12) */
                uint64_t how[3] = { (uint64_t) hf, PEMU_OPEN_HAS_MODE(hf) ? (uint64_t) mode : 0, 0x10 | (rf & (PEMU_RS_NO_MAGICLINK | PEMU_RS_NO_SYMLINKS | PEMU_RS_CACHED)) };
                const char *rel = gc[1] ? gc + 1 : ".";
                char rbuf[PEMU_HP]; size_t pl = strlen(p);
                if (pl > 1 && p[pl - 1] == '/' && strlen(rel) + 2 < sizeof rbuf) { snprintf(rbuf, sizeof rbuf, "%s/", rel); rel = rbuf; }
                fd = hs(SYS_openat2, rfd, (long) rel, (long) how, (long) sizeof how, 0, 0);
                if (fd == -EACCES) {      /* for the fake-root retry: pin the same file through the same race-free walk, not by the host name */
                    uint64_t ph[3] = { (uint64_t) (O_PATH | O_CLOEXEC), 0, 0x10 };
                    long q = hs(SYS_openat2, rfd, (long) rel, (long) ph, (long) sizeof ph, 0, 0);
                    if (q >= 0) pin = (int) q;
                }
                close(rfd);
                if (fd == -ENOSYS) pemu_o2 = 0;
            }
        }
    }
    if (fd == -ENOSYS) {
        if (rf & (PEMU_RS_NO_MAGICLINK | PEMU_RS_CACHED)) { uint64_t how[3] = { (uint64_t) hf, PEMU_OPEN_HAS_MODE(hf) ? (uint64_t) mode : 0, rf & (PEMU_RS_NO_MAGICLINK | PEMU_RS_CACHED) }; fd = hs(SYS_openat2, AT_FDCWD, (long) host, (long) how, (long) sizeof how, 0, 0); }
        if (fd == -ENOSYS) fd = hs(SYS_openat, AT_FDCWD, (long) host, hf, mode, 0, 0);
    }
    if (fd == -EACCES) { long pf = pemu_open_priv(host, hf, mode, pin); if (pin >= 0) { close(pin); pin = -1; } if (pf >= 0) return pf; }
    if (pin >= 0) close(pin);
    if (fd < 0 && (fd == -EACCES || fd == -ENOENT || fd == -EPERM) && (f = pemu_fakeproc_open(gc, hf)) != LONG_MIN) return f;
    return fd;
}
/* guest openat2(dfd, path, how, size); `how` flags already converted to host O_* by the caller */
static long pemu_openat2(long dfd, const char *p, const uint64_t *how, uint64_t sz) {
    unsigned rf = sz >= 24 ? (unsigned) how[2] : 0;
    if (rf & ~0x3fu) return -EINVAL;
    if (how[1] & ~07777ULL) return -EINVAL;                                  /* as the kernel: mode is permission bits only ... */
    if (how[1] && !PEMU_OPEN_HAS_MODE((long) how[0])) return -EINVAL;         /* ... and only with O_CREAT / O_TMPFILE */
    return pemu_open(dfd, p, (long) how[0], (long) how[1], rf);
}
