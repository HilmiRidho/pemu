/* linux-user/proot/kompat.inc.h - -k RELEASE: kernel release shown to the guest (uname). Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- -k RELEASE (proot's kompat extension, the uname part): the kernel release the guest sees, or the complex form
 * \sysname\nodename\release\version\machine\domainname\hwcap\ .  sethostname / setdomainname then change the faked values. */
static long pemu_hwcap = -1;
static struct utsname pemu_uts; static char pemu_kmach[65];
static int pemu_opt_kernel(const char *s, int internal) {
    struct utsname u; memset(&u, 0, sizeof u);
    if (s[0] == '\\') {
        const char *start, *end = s; char *e2;
        char *f[6] = { u.sysname, u.nodename, u.release, u.version, u.machine, u.domainname };
        static const char *nm[6] = { "sysname", "nodename", "release", "version", "machine", "domainname" };
        for (int i = 0; i < 6; i++) {
            start = end + 1; end = strchr(start, '\\');
            if (!end) { pemu_note(PEMU_ERROR, "can't find %s field in '%s'", nm[i], s); return -1; }
            size_t n = (size_t) (end - start); if (n > 64) n = 64;
            memcpy(f[i], start, n); f[i][n] = 0;
        }
        errno = 0; long hw = strtol(end + 1, &e2, 16);
        if (errno || e2[0] != '\\') { pemu_note(PEMU_ERROR, "can't find hwcap field in '%s'", s); return -1; }
        pemu_hwcap = hw;
        snprintf(pemu_kmach, sizeof pemu_kmach, "%s", u.machine);     /* empty: keep the guest architecture */
    } else {
        if (uname(&u) < 0) return -1;
        size_t n = strlen(s); if (n > 64) n = 64;
        memcpy(u.release, s, n); u.release[n] = 0;
        pemu_hwcap = -1; pemu_kmach[0] = 0;
    }
    (void) internal;
    pemu_uts = u; pemu_kset = 1;
    return 0;
}
/* fix up the host's struct utsname for the guest: architecture, and the -k values */
static void pemu_uname_fix(struct utsname *u, const char *machine) {
    if (pemu_kset) {
        memcpy(u->sysname, pemu_uts.sysname, sizeof u->sysname); memcpy(u->nodename, pemu_uts.nodename, sizeof u->nodename);
        memcpy(u->release, pemu_uts.release, sizeof u->release); memcpy(u->version, pemu_uts.version, sizeof u->version);
        memcpy(u->domainname, pemu_uts.domainname, sizeof u->domainname);
        if (pemu_kmach[0]) machine = pemu_kmach;
    }
    snprintf(u->machine, sizeof u->machine, "%s", machine);
}
/* sethostname / setdomainname(name, len).  1 = handled (*res set); 0 = no -k: the caller does the real call */
static int pemu_set_uts_name(int domain, const char *name, size_t len, long *res) {
    if (!pemu_kset) return 0;
    if (len > sizeof pemu_uts.domainname - 1) { *res = -EINVAL; return 1; }
    char *dst = domain ? pemu_uts.domainname : pemu_uts.nodename;
    memcpy(dst, name, len); dst[len] = 0;
    *res = 0; return 1;
}
/* "\sysname\nodename\release\version\machine\domainname\hwcap\" for a re-exec'd pemu */
static void pemu_uts_string(char *o, size_t sz) {
    snprintf(o, sz, "\\%s\\%s\\%s\\%s\\%s\\%s\\%lx\\", pemu_uts.sysname, pemu_uts.nodename, pemu_uts.release, pemu_uts.version, pemu_kmach, pemu_uts.domainname, pemu_hwcap < 0 ? 0L : pemu_hwcap);
}
