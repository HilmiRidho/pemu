/* linux-user/proot/setup.inc.h - setup: placeholders and user bind mounts. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- setup */
/* Placeholders (proot calls them "glue"): a bind whose guest path does not exist in the rootfs gets an empty
 * directory / file there, so the mount point shows up in readdir(); everything pemu created is removed again when the
 * guest exits, if still empty.  Directories get mode 0777, the last component mode 0, as in proot.  Nothing is created
 * inside another bind, and PROOT_DONT_POLLUTE_ROOTFS=1 creates nothing at all (the path still resolves). */
static void pemu_ph_add(const char *host) {
    if (pemu_nph >= PEMU_MAXPH) return;
    pemu_ph[pemu_nph++] = strdup(host); pemu_ph_owner = getpid();
}
static void pemu_placeholder(const char *g, mode_t type) {
    if (pemu_reexec || getenv("PROOT_DONT_POLLUTE_ROOTFS")) return;
    char cur[PATH_MAX]; size_t cl = 0; const char *p = g;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *e = p; while (*e && *e != '/') e++;
        size_t n = (size_t) (e - p);
        if (cl + n + 2 >= sizeof cur) return;
        cur[cl++] = '/'; memcpy(cur + cl, p, n); cl += n; cur[cl] = 0;
        int last = !*e; p = e;
        char host[PEMU_HP]; int raw = 0;
        if (pemu_g2h(cur, host, &raw) < 0 || raw || !pemu_in_rootfs(cur)) return;
        struct stat st;
        if (lstat(host, &st) == 0) { if (last) return; continue; }
        int rc = (last && !S_ISDIR(type)) ? mknod(host, (type & S_IFMT) | 0, 0) : mkdir(host, last ? 0 : 0777);
        if (rc < 0) return;                                   /* e.g. EACCES: the bind table is enough */
        pemu_ph_add(host);
    }
}

/* one binding "host:guest" (-b, or a recommended one with must = 0: silently skipped when the host path is missing).
 * Like proot: the host part goes through realpath, the guest part is resolved inside the guest (a symlink at the end is
 * followed unless the guest path ends in '!'), a missing host path is a warning, not an error. */
static int pemu_bind_user(const char *h, const char *g, int must) {
    char rh[PATH_MAX], gg[PATH_MAX], gc[PATH_MAX];
    if (!realpath(h, rh)) {
        if (must && !getenv("PROOT_IGNORE_MISSING_BINDINGS")) pemu_note(PEMU_WARNING, "can't sanitize binding \"%s\": %s", h, strerror(errno));
        return -1;
    }
    if (!g) g = h;
    if (g[0] != '/') {                                        /* relative guest path: relative to the current directory ("-b .") */
        char cw[PATH_MAX];
        if (!getcwd(cw, sizeof cw) || strlen(cw) + strlen(g) + 2 > sizeof gg) { pemu_note(PEMU_WARNING, "can't sanitize binding \"%s\": %s", h, strerror(errno ? errno : ENAMETOOLONG)); return -1; }
        size_t cl = strlen(cw), gl = strlen(g); memcpy(gg, cw, cl); gg[cl] = '/'; memcpy(gg + cl + 1, g, gl + 1);
    } else snprintf(gg, sizeof gg, "%s", g);
    size_t l = strlen(gg); int deref = 1;
    if (l > 1 && gg[l - 1] == '!') { gg[l - 1] = 0; deref = 0; }
    struct stat st;
    mode_t type = (lstat(rh, &st) < 0 || S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) ? S_IFREG : (st.st_mode & S_IFMT);
    long r = pemu_canon("/", gg, deref, gc, 0);
    if (r < 0) { pemu_note(PEMU_WARNING, "sanitizing the guest path (binding) \"%s\": %s", gg, strerror((int) -r)); return -1; }
    if (pemu_slot() < 0) { pemu_note(PEMU_WARNING, "too many bindings, \"%s\" ignored", h); return -1; }
    pemu_placeholder(gc, type);
    return pemu_add_bind(rh, gc) < 0 ? -1 : 0;
}
