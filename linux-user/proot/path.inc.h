/* linux-user/proot/path.inc.h - guest path walk: canonicalisation and guest -> host path. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* Canonicalise `path` (relative to guest dir `base`): symlinks are resolved inside the guest
 * (the last one only when `follow`), ".." never leaves "/". Result: canonical guest path. */
#define PEMU_RS_NO_XDEV      0x01
#define PEMU_RS_NO_MAGICLINK 0x02
#define PEMU_RS_NO_SYMLINKS  0x04
#define PEMU_RS_BENEATH      0x08
#define PEMU_RS_IN_ROOT      0x10
#define PEMU_RS_CACHED       0x20
/* rf = openat2 RESOLVE_* flags. BENEATH / IN_ROOT use `base` as the anchor directory. */
static long pemu_canon(const char *base, const char *path, int follow, char *cur, unsigned rf) {
    char rest[PEMU_HP], nr[PEMU_HP]; int links = 0;
    const unsigned conf = rf & (PEMU_RS_BENEATH | PEMU_RS_IN_ROOT);
    char anchor[PATH_MAX]; snprintf(anchor, sizeof anchor, "%s", base);
    if (strlen(path) >= PEMU_HP / 2) return -ENAMETOOLONG;
    if (path[0] == '/') {
        if (rf & PEMU_RS_BENEATH) return -EXDEV;
        if (rf & PEMU_RS_IN_ROOT) snprintf(cur, PATH_MAX, "%s", anchor); else strcpy(cur, "/");
    } else snprintf(cur, PATH_MAX, "%s", base);
    strcpy(rest, path);
    char *p = rest;
    /* RESOLVE_NO_XDEV: a mount is an entry of the bind table (the rootfs itself is entry 0). Stepping onto a path that another
     * entry serves, in either direction (into a bind, or out of it through ".."), crosses a mount point: EXDEV. */
    int cm = (rf & PEMU_RS_NO_XDEV) ? pemu_mnt_of(cur) : 0;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        char *e = p; while (*e && *e != '/') e++;
        size_t n = (size_t)(e - p);
        char *q = e; while (*q == '/') q++;
        int last = !*q, slash = last && *e == '/';
        if (n == 1 && p[0] == '.') { p = e; continue; }
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (conf && !strcmp(cur, anchor)) { if (rf & PEMU_RS_BENEATH) return -EXDEV; p = e; continue; }   /* IN_ROOT: ".." stays put */
            char *s = strrchr(cur, '/'); if (s == cur) cur[1] = 0; else *s = 0; p = e;
            if ((rf & PEMU_RS_NO_XDEV) && pemu_mnt_of(cur) != cm) return -EXDEV;
            continue; }
        if (n > 255) return -ENAMETOOLONG;
        char name[256]; memcpy(name, p, n); name[n] = 0;
        size_t cl = strlen(cur);
        char cand[PATH_MAX];
        if (cl + n + 2 >= PATH_MAX) return -ENAMETOOLONG;
        if (cl == 1) cl = 0;                              /* cur == "/": no separator to add twice */
        memcpy(cand, cur, cl); cand[cl] = '/'; memcpy(cand + cl + 1, name, n + 1);
        char host[PEMU_HP]; int raw = 0;
        if (pemu_g2h(cand, host, &raw) < 0) return -ENAMETOOLONG;
        if ((rf & PEMU_RS_NO_XDEV) && pemu_mnt_of(cand) != cm) return -EXDEV;
        if (raw) {                       /* /proc, /sys, /dev: the host kernel resolves them */
            if (!strcmp(cur, "/proc") && !strcmp(name, "self")) snprintf(cand, sizeof cand, "/proc/%d", (int) getpid());
            else if (!strcmp(cur, "/proc") && !strcmp(name, "thread-self")) snprintf(cand, sizeof cand, "/proc/%d/task/%d", (int) getpid(), (int) syscall(SYS_gettid));
            strcpy(cur, cand); p = e; continue;
        }
        if (last && !follow && !slash && !(rf & PEMU_RS_NO_SYMLINKS)) { strcpy(cur, cand); break; }
        struct stat st;
        if (lstat(host, &st) == 0 && S_ISLNK(st.st_mode)) {
            char tgt[PATH_MAX]; ssize_t l = readlink(host, tgt, sizeof tgt - 1);
            if (l < 0) return -errno;
            if (l == 0) return -ENOENT;
            tgt[l] = 0;
            if (rf & PEMU_RS_NO_SYMLINKS) return -ELOOP;
            if (++links > 40) return -ELOOP;
            if (pemu_l2s) l2s_hop(host, tgt, &l);
            size_t tl = (size_t) l, el = strlen(e);
            if (tl + el + 2 >= sizeof nr) return -ENAMETOOLONG;
            memcpy(nr, tgt, tl); nr[tl] = '/'; memcpy(nr + tl + 1, e, el + 1);
            if (tgt[0] == '/') {
                if (rf & PEMU_RS_BENEATH) return -EXDEV;
                if (rf & PEMU_RS_IN_ROOT) snprintf(cur, PATH_MAX, "%s", anchor); else strcpy(cur, "/");
                if (rf & PEMU_RS_NO_XDEV) cm = pemu_mnt_of(cur);          /* jumping to the root is not a crossing; what follows is checked from there */
            }
            strcpy(rest, nr); p = rest; continue;
        }
        strcpy(cur, cand); p = e;
    }
    return 0;
}

/* Guest (dirfd, path) -> host absolute path. 0 = translated into `host`, 1 = pass the call through
 * unchanged (no rootfs, empty path, dirfd outside the rootfs), <0 = -errno. gout: canonical guest path. */
static long pemu_path_r(long dfd, const char *p, int follow, char *host, size_t hsz, char *gout, unsigned rf) {
    if (!pemu_on || !p || !*p) return 1;
    char base[PATH_MAX], gc[PATH_MAX];
    if (p[0] == '/' && !(rf & PEMU_RS_IN_ROOT)) strcpy(base, "/");
    else if ((int) dfd == AT_FDCWD) { pemu_lock(); snprintf(base, sizeof base, "%s", pemu_cwd); pemu_unlock(); }
    else if (pemu_fd2g((int) dfd, base) < 0) return 1;
    long r = pemu_canon(base, p, follow, gc, rf);
    if (r < 0) return r;
    if (pemu_g2h(gc, host, NULL) < 0) return -ENAMETOOLONG;
    size_t pl = strlen(p), hl = strlen(host);
    if (pl > 1 && p[pl - 1] == '/' && hl > 1 && host[hl - 1] != '/' && hl + 1 < hsz) { host[hl] = '/'; host[hl + 1] = 0; }
    if (gout) strcpy(gout, gc);
    return 0;
}
static long pemu_path(long dfd, const char *p, int follow, char *host, size_t hsz, char *gout) {
    return pemu_path_r(dfd, p, follow, host, hsz, gout, 0);
}
