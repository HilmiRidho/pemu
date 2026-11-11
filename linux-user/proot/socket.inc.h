/* linux-user/proot/socket.inc.h - AF_UNIX socket address translation. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* ---- AF_UNIX addresses (bind, connect, sendto, sendmsg, sendmmsg) and their results (getsockname,
 * getpeername, recvfrom, recvmsg, accept). Guest paths map to host paths and back.
 * pemu_unix_prep: 0 = unchanged, 1 = rewritten into *o (close *tfd afterwards if >= 0), <0 = -errno.
 * follow: 0 for bind (the name is created, a last symlink is not followed), 1 for connect / sendto (the kernel follows a last symlink,
 * and it must do so in the GUEST's file system: an absolute link target resolved by the host kernel would land on the host).
 * Host paths longer than sun_path (108) are reached through /proc/self/fd/<dir fd>/<name>. */
static int pemu_unix_prep(const void *a, unsigned len, struct sockaddr_un *o, unsigned *ol, int *tfd, int follow) {
    *tfd = -1;
    if (!pemu_on || !a || len < 3 || len > sizeof *o) return 0;
    const struct sockaddr_un *s = a;
    if (s->sun_family != AF_UNIX || !s->sun_path[0]) return 0;
    char gp[sizeof s->sun_path + 1]; size_t n = len - 2; if (n > sizeof s->sun_path) n = sizeof s->sun_path;
    memcpy(gp, s->sun_path, n); gp[n] = 0;
    char host[PEMU_HP];
    long pr = pemu_path(AT_FDCWD, gp, follow, host, sizeof host, NULL);
    if (pr < 0) return (int) pr;                       /* ELOOP, ENAMETOOLONG ...: answer as the kernel would; passing the GUEST path on would have the host resolve it (reproduced: connect() reached a host socket where open() said ELOOP) */
    if (pr != 0) return 0;                             /* 1: not a rootfs path (raw bind such as /proc): the guest path is the host path */
    if (strlen(host) >= sizeof o->sun_path) {
        char *sl = strrchr(host, '/');
        if (!sl || sl == host || !sl[1]) return -ENAMETOOLONG;
        *sl = 0;
        int d = open(host, O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (d < 0) return -errno;
        char sh[PATH_MAX]; snprintf(sh, sizeof sh, "/proc/self/fd/%d/%s", d, sl + 1);
        if (strlen(sh) >= sizeof o->sun_path) { close(d); return -ENAMETOOLONG; }
        *tfd = d; strcpy(host, sh);
    }
    memset(o, 0, sizeof *o); o->sun_family = AF_UNIX; strcpy(o->sun_path, host);
    *ol = (unsigned) (offsetof(struct sockaddr_un, sun_path) + strlen(host) + 1);
    return 1;
}
/* address the host kernel returned -> guest path; cap = size of the guest's buffer, *lenp = returned length */
static void pemu_unix_out(void *addr, unsigned cap, unsigned *lenp) {
    if (!pemu_on || !addr || !lenp) return;
    struct sockaddr_un *s = addr; unsigned len = *lenp < cap ? *lenp : cap;
    if (len < 3 || s->sun_family != AF_UNIX || s->sun_path[0] != '/') return;
    char h[sizeof s->sun_path + 1], g[PATH_MAX]; size_t n = len - 2; if (n > sizeof s->sun_path) n = sizeof s->sun_path;
    memcpy(h, s->sun_path, n); h[n] = 0;
    if (pemu_h2g(h, g) != 0) return;
    size_t gl = strlen(g); if (gl >= sizeof s->sun_path) return;
    unsigned full = (unsigned) (offsetof(struct sockaddr_un, sun_path) + gl + 1);
    struct sockaddr_un t; memset(&t, 0, sizeof t); t.sun_family = AF_UNIX; memcpy(t.sun_path, g, gl);
    memcpy(addr, &t, full < cap ? full : cap); *lenp = full;
}
enum { PEMU_S_BIND = 1, PEMU_S_CONNECT, PEMU_S_SENDTO, PEMU_S_SENDMSG, PEMU_S_SENDMMSG, PEMU_S_GETSOCKNAME, PEMU_S_GETPEERNAME,
       PEMU_S_RECVFROM, PEMU_S_RECVMSG, PEMU_S_ACCEPT, PEMU_S_ACCEPT4, PEMU_S_RECVMMSG };
struct pemu_mmsg { struct msghdr h; unsigned len; };
static long pemu_sendmsg1(long fd, const struct msghdr *g, long flags) {
    struct msghdr m = *g; struct sockaddr_un su; unsigned sl = 0; int tfd = -1;
    if (m.msg_name) {
        int e = pemu_unix_prep(m.msg_name, m.msg_namelen, &su, &sl, &tfd, 1);
        if (e < 0) return e;
        if (e == 1) { m.msg_name = &su; m.msg_namelen = sl; }
    }
    long r = hs(SYS_sendmsg, fd, (long) &m, flags, 0, 0, 0);
    if (tfd >= 0) close(tfd);
    return r;
}
/* 1 = handled (*res set), 0 = let the normal translation run. Same argument order as the syscall. */
static int pemu_sock_hook(int op, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, long *res) {
#define RET(x) do { *res = (x); return 1; } while (0)
    struct sockaddr_un su; unsigned sl = 0; int tfd = -1, e; long r;
    switch (op) {
    case PEMU_S_BIND: case PEMU_S_CONNECT:
        e = pemu_unix_prep((const void*) a1, (unsigned) a2, &su, &sl, &tfd, op == PEMU_S_CONNECT);
        if (e < 0) RET(e);
        if (e == 0) return 0;
        r = hs(op == PEMU_S_BIND ? SYS_bind : SYS_connect, (long) a0, (long) &su, (long) sl, 0, 0, 0);
        if (tfd >= 0) close(tfd);
        RET(r);
    case PEMU_S_SENDTO:
        e = pemu_unix_prep((const void*) a4, (unsigned) a5, &su, &sl, &tfd, 1);
        if (e < 0) RET(e);
        if (e == 0) return 0;
        r = hs(SYS_sendto, (long) a0, (long) a1, (long) a2, (long) a3, (long) &su, (long) sl);
        if (tfd >= 0) close(tfd);
        RET(r);
    case PEMU_S_SENDMSG:
        if (!a1) return 0;
        RET(pemu_sendmsg1((long) a0, (const struct msghdr*) a1, (long) a2));
    case PEMU_S_SENDMMSG: {
        struct pemu_mmsg *mm = (struct pemu_mmsg*) a1; unsigned vl = (unsigned) a2; unsigned i;
        if (!mm) return 0;
        for (i = 0; i < vl; i++) {
            r = pemu_sendmsg1((long) a0, &mm[i].h, (long) a3);
            if (r < 0) { if (i == 0) RET(r); break; }
            mm[i].len = (unsigned) r;
        }
        RET((long) i); }
    case PEMU_S_GETSOCKNAME: case PEMU_S_GETPEERNAME: {
        unsigned *lp = (unsigned*) a2; if (!a1 || !lp) return 0;
        unsigned cap = *lp;
        r = hs(op == PEMU_S_GETSOCKNAME ? SYS_getsockname : SYS_getpeername, (long) a0, (long) a1, (long) a2, 0, 0, 0);
        if (r == 0) pemu_unix_out((void*) a1, cap, lp);
        RET(r); }
    case PEMU_S_RECVFROM: {
        unsigned *lp = (unsigned*) a5; if (!a4 || !lp) return 0;
        unsigned cap = *lp;
        r = hs(SYS_recvfrom, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        if (r >= 0) pemu_unix_out((void*) a4, cap, lp);
        RET(r); }
    case PEMU_S_RECVMSG: {
        struct msghdr *m = (struct msghdr*) a1; if (!m || !m->msg_name) return 0;
        unsigned cap = m->msg_namelen;
        r = hs(SYS_recvmsg, (long) a0, (long) m, (long) a2, 0, 0, 0);
        if (r >= 0) pemu_unix_out(m->msg_name, cap, &m->msg_namelen);
        RET(r); }
    case PEMU_S_RECVMMSG: {                         /* like recvmsg, for every message received: source names back to guest paths */
        struct pemu_mmsg *mm = (struct pemu_mmsg*) a1; unsigned vl = (unsigned) a2, caps[1024];
        if (!mm || !vl) return 0;
        if (vl > 1024) vl = 1024;                   /* the kernel clamps to UIO_MAXIOV as well */
        for (unsigned i = 0; i < vl; i++) caps[i] = mm[i].h.msg_name ? mm[i].h.msg_namelen : 0;
        r = hs(SYS_recvmmsg, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, 0);
        for (long i = 0; i < r && i < (long) vl; i++) if (mm[i].h.msg_name) pemu_unix_out(mm[i].h.msg_name, caps[i], &mm[i].h.msg_namelen);
        RET(r); }
    case PEMU_S_ACCEPT: case PEMU_S_ACCEPT4: {
        unsigned *lp = (unsigned*) a2; unsigned cap = (a1 && lp) ? *lp : 0;
        /* plain accept == accept4 with flags 0 (Android's app seccomp filter lacks accept) */
        r = hs(SYS_accept4, (long) a0, (long) a1, (long) a2, op == PEMU_S_ACCEPT4 ? (long) a3 : 0, 0, 0);
        if (r >= 0 && a1 && lp) pemu_unix_out((void*) a1, cap, lp);
        RET(r); }
    }
    (void) a3; (void) a5;
    return 0;
#undef RET
}
