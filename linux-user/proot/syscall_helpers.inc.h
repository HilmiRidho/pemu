/* linux-user/proot/syscall_helpers.inc.h - translated path syscalls (stat, link, readlink, chdir, ...). Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- translated syscall helpers (nr = host syscall number) */
/* nr(dfd, path, a2..a5) */
static int pemu_fakeproc(const char *g, char *b, size_t sz);
/* stat of a fake /proc file whose real counterpart is hidden: 0 = *st filled in */
static int pemu_fakeproc_stat(const char *g, struct stat *st) {
    char b[4096];
    if (strncmp(g, "/proc/", 6) || pemu_fakeproc(g, b, sizeof b) < 0) return -1;
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFREG | 0444; st->st_nlink = 1; st->st_uid = getuid(); st->st_gid = getgid(); st->st_blksize = 1024;
    st->st_atime = st->st_mtime = st->st_ctime = time(NULL);
    return 0;
}
/* syscalls whose EPERM becomes success for a fake root (proot's fake_id0: mknod, chmod, chown, setxattr) */
static int pemu_forced(long nr) {
    return nr == SYS_mknodat || nr == SYS_fchmodat || nr == 452 || nr == SYS_fchownat || nr == SYS_setxattr || nr == SYS_lsetxattr;
}
/* link2symlink on the stat family: the answer is the stat of the file behind a fake hard link, with its link count.
 * 1 = *x is the result of the call */
static int l2s_stat(long nr, long dfd, const char *p, long a2, long a3, long a4, long *x) {
    char hp[PEMU_HP], inter[PATH_MAX], fin[PATH_MAX]; int nl;
    if (pemu_path(dfd, p, 0, hp, sizeof hp, NULL) != 0 || !l2s_resolve(hp, inter, fin, &nl)) return 0;
    if (nr == SYS_newfstatat) { *x = hs(nr, (long) AT_FDCWD, (long) fin, a2, a3 | AT_SYMLINK_NOFOLLOW, 0, 0); if (*x == 0) ((struct stat*) a2)->st_nlink = (nlink_t) nl; }
    else { *x = hs(nr, (long) AT_FDCWD, (long) fin, a2 | AT_SYMLINK_NOFOLLOW, a3, a4, 0); if (*x == 0) *(uint32_t*) ((char*) a4 + 16) = (uint32_t) nl; }   /* statx: stx_nlink */
    return 1;
}
/* link(2) / linkat(2) as proot's link2symlink does it: old becomes a fake hard link, new is a second symlink to it.
 * 1 = *x is the result of the call */
static int l2s_link(long od, const char *o, int ofol, long nd, const char *n, long *x) {
    char ho[PEMU_HP], hn[PEMU_HP], inter[PATH_MAX]; struct stat st;
    long r1 = pemu_path(od, o, ofol, ho, sizeof ho, NULL); if (r1 != 0) { if (r1 < 0) { *x = r1; return 1; } return 0; }
    long r2 = pemu_path(nd, n, 0, hn, sizeof hn, NULL); if (r2 != 0) { if (r2 < 0) { *x = r2; return 1; } return 0; }
    if (lstat(hn, &st) == 0) { *x = -EEXIST; return 1; }
    long rc = l2s_move(ho, inter);
    if (rc == 0 && symlink(inter, hn) < 0) rc = -errno;
    *x = rc; return 1;
}
static long pemu_callat(long nr, long dfd, const char *p, int follow, long a2, long a3, long a4, long a5) {
    char host[PEMU_HP]; int tfd;
    if (pemu_l2s && pemu_on) {
        long x;
        if (nr == SYS_unlinkat && !(a2 & AT_REMOVEDIR) && pemu_path(dfd, p, 0, host, sizeof host, NULL) == 0) l2s_dec(host);
        else if ((nr == SYS_newfstatat || nr == SYS_statx) && l2s_stat(nr, dfd, p, a2, a3, a4, &x)) return x;
    }
    if (nr == SYS_fchownat) { a2 = (long) pemu_chown_id((unsigned long) a2, pemu_uid, getuid()); a3 = (long) pemu_chown_id((unsigned long) a3, pemu_gid, getgid()); }
    long r = pemu_path_s(pemu_mutating(nr), dfd, p, follow, host, sizeof host, NULL, &tfd);
    if (r < 0) return r;
    long x = r == 0 ? hs(nr, (long) AT_FDCWD, (long) host, a2, a3, a4, a5) : hs(nr, dfd, (long) p, a2, a3, a4, a5);
    if (tfd >= 0) close(tfd);
    if (pemu_forced(nr)) x = (nr == SYS_fchownat) ? pemu_force_acc(x) : pemu_force(x);
    if (r == 0 && (nr == SYS_newfstatat || nr == SYS_statx) && (x == -ENOENT || x == -EACCES || x == -EPERM)) {   /* hidden /proc file that pemu fakes on open */
        char gc[PATH_MAX]; struct stat fs;
        if (pemu_path(dfd, p, follow, host, sizeof host, gc) == 0 && pemu_fakeproc_stat(gc, &fs) == 0) {
            if (nr == SYS_newfstatat) { *(struct stat*) a2 = fs; return 0; }
            unsigned char *sx = (unsigned char*) a4; memset(sx, 0, 256);             /* struct statx, a4 = buffer */
            *(uint32_t*) (sx + 0) = 0x7ff & ~0x40u;                                   /* STATX_BASIC_STATS without BTIME */
            *(uint32_t*) (sx + 4) = (uint32_t) fs.st_blksize; *(uint32_t*) (sx + 16) = (uint32_t) fs.st_nlink;
            *(uint32_t*) (sx + 20) = (uint32_t) fs.st_uid;    *(uint32_t*) (sx + 24) = (uint32_t) fs.st_gid;
            *(uint16_t*) (sx + 28) = (uint16_t) fs.st_mode;
            *(int64_t*) (sx + 64) = *(int64_t*) (sx + 96) = *(int64_t*) (sx + 112) = (int64_t) fs.st_mtime;   /* atime, ctime, mtime */
            return 0;
        }
    }
    return x;
}
/* nr(path, a1..a5) */
static long pemu_call1(long nr, const char *p, int follow, long a1, long a2, long a3, long a4, long a5) {
    char host[PEMU_HP]; int tfd; long r = pemu_path_s(pemu_mutating(nr), AT_FDCWD, p, follow, host, sizeof host, NULL, &tfd);
    if (r < 0) return r;
    long x = hs(nr, (long) (r == 0 ? host : p), a1, a2, a3, a4, a5);
    if (tfd >= 0) close(tfd);
    if (pemu_forced(nr)) x = (nr == SYS_fchownat) ? pemu_force_acc(x) : pemu_force(x);
    return x;
}
/* nr(olddfd, old, newdfd, new, flags) : rename / renameat2 / link */
static long pemu_call2at(long nr, long od, const char *o, int ofol, long nd, const char *n, int nfol, long flags) {
    char ho[PEMU_HP], hn[PEMU_HP]; int t1, t2;
    if (pemu_l2s && pemu_on) {
        long x;
        if (nr == SYS_linkat && l2s_link(od, o, ofol, nd, n, &x)) return x;
        if ((nr == SYS_renameat || (nr == SYS_renameat2 && !(flags & 1))) && pemu_path(nd, n, 0, hn, sizeof hn, NULL) == 0) l2s_dec(hn);
    }
    long r1 = pemu_path_s(1, od, o, ofol, ho, sizeof ho, NULL, &t1); if (r1 < 0) return r1;
    long r2 = pemu_path_s(1, nd, n, nfol, hn, sizeof hn, NULL, &t2); if (r2 < 0) { if (t1 >= 0) close(t1); return r2; }
    long x = hs(nr, r1 == 0 ? (long) AT_FDCWD : od, (long) (r1 == 0 ? ho : o), r2 == 0 ? (long) AT_FDCWD : nd, (long) (r2 == 0 ? hn : n), flags, 0);
    if (t1 >= 0) close(t1);
    if (t2 >= 0) close(t2);
    return x;
}
static long pemu_symlinkat(const char *target, long nd, const char *n) {
    char hn[PEMU_HP]; int tfd; long r = pemu_path_s(1, nd, n, 0, hn, sizeof hn, NULL, &tfd); if (r < 0) return r;
    long x = hs(SYS_symlinkat, (long) target, r == 0 ? (long) AT_FDCWD : nd, (long) (r == 0 ? hn : n), 0, 0, 0);
    if (tfd >= 0) close(tfd);
    return x;
}
static long pemu_readlinkat(long dfd, const char *p, char *buf, long sz) {
    char host[PEMU_HP], gc[PATH_MAX]; long r = pemu_path(dfd, p, 0, host, sizeof host, gc);
    if (r < 0) return r;
    if (r == 1) return hs(SYS_readlinkat, dfd, (long) p, (long) buf, sz, 0, 0);
    if (pemu_l2s) { char i2[PATH_MAX], f2[PATH_MAX]; int n2; if (!l2s_named(host) && l2s_resolve(host, i2, f2, &n2)) return -EINVAL; }   /* a hard link is no symlink */
    char pre[64]; snprintf(pre, sizeof pre, "/proc/%d/", (int) getpid());
    size_t pl = strlen(pre);
    if (!strncmp(gc, pre, pl)) {
        const char *t = gc + pl, *v = NULL;
        pemu_lock();
        if (!strcmp(t, "exe") && pemu_exe[0]) v = pemu_exe; else if (!strcmp(t, "cwd")) v = pemu_cwd; else if (!strcmp(t, "root")) v = "/";
        if (v) { long l = (long) strlen(v); if (l > sz) l = sz; memcpy(buf, v, (size_t) l); pemu_unlock(); return l; }
        pemu_unlock();
        long n = hs(SYS_readlinkat, (long) AT_FDCWD, (long) host, (long) buf, sz, 0, 0);   /* fd/N, ...: show guest paths */
        if (n > 0 && buf[0] == '/') {
            char tmp[PATH_MAX], g[PATH_MAX]; long m = n < (long) sizeof tmp - 1 ? n : (long) sizeof tmp - 1;
            memcpy(tmp, buf, (size_t) m); tmp[m] = 0;
            if (pemu_h2g(tmp, g) == 0) { long l = (long) strlen(g); if (l > sz) l = sz; memcpy(buf, g, (size_t) l); return l; }
        }
        return n;
    }
    return hs(SYS_readlinkat, (long) AT_FDCWD, (long) host, (long) buf, sz, 0, 0);
}
static long pemu_getcwd(char *buf, long size) {
    pemu_lock(); long l = (long) strlen(pemu_cwd) + 1;
    if (l > size) { pemu_unlock(); return -ERANGE; }
    memcpy(buf, pemu_cwd, (size_t) l); pemu_unlock(); return l;
}
static long pemu_chdir(const char *p) {
    char host[PEMU_HP], gc[PATH_MAX]; int tfd; long r = pemu_path_s(1, AT_FDCWD, p, 1, host, sizeof host, gc, &tfd);
    if (r < 0) return r;
    if (r == 1) return hs(SYS_chdir, (long) p, 0, 0, 0, 0, 0);
    r = hs(SYS_chdir, (long) host, 0, 0, 0, 0, 0);
    if (tfd >= 0) close(tfd);
    if (r == 0) { pemu_lock(); strcpy(pemu_cwd, gc); pemu_unlock(); }
    return r;
}
static long pemu_fchdir(long fd) {
    long r = hs(SYS_fchdir, fd, 0, 0, 0, 0, 0); char g[PATH_MAX];
    if (r == 0 && pemu_fd2g((int) fd, g) == 0) { pemu_lock(); strcpy(pemu_cwd, g); pemu_unlock(); }
    return r;
}
