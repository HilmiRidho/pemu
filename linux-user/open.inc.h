/* linux-user/proot/open.inc.h - open / openat2 with RESOLVE_* flags. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- open with RESOLVE_* and, for paths in the rootfs, a race-free walk.
 * The guest walk above validates a path, but the host kernel walks it again; a guest thread could swap a
 * directory for a symlink in between. For the rootfs the final open is therefore done as
 * openat2(rootfd, guest-path, RESOLVE_IN_ROOT), which the kernel itself keeps inside the rootfs.
 * PEMU_SECURE_OPEN=0/1 overrides the default (off on Android, whose seccomp filter may reject openat2). */
/* Fake root (-0): the host kernel still sees the plain Termux user, but a real root opens files whose permission bits forbid it
 * (dpkg re-opens a 0440 conffile such as /etc/sudoers.d/README O_WRONLY for fsync). The file belongs to the host user, so add the
 * missing bits for the duration of the open and put the old mode back through the new fd. */
static long pemu_open_priv(const char *host, long hf, long mode) {
    struct stat st; int acc = (int) (hf & O_ACCMODE);
    if (!PEMU_PRIV() || geteuid() == 0 || stat(host, &st) != 0) return -EACCES;
    if (st.st_uid != geteuid() || !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) return -EACCES;
    mode_t need = (acc != O_RDONLY ? 0200 : 0) | (acc != O_WRONLY ? 0400 : 0), old = st.st_mode & 07777;
    if ((old & need) == need || chmod(host, old | need) != 0) return -EACCES;
    long fd = hs(SYS_openat, AT_FDCWD, (long) host, hf, mode, 0, 0);
    if (fd >= 0) fchmod((int) fd, old); else chmod(host, old);
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
    long fd = -ENOSYS;
    if (pemu_secure()) {
        int bi = pemu_best_bind(gc);
        if (pemu_b[bi].gl == 1 && !pemu_b[bi].raw) {
            int rfd = open(pemu_root, O_PATH | O_DIRECTORY | O_CLOEXEC);
            if (rfd >= 0) {
                uint64_t how[3] = { (uint64_t) hf, (hf & (O_CREAT | 020200000)) ? (uint64_t) mode : 0, 0x10 | (rf & (PEMU_RS_NO_MAGICLINK | PEMU_RS_NO_SYMLINKS)) };
                const char *rel = gc[1] ? gc + 1 : ".";
                char rbuf[PEMU_HP]; size_t pl = strlen(p);
                if (pl > 1 && p[pl - 1] == '/' && strlen(rel) + 2 < sizeof rbuf) { snprintf(rbuf, sizeof rbuf, "%s/", rel); rel = rbuf; }
                fd = hs(SYS_openat2, rfd, (long) rel, (long) how, (long) sizeof how, 0, 0);
                close(rfd);
                if (fd == -ENOSYS) pemu_o2 = 0;
            }
        }
    }
    if (fd == -ENOSYS) {
        if (rf & PEMU_RS_NO_MAGICLINK) { uint64_t how[3] = { (uint64_t) hf, (uint64_t) mode, PEMU_RS_NO_MAGICLINK }; fd = hs(SYS_openat2, AT_FDCWD, (long) host, (long) how, (long) sizeof how, 0, 0); }
        if (fd == -ENOSYS) fd = hs(SYS_openat, AT_FDCWD, (long) host, hf, mode, 0, 0);
    }
    if (fd == -EACCES) { long pf = pemu_open_priv(host, hf, mode); if (pf >= 0) return pf; }
    if (fd < 0 && (fd == -EACCES || fd == -ENOENT || fd == -EPERM) && (f = pemu_fakeproc_open(gc, hf)) != LONG_MIN) return f;
    return fd;
}
/* guest openat2(dfd, path, how, size); `how` flags already converted to host O_* by the caller */
static long pemu_openat2(long dfd, const char *p, const uint64_t *how, uint64_t sz) {
    unsigned rf = sz >= 24 ? (unsigned) how[2] : 0;
    if (rf & ~0x3fu) return -EINVAL;
    return pemu_open(dfd, p, (long) how[0], (long) how[1], rf);
}
