/* linux-user/proot/exec.inc.h - execve: #! script expansion and re-exec options. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- exec: expand #! scripts against the guest file system.
 * gpath: path given by the guest. On success *argvp is the (possibly rewritten) argv, host = file to
 * load, gprog = canonical guest path of that file. */
static long pemu_exec_resolve(const char *gpath, char **argv, char ***argvp, char *host, char *gprog) {
    int n = 0; while (argv && argv[n]) n++;
    char **av = argv; char cur[PATH_MAX]; snprintf(cur, sizeof cur, "%s", gpath);
    for (int depth = 0; depth < 6; depth++) {
        char gc[PATH_MAX]; long r = pemu_path(AT_FDCWD, cur, 1, host, PEMU_HP, gc);
        if (r < 0) return r;
        if (r == 1) { snprintf(gc, sizeof gc, "%s", cur); snprintf(host, PEMU_HP, "%s", cur); }
        { char pre[64]; snprintf(pre, sizeof pre, "/proc/%d/fd/", (int) getpid());
          if (!strncmp(gc, pre, strlen(pre))) {          /* execveat(fd): the new process cannot see our fd, name the file */
              char t[PATH_MAX], g2[PATH_MAX]; ssize_t l = readlink(host, t, sizeof t - 1);
              if (l > 0) { t[l] = 0; if (pemu_h2g(t, g2) == 0) strcpy(gc, g2); } } }
        struct stat st;
        if (stat(host, &st) < 0) return -errno;
        if (S_ISDIR(st.st_mode)) return -EACCES;
        if (!S_ISREG(st.st_mode) || access(host, X_OK) < 0) return -EACCES;
        int fd = open(host, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return -errno;
        char h[260]; ssize_t k = read(fd, h, sizeof h - 1); close(fd);
        if (k < 2 || h[0] != '#' || h[1] != '!') { snprintf(gprog, PATH_MAX, "%s", gc); *argvp = av; return 0; }
        h[k] = 0; char *nl = strchr(h, '\n'); if (nl) *nl = 0;
        char *s = h + 2; while (*s == ' ' || *s == '\t') s++;
        char *ip = s; while (*s && *s != ' ' && *s != '\t') s++;
        char *arg = NULL; if (*s) { *s++ = 0; while (*s == ' ' || *s == '\t') s++; if (*s) { arg = s; char *t = s + strlen(s); while (t > s && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\r')) *--t = 0; } }
        if (!*ip) return -ENOEXEC;
        char **nv = calloc((size_t) (n + 4), sizeof *nv); int j = 0;
        nv[j++] = strdup(ip); if (arg) nv[j++] = strdup(arg);
        nv[j++] = strdup(gc);
        for (int i = 1; i < n; i++) nv[j++] = av[i];
        av = nv; n = j; snprintf(cur, sizeof cur, "%s", ip);
    }
    return -ELOOP;
}
/* options that make a re-exec'd pemu rebuild the same state (table, cwd, identity, -k, placeholders); returns the new k */
static int pemu_reexec_opts(char **nv, int k) {
    nv[k++] = "--reexec"; nv[k++] = "-r"; nv[k++] = pemu_root; nv[k++] = "--no-default-binds";
    for (int i = 1; i < pemu_nb; i++) {
        if (pemu_b[i].off) continue;
        char *s = malloc(strlen(pemu_b[i].h) + strlen(pemu_b[i].g) + 3);
        sprintf(s, "%s:%s!", pemu_b[i].h, pemu_b[i].g); nv[k++] = "-b"; nv[k++] = s;      /* '!': the guest path is already canonical */
    }
    pemu_lock(); nv[k++] = "-w"; nv[k++] = strdup(pemu_cwd); pemu_unlock();
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
static int pemu_reexec_count(void) { return 28 + 2 * pemu_nb + 2 * pemu_nph; }
