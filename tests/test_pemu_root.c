#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <sys/stat.h>
static long hs(long nr, long a, long b, long c_, long d, long e, long f) { long r = syscall(nr,a,b,c_,d,e,f); return r == -1 ? -errno : r; }
#include "pemu_root.h"
int swap_stop;
void *swapper(void *a) {
    (void) a;
    while (!swap_stop) syscall(SYS_renameat2, AT_FDCWD, "/tmp/pemu-test/rf/tmp/swap_a", AT_FDCWD, "/tmp/pemu-test/rf/tmp/swap_b", 2 /* RENAME_EXCHANGE */);
    return NULL;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void want(long dfd, const char*p, int fol, const char*exp) {
    char h[PEMU_HP]; long r = pemu_path(dfd, p, fol, h, sizeof h, NULL);
    CHECK(r == 0 && !strcmp(h, exp), "path '%s' -> r=%ld host='%s' expected '%s'", p, r, r==0?h:"-", exp);
}
int main(void) {
    char R[PATH_MAX]; system("mkdir -p /tmp/pemu-test; rm -rf /tmp/pemu-test/rf /tmp/pemu-test/hostdir; mkdir -p /tmp/pemu-test/rf/usr/bin /tmp/pemu-test/rf/etc /tmp/pemu-test/rf/root /tmp/pemu-test/rf/tmp /tmp/pemu-test/hostdir/sub && cd /tmp/pemu-test/rf && ln -s usr/bin bin && ln -s /usr/bin/sh etc/alt && ln -s /../../../etc/passwd evil && ln -s /loop2 loop1 && ln -s /loop1 loop2 && ln -s ../../hostdir/x tmp/escape && printf 'x' > usr/bin/sh && chmod +x usr/bin/sh && printf '#!/usr/bin/env python3 -u\\nprint(1)\\n' > usr/bin/script && chmod +x usr/bin/script && printf '#!/bin/sh\\n' > usr/bin/s2 && chmod +x usr/bin/s2 && printf 'e' > usr/bin/env && chmod +x usr/bin/env && printf 'p' > usr/bin/python3 && chmod +x usr/bin/python3; echo data > /tmp/pemu-test/hostdir/sub/file");
    realpath("/tmp/pemu-test/rf", R);
    char *av[] = {"x","-r","/tmp/pemu-test/rf","-b","/tmp/pemu-test/hostdir:/mnt/h","-w","/root","--root-id"}; int a;
    for (a = 1; a < 8; a++) { int r = pemu_opt(8, av, &a); CHECK(r == 1, "pemu_opt"); }
    CHECK(pemu_finish() == 0, "finish");
    CHECK(!strcmp(pemu_cwd, "/root"), "cwd=%s", pemu_cwd);
    want(AT_FDCWD, "/etc/alt", 1, "/tmp/pemu-test/rf/usr/bin/sh");          /* absolute symlink resolved inside rootfs */
    want(AT_FDCWD, "/bin/sh", 1, "/tmp/pemu-test/rf/usr/bin/sh");           /* relative dir symlink */
    want(AT_FDCWD, "/etc/alt", 0, "/tmp/pemu-test/rf/etc/alt");             /* nofollow keeps the link */
    want(AT_FDCWD, "/../../../etc", 1, "/tmp/pemu-test/rf/etc");            /* .. clamped at / */
    want(AT_FDCWD, "/evil", 1, "/tmp/pemu-test/rf/etc/passwd");             /* symlink to ../../..: still inside */
    want(AT_FDCWD, "etc/../usr", 1, "/tmp/pemu-test/rf/root/usr");            /* cwd=/root: /root/etc/../usr */
    { char h[PEMU_HP]; long r = pemu_path(AT_FDCWD, "/loop1", 1, h, sizeof h, NULL); CHECK(r == -ELOOP, "loop r=%ld", r); }
    want(AT_FDCWD, "/mnt/h/sub/file", 1, "/tmp/pemu-test/hostdir/sub/file"); /* bind */

    { char h[PEMU_HP]; pemu_path(AT_FDCWD, "/proc/self/status", 1, h, sizeof h, NULL); char e2[64]; snprintf(e2, sizeof e2, "/proc/%d/status", (int) getpid()); CHECK(!strcmp(h, e2), "proc self -> %s", h); }
    want(AT_FDCWD, "/tmp/escape", 1, "/tmp/pemu-test/rf/hostdir/x");          /* ../../ from /tmp is clamped at the guest root */
    /* trailing slash kept */
    { char h[PEMU_HP]; pemu_path(AT_FDCWD, "/usr/", 1, h, sizeof h, NULL); CHECK(!strcmp(h, "/tmp/pemu-test/rf/usr/"), "trailing %s", h); }
    /* chdir / getcwd / relative */
    CHECK(pemu_chdir("/bin") == 0, "chdir");
    CHECK(!strcmp(pemu_cwd, "/usr/bin"), "cwd after chdir via symlink = %s", pemu_cwd);
    { char b[256]; long n = pemu_getcwd(b, sizeof b); CHECK(n == 9 && !strcmp(b, "/usr/bin"), "getcwd %ld %s", n, b); }
    { char h[PEMU_HP]; pemu_path(AT_FDCWD, "sh", 1, h, sizeof h, NULL); CHECK(!strcmp(h, "/tmp/pemu-test/rf/usr/bin/sh"), "relative %s", h); }
    { char b[PATH_MAX]; getcwd(b, sizeof b); CHECK(!strcmp(b, "/tmp/pemu-test/rf/usr/bin"), "host cwd %s", b); }
    /* dirfd-relative */
    { int fd = open("/tmp/pemu-test/rf/usr", O_RDONLY | O_DIRECTORY); char h[PEMU_HP]; long r = pemu_path(fd, "bin/sh", 1, h, sizeof h, NULL);
      CHECK(r == 0 && !strcmp(h, "/tmp/pemu-test/rf/usr/bin/sh"), "dirfd r=%ld %s", r, h);
      char g[PATH_MAX]; CHECK(pemu_fd2g(fd, g) == 0 && !strcmp(g, "/usr"), "fd2g %s", g);
      int fb = open("/tmp/pemu-test/hostdir/sub", O_RDONLY | O_DIRECTORY); CHECK(pemu_fd2g(fb, g) == 0 && !strcmp(g, "/mnt/h/sub"), "fd2g bind %s", g);
      int fo = open("/etc", O_RDONLY | O_DIRECTORY); CHECK(pemu_fd2g(fo, g) != 0 || strncmp(g, "/tmp/pemu-test/rf", 9), "fd outside rootfs"); close(fd); close(fb); close(fo); }
    /* readlinkat */
    { char b[256]; strcpy(pemu_exe, "/usr/bin/foo"); char p[64]; snprintf(p, sizeof p, "/proc/%d/exe", (int) getpid());
      long n = pemu_readlinkat(AT_FDCWD, "/proc/self/exe", b, sizeof b); CHECK(n == 12 && !memcmp(b, "/usr/bin/foo", 12), "exe n=%ld", n);
      n = pemu_readlinkat(AT_FDCWD, "/etc/alt", b, sizeof b); CHECK(n == 11 && !memcmp(b, "/usr/bin/sh", 11), "readlink n=%ld", n);
      int fd = open("/tmp/pemu-test/hostdir/sub/file", O_RDONLY); char q[64]; snprintf(q, sizeof q, "/proc/self/fd/%d", fd);
      n = pemu_readlinkat(AT_FDCWD, q, b, sizeof b); b[n > 0 ? n : 0] = 0; CHECK(n > 0 && !strcmp(b, "/mnt/h/sub/file"), "fd link -> %s", b); close(fd); }
    /* exec: shebang expansion */
    { char host[PEMU_HP], gp[PATH_MAX]; char *args[] = {"script", "a1", NULL}; char **nv;
      long r = pemu_exec_resolve("/usr/bin/script", args, &nv, host, gp);
      CHECK(r == 0 && !strcmp(gp, "/usr/bin/env") && !strcmp(host, "/tmp/pemu-test/rf/usr/bin/env"), "shebang r=%ld gp=%s", r, gp);
      CHECK(r == 0 && !strcmp(nv[0], "/usr/bin/env") && !strcmp(nv[1], "python3 -u") && !strcmp(nv[2], "/usr/bin/script") && !strcmp(nv[3], "a1") && !nv[4], "argv: %s|%s|%s|%s", nv[0], nv[1], nv[2], nv[3]);
      r = pemu_exec_resolve("s2", (char*[]){"s2", NULL}, &nv, host, gp);       /* relative to cwd /usr/bin, #!/bin/sh through symlink */
      CHECK(r == 0 && !strcmp(gp, "/usr/bin/sh") && !strcmp(nv[0], "/bin/sh") && !strcmp(nv[1], "/usr/bin/s2"), "s2 r=%ld gp=%s", r, gp);
      CHECK(pemu_exec_resolve("/nope", (char*[]){"n", NULL}, &nv, host, gp) == -ENOENT, "enoent");
      CHECK(pemu_exec_resolve("/etc", (char*[]){"n", NULL}, &nv, host, gp) == -EACCES, "dir exec"); }
    /* openat through callat (real syscalls) */
    { long fd = pemu_callat(SYS_openat, AT_FDCWD, "/mnt/h/sub/file", 1, O_RDONLY, 0, 0, 0); char b[16] = ""; CHECK(fd >= 0, "open %ld", fd); if (fd >= 0) { read(fd, b, 5); CHECK(!strcmp(b, "data\n"), "read '%s'", b); close(fd); }
      long e = pemu_callat(SYS_openat, AT_FDCWD, "/etc/passwd-nonexistent", 1, O_RDONLY, 0, 0, 0); CHECK(e == -ENOENT, "enoent2 %ld", e);
      CHECK(pemu_callat(SYS_mkdirat, AT_FDCWD, "/tmp/newdir", 0, 0755, 0, 0, 0) == 0 && access("/tmp/pemu-test/rf/tmp/newdir", F_OK) == 0, "mkdir"); }
    /* reexec options */
    { char *nv[64]; int k = pemu_reexec_opts(nv, 0); int hasr = 0, hasw = 0, hasroot = 0; for (int i = 0; i < k; i++) { if (!strcmp(nv[i], "-b")) hasr++; if (!strcmp(nv[i], "-w")) hasw = !strcmp(nv[i+1], "/usr/bin"); if (!strcmp(nv[i], "--ids")) hasroot = 1; }
      CHECK(hasr == 4 && hasw && hasroot && k <= pemu_reexec_count(), "reexec opts k=%d binds=%d", k, hasr); }
    /* unix socket path */
    { struct sockaddr_un su = {AF_UNIX}, o; strcpy(su.sun_path, "/tmp/sock"); unsigned ol; int tf; int e = pemu_unix_prep(&su, sizeof su, &o, &ol, &tf); CHECK(e == 1 && !strcmp(o.sun_path, "/tmp/pemu-test/rf/tmp/sock"), "unix %d %s", e, o.sun_path); }
    /* ---- openat2 RESOLVE_* semantics (guest-side walk) */
    { char h[PEMU_HP]; int fu = open("/tmp/pemu-test/rf/usr", O_RDONLY | O_DIRECTORY), fe = open("/tmp/pemu-test/rf/etc", O_RDONLY | O_DIRECTORY);
      CHECK(pemu_path_r(AT_FDCWD, "/etc/alt", 1, h, sizeof h, NULL, PEMU_RS_NO_SYMLINKS) == -ELOOP, "NO_SYMLINKS on a symlink");
      CHECK(pemu_path_r(AT_FDCWD, "/bin/sh", 1, h, sizeof h, NULL, PEMU_RS_NO_SYMLINKS) == -ELOOP, "NO_SYMLINKS on a symlinked dir");
      CHECK(pemu_path_r(AT_FDCWD, "/usr/bin/sh", 1, h, sizeof h, NULL, PEMU_RS_NO_SYMLINKS) == 0, "NO_SYMLINKS plain path");
      CHECK(pemu_path_r(AT_FDCWD, "/etc/alt", 0, h, sizeof h, NULL, PEMU_RS_NO_SYMLINKS) == -ELOOP, "NO_SYMLINKS + nofollow on a symlink");
      CHECK(pemu_path_r(fu, "/usr/bin/sh", 1, h, sizeof h, NULL, PEMU_RS_BENEATH) == -EXDEV, "BENEATH absolute");
      CHECK(pemu_path_r(fu, "bin/sh", 1, h, sizeof h, NULL, PEMU_RS_BENEATH) == 0, "BENEATH relative ok");
      CHECK(pemu_path_r(fu, "bin/../bin/sh", 1, h, sizeof h, NULL, PEMU_RS_BENEATH) == 0, "BENEATH .. that stays inside");
      CHECK(pemu_path_r(fu, "bin/../../etc", 1, h, sizeof h, NULL, PEMU_RS_BENEATH) == -EXDEV, "BENEATH .. escapes");
      CHECK(pemu_path_r(fe, "alt", 1, h, sizeof h, NULL, PEMU_RS_BENEATH) == -EXDEV, "BENEATH absolute symlink");
      CHECK(pemu_path_r(fu, "/bin/sh", 1, h, sizeof h, NULL, PEMU_RS_IN_ROOT) == 0 && !strcmp(h, "/tmp/pemu-test/rf/usr/bin/sh"), "IN_ROOT absolute -> %s", h);
      CHECK(pemu_path_r(fu, "../../etc", 1, h, sizeof h, NULL, PEMU_RS_IN_ROOT) == 0 && !strcmp(h, "/tmp/pemu-test/rf/usr/etc"), ".. clamped at the IN_ROOT anchor -> %s", h);
      CHECK(pemu_path_r(fe, "alt", 1, h, sizeof h, NULL, PEMU_RS_IN_ROOT) == 0 && !strcmp(h, "/tmp/pemu-test/rf/etc/usr/bin/sh"), "IN_ROOT absolute symlink -> %s", h);
      close(fu); close(fe); }
    /* ---- pemu_open: race-free openat2 path and the plain fallback must agree */
    for (int mode = 1; mode >= 0; mode--) {
        pemu_o2 = mode;
        long fd = pemu_open(AT_FDCWD, "/etc/alt", O_RDONLY, 0, 0); char c = 0;
        CHECK(fd >= 0 && read((int) fd, &c, 1) == 1 && c == 'x', "open via symlink (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        fd = pemu_open(AT_FDCWD, "/evil", O_RDONLY, 0, 0); CHECK(fd == -ENOENT, "symlink to /../../../etc/passwd must not reach the host file (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        fd = pemu_open(AT_FDCWD, "/tmp/created", O_CREAT | O_WRONLY | O_TRUNC, 0644, 0);
        CHECK(fd >= 0 && access("/tmp/pemu-test/rf/tmp/created", F_OK) == 0, "O_CREAT lands in the rootfs (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        unlink("/tmp/pemu-test/rf/tmp/created");
        fd = pemu_open(AT_FDCWD, "/loop1", O_RDONLY, 0, 0); CHECK(fd == -ELOOP, "loop (secure=%d) fd=%ld", mode, fd);
        fd = pemu_open(AT_FDCWD, "/usr/", O_RDONLY | O_DIRECTORY, 0, 0); CHECK(fd >= 0, "dir with trailing slash (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        fd = pemu_open(AT_FDCWD, "/", O_RDONLY | O_DIRECTORY, 0, 0); CHECK(fd >= 0, "root dir (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        { uint64_t how[3] = { O_RDONLY, 0, PEMU_RS_NO_SYMLINKS };
          fd = pemu_openat2(AT_FDCWD, "/etc/alt", how, sizeof how); CHECK(fd == -ELOOP, "openat2 NO_SYMLINKS (secure=%d) fd=%ld", mode, fd);
          fd = pemu_openat2(AT_FDCWD, "/usr/bin/sh", how, sizeof how); CHECK(fd >= 0, "openat2 NO_SYMLINKS plain (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
          how[2] = 0x80; CHECK(pemu_openat2(AT_FDCWD, "/usr/bin/sh", how, sizeof how) == -EINVAL, "unknown RESOLVE bit"); }
    }
    pemu_o2 = -1;
    /* ---- fake /proc */
    { char b[4096];
      CHECK(pemu_fakeproc("/proc/stat", b, sizeof b) > 0 && !strncmp(b, "cpu  ", 5) && strstr(b, "\ncpu0 ") && strstr(b, "\nbtime "), "fake /proc/stat");
      CHECK(pemu_fakeproc("/proc/loadavg", b, sizeof b) > 0 && strstr(b, " 1/1 "), "fake loadavg");
      CHECK(pemu_fakeproc("/proc/uptime", b, sizeof b) > 0, "fake uptime");
      CHECK(pemu_fakeproc("/proc/version", b, sizeof b) > 0 && !strncmp(b, "Linux version ", 14), "fake version");
      CHECK(pemu_fakeproc("/proc/meminfo", b, sizeof b) < 0, "meminfo is not faked");
      long fd = pemu_fakeproc_open("/proc/stat", O_RDONLY | O_CLOEXEC); ssize_t n = fd >= 0 ? read((int) fd, b, sizeof b) : -1;
      CHECK(fd >= 0 && n > 10 && !strncmp(b, "cpu  ", 5), "fake /proc/stat memfd fd=%ld n=%zd", fd, n); if (fd >= 0) close((int) fd);
      CHECK(pemu_fakeproc_open("/etc/passwd", O_RDONLY) == LONG_MIN, "only /proc paths are faked");
      CHECK(pemu_fakeproc_open("/proc/stat", O_WRONLY) == -EACCES, "fake file is read-only"); }
    /* ---- AF_UNIX: translate on the way in and out */
    { long res; int sv = socket(AF_UNIX, SOCK_DGRAM, 0), cl = socket(AF_UNIX, SOCK_DGRAM, 0);
      struct sockaddr_un a = { AF_UNIX }, b = { AF_UNIX };
      strcpy(a.sun_path, "/tmp/srv.sock"); strcpy(b.sun_path, "/tmp/cli.sock");
      CHECK(pemu_sock_hook(PEMU_S_BIND, sv, (uintptr_t) &a, sizeof a, 0, 0, 0, &res) == 1 && res == 0, "bind res=%ld", res);
      CHECK(pemu_sock_hook(PEMU_S_BIND, cl, (uintptr_t) &b, sizeof b, 0, 0, 0, &res) == 1 && res == 0, "bind client res=%ld", res);
      CHECK(access("/tmp/pemu-test/rf/tmp/srv.sock", F_OK) == 0, "socket file is inside the rootfs");
      struct sockaddr_un g; unsigned gl = sizeof g; memset(&g, 0, sizeof g);
      CHECK(pemu_sock_hook(PEMU_S_GETSOCKNAME, sv, (uintptr_t) &g, (uintptr_t) &gl, 0, 0, 0, &res) == 1 && res == 0 && !strcmp(g.sun_path, "/tmp/srv.sock") && gl == offsetof(struct sockaddr_un, sun_path) + 14, "getsockname -> '%s' len %u", g.sun_path, gl);
      CHECK(pemu_sock_hook(PEMU_S_SENDTO, cl, (uintptr_t) "hello", 5, 0, (uintptr_t) &a, sizeof a, &res) == 1 && res == 5, "sendto res=%ld", res);
      char buf[16]; struct sockaddr_un from; unsigned fl = sizeof from; memset(&from, 0, sizeof from);
      CHECK(pemu_sock_hook(PEMU_S_RECVFROM, sv, (uintptr_t) buf, sizeof buf, 0, (uintptr_t) &from, (uintptr_t) &fl, &res) == 1 && res == 5 && !memcmp(buf, "hello", 5) && !strcmp(from.sun_path, "/tmp/cli.sock"), "recvfrom res=%ld from='%s'", res, from.sun_path);
      struct iovec iv = { (void*) "again", 5 }; struct msghdr mh; memset(&mh, 0, sizeof mh); mh.msg_name = &a; mh.msg_namelen = sizeof a; mh.msg_iov = &iv; mh.msg_iovlen = 1;
      CHECK(pemu_sock_hook(PEMU_S_SENDMSG, cl, (uintptr_t) &mh, 0, 0, 0, 0, &res) == 1 && res == 5, "sendmsg res=%ld", res);
      struct iovec ri = { buf, sizeof buf }; struct sockaddr_un mf; memset(&mf, 0, sizeof mf);
      struct msghdr rm; memset(&rm, 0, sizeof rm); rm.msg_name = &mf; rm.msg_namelen = sizeof mf; rm.msg_iov = &ri; rm.msg_iovlen = 1;
      CHECK(pemu_sock_hook(PEMU_S_RECVMSG, sv, (uintptr_t) &rm, 0, 0, 0, 0, &res) == 1 && res == 5 && !strcmp(mf.sun_path, "/tmp/cli.sock"), "recvmsg res=%ld from='%s'", res, mf.sun_path);
      struct pemu_mmsg mm[2]; memset(mm, 0, sizeof mm); mm[0].h = mh; mm[1].h = mh;
      CHECK(pemu_sock_hook(PEMU_S_SENDMMSG, cl, (uintptr_t) mm, 2, 0, 0, 0, &res) == 1 && res == 2 && mm[0].len == 5 && mm[1].len == 5, "sendmmsg res=%ld", res);
      close(sv); close(cl); }
    /* host path longer than sun_path: reached through /proc/self/fd/N */
    { long res; char d[300] = "/tmp/pemu-test/rf/tmp/", gp[300] = "/tmp/";
      for (int i = 0; i < 3; i++) { char seg[40]; memset(seg, 'a' + i, 30); seg[30] = 0; strcat(d, seg); strcat(d, "/"); strcat(gp, seg); strcat(gp, "/"); }
      char mk[400]; snprintf(mk, sizeof mk, "mkdir -p %s", d); system(mk);
      strcat(gp, "long.sock");
      struct sockaddr_un a = { AF_UNIX }; strcpy(a.sun_path, gp);
      CHECK(strlen(d) + 9 > 108, "test path really is too long");
      int sv = socket(AF_UNIX, SOCK_STREAM, 0), cl = socket(AF_UNIX, SOCK_STREAM, 0);
      CHECK(pemu_sock_hook(PEMU_S_BIND, sv, (uintptr_t) &a, sizeof a, 0, 0, 0, &res) == 1 && res == 0, "bind long path res=%ld", res);
      listen(sv, 1);
      CHECK(pemu_sock_hook(PEMU_S_CONNECT, cl, (uintptr_t) &a, sizeof a, 0, 0, 0, &res) == 1 && res == 0, "connect long path res=%ld", res);
      struct sockaddr_un pa; unsigned pl = sizeof pa; memset(&pa, 0, sizeof pa);
      CHECK(pemu_sock_hook(PEMU_S_ACCEPT, sv, (uintptr_t) &pa, (uintptr_t) &pl, 0, 0, 0, &res) == 1 && res >= 0, "accept res=%ld", res);
      if (res >= 0) close((int) res);
      close(sv); close(cl); }
    /* ---- mutating calls through the race-free walk; both modes must behave the same */
    for (int mode = 1; mode >= 0; mode--) {
        pemu_o2 = mode; long r; struct stat st; const char *H = "/tmp/pemu-test/rf";
        char hp[PATH_MAX];
        r = pemu_callat(SYS_mkdirat, AT_FDCWD, "/tmp/d1", 0, 0755, 0, 0, 0); CHECK(r == 0 && stat("/tmp/pemu-test/rf/tmp/d1", &st) == 0, "mkdir (secure=%d) r=%ld", mode, r);
        r = pemu_callat(SYS_mkdirat, AT_FDCWD, "/tmp/d1/", 0, 0755, 0, 0, 0); CHECK(r == -EEXIST, "mkdir existing with trailing slash (secure=%d) r=%ld", mode, r);
        r = pemu_symlinkat("/etc/alt", AT_FDCWD, "/tmp/d1/ln"); CHECK(r == 0, "symlink (secure=%d) r=%ld", mode, r);
        snprintf(hp, sizeof hp, "%s/tmp/d1/ln", H); { char t[64]; ssize_t n = readlink(hp, t, sizeof t - 1); t[n > 0 ? n : 0] = 0; CHECK(!strcmp(t, "/etc/alt"), "symlink target kept verbatim '%s'", t); }
        r = pemu_callat(SYS_fchmodat, AT_FDCWD, "/tmp/d1/ln", 1, 0600, 0, 0, 0); CHECK(r == 0 && stat("/tmp/pemu-test/rf/usr/bin/sh", &st) == 0 && (st.st_mode & 0777) == 0600, "chmod follows the symlink inside the rootfs (secure=%d) r=%ld mode=%o", mode, r, st.st_mode & 0777);
        chmod("/tmp/pemu-test/rf/usr/bin/sh", 0755);
        r = pemu_call1(SYS_truncate, "/tmp/d1/ln", 1, 0, 0, 0, 0, 0); CHECK(r == 0 && stat("/tmp/pemu-test/rf/usr/bin/sh", &st) == 0 && st.st_size == 0, "truncate (secure=%d) r=%ld", mode, r);
        { FILE *f = fopen("/tmp/pemu-test/rf/usr/bin/sh", "w"); fputc('x', f); fclose(f); }
        r = pemu_call2at(SYS_renameat, AT_FDCWD, "/tmp/d1/ln", 0, AT_FDCWD, "/tmp/d1/ln2", 0, 0); CHECK(r == 0 && lstat("/tmp/pemu-test/rf/tmp/d1/ln2", &st) == 0, "rename (secure=%d) r=%ld", mode, r);
        r = pemu_call2at(SYS_linkat, AT_FDCWD, "/usr/bin/sh", 0, AT_FDCWD, "/tmp/d1/hard", 0, 0); CHECK(r == 0 && stat("/tmp/pemu-test/rf/tmp/d1/hard", &st) == 0 && st.st_nlink >= 2, "link (secure=%d) r=%ld", mode, r);
        r = pemu_callat(SYS_unlinkat, AT_FDCWD, "/tmp/d1/hard", 0, 0, 0, 0, 0); CHECK(r == 0, "unlink (secure=%d) r=%ld", mode, r);
        r = pemu_callat(SYS_unlinkat, AT_FDCWD, "/tmp/d1/ln2", 0, 0, 0, 0, 0); CHECK(r == 0, "unlink symlink itself (secure=%d) r=%ld", mode, r);
        CHECK(stat("/tmp/pemu-test/rf/usr/bin/sh", &st) == 0, "unlinking the symlink must not remove its target");
        r = pemu_callat(SYS_unlinkat, AT_FDCWD, "/tmp/d1", 0, AT_REMOVEDIR, 0, 0, 0); CHECK(r == 0, "rmdir (secure=%d) r=%ld", mode, r);
        r = pemu_callat(SYS_fchmodat, AT_FDCWD, "/nonexistent", 1, 0600, 0, 0, 0); CHECK(r == -ENOENT, "chmod missing (secure=%d) r=%ld", mode, r);
        r = pemu_callat(SYS_unlinkat, AT_FDCWD, "/", 0, AT_REMOVEDIR, 0, 0, 0); CHECK(r < 0, "rmdir / is refused (secure=%d) r=%ld", mode, r);
    }
    pemu_o2 = -1;
    /* ---- the symlink race: one thread swaps /tmp/swap_a between a real directory and an absolute symlink to a host
     * directory (RENAME_EXCHANGE), the other creates /tmp/swap_a/esc through the translated mkdir. With the race-free
     * walk nothing may ever appear in the host directory. */
    {
        system("rm -rf /tmp/pemu-test/hostdir/esc; mkdir -p /tmp/pemu-test/rf/tmp/swap_a && ln -sfn /tmp/pemu-test/hostdir /tmp/pemu-test/rf/tmp/swap_b");
        extern int swap_stop; extern void *swapper(void *);
        int escapes[2] = { 0, 0 };
        for (int mode = 1; mode >= 0; mode--) {
            pemu_o2 = mode; swap_stop = 0; pthread_t th; pthread_create(&th, NULL, swapper, NULL);
            for (int i = 0; i < (mode ? 40000 : 40000); i++) {
                pemu_callat(SYS_mkdirat, AT_FDCWD, "/tmp/swap_a/esc", 0, 0755, 0, 0, 0);
                if (access("/tmp/pemu-test/hostdir/esc", F_OK) == 0) { escapes[mode]++; rmdir("/tmp/pemu-test/hostdir/esc"); }
                else rmdir("/tmp/pemu-test/rf/tmp/swap_a/esc");
            }
            swap_stop = 1; pthread_join(th, NULL);
            rmdir("/tmp/pemu-test/rf/tmp/swap_a/esc"); rmdir("/tmp/pemu-test/rf/tmp/swap_b/esc");
        }
        CHECK(escapes[1] == 0, "race-free walk let %d mkdirs escape to the host", escapes[1]);
        printf("symlink race: %d escapes with the plain walk, %d with the race-free walk\n", escapes[0], escapes[1]);
        pemu_o2 = -1;
    }
    /* ---- mount --bind / umount emulation, fake /proc stat */
    {
        system("mkdir -p /tmp/pemu-test/rf/mnt2 && touch /tmp/pemu-test/rf/mnt2/orig");
        long r = pemu_mount("/mnt/h/sub", "/mnt2", PEMU_MS_BIND);
        CHECK(r == 0, "bind mount r=%ld", r);
        want(AT_FDCWD, "/mnt2/file", 1, "/tmp/pemu-test/hostdir/sub/file");
        r = pemu_mount("/mnt/h/sub", "/mnt2", PEMU_MS_BIND | PEMU_MS_REMOUNT); CHECK(r == 0, "remount r=%ld", r);
        r = pemu_mount("none", "/mnt2", 0); CHECK(r == -EPERM, "non-bind mount must stay EPERM, r=%ld", r);
        r = pemu_mount("/nonexistent", "/mnt2", PEMU_MS_BIND); CHECK(r == -ENOENT, "missing source r=%ld", r);
        r = pemu_mount("/mnt/h/sub", "/", PEMU_MS_BIND); CHECK(r == -EBUSY, "mount over / r=%ld", r);
        r = pemu_umount("/mnt2"); CHECK(r == 0, "umount r=%ld", r);
        want(AT_FDCWD, "/mnt2/orig", 1, "/tmp/pemu-test/rf/mnt2/orig");          /* the original directory is visible again */
        r = pemu_umount("/mnt2"); CHECK(r == -EINVAL, "second umount r=%ld", r);
        r = pemu_umount("/"); CHECK(r == -EINVAL, "umount / r=%ld", r);
        { char h[PEMU_HP]; int raw; CHECK(pemu_g2h("/mnt/h/sub", h, &raw) == 0 && !strcmp(h, "/tmp/pemu-test/hostdir/sub"), "-b binds still work after umount of another"); }
        struct stat fs;
        CHECK(pemu_fakeproc_stat("/proc/loadavg", &fs) == 0 && S_ISREG(fs.st_mode) && (fs.st_mode & 0777) == 0444, "fake proc stat");
        CHECK(pemu_fakeproc_stat("/proc/nonexistent_xyz", &fs) != 0, "unknown /proc file is not faked");
        CHECK(pemu_fakeproc_stat("/etc/passwd", &fs) != 0, "non-/proc path is not faked");
    }
    /* ---- statx of a fake /proc file, chroot into a subdirectory */
    {
        struct { unsigned char b[256]; } sx; (void) sx;
        system("mkdir -p /tmp/pemu-test/rf/jail/etc /tmp/pemu-test/rf/jail/work /tmp/pemu-test/rf/jail/mnt/h && echo j > /tmp/pemu-test/rf/jail/etc/j");
        long r = pemu_mount("/mnt/h/sub", "/jail/mnt/h", PEMU_MS_BIND); CHECK(r == 0, "bind into jail r=%ld", r);
        r = pemu_mount("/mnt/h/sub", "/mnt2", PEMU_MS_BIND); CHECK(r == 0, "bind outside jail r=%ld", r);
        CHECK(pemu_chdir("/jail/work") == 0, "chdir work");
        r = pemu_chroot("/nonexistent"); CHECK(r == -ENOENT, "chroot missing r=%ld", r);
        r = pemu_chroot("/usr/bin/sh"); CHECK(r == -ENOTDIR, "chroot file r=%ld", r);
        r = pemu_chroot("/jail"); CHECK(r == 0, "chroot r=%ld", r);
        CHECK(!strcmp(pemu_cwd, "/work"), "cwd clamped: %s", pemu_cwd);
        want(AT_FDCWD, "/etc/j", 1, "/tmp/pemu-test/rf/jail/etc/j");
        want(AT_FDCWD, "/../../etc/j", 1, "/tmp/pemu-test/rf/jail/etc/j");          /* .. cannot leave the new root */
        want(AT_FDCWD, "/mnt/h/file", 1, "/tmp/pemu-test/hostdir/sub/file");         /* bind below the jail survives, re-rooted */
        { char h[PEMU_HP]; pemu_path(AT_FDCWD, "/mnt2/file", 1, h, sizeof h, NULL); CHECK(!strncmp(h, "/tmp/pemu-test/rf/jail/", 23), "bind outside the jail is hidden: %s", h); }
        { char b[PATH_MAX]; getcwd(b, sizeof b); CHECK(!strcmp(b, "/tmp/pemu-test/rf/jail/work"), "host cwd %s", b); }
        CHECK(!strcmp(pemu_root, "/tmp/pemu-test/rf/jail"), "pemu_root %s", pemu_root);
        r = pemu_chroot("/"); CHECK(r == 0, "chroot / is a no-op r=%ld", r);
        r = pemu_chroot("/etc"); CHECK(r == 0, "nested chroot r=%ld", r);
        want(AT_FDCWD, "/j", 1, "/tmp/pemu-test/rf/jail/etc/j");
    }
    /* ---- pemu uninstall */
    {
        system("rm -rf /tmp/pemu-test/rfs; mkdir -p /tmp/pemu-test/rfs/deb/usr/bin /tmp/pemu-test/rfs/deb/ro/sub /tmp/pemu-test/keep && touch /tmp/pemu-test/keep/f /tmp/pemu-test/rfs/deb/usr/bin/sh /tmp/pemu-test/rfs/deb/ro/sub/f"
               " && ln -s /tmp/pemu-test/keep /tmp/pemu-test/rfs/deb/outside && ln -s /tmp/pemu-test/rfs/deb /tmp/pemu-test/rfs/lnk && mkdir -p /tmp/pemu-test/rfs/other/usr && chmod 555 /tmp/pemu-test/rfs/deb/ro/sub /tmp/pemu-test/rfs/deb/ro");
        setenv("PEMU_ROOTFS_DIR", "/tmp/pemu-test/rfs", 1);
        FILE *nul = freopen("/dev/null", "r", stdin); (void) nul;       /* stdin is not a terminal: confirmation must be refused */
        { char *u[] = { "pemu", "uninstall", "deb" }; CHECK(pemu_uninstall(3, u) == 1 && pemu_isdir("/tmp/pemu-test/rfs/deb"), "uninstall without -y must refuse"); }
        { char *u[] = { "pemu", "uninstall", "../keep", "-y" }; CHECK(pemu_uninstall(4, u) == 2 && pemu_isdir("/tmp/pemu-test/keep"), "path-like name rejected"); }
        { char *u[] = { "pemu", "uninstall", "..", "-y" }; CHECK(pemu_uninstall(4, u) == 2, ".. rejected"); }
        { char *u[] = { "pemu", "uninstall", "nope", "-y" }; CHECK(pemu_uninstall(4, u) == 1, "missing rootfs"); }
        { char *u[] = { "pemu", "uninstall", "lnk", "-y" }; CHECK(pemu_uninstall(4, u) == 0 && pemu_isdir("/tmp/pemu-test/rfs/deb"), "symlink: only the link goes"); }
        { char *u[] = { "pemu", "uninstall", "deb", "--yes" }; CHECK(pemu_uninstall(4, u) == 0 && access("/tmp/pemu-test/rfs/deb", F_OK) != 0, "rootfs removed (read-only dirs included)"); }
        CHECK(access("/tmp/pemu-test/keep/f", F_OK) == 0, "symlink target outside the rootfs must survive");
        CHECK(pemu_isdir("/tmp/pemu-test/rfs/other"), "other rootfs untouched");
        unsetenv("PEMU_ROOTFS_DIR");
    }
    /* ---- resolv.conf usability */
    {
        const char *cases[][2] = { { "", "0" }, { "# Generated by NetworkManager\n", "0" }, { "nameserver 127.0.0.53\noptions edns0\n", "0" },
            { "nameserver ::1\n", "0" }, { "nameserver 8.8.8.8\n", "1" }, { "# x\nnameserver 127.0.0.1\n  nameserver\t1.1.1.1\n", "1" }, { "nameservers 8.8.8.8\n", "0" } };
        for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
            FILE *f = fopen("/tmp/pemu-test/resolv.t", "w"); fputs(cases[i][0], f); fclose(f);
            CHECK(pemu_resolv_usable("/tmp/pemu-test/resolv.t") == (cases[i][1][0] == '1'), "resolv case %u", i);
        }
        CHECK(pemu_resolv_usable("/tmp/pemu-test/does-not-exist") == 0, "missing resolv.conf");
    }
    printf(fails ? "%d FAILURES\n" : "all harness tests passed\n", fails);
    return fails != 0;
}
