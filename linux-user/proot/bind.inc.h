/* linux-user/proot/bind.inc.h - path normalisation and the bind table. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* lexical normalisation of an absolute path, in place */
static void pemu_norm(char *p) {
    static __thread char out[PATH_MAX]; size_t o = 0; out[0] = 0;
    for (char *s = p; *s;) {
        while (*s == '/') s++;
        if (!*s) break;
        char *e = s; while (*e && *e != '/') e++;
        size_t n = (size_t)(e - s);
        if (n == 1 && s[0] == '.') { }
        else if (n == 2 && s[0] == '.' && s[1] == '.') { while (o > 0 && out[o - 1] != '/') o--; if (o) o--; out[o] = 0; }
        else { if (o + n + 2 >= PATH_MAX) break; out[o++] = '/'; memcpy(out + o, s, n); o += n; out[o] = 0; }
        s = e;
    }
    if (!o) { out[0] = '/'; out[1] = 0; }
    strcpy(p, out);
}

static int pemu_add_bind(const char *h, const char *g) {
    char rh[PATH_MAX], gg[PATH_MAX];
    if (pemu_nb >= PEMU_MAXB || g[0] != '/' || !realpath(h, rh)) return -1;
    snprintf(gg, sizeof gg, "%s", g); pemu_norm(gg);
    struct pemu_bind *b = &pemu_b[pemu_nb];
    b->g = strdup(gg); b->h = strdup(rh); b->gl = strlen(gg); b->hl = strlen(rh); b->off = 0;
    b->raw = !strcmp(rh, "/proc") || !strcmp(rh, "/sys") || !strcmp(rh, "/dev");
    __sync_synchronize(); pemu_nb++;                 /* publish last: other threads may be walking the table */
    return 0;
}

/* guest path (canonical) -> host path. The longest matching bind wins; later binds win ties. */
static int pemu_g2h(const char *g, char *out, int *raw) {
    size_t n = strlen(g); int best = 0; size_t bl = 0;
    for (int i = 0; i < pemu_nb; i++) {
        struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
        if (b->gl == 1 || (n >= b->gl && !memcmp(g, b->g, b->gl) && (g[b->gl] == '/' || !g[b->gl])))
            if (b->gl >= bl) { best = i; bl = b->gl; }
    }
    struct pemu_bind *b = &pemu_b[best];
    const char *suf = g + (b->gl == 1 ? 0 : b->gl);
    if (b->hl == 1) snprintf(out, PEMU_HP, "%s", *suf ? suf : "/");
    else { if (strlen(suf) + b->hl >= PEMU_HP) return -ENAMETOOLONG; snprintf(out, PEMU_HP, "%s%s", b->h, suf); }
    if (raw) *raw = b->raw;
    return 0;
}
/* host path -> guest path (for /proc/self/fd/N and the like); -1 when it is outside every bind */
static int pemu_h2g(const char *h, char *out) {
    size_t n = strlen(h); int best = -1; size_t bl = 0;
    for (int i = 0; i < pemu_nb; i++) {
        struct pemu_bind *b = &pemu_b[i]; if (b->off) continue;
        if (b->hl == 1 || (n >= b->hl && !memcmp(h, b->h, b->hl) && (h[b->hl] == '/' || !h[b->hl])))
            if (best < 0 || b->hl > bl) { best = i; bl = b->hl; }
    }
    if (best < 0) return -1;
    struct pemu_bind *b = &pemu_b[best];
    const char *suf = h + (b->hl == 1 ? 0 : b->hl);
    snprintf(out, PATH_MAX, "%s%s", b->gl == 1 ? "" : b->g, suf);
    if (!*out) strcpy(out, "/");
    return 0;
}
static int pemu_fd2g(int fd, char *g) {
    char l[64], h[PATH_MAX];
    snprintf(l, sizeof l, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(l, h, sizeof h - 1);
    if (n <= 0) return -1;
    h[n] = 0;
    return h[0] == '/' ? pemu_h2g(h, g) : -1;
}
