/* Freestanding guest used by tests/deny.sh (AArch64 under `pemu`, x86_64 under `pemu_x64`; raw syscalls, no libc).
 * Every call is made so that it is harmless even if the refusal policy were missing: paths that cannot exist, invalid fds, a pid that
 * cannot exist, and unshare flags carrying a bit the kernel rejects with EINVAL before it looks at anything else.  (vhangup takes no argument and is therefore only covered by test_deny.)  Nothing is ever mounted,
 * moved, traced or attached.  Prints "name=RESULT" (negative errno, or "found" for a name_to_handle_at that resolved the path). */
typedef unsigned long u64; typedef long i64;
#if defined(__aarch64__)
enum { S_write = 64, S_exit = 94, S_ptrace = 117, S_setns = 268, S_unshare = 97, S_open_by_handle_at = 265, S_name_to_handle_at = 264, S_setsid = 157,
       S_process_vm_readv = 270, S_process_vm_writev = 271, S_kcmp = 272, S_bpf = 280, S_perf_event_open = 241, S_userfaultfd = 282, S_keyctl = 219, S_add_key = 217, S_request_key = 218,
       S_sethostname = 161, S_setdomainname = 162, S_settimeofday = 170, S_clock_settime = 112, S_clock_adjtime = 266, S_adjtimex = 171, S_vhangup = 58, S_syslog = 116, S_migrate_pages = 238, S_move_pages = 239 };
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c,
                 x3 __asm__("x3") = d, x4 __asm__("x4") = e, x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}
#elif defined(__x86_64__)
enum { S_write = 1, S_exit = 231, S_ptrace = 101, S_setns = 308, S_unshare = 272, S_open_by_handle_at = 304, S_name_to_handle_at = 303, S_setsid = 112,
       S_process_vm_readv = 310, S_process_vm_writev = 311, S_kcmp = 312, S_bpf = 321, S_perf_event_open = 298, S_userfaultfd = 323, S_keyctl = 250, S_add_key = 248, S_request_key = 249,
       S_sethostname = 170, S_setdomainname = 171, S_settimeofday = 164, S_clock_settime = 227, S_clock_adjtime = 305, S_adjtimex = 159, S_vhangup = 153, S_syslog = 103, S_migrate_pages = 256, S_move_pages = 279 };
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 r10 __asm__("r10") = d, r8 __asm__("r8") = e, r9 __asm__("r9") = f;
    i64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#else
#error unsupported architecture
#endif
/* the new mount API has the same numbers on every architecture */
enum { S_open_tree = 428, S_move_mount = 429, S_fsopen = 430, S_fsconfig = 431, S_fsmount = 432, S_fspick = 433, S_mount_setattr = 442, S_open_tree_attr = 467,
       S_quotactl_fd = 443, S_lsm_set_self_attr = 460, S_process_madvise = 440,
       S_pidfd_getfd = 438, S_landlock_create_ruleset = 444, S_landlock_add_rule = 445, S_landlock_restrict_self = 446, S_statmount = 457, S_listmount = 458 };
#define AT_FDCWD (-100)
#define NOPE "/pemu-deny-test-does-not-exist"
#define ONLY_IN_ROOTFS "/pemu-deny-test-only-in-rootfs"      /* tests/deny.sh creates it in the rootfs only */
static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc(S_write, 1, (i64) s, (i64) slen(s), 0, 0, 0); }
static void num(i64 v) {
    char b[24]; int i = 23; b[i] = 0; int neg = v < 0; u64 u = neg ? (u64) -v : (u64) v;
    do { b[--i] = (char) ('0' + u % 10); u /= 10; } while (u);
    if (neg) b[--i] = '-';
    out(b + i);
}
static void res(const char *name, i64 r) { out(name); out("="); num(r); out("\n"); }
struct fh { unsigned bytes; int type; unsigned char f[128]; };

void _start(void) {
    res("ptrace", sc(S_ptrace, 16 /*PTRACE_ATTACH*/, 0x7ffffff0 /* no such pid */, 0, 0, 0, 0));
    res("setns", sc(S_setns, -1, 0, 0, 0, 0, 0));
    /* unshare(NEW* | 1): bit 0 is no unshare flag, so a host that saw the call would answer EINVAL (-22) before doing anything */
    res("unshare NEWNS", sc(S_unshare, 0x20000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWCGROUP", sc(S_unshare, 0x2000000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWUTS", sc(S_unshare, 0x4000000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWIPC", sc(S_unshare, 0x8000000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWUSER", sc(S_unshare, 0x10000000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWPID", sc(S_unshare, 0x20000000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWNET", sc(S_unshare, 0x40000000 | 1, 0, 0, 0, 0, 0));
    res("unshare NEWTIME", sc(S_unshare, 0x80 | 1, 0, 0, 0, 0, 0));
    res("unshare no NEW flag (passes to the host)", sc(S_unshare, 1, 0, 0, 0, 0, 0));
    struct fh h = { 0, 0, {0} };
    res("open_by_handle_at", sc(S_open_by_handle_at, -1, (i64) &h, 0, 0, 0, 0));
    res("open_tree", sc(S_open_tree, AT_FDCWD, (i64) NOPE, 0, 0, 0, 0));
    res("move_mount", sc(S_move_mount, AT_FDCWD, (i64) NOPE, AT_FDCWD, (i64) NOPE, 0, 0));
    res("fsopen", sc(S_fsopen, (i64) "pemu-deny-test-no-such-fs", 0, 0, 0, 0, 0));
    res("fsconfig", sc(S_fsconfig, -1, 0, 0, 0, 0, 0));
    res("fsmount", sc(S_fsmount, -1, 0, 0, 0, 0, 0));
    res("fspick", sc(S_fspick, AT_FDCWD, (i64) NOPE, 0, 0, 0, 0));
    res("mount_setattr", sc(S_mount_setattr, AT_FDCWD, (i64) NOPE, 0, 0, 0, 0));
    res("open_tree_attr", sc(S_open_tree_attr, AT_FDCWD, (i64) NOPE, 0, 0, 0, 0));
    res("process_vm_readv", sc(S_process_vm_readv, 0x7ffffff0, 0, 0, 0, 0, 0));
    res("process_vm_writev", sc(S_process_vm_writev, 0x7ffffff0, 0, 0, 0, 0, 0));
    res("kcmp", sc(S_kcmp, 0x7ffffff0, 0x7ffffff0, 0, 0, 0, 0));
    res("pidfd_getfd", sc(S_pidfd_getfd, -1, -1, 0, 0, 0, 0));
    res("bpf", sc(S_bpf, 0x7fffffff, 0, 0, 0, 0, 0));
    res("perf_event_open", sc(S_perf_event_open, 0, 0x7ffffff0, 0, -1, 0, 0));
    res("userfaultfd", sc(S_userfaultfd, 0x7fffffff, 0, 0, 0, 0, 0));
    res("landlock_create_ruleset", sc(S_landlock_create_ruleset, 0, 0, 0, 0, 0, 0));
    res("landlock_add_rule", sc(S_landlock_add_rule, -1, 0, 0, 0, 0, 0));
    res("landlock_restrict_self", sc(S_landlock_restrict_self, -1, 0, 0, 0, 0, 0));
    res("statmount", sc(S_statmount, 0, 0, 0, 0, 0, 0));
    res("listmount", sc(S_listmount, 0, 0, 0, 0, 0, 0));
    res("keyctl", sc(S_keyctl, 0x7fffffff, 0, 0, 0, 0, 0));
    res("add_key", sc(S_add_key, 0, 0, 0, 0, 0, 0));
    res("request_key", sc(S_request_key, 0, 0, 0, 0, 0, 0));
    res("sethostname", sc(S_sethostname, 0x1, 0x7fffffff, 0, 0, 0, 0));
    res("setdomainname", sc(S_setdomainname, 0x1, 0x7fffffff, 0, 0, 0, 0));
    res("settimeofday", sc(S_settimeofday, 0x1, 0, 0, 0, 0, 0));
    res("clock_settime", sc(S_clock_settime, 0, 0x1, 0, 0, 0, 0));
    /* a struct timex asking for ADJ_SETOFFSET with an invalid tv_usec: the kernel would answer EINVAL / EPERM and never touch the clock */
    i64 tx[26]; for (int i = 0; i < 26; i++) tx[i] = 0;
    tx[0] = 0x100; tx[10] = 2000000000;
    res("clock_adjtime", sc(S_clock_adjtime, 0, (i64) tx, 0, 0, 0, 0));
    res("adjtimex", sc(S_adjtimex, (i64) tx, 0, 0, 0, 0, 0));
    /* the read-only forms (modes 0) must still work */
    tx[0] = 0; tx[10] = 0;
    out("clock_adjtime read-only="); out(sc(S_clock_adjtime, 0, (i64) tx, 0, 0, 0, 0) >= 0 ? "ok" : "FAILED"); out("\n");
    tx[0] = 0;
    out("adjtimex read-only="); out(sc(S_adjtimex, (i64) tx, 0, 0, 0, 0, 0) >= 0 ? "ok" : "FAILED"); out("\n");
    res("syslog", sc(S_syslog, 0x7fffffff, 0, 0, 0, 0, 0));
    res("quotactl_fd", sc(S_quotactl_fd, -1, 0, 0, 0, 0, 0));
    res("lsm_set_self_attr", sc(S_lsm_set_self_attr, 0x7fffffff, 0, 0, 0, 0, 0));
    res("migrate_pages", sc(S_migrate_pages, 0x7ffffff0, 0, 0, 0, 0, 0));
    res("move_pages", sc(S_move_pages, 0x7ffffff0, 0, 0, 0, 0, 0));
    res("process_madvise", sc(S_process_madvise, -1, 0, 0, 0, 0, 0));
    /* vhangup() would hang up the controlling terminal: leave it first (setsid), so that even a missing policy finds none to hang up */
    if (sc(S_setsid, 0, 0, 0, 0, 0, 0) >= 0) res("vhangup", sc(S_vhangup, 0, 0, 0, 0, 0, 0));
    else out("vhangup=setsid-failed\n");
    int mid = 0; i64 r;
    r = sc(S_name_to_handle_at, AT_FDCWD, (i64) NOPE, (i64) &h, (i64) &mid, 0, 0);
    res("name_to_handle_at on a missing path", r);
    h.bytes = 0;
    r = sc(S_name_to_handle_at, AT_FDCWD, (i64) ONLY_IN_ROOTFS, (i64) &h, (i64) &mid, 0, 0);
    out("name_to_handle_at on a rootfs-only path="); if (r == 0 || r == -75 /*EOVERFLOW*/ || r == -95 /*EOPNOTSUPP*/) out("found"); else num(r); out("\n");
    sc(S_exit, 0, 0, 0, 0, 0, 0);
    for (;;) { }
}
