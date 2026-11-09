/* linux-user/proot/link2symlink.inc.h - link2symlink (-l): hard links emulated with symlinks. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- link2symlink (-l): proot's extension that emulates hard links with symlinks, for file systems that refuse link(2).
 * link(old, new) turns `old` into  old -> dir/.l2s.NAME0001 -> dir/.l2s.NAME0001.0002  (the file itself; the last four digits
 * are the link count) and makes `new` another symlink to the intermediate one.  unlink/rename over a name lowers the count
 * (the last one removes the file), stat reports the count in st_nlink, and every path that walks through such a symlink
 * reaches the file.  The files are the same ones real proot makes, so a rootfs can go back and forth between the two. */
#define PEMU_L2S ".l2s."
static int l2s_named(const char *p) { const char *b = strrchr(p, '/'); b = b ? b + 1 : p; return !strncmp(b, PEMU_L2S, sizeof PEMU_L2S - 1); }
static int l2s_rl(const char *p, char *o) { ssize_t n = readlink(p, o, PATH_MAX - 1); if (n < 0) return -errno; o[n] = 0; return 0; }
/* the target `t` of the symlink `link`, as an absolute host path */
static int l2s_abs(const char *link, const char *t, char *o) {
    if (t[0] == '/') { if (strlen(t) >= PATH_MAX) return -ENAMETOOLONG; strcpy(o, t); return 0; }
    const char *sl = strrchr(link, '/'); size_t d = sl ? (size_t) (sl - link) + 1 : 0;
    if (d + strlen(t) >= PATH_MAX) return -ENAMETOOLONG;
    memcpy(o, link, d); strcpy(o + d, t); return 0;
}
static int l2s_count(const char *fin) {                     /* the link count in the name of the file, -1 when it has none */
    size_t n = strlen(fin);
    if (n < 4) return -1;
    for (int i = 1; i <= 4; i++) if (fin[n - i] < '0' || fin[n - i] > '9') return -1;
    return atoi(fin + n - 4);
}
/* Called by pemu_canon for a symlink it is about to follow: when it leads to a .l2s. name, follow the chain to the file
 * (which lives in the same directory, so a relative name is enough).  *l = length of tgt. */
static void l2s_hop(const char *hostlink, char *tgt, ssize_t *l) {
    if (!pemu_l2s || !l2s_named(tgt)) return;
    char inter[PATH_MAX], t2[PATH_MAX];
    const char *src = tgt;
    if (l2s_abs(hostlink, tgt, inter) == 0 && l2s_rl(inter, t2) == 0 && l2s_named(t2)) src = t2;
    const char *b = strrchr(src, '/'); b = b ? b + 1 : src;
    size_t n = strlen(b); memmove(tgt, b, n + 1); *l = (ssize_t) n;
}
/* `hp` (not followed) is a fake hard link, its intermediate symlink or the file itself: 1 and the names, else 0 */
static int l2s_resolve(const char *hp, char *inter, char *fin, int *nl) {
    struct stat st; char t[PATH_MAX]; inter[0] = 0;
    if (lstat(hp, &st) < 0) return 0;
    if (l2s_named(hp)) {
        if (S_ISLNK(st.st_mode)) strcpy(inter, hp);
        else { strcpy(fin, hp); goto count; }
    } else {
        if (!S_ISLNK(st.st_mode)) return 0;
        if (l2s_rl(hp, t) < 0 || !l2s_named(t) || l2s_abs(hp, t, inter) < 0) return 0;
    }
    if (l2s_rl(inter, t) < 0 || l2s_abs(inter, t, fin) < 0) return 0;
count:
    *nl = l2s_count(fin);
    return *nl >= 0;
}
/* move `o` (host path) away to .l2s.NAMEnnnn.cccc and leave symlinks behind; *inter = the intermediate symlink.
 * A name that is already a fake link just gets its count raised. */
static int l2s_move(const char *o, char *inter) {
    struct stat st; char t[PATH_MAX], fin[PATH_MAX], nf[PATH_MAX]; int first = 1;
    if (lstat(o, &st) < 0) return -errno;
    if (S_ISDIR(st.st_mode)) return -EPERM;                  /* directories cannot be linked */
    if (S_ISLNK(st.st_mode) && l2s_rl(o, t) == 0 && l2s_named(t)) {
        if (l2s_abs(o, t, inter) < 0) return -ENAMETOOLONG;
        first = 0;
    }
    if (first) {
        const char *nm = strrchr(o, '/'); nm = nm ? nm + 1 : o; size_t dl = (size_t) (nm - o);
        if (dl + sizeof PEMU_L2S + strlen(nm) + 10 >= PATH_MAX) return -ENAMETOOLONG;
        char base[PATH_MAX]; memcpy(base, o, dl); strcpy(base + dl, PEMU_L2S); strcat(base, nm);
        for (int i = 1; ; i++) {                              /* a free intermediate name */
            { size_t bl = strlen(base); memcpy(inter, base, bl); snprintf(inter + bl, 5, "%04u", (unsigned) i % 1000u); }
            if (lstat(inter, &st) < 0 || i >= 999) break;
        }
        if (strlen(inter) + 6 >= sizeof fin) return -ENAMETOOLONG;
        strcpy(fin, inter); strcat(fin, ".0002");
        if (rename(o, fin) < 0) return -errno;
        if (symlink(fin, inter) < 0) return -errno;
        if (symlink(inter, o) < 0) return -errno;
        return 0;
    }
    if (l2s_rl(inter, t) < 0 || l2s_abs(inter, t, fin) < 0) return -EIO;
    int c = l2s_count(fin); if (c < 0) return -EIO;
    size_t n = strlen(fin); memcpy(nf, fin, n - 4); snprintf(nf + n - 4, 5, "%04u", (unsigned) (c + 1) % 10000u);
    if (rename(fin, nf) < 0) return -errno;
    if (unlink(inter) < 0 || symlink(nf, inter) < 0) return -errno;
    return 0;
}
/* `hp` is about to be unlinked (or replaced by rename): when it is a fake hard link lower the count, the last one removes
 * the intermediate symlink and the file */
static void l2s_dec(const char *hp) {
    char inter[PATH_MAX], fin[PATH_MAX], nf[PATH_MAX], t[PATH_MAX]; struct stat st;
    if (lstat(hp, &st) < 0 || !S_ISLNK(st.st_mode)) return;
    if (l2s_rl(hp, t) < 0 || !l2s_named(t) || l2s_abs(hp, t, inter) < 0) return;
    if (l2s_rl(inter, t) < 0 || l2s_abs(inter, t, fin) < 0) return;
    int c = l2s_count(fin) - 1; size_t n = strlen(fin);
    if (c < -1) return;
    if (c > 0) {
        memcpy(nf, fin, n - 4); snprintf(nf + n - 4, 5, "%04u", (unsigned) c % 10000u);
        if (rename(fin, nf) == 0) { unlink(inter); if (symlink(nf, inter) < 0) (void) 0; }
    } else { unlink(inter); unlink(fin); }
}
/* fstat on a descriptor that refers to the file of a fake hard link: its name tells the count */
static void l2s_fix_fd(int fd, struct stat *st) {
    if (!pemu_l2s || !S_ISREG(st->st_mode)) return;
    char l[64], p[PATH_MAX]; snprintf(l, sizeof l, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(l, p, sizeof p - 1); if (n <= 0) return;
    p[n] = 0;
    static const char del[] = " (deleted)"; size_t dl = sizeof del - 1;
    if ((size_t) n > dl && !strcmp(p + n - dl, del)) p[n - dl] = 0;
    if (!l2s_named(p)) return;
    int c = l2s_count(p); if (c >= 0) st->st_nlink = (nlink_t) c;
}
