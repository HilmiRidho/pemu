/* linux-user/proot/subcmd.inc.h - sub-commands: login / install / uninstall / list. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- sub-commands: login / install / uninstall / list */
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
static void pemu_usage(FILE *f) {
    fputs("pemu - user-mode emulator with a built-in proot\n\n"
          "usage:\n"
          "  pemu login NAME|DIR [options] [command [args...]]\n"
          "        enter a rootfs (default command: bash -l, fake root, cwd /root)\n"
          "      -b, --bind SRC[:DST]    bind a host path into the guest (repeatable)\n"
          "      -w, --cwd DIR           start directory (default /root)\n"
          "      -e, --env VAR=val       set a guest environment variable (repeatable)\n"
          "      -i, --change-id U[:G]   fake uid/gid (instead of root)\n"
          "      -k, --kernel-release R  fake `uname -r`\n"
          "      -l, --link2symlink      emulate hard links with symlinks\n"
          "          --kill-on-exit      kill leftover guest processes when the main one exits\n"
          "          --no-root-id        do not pretend to be root (uid/gid stay the host's)\n"
          "          --no-default-binds  do not bind /dev /proc /sys\n"
          "      -v, --verbose N         log level\n"
          "      -strace  -nojit         trace guest syscalls / use the interpreter\n"
          "      -h, --help\n"
          "        options go after NAME; the first non-option argument starts the command\n"
          "        (use `--` before a command that begins with `-`). Value options also take --name=value.\n"
          "  pemu install NAME TARBALL [--strip N]\n"
          "        unpack a rootfs tarball into $PEMU_ROOTFS_DIR or ~/.pemu/rootfs (needs tar)\n"
          "        --strip N: leading path components to drop (default: automatic)\n"
          "  pemu uninstall NAME [-y]\n"
          "        delete an installed rootfs; asks for confirmation unless -y/--yes is given\n"
          "  pemu list\n"
          "        list installed rootfs\n"
          "  pemu [-strace] [-nojit] program [args...]\n"
          "        run a single program, no rootfs\n"
          "  pemu --version, -h/--help\n\n"
          "NAME is looked up in $PEMU_ROOTFS_DIR, ~/.pemu/rootfs, $PREFIX/var/lib/pemu/rootfs and\n"
          "$PREFIX/var/lib/proot-distro/installed-rootfs.\n\n"
          "environment:\n"
          "  PEMU_ROOTFS_DIR=DIR     extra rootfs directory (first choice for install)\n"
          "  PEMU_SECURE_OPEN=0|1    race-free openat2 path off/on (default on, off on Android)\n"
          "  PEMU_FAKE_PROC=1        always serve the fake /proc files\n"
          "  PEMU_JIT=0              use the interpreter instead of the JIT\n"
          "  PEMU_STRACE=1           trace guest syscalls (same as -strace)\n"
          "  PEMU_SET_ENV=A=1,B=2    extra guest environment variables\n", f);
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
/* "pemu login NAME [options] [command...]" -> rewrite argv into the internal form (also what a re-exec'd pemu receives);
 * returns -1 = continue with the new argc/argv, else the exit code */
static int pemu_login(int *pargc, char ***pargv) {
    int argc = *pargc; char **argv = *pargv;
    if (argc < 3) { pemu_usage(stderr); return 2; }
    if (!strcmp(argv[2], "-h") || !strcmp(argv[2], "--help")) { pemu_usage(stdout); return 0; }
    if (argv[2][0] == '-' && argv[2][1] && strcmp(argv[2], "--")) {
        fprintf(stderr, "pemu login: NAME must come first: pemu login NAME [options] [command...]\n"); return 2;
    }
    const char *name = argv[2];
    int i = 3, rootid = 1, chid = 0, idroot = 1, nodef = 0, ne = 0, nb = 0, nx = 0; const char *env[64], *bind[PEMU_MAXB], *cwd = NULL;
    const char *xo[96];                                   /* options handed to the internal parser as they are */
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *o = argv[i], *v = NULL; int m, i0 = i;
        if (!strcmp(o, "--")) { i++; break; }
        if (!strcmp(o, "-h") || !strcmp(o, "--help")) { pemu_usage(stdout); return 0; }
#define LOPT(s, l) ((m = pemu_opt_value(o, s, l, argc, argv, &i, &v)) != 0)
#define NEEDV() do { if (m < 0) { fprintf(stderr, "pemu login: option %s needs a value\n", o); return 2; } } while (0)
        if (LOPT("-b", "--bind")) { NEEDV(); if (nb >= PEMU_MAXB) { fputs("pemu login: too many binds\n", stderr); return 2; } bind[nb++] = v; }
        else if (LOPT("-w", "--cwd")) { NEEDV(); cwd = v; }
        else if (LOPT("-e", "--env")) { NEEDV(); if (ne >= 64) { fputs("pemu login: too many -e options\n", stderr); return 2; } env[ne++] = v; }
        else if (LOPT("-i", "--change-id") || LOPT("-k", "--kernel-release") || LOPT("-v", "--verbose")
                 ){
            NEEDV();
            if (nx + 2 > 96) { fputs("pemu login: too many options\n", stderr); return 2; }
            if (i != i0) { xo[nx++] = argv[i0]; xo[nx++] = argv[i]; } else xo[nx++] = o;   /* "-x value" or "--name=value" */
            if (!strcmp(o, "-i") || !strncmp(o, "--change-id", 11)) { chid = 1; if (strtol(v, NULL, 10) != 0) idroot = 0; }
        }
        else if (!strcmp(o, "--no-root-id")) rootid = 0;
        else if (!strcmp(o, "--no-default-binds")) nodef = 1;
        else if (!strcmp(o, "-l") || !strcmp(o, "--link2symlink") || !strcmp(o, "--kill-on-exit")
                 || !strcmp(o, "-strace") || !strcmp(o, "-nojit")) { if (nx + 1 > 96) return 2; xo[nx++] = o; }
        else { fprintf(stderr, "pemu login: unknown option %s\n", o); return 2; }
#undef LOPT
#undef NEEDV
    }
    if (chid) rootid = idroot;                            /* -i replaces the default fake root (keeps HOME/USER consistent) */
    char root[PATH_MAX];
    if (pemu_find_rootfs(name, root, sizeof root) < 0) { fprintf(stderr, "pemu: rootfs '%s' not found (see: pemu list)\n", name); return 1; }
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
    char ps[1024] = ""; { const char *v = getenv("PEMU_SET_ENV"); if (v) snprintf(ps, sizeof ps, "%s", v); }
    static const char *keep[] = { "PEMU_SECURE_OPEN", "PEMU_FAKE_PROC", "PEMU_JIT", "PEMU_STRACE" };   /* pemu's own settings survive clearenv */
    char kv[sizeof keep / sizeof *keep][256]; int kh[sizeof keep / sizeof *keep];
    for (unsigned q = 0; q < sizeof keep / sizeof *keep; q++) { const char *v = getenv(keep[q]); kh[q] = v != NULL; if (v) snprintf(kv[q], sizeof kv[q], "%s", v); }
    clearenv();
    for (unsigned q = 0; q < sizeof keep / sizeof *keep; q++) if (kh[q]) setenv(keep[q], kv[q], 1);
    setenv("HOME", rootid ? "/root" : "/tmp", 1); setenv("USER", rootid ? "root" : "user", 1); setenv("LOGNAME", rootid ? "root" : "user", 1);
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games", 1);
    setenv("TERM", termv, 1); setenv("LANG", langv, 1); setenv("TMPDIR", "/tmp", 1);
    if (*ctv) setenv("COLORTERM", ctv, 1);
    if (*ps) setenv("PEMU_SET_ENV", ps, 1);
    for (int k = 0; k < ne; k++) { char *v = strdup(env[k]), *eq = strchr(v, '='); if (eq) { *eq = 0; setenv(v, eq + 1, 1); } }
    /* command */
    int cc = argc - i; const char *shell = NULL; char hp[PEMU_HP];
    if (cc <= 0) {
        static const char *sh[] = { "/bin/bash", "/usr/bin/bash", "/bin/sh" };
        for (int k = 0; k < 3; k++) { snprintf(hp, sizeof hp, "%s%s", rr, sh[k]); if (access(hp, F_OK) == 0 || k == 2) { shell = sh[k]; break; } }
        setenv("SHELL", shell, 1);
    }
    char **nv = calloc((size_t) (argc + 4 * nb + nx + 16), sizeof *nv); int k = 0;
    nv[k++] = argv[0]; nv[k++] = "-r"; nv[k++] = strdup(rr);
    for (int j = 0; j < nb; j++) { nv[k++] = "-b"; nv[k++] = (char*) bind[j]; }
    char rd[PEMU_HP]; snprintf(rd, sizeof rd, "%s/root", rr);
    nv[k++] = "-w"; nv[k++] = (char*) (cwd ? cwd : (rootid && pemu_isdir(rd) ? "/root" : "/"));
    if (rootid) nv[k++] = "--root-id";
    if (nodef) nv[k++] = "--no-default-binds";
    for (int j = 0; j < nx; j++) nv[k++] = (char*) xo[j];
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
    if (!name || !tb || strchr(name, '/')) { pemu_usage(stderr); return 2; }
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
/* ---- uninstall: delete a rootfs by name.  fd-based walk: symlinks are never followed (they are unlinked), directories that
 * belong to another device (a real mount inside the rootfs) are left alone, and read-only directories are made writable first. */
static int pemu_rm_children(int dfd, dev_t dev, int depth) {   /* consumes dfd */
    int bad = 0;
    DIR *dp = fdopendir(dfd); if (!dp) { close(dfd); return 1; }
    int fd = dirfd(dp);
    struct dirent *e;
    while ((e = readdir(dp))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct stat st;
        if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0) { bad = 1; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (st.st_dev != dev || depth >= 512) { fprintf(stderr, "pemu: skipping %s (%s)\n", e->d_name, st.st_dev != dev ? "other device, is something mounted there?" : "too deep"); bad = 1; continue; }
            int sub = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (sub < 0 && errno == EACCES && fchmodat(fd, e->d_name, 0700, 0) == 0) sub = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (sub < 0) { fprintf(stderr, "pemu: %s: %s\n", e->d_name, strerror(errno)); bad = 1; continue; }
            fchmod(sub, 0700);                              /* a 0555 directory from a tarball would block unlinkat */
            if (pemu_rm_children(sub, dev, depth + 1)) bad = 1;
            if (unlinkat(fd, e->d_name, AT_REMOVEDIR) < 0) { fprintf(stderr, "pemu: %s: %s\n", e->d_name, strerror(errno)); bad = 1; }
        } else if (unlinkat(fd, e->d_name, 0) < 0) { fprintf(stderr, "pemu: %s: %s\n", e->d_name, strerror(errno)); bad = 1; }
    }
    closedir(dp);
    return bad;
}
static int pemu_uninstall(int argc, char **argv) {
    const char *name = NULL; int yes = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-y") || !strcmp(argv[i], "--yes")) yes = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { pemu_usage(stdout); return 0; }
        else if (argv[i][0] == '-' && argv[i][1]) { fprintf(stderr, "pemu uninstall: unknown option %s\n", argv[i]); return 2; }
        else if (!name) name = argv[i];
        else { pemu_usage(stderr); return 2; }
    }
    if (!name) { pemu_usage(stderr); return 2; }
    if (!*name || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) {
        fputs("pemu uninstall: NAME must be a plain rootfs name, not a path (see: pemu list)\n", stderr); return 2;
    }
    char b[PATH_MAX], target[PATH_MAX]; struct stat st; int found = 0;
    for (int i = 0; i < 4 && !found; i++) {
        const char *d = pemu_dirs(i, b, sizeof b); if (!d) continue;
        char rd[PATH_MAX]; if (!realpath(d, rd) || !strcmp(rd, "/")) continue;     /* never operate directly under / */
        size_t rl = strlen(rd), nl = strlen(name);
        if (rl + 1 + nl >= sizeof target) continue;
        memcpy(target, rd, rl); target[rl] = '/'; memcpy(target + rl + 1, name, nl + 1);
        if (lstat(target, &st) == 0) found = 1;
    }
    if (!found) { fprintf(stderr, "pemu: rootfs '%s' not found (see: pemu list)\n", name); return 1; }
    if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) { fprintf(stderr, "pemu: %s is not a directory\n", target); return 1; }
    if (!yes) {
        if (!isatty(0)) { fputs("pemu: refusing to delete without confirmation (stdin is not a terminal); use -y\n", stderr); return 1; }
        fprintf(stderr, "Delete rootfs '%s' (%s%s) and everything in it? [y/N] ", name, target, S_ISLNK(st.st_mode) ? ", symlink: only the link is removed" : "");
        char ans[16] = ""; if (!fgets(ans, sizeof ans, stdin) || (ans[0] != 'y' && ans[0] != 'Y')) { fputs("pemu: cancelled\n", stderr); return 1; }
    }
    if (S_ISLNK(st.st_mode)) {
        if (unlink(target) < 0) { fprintf(stderr, "pemu: %s: %s\n", target, strerror(errno)); return 1; }
        fprintf(stderr, "pemu: removed link %s (its target was left in place)\n", target); return 0;
    }
    int fd = open(target, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == EACCES && chmod(target, 0700) == 0) fd = open(target, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr, "pemu: %s: %s\n", target, strerror(errno)); return 1; }
    struct stat ds; if (fstat(fd, &ds) < 0) { close(fd); return 1; }
    fchmod(fd, 0700);
    int bad = pemu_rm_children(fd, ds.st_dev, 0);
    if (rmdir(target) < 0) { fprintf(stderr, "pemu: %s: %s\n", target, strerror(errno)); bad = 1; }
    if (bad) fprintf(stderr, "pemu: %s was not removed completely\n", target);
    else fprintf(stderr, "pemu: removed %s\n", target);
    return bad ? 1 : 0;
}
/* entry for main(): handles the sub-commands. -1 = go on with the new argc/argv */
static int pemu_subcmd(int *pargc, char ***pargv) {
    if (*pargc < 2) return -1;
    const char *c = (*pargv)[1];
    if (!strcmp(c, "login"))   return pemu_login(pargc, pargv);
    if (!strcmp(c, "install")) return pemu_install(*pargc, *pargv);
    if (!strcmp(c, "uninstall")) return pemu_uninstall(*pargc, *pargv);
    if (!strcmp(c, "list"))    return pemu_list();
    if (!strcmp(c, "--help") || !strcmp(c, "-h")) { pemu_usage(stdout); return 0; }
    return -1;
}
