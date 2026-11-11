/* linux-user/proot/exec.inc.h - execve: #! script expansion and re-exec options. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- exec: expand #! scripts against the guest file system.
 * gpath: path given by the guest. On success *argvp is the (possibly rewritten) argv, host = file to
 * load, gprog = canonical guest path of that file. */
static __thread int pemu_exec_fdnum;     /* set by pemu_exec_resolve: the exec goes through /proc/<pid>/fd/N -> N, else -1 */
static __thread int pemu_exec_unnamed;   /* ... and that file has no name in the guest (memfd, deleted, outside every bind): keep the fd */
/* "/proc/self/fd/N" or "/proc/<this pid>/fd/N" -> N, else -1 */
static int pemu_exec_fdpath(const char *p) {
    char pre[64]; const char *q = NULL; char *e; long v;
    snprintf(pre, sizeof pre, "/proc/%d/fd/", (int) getpid());
    if (!strncmp(p, "/proc/self/fd/", 14)) q = p + 14; else if (!strncmp(p, pre, strlen(pre))) q = p + strlen(pre);
    if (!q || !*q) return -1;
    v = strtol(q, &e, 10);
    return (*e || v < 0 || v > (1 << 20)) ? -1 : (int) v;
}
static long pemu_exec_resolve(const char *gpath, char **argv, char ***argvp, char *host, char *gprog) {
    pemu_exec_fdnum = -1; pemu_exec_unnamed = 0;
    int n = 0; while (argv && argv[n]) n++;
    char **av = argv; char cur[PATH_MAX]; snprintf(cur, sizeof cur, "%s", gpath);
    for (int depth = 0; depth < 6; depth++) {
        int viafd = 0;                                   /* the name is rebuilt from an fd: the guest never gave a usable one */
        char gc[PATH_MAX]; long r = pemu_path(AT_FDCWD, cur, 1, host, PEMU_HP, gc);
        if (r < 0) return r;
        if (r == 1) { snprintf(gc, sizeof gc, "%s", cur); snprintf(host, PEMU_HP, "%s", cur); }
        { char pre[64]; snprintf(pre, sizeof pre, "/proc/%d/fd/", (int) getpid());
          if (!strncmp(gc, pre, strlen(pre))) {          /* execveat(fd): the new process cannot see our fd, name the file */
              viafd = 1; char t[PATH_MAX], g2[PATH_MAX]; ssize_t l = readlink(host, t, sizeof t - 1);
              pemu_exec_fdnum = pemu_exec_fdpath(gc); pemu_exec_unnamed = 1;              /* until the file is named */
              if (l > 0) {
                  t[l] = 0;
                  int gone = l > 10 && !strcmp(t + l - 10, " (deleted)");
                  if (t[0] == '/' && !gone && pemu_h2g(t, g2) == 0) { strcpy(gc, g2); pemu_exec_unnamed = 0; }
                  else snprintf(gc, sizeof gc, "%s", t);          /* memfd, deleted file: the name /proc/self/exe shows, as the kernel does */
              } } }
        struct stat st;
        if (stat(host, &st) < 0) return -errno;
        if (S_ISDIR(st.st_mode)) return -EACCES;
        if (!S_ISREG(st.st_mode) || access(host, X_OK) < 0) return -EACCES;
        int fd = open(host, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return -errno;
        char h[260]; ssize_t k = read(fd, h, sizeof h - 1); close(fd);
        if (k < 2 || h[0] != '#' || h[1] != '!') { snprintf(gprog, PATH_MAX, "%s", gc); *argvp = av; return 0; }
        if (viafd && pemu_exec_unnamed) return -ENOENT;     /* a #! script with no name: the interpreter cannot open it (the kernel too, for a close-on-exec fd) */
        h[k] = 0; char *nl = strchr(h, '\n'); if (nl) *nl = 0;
        char *s = h + 2; while (*s == ' ' || *s == '\t') s++;
        char *ip = s; while (*s && *s != ' ' && *s != '\t') s++;
        char *arg = NULL; if (*s) { *s++ = 0; while (*s == ' ' || *s == '\t') s++; if (*s) { arg = s; char *t = s + strlen(s); while (t > s && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\r')) *--t = 0; } }
        if (!*ip) return -ENOEXEC;
        char **nv = calloc((size_t) (n + 4), sizeof *nv); int j = 0;
        nv[j++] = strdup(ip); if (arg) nv[j++] = strdup(arg);
        /* the kernel hands the interpreter the script path as it was given to execve, symlinks unresolved: programs that look at
         * their own name (adduser/addgroup, ...) depend on it.  Only a path that came from an fd has to use the canonical one. */
        nv[j++] = strdup(viafd ? gc : cur);
        for (int i = 1; i < n; i++) nv[j++] = av[i];
        av = nv; n = j; snprintf(cur, sizeof cur, "%s", ip);
    }
    return -ELOOP;
}
/* The first command of `pemu login NAME cmd`: execve takes the name as given, but a bare name is meant to be found in PATH
 * (`pemu login debian apt update`).  The name as given is tried first, so nothing that worked before changes; only an ENOENT
 * falls back to the directories of PATH (the guest's, set by login), and an unreadable match is reported as EACCES like a shell. */
static long pemu_exec_resolve_cmd(const char *prog, char **argv, char ***argvp, char *host, char *gprog) {
    long r = pemu_exec_resolve(prog, argv, argvp, host, gprog);
    const char *p = getenv("PATH");
    if (r != -ENOENT || !*prog || strchr(prog, '/') || !p || !*p) return r;
    long best = r; char *dup = strdup(p), *sp, *d;
    if (!dup) return r;
    for (d = strtok_r(dup, ":", &sp); d; d = strtok_r(NULL, ":", &sp)) {
        char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", d, prog);
        long e = pemu_exec_resolve(cand, argv, argvp, host, gprog);
        if (e == 0) { free(dup); return 0; }
        if (e == -EACCES) best = e;
    }
    free(dup); return best;
}
/* options that make a re-exec'd pemu rebuild the same state (table, cwd, identity, -k, placeholders); returns the new k */
static int pemu_reexec_opts(char **nv, int k) {
    char *rt = malloc(PATH_MAX); pemu_root_get(rt);       /* a copy: chroot may rewrite pemu_root while this argv is being built */
    nv[k++] = "--reexec"; nv[k++] = "-r"; nv[k++] = rt; nv[k++] = "--no-default-binds";
    nv[k++] = "--secure-open"; nv[k++] = pemu_secure() ? "1" : "0";      /* the decision, not the environment it was taken from */
    pemu_lock();                                          /* mount / umount / chroot edit the table under this lock */
    int ord[PEMU_MAXB], cnt = 0;                          /* oldest first: the child re-adds them in this order, so ties resolve as here */
    for (int i = 1; i < pemu_nb; i++) {
        if (pemu_b[i].off) continue;
        int j = cnt++; while (j > 0 && pemu_b[ord[j - 1]].ord > pemu_b[i].ord) { ord[j] = ord[j - 1]; j--; }
        ord[j] = i;
    }
    for (int q = 0; q < cnt; q++) {
        const struct pemu_bind *b = &pemu_b[ord[q]];
        char *s = malloc(strlen(b->h) + strlen(b->g) + 3);
        sprintf(s, "%s:%s!", b->h, b->g); nv[k++] = "-b"; nv[k++] = s;      /* '!': the guest path is already canonical */
    }
    nv[k++] = "-w"; nv[k++] = strdup(pemu_cwd);
    pemu_unlock();
    if (pemu_fakeroot) {
        char *s = malloc(160); snprintf(s, 160, "%u,%u,%u,%u,%u,%u,%u,%u", pemu_uid[0], pemu_uid[1], pemu_uid[2], pemu_uid[3], pemu_gid[0], pemu_gid[1], pemu_gid[2], pemu_gid[3]);
        nv[k++] = "--ids"; nv[k++] = s;
    }
    if (pemu_kset) { char *s = malloc(512); pemu_uts_string(s, 512); nv[k++] = "--uts"; nv[k++] = s; }
    if (pemu_l2s) nv[k++] = "--link2symlink";
    if (pemu_verbose) { char *s = malloc(16); snprintf(s, 16, "%d", pemu_verbose); nv[k++] = "-v"; nv[k++] = s; }
    if (pemu_wait && getpid() == pemu_top) { nv[k++] = "--top"; if (pemu_killall) nv[k++] = "--kill-on-exit"; }
    if (pemu_ph_owner == getpid()) for (int i = 0; i < pemu_nph; i++) { nv[k++] = "--ph"; nv[k++] = pemu_ph[i]; }
    return k;
}
static int pemu_reexec_count(void) { return 30 + 2 * pemu_nb + 2 * pemu_nph; }
