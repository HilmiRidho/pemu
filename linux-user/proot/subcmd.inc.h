/* linux-user/proot/subcmd.inc.h - sub-commands: login / install / list. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- sub-commands: login / install / list */
static const char *pemu_dirs(int i, char *buf, size_t sz) {
    const char *e = getenv("PEMU_ROOTFS_DIR"), *h = getenv("HOME"), *pf = getenv("PREFIX");
    switch (i) {
    case 0: return e && *e ? e : NULL;
    case 1: if (!h) return NULL; snprintf(buf, sz, "%s/.pemu/rootfs", h); return buf;
    case 2: if (!pf) return NULL; snprintf(buf, sz, "%s/var/lib/pemu/rootfs", pf); return buf;
    case 3: if (!pf) return NULL; snprintf(buf, sz, "%s/var/lib/proot-distro/installed-rootfs", pf); return buf;
    }
    return NULL;
}
static int pemu_isdir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int pemu_find_rootfs(const char *name, char *out, size_t sz) {
    if (strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) { snprintf(out, sz, "%s", name); return pemu_isdir(out) ? 0 : -1; }
    char b[PATH_MAX];
    for (int i = 0; i < 4; i++) {
        const char *d = pemu_dirs(i, b, sizeof b); if (!d) continue;
        snprintf(out, sz, "%s/%s", d, name); if (pemu_isdir(out)) return 0;
    }
    return pemu_isdir(name) ? (snprintf(out, sz, "%s", name), 0) : -1;
}
static void pemu_usage(void) {
    fputs("pemu - user-mode emulator with a built-in proot\n\n"
          "  pemu login [options] NAME|DIR [command [args...]]   enter a rootfs (default: bash -l, fake root)\n"
          "      -b, --bind SRC[:DST]   bind a host path into the guest (repeatable)\n"
          "      -w, --cwd DIR          start directory (default /root)\n"
          "      -e, --env VAR=val      set a guest environment variable\n"
          "          --no-root-id       do not pretend to be root\n"
          "          --no-default-binds do not bind /dev /proc /sys\n"
          "  pemu install NAME TARBALL [--strip N]                unpack a rootfs tarball (tar must be installed)\n"
          "  pemu list                                            list installed rootfs\n"
          "  pemu [-r DIR] [-b SRC[:DST]] [-w DIR] [--root-id] [qemu-style options] program [args...]\n\n"
          "NAME is looked up in $PEMU_ROOTFS_DIR, ~/.pemu/rootfs, $PREFIX/var/lib/pemu/rootfs and\n"
          "$PREFIX/var/lib/proot-distro/installed-rootfs.\n", stderr);
}
/* 1 when resolv.conf holds at least one nameserver that is not loopback (nothing listens on 127.x inside a guest) */
static int pemu_resolv_usable(const char *path) {
    char b[4096]; FILE *f = fopen(path, "r"); if (!f) return 0;
    size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    char *sp = NULL;
    for (char *l = strtok_r(b, "\n", &sp); l; l = strtok_r(NULL, "\n", &sp)) {
        while (*l == ' ' || *l == '\t') l++;
        if (strncmp(l, "nameserver", 10) || (l[10] != ' ' && l[10] != '\t')) continue;
        l += 10; while (*l == ' ' || *l == '\t') l++;
        if (!*l || !strncmp(l, "127.", 4) || !strncmp(l, "::1", 3) || !strncmp(l, "0.0.0.0", 7)) continue;
        return 1;
    }
    return 0;
}
static void pemu_write_if_missing(const char *root, const char *rel, const char *text, int replace_empty) {
    char p[PEMU_HP]; snprintf(p, sizeof p, "%s%s", root, rel);
    struct stat st;
    if (lstat(p, &st) == 0) {
        if (S_ISLNK(st.st_mode)) unlink(p);
        else if (!(replace_empty && S_ISREG(st.st_mode) && st.st_size == 0)) return;
    }
    FILE *f = fopen(p, "w"); if (f) { fputs(text, f); fclose(f); }
}
/* "pemu login ..." -> rewrite argv into the plain form; returns -1 = continue with the new argc/argv, else the exit code */
static int pemu_login(int *pargc, char ***pargv) {
    int argc = *pargc; char **argv = *pargv;
    int i = 2, rootid = 1, nodef = 0; const char *env[64]; int ne = 0, nb = 0; const char *bind[PEMU_MAXB]; const char *cwd = NULL;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *o = argv[i];
        if (!strcmp(o, "--")) { i++; break; }
        if ((!strcmp(o, "-b") || !strcmp(o, "--bind")) && i + 1 < argc && nb < PEMU_MAXB) bind[nb++] = argv[++i];
        else if ((!strcmp(o, "-w") || !strcmp(o, "--cwd")) && i + 1 < argc) cwd = argv[++i];
        else if ((!strcmp(o, "-e") || !strcmp(o, "--env")) && i + 1 < argc && ne < 64) env[ne++] = argv[++i];
        else if (!strcmp(o, "--no-root-id")) rootid = 0;
        else if (!strcmp(o, "--no-default-binds")) nodef = 1;
        else if (!strcmp(o, "-h") || !strcmp(o, "--help")) { pemu_usage(); return 0; }
        else { fprintf(stderr, "pemu login: unknown option %s\n", o); return 2; }
    }
    if (i >= argc) { pemu_usage(); return 2; }
    char root[PATH_MAX];
    if (pemu_find_rootfs(argv[i], root, sizeof root) < 0) { fprintf(stderr, "pemu: rootfs '%s' not found (see: pemu list)\n", argv[i]); return 1; }
    i++;
    char rr[PATH_MAX]; if (!realpath(root, rr)) { fprintf(stderr, "pemu: %s: %s\n", root, strerror(errno)); return 1; }
    /* rootfs preparation: resolver, hosts, apt without the _apt sandbox user (setuid is faked) */
    {
        char t[PEMU_HP];
        static const char *sub[] = { "/etc", "/tmp", "/root" };
        for (int k = 0; k < 3; k++) { snprintf(t, sizeof t, "%s%s", rr, sub[k]); mkdir(t, k == 1 ? 01777 : 0755); }
        snprintf(t, sizeof t, "%s/etc/apt/apt.conf.d", rr);
        { char rc[PEMU_HP]; struct stat rs; snprintf(rc, sizeof rc, "%s/etc/resolv.conf", rr);
          if (lstat(rc, &rs) == 0 && S_ISREG(rs.st_mode) && !pemu_resolv_usable(rc) && truncate(rc, 0) < 0) { }  }   /* comments only / loopback only: replace below */
        pemu_write_if_missing(rr, "/etc/resolv.conf", "nameserver 8.8.8.8\nnameserver 8.8.4.4\n", 1);
        pemu_write_if_missing(rr, "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n", 1);
        if (pemu_isdir(t)) pemu_write_if_missing(rr, "/etc/apt/apt.conf.d/99pemu-nosandbox", "APT::Sandbox::User \"root\";\n", 0);
    }
    /* guest environment */
    const char *term = getenv("TERM"), *ct = getenv("COLORTERM"), *lang = getenv("LANG");
    char termv[128], ctv[64], langv[64];
    snprintf(termv, sizeof termv, "%s", term && *term ? term : "xterm-256color"); snprintf(ctv, sizeof ctv, "%s", ct ? ct : ""); snprintf(langv, sizeof langv, "%s", lang && *lang ? lang : "C.UTF-8");
    char qs[1024] = ""; { const char *v = getenv("QEMU_SET_ENV"); if (v) snprintf(qs, sizeof qs, "%s", v); }
    clearenv();
    setenv("HOME", rootid ? "/root" : "/tmp", 1); setenv("USER", rootid ? "root" : "user", 1); setenv("LOGNAME", rootid ? "root" : "user", 1);
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games", 1);
    setenv("TERM", termv, 1); setenv("LANG", langv, 1); setenv("TMPDIR", "/tmp", 1);
    if (*ctv) setenv("COLORTERM", ctv, 1);
    if (*qs) setenv("QEMU_SET_ENV", qs, 1);
    for (int k = 0; k < ne; k++) { char *v = strdup(env[k]), *eq = strchr(v, '='); if (eq) { *eq = 0; setenv(v, eq + 1, 1); } }
    /* command */
    int cc = argc - i; const char *shell = NULL; char hp[PEMU_HP];
    if (cc <= 0) {
        static const char *sh[] = { "/bin/bash", "/usr/bin/bash", "/bin/sh" };
        for (int k = 0; k < 3; k++) { snprintf(hp, sizeof hp, "%s%s", rr, sh[k]); if (access(hp, F_OK) == 0 || k == 2) { shell = sh[k]; break; } }
        setenv("SHELL", shell, 1);
    }
    char **nv = calloc((size_t) (argc + 4 * nb + 16), sizeof *nv); int k = 0;
    nv[k++] = argv[0]; nv[k++] = "-r"; nv[k++] = strdup(rr);
    for (int j = 0; j < nb; j++) { nv[k++] = "-b"; nv[k++] = (char*) bind[j]; }
    char rd[PEMU_HP]; snprintf(rd, sizeof rd, "%s/root", rr);
    nv[k++] = "-w"; nv[k++] = (char*) (cwd ? cwd : (rootid && pemu_isdir(rd) ? "/root" : "/"));
    if (rootid) nv[k++] = "--root-id";
    if (nodef) nv[k++] = "--no-default-binds";
    nv[k++] = "--";
    if (cc <= 0) { nv[k++] = (char*) shell; nv[k++] = "-l"; } else for (int j = i; j < argc; j++) nv[k++] = argv[j];
    nv[k] = NULL; *pargv = nv; *pargc = k;
    return -1;
}
static int pemu_list(void) {
    char b[PATH_MAX]; int found = 0;
    for (int i = 0; i < 4; i++) {
        const char *d = pemu_dirs(i, b, sizeof b); if (!d) continue;
        DIR *dp = opendir(d); if (!dp) continue;
        struct dirent *e;
        while ((e = readdir(dp))) if (e->d_name[0] != '.') { printf("%-24s %s/%s\n", e->d_name, d, e->d_name); found = 1; }
        closedir(dp);
    }
    if (!found) fputs("no rootfs installed (pemu install NAME TARBALL)\n", stderr);
    return 0;
}
/* fork+exec without a shell. rfd != NULL: child's stdout is returned through *rfd, otherwise it goes to /dev/null. stderr is silenced. */
static int pemu_spawn(char *const av[], int *rfd) {
    int p[2] = { -1, -1 };
    if (rfd && pipe(p) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { if (rfd) { close(p[0]); close(p[1]); } return -1; }
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        if (rfd) { dup2(p[1], 1); close(p[0]); close(p[1]); } else if (nul >= 0) dup2(nul, 1);
        if (nul >= 0) { dup2(nul, 2); dup2(nul, 0); if (nul > 2) close(nul); }
        execvp(av[0], av); _exit(127);
    }
    if (rfd) { close(p[1]); *rfd = p[0]; }
    return pid;
}
static int pemu_install(int argc, char **argv) {
    const char *name = NULL, *tb = NULL; int strip = -1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--strip") && i + 1 < argc) strip = atoi(argv[++i]);
        else if (!name) name = argv[i]; else if (!tb) tb = argv[i];
    }
    if (!name || !tb || strchr(name, '/')) { pemu_usage(); return 2; }
    char b[PATH_MAX], dest[PATH_MAX]; const char *d = pemu_dirs(0, b, sizeof b);
    if (!d) d = pemu_dirs(1, b, sizeof b);
    if (!d) { fputs("pemu: set HOME or PEMU_ROOTFS_DIR\n", stderr); return 1; }
    snprintf(dest, sizeof dest, "%s/%s", d, name);
    if (pemu_isdir(dest)) { fprintf(stderr, "pemu: %s already exists\n", dest); return 1; }
    if (strip < 0) {                                  /* a single top-level directory that is not a Linux dir is stripped */
        char first[PATH_MAX] = ""; char *lv[] = { "tar", "-tf", (char*) tb, NULL };
        int rfd = -1, pid = pemu_spawn(lv, &rfd);
        if (pid > 0) {
            FILE *f = fdopen(rfd, "r");
            if (f) { if (!fgets(first, sizeof first, f)) first[0] = 0; fclose(f); }
            waitpid(pid, NULL, 0);
        }
        first[strcspn(first, "\n")] = 0;
        const char *s = first; while (!strncmp(s, "./", 2)) s += 2;
        char top[256]; size_t n = strcspn(s, "/"); if (n >= sizeof top) n = sizeof top - 1; memcpy(top, s, n); top[n] = 0;
        static const char *std[] = { "bin", "boot", "dev", "etc", "home", "lib", "lib32", "lib64", "media", "mnt", "opt", "proc", "root", "run", "sbin", "srv", "sys", "tmp", "usr", "var", "." };
        strip = 1; if (!*top) strip = 0;
        for (unsigned j = 0; j < sizeof std / sizeof *std; j++) if (!strcmp(top, std[j])) strip = 0;
    }
    { char t[PATH_MAX]; snprintf(t, sizeof t, "%s", dest);   /* mkdir -p */
      for (char *q = t + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(t, 0755); *q = '/'; }
      if (mkdir(t, 0755) != 0 && errno != EEXIST) { fprintf(stderr, "pemu: %s: %s\n", t, strerror(errno)); return 1; } }
    {   /* no shell: tarball and destination paths are passed as plain arguments */
        char sc[32]; snprintf(sc, sizeof sc, "--strip-components=%d", strip);
        char *av[] = { "tar", "-xpf", (char*) tb, "-C", dest, sc, "--no-same-owner", "--exclude=./dev/*", "--exclude=dev/*", "--exclude=*/dev/*", NULL };
        fprintf(stderr, "pemu: unpacking %s into %s (strip %d)...\n", tb, dest, strip);
        int pid = pemu_spawn(av, NULL);
        if (pid > 0) waitpid(pid, NULL, 0);             /* tar exits 2 for harmless errors (device nodes, ownership) */
    }
    chmod(dest, 0755);
    char chk[PATH_MAX + 16]; int rc = 1;
    snprintf(chk, sizeof chk, "%s/usr", dest); if (pemu_isdir(chk)) rc = 0;
    snprintf(chk, sizeof chk, "%s/bin", dest); if (pemu_isdir(chk)) rc = 0;
    if (rc) fprintf(stderr, "pemu: nothing usable was unpacked into %s (is tar installed? right --strip?)\n", dest);
    else fprintf(stderr, "pemu: done. enter with: pemu login %s\n", name);
    return rc ? 1 : 0;
}
/* entry for main(): handles the sub-commands. -1 = go on with the new argc/argv */
static int pemu_subcmd(int *pargc, char ***pargv) {
    if (*pargc < 2) return -1;
    const char *c = (*pargv)[1];
    if (!strcmp(c, "login"))   return pemu_login(pargc, pargv);
    if (!strcmp(c, "install")) return pemu_install(*pargc, *pargv);
    if (!strcmp(c, "list"))    return pemu_list();
    if (!strcmp(c, "--help") || !strcmp(c, "-h")) { pemu_usage(); return 0; }
    return -1;
}
