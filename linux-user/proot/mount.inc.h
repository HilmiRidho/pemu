/* linux-user/proot/mount.inc.h - mount(MS_BIND) / umount2 / chroot / fanotify_mark. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- mount(MS_BIND) / umount2 : handled as an edit of the bind table (the host is never touched).
 * Other mount types stay EPERM. Only affects this process tree (fork copies the table, re-exec rebuilds it). */
#define PEMU_MS_REMOUNT 32
#define PEMU_MS_BIND    4096
static long pemu_mount(const char *src, const char *tgt, unsigned long flags) {
    if (!pemu_on || !PEMU_PRIV()) return -EPERM;
    if (!(flags & PEMU_MS_BIND) || !src || !tgt) return -EPERM;
    char hs_[PEMU_HP], ht[PEMU_HP], gs[PATH_MAX], gt[PATH_MAX]; struct stat a, b;
    long r = pemu_path(AT_FDCWD, src, 1, hs_, sizeof hs_, gs); if (r < 0) return r;
    if (r == 1) { snprintf(hs_, sizeof hs_, "%s", src); }
    r = pemu_path(AT_FDCWD, tgt, 1, ht, sizeof ht, gt); if (r < 0) return r;
    if (r == 1) return -EPERM;
    if (stat(hs_, &a) < 0) return -errno;
    if (stat(ht, &b) < 0) return -errno;
    if (S_ISDIR(a.st_mode) != S_ISDIR(b.st_mode)) return S_ISDIR(a.st_mode) ? -ENOTDIR : -EINVAL;
    if (flags & PEMU_MS_REMOUNT) return 0;           /* read-only / nosuid flags are not enforced */
    if (!strcmp(gt, "/")) return -EBUSY;
    pemu_lock(); long e = pemu_add_bind(hs_, gt) < 0 ? -ENOSPC : 0; pemu_unlock();
    return e;
}
static long pemu_umount(const char *tgt) {
    if (!pemu_on || !tgt || !PEMU_PRIV()) return -EPERM;
    char h[PEMU_HP], gc[PATH_MAX];
    long r = pemu_path(AT_FDCWD, tgt, 0, h, sizeof h, gc); if (r < 0) return r;
    if (r == 1) return -EPERM;
    pemu_lock();
    for (int i = pemu_nb - 1; i > 0; i--)
        if (!pemu_b[i].off && !strcmp(pemu_b[i].g, gc)) { pemu_b[i].off = 1; pemu_unlock(); return 0; }
    pemu_unlock();
    return -EINVAL;                                  /* not a mount point (or the rootfs itself) */
}
/* fanotify_mark(fd, flags, mask, dirfd, path): the path is a guest path */
static long pemu_fanotify_mark(long nr, long fd, long flags, long mask, long dirfd, const char *p) {
    char host[PEMU_HP]; int tfd;
    if (!p) return hs(nr, fd, flags, mask, dirfd, 0, 0);
    long r = pemu_path_s(0, dirfd, p, !(flags & 4 /* FAN_MARK_DONT_FOLLOW */), host, sizeof host, NULL, &tfd);
    if (r < 0) return r;
    long x = r == 0 ? hs(nr, fd, flags, mask, AT_FDCWD, (long) host, 0) : hs(nr, fd, flags, mask, dirfd, (long) p, 0);
    if (tfd >= 0) close(tfd);
    return x;
}

/* chroot(path): the directory becomes the new guest root. The root bind is re-pointed at it, binds below it are
 * re-rooted, the others are dropped (as the kernel hides what is outside), the cwd is clamped. A chroot while
 * other threads are walking paths can briefly show them a mix of old and new table; programs normally chroot
 * from a single thread. Returns 0 or -errno. */
static long pemu_chroot(const char *path) {
    if (!pemu_on || !path || !PEMU_PRIV()) return -EPERM;       /* as the kernel: only root (-0 / -i 0:0) may chroot */
    char h[PEMU_HP], gc[PATH_MAX], rh[PATH_MAX]; struct stat st;
    long r = pemu_path(AT_FDCWD, path, 1, h, sizeof h, gc); if (r < 0) return r;
    if (r == 1) return -EPERM;
    if (!strcmp(gc, "/")) return 0;
    if (stat(h, &st) < 0) return -errno;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
    if (!realpath(h, rh)) return -errno;
    size_t gl = strlen(gc);
    pemu_lock();
    for (int i = 1; i < pemu_nb; i++) {
        struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
        if (b->gl > gl && !memcmp(b->g, gc, gl) && b->g[gl] == '/') {         /* below the new root: keep, shorter guest path */
            char *ng = strdup(b->g + gl); size_t nl = strlen(ng);
            b->g = ng; b->gl = nl;
        } else b->off = 1;                                                    /* outside, or exactly the new root (covered by rh) */
    }
    struct pemu_bind *b0 = &pemu_b[0];
    b0->h = strdup(rh); b0->hl = strlen(rh); b0->raw = 0;
    snprintf(pemu_root, sizeof pemu_root, "%s", rh);
    if (!strncmp(pemu_cwd, gc, gl) && (pemu_cwd[gl] == '/' || !pemu_cwd[gl])) memmove(pemu_cwd, pemu_cwd + gl, strlen(pemu_cwd + gl) + 1);
    else strcpy(pemu_cwd, "/");
    if (!pemu_cwd[0]) strcpy(pemu_cwd, "/");
    char cw[PATH_MAX]; strcpy(cw, pemu_cwd);
    pemu_unlock();
    char hc[PEMU_HP]; if (pemu_g2h(cw, hc, NULL) == 0) (void) !chdir(hc);   /* keep the host cwd in step with the guest cwd */
    return 0;
}
