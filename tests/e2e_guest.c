/* Freestanding guest used by tests/e2e.sh. Raw syscalls only, so the same source runs as an AArch64 guest
 * (under `pemu`) and as an x86_64 guest (under `pemu_x64`), with no libc or rootfs userland needed. */
typedef unsigned long u64; typedef long i64;
#if defined(__aarch64__)
enum { S_read = 63, S_write = 64, S_openat = 56, S_close = 57, S_socket = 198, S_bind = 200, S_getsockname = 204, S_sendto = 206,
       S_recvfrom = 207, S_sendmsg = 211, S_openat2 = 437, S_mkdirat = 34, S_unlinkat = 35, S_symlinkat = 36, S_readlinkat = 78,
       S_getcwd = 17, S_chdir = 49, S_exit = 94, S_getuid = 174 };
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c,
                 x3 __asm__("x3") = d, x4 __asm__("x4") = e, x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#elif defined(__x86_64__)
enum { S_read = 0, S_write = 1, S_openat = 257, S_close = 3, S_socket = 41, S_bind = 49, S_getsockname = 51, S_sendto = 44,
       S_recvfrom = 45, S_sendmsg = 46, S_openat2 = 437, S_mkdirat = 258, S_unlinkat = 263, S_symlinkat = 266, S_readlinkat = 267,
       S_getcwd = 79, S_chdir = 80, S_exit = 231, S_getuid = 102 };
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 r10 __asm__("r10") = d, r8 __asm__("r8") = e, r9 __asm__("r9") = f;
    i64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#else
#error unsupported architecture
#endif
#define AT_FDCWD (-100)
static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc(S_write, 1, (i64) s, (i64) slen(s), 0, 0, 0); }
static void num(i64 v) {
    char b[24]; int i = 23; b[i] = 0; int neg = v < 0; u64 u = neg ? (u64) -v : (u64) v;
    do { b[--i] = (char) ('0' + u % 10); u /= 10; } while (u);
    if (neg) b[--i] = '-';
    out(b + i);
}
static void mem0(void *p, u64 n) { char *c = p; while (n--) *c++ = 0; }
static void cpy(char *d, const char *s) { while ((*d++ = *s++)); }
struct sa { unsigned short fam; char path[108]; };
struct iov { void *base; u64 len; };
struct mh { void *name; unsigned namelen, pad; struct iov *iov; u64 iovlen; void *ctl; u64 ctllen; int flags, pad2; };
struct how { u64 flags, mode, resolve; };

void _start(void) {
    char b[300]; i64 r, fd;
    fd = sc(S_openat, AT_FDCWD, (i64) "/etc/motd", 0, 0, 0, 0); r = sc(S_read, fd, (i64) b, 100, 0, 0, 0);
    out("motd="); if (r > 0) { b[r] = 0; out(b); } else num(r);
    fd = sc(S_openat, AT_FDCWD, (i64) "/etc/link", 0, 0, 0, 0); r = sc(S_read, fd, (i64) b, 100, 0, 0, 0);
    out("via symlink="); if (r > 0) { b[r] = 0; out(b); } else { num(r); out("\n"); }
    r = sc(S_getcwd, (i64) b, sizeof b, 0, 0, 0, 0); out("cwd="); out(b); out("\n");
    out("uid="); num(sc(S_getuid, 0, 0, 0, 0, 0, 0)); out("\n");

    i64 s = sc(S_socket, 1, 2, 0, 0, 0, 0), c = sc(S_socket, 1, 2, 0, 0, 0, 0);   /* AF_UNIX, SOCK_DGRAM */
    struct sa a, ca, g, f; mem0(&a, sizeof a); mem0(&ca, sizeof ca); a.fam = ca.fam = 1; cpy(a.path, "/tmp/s"); cpy(ca.path, "/tmp/c");
    out("bind="); num(sc(S_bind, s, (i64) &a, 2 + 7, 0, 0, 0)); out("\n"); sc(S_bind, c, (i64) &ca, 2 + 7, 0, 0, 0);
    unsigned gl = sizeof g; mem0(&g, sizeof g); sc(S_getsockname, s, (i64) &g, (i64) &gl, 0, 0, 0);
    out("sockname="); out(g.path); out("\n");
    sc(S_sendto, c, (i64) "hi", 2, 0, (i64) &a, 2 + 7);
    unsigned fl = sizeof f; mem0(&f, sizeof f); r = sc(S_recvfrom, s, (i64) b, sizeof b, 0, (i64) &f, (i64) &fl);
    out("recvfrom="); num(r); out(" from="); out(f.path); out("\n");
    struct iov iv = { "again", 5 }; struct mh m; mem0(&m, sizeof m); m.name = &a; m.namelen = 2 + 7; m.iov = &iv; m.iovlen = 1;
    out("sendmsg="); num(sc(S_sendmsg, c, (i64) &m, 0, 0, 0, 0)); out("\n");

    struct how h = { 0, 0, 4 /* RESOLVE_NO_SYMLINKS */ };
    out("openat2 nosymlinks on a symlink="); num(sc(S_openat2, AT_FDCWD, (i64) "/etc/link", (i64) &h, sizeof h, 0, 0)); out("\n");
    fd = sc(S_openat2, AT_FDCWD, (i64) "/etc/motd", (i64) &h, sizeof h, 0, 0); out("openat2 nosymlinks on a file="); out(fd >= 0 ? "ok" : "fail"); out("\n");

    out("mkdir="); num(sc(S_mkdirat, AT_FDCWD, (i64) "/tmp/dd", 0755, 0, 0, 0)); out("\n");
    out("symlink="); num(sc(S_symlinkat, (i64) "/etc/motd", AT_FDCWD, (i64) "/tmp/dd/ln", 0, 0, 0)); out("\n");
    r = sc(S_readlinkat, AT_FDCWD, (i64) "/tmp/dd/ln", (i64) b, sizeof b, 0, 0); out("readlink="); if (r > 0) { b[r] = 0; out(b); } out("\n");
    out("chdir="); num(sc(S_chdir, (i64) "/tmp/dd", 0, 0, 0, 0, 0)); sc(S_getcwd, (i64) b, sizeof b, 0, 0, 0, 0); out(" cwd="); out(b); out("\n");
    sc(S_chdir, (i64) "/", 0, 0, 0, 0, 0);
    out("unlink="); num(sc(S_unlinkat, AT_FDCWD, (i64) "/tmp/dd/ln", 0, 0, 0, 0)); out("\n");
    out("rmdir="); num(sc(S_unlinkat, AT_FDCWD, (i64) "/tmp/dd", 0x200, 0, 0, 0)); out("\n");

    fd = sc(S_openat, AT_FDCWD, (i64) "/proc/stat", 0, 0, 0, 0); r = sc(S_read, fd, (i64) b, 5, 0, 0, 0);
    out("proc/stat="); if (r == 5) { b[5] = 0; out(b); } else num(r); out("|\n");
    sc(S_exit, 0, 0, 0, 0, 0, 0);
    for (;;) { }
}
