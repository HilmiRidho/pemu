/* tests/test_pemu_fixes.c - regression tests for the second review round (see REVIEW.md, "Round 2").
 *   1. the openat2 default (off on Android) must survive `pemu login`'s clearenv() and reach re-exec'd children;
 *   2. openat2 gets a mode only when a file can be created (host O_TMPFILE, not a hard-coded x86 constant);
 *   3. fake root's temporary chmod (pemu_open_priv): inode pinned, threads serialised, mode always restored;
 *   4. bind table: mount/umount reuse slots, `later wins` survives reuse, readers never see a half-edited entry
 *      (stress with a writer thread and with chroot), RESOLVE_CACHED reaches the kernel.
 * Built by build.sh together with the other host-native tests; no guest binaries needed. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
/* hs() is the test's own: it also records the `how` struct of the last openat2 so the tests can see what reached the kernel */
static uint64_t last_how[3]; static int last_how_valid;
static long hs(long nr, long a, long b, long c_, long d, long e, long f) {
    if (nr == SYS_openat2 && c_) { memcpy(last_how, (const void *) c_, sizeof last_how); last_how_valid = 1; }
    long r = syscall(nr, a, b, c_, d, e, f); return r == -1 ? -errno : r;
}
#include "pemu_root.h"
#ifdef PEMU_TEST_ORIG                                  /* compile against the code before the round-2 fixes, to see the tests fail there */
static volatile unsigned pemu_seq; static volatile int pemu_privlk;
static void pemu_atfork_child(void) { pemu_lk = 0; }
#define PEMU_OPEN_HAS_MODE(f) 0
#endif
#define T "/tmp/pemu-fixes"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

static void mk_tree(void) {
    if (chdir("/") != 0) { }                              /* the previous tree (and the cwd inside it) is about to go */
    system("rm -rf " T "; mkdir -p " T "/rf/usr/bin " T "/rf/tmp " T "/rf/m " T "/rf/p " T "/rf/s " T "/rf/longer-name " T "/rf/jail/mnt/h " T "/rf/mnt/h "
           T "/hostA " T "/hostB && printf x > " T "/rf/usr/bin/sh && touch " T "/hostA/x " T "/hostB/x && chmod -R a+rwX " T);
}
static int setup(char **av, int n) {
    pemu_npb = 0; pemu_nb = 0; pemu_fakeroot = 0; pemu_o2 = -1;
    for (int a = 1; a < n; a++) { int r = pemu_opt(n, av, &a); if (r <= 0) return -1; }
    return pemu_finish();
}
static void want_h(const char *g, const char *exp) {
    char h[PEMU_HP]; int raw; long r = pemu_g2h(g, h, &raw);
    CHECK(r == 0 && !strcmp(h, exp), "g2h('%s') = r=%ld '%s', expected '%s'", g, r, r == 0 ? h : "-", exp);
}
static void base_table(void) {
    char *a[] = { "pemu", "--no-default-binds", "-r", T "/rf", "-b", T "/hostA:/ha", "-b", T "/hostB:/hb", "--root-id" };
    CHECK(setup(a, 9) == 0, "setup");
}

/* ---- 1. the openat2 default is decided once, before the environment is cleared ---- */
static void t_secure_default(void) {
    pid_t p = fork();
    if (p == 0) {
        fails = 0;                                      /* the child reports its own failures only */
        setenv("ANDROID_ROOT", "/system", 1); unsetenv("PEMU_SECURE_OPEN"); pemu_o2 = -1;
        setenv("PEMU_ROOTFS_DIR", T, 1);
        char *argv[] = { "pemu", "login", "rf", "true", NULL }; char **av = argv; int ac = 4;
        int rc = pemu_login(&ac, &av);
        CHECK(rc == -1, "pemu_login returned %d", rc);
        CHECK(getenv("ANDROID_ROOT") == NULL, "login is expected to clear the environment");
        CHECK(pemu_secure() == 0, "after `pemu login` on Android the race-free openat2 path must still be OFF (got %d)", pemu_secure());
        /* the re-exec'd pemu gets the decision as an option, so a guest that scrubs its environment cannot flip it */
        pemu_npb = 0;
        int a; for (a = 1; a < ac; a++) { if (!strcmp(av[a], "--")) break; int r = pemu_opt(ac, av, &a); if (r != 1) { CHECK(0, "login option %s", av[a]); break; } }
        CHECK(pemu_finish() == 0, "finish after login");
        char *nv[256]; int k = pemu_reexec_opts(nv, 0), found = 0;
        for (int i = 0; i + 1 < k; i++) if (!strcmp(nv[i], "--secure-open")) found = !strcmp(nv[i + 1], "0");
        CHECK(found, "re-exec options must carry --secure-open 0");
        CHECK(k <= pemu_reexec_count(), "reexec count %d > %d", k, pemu_reexec_count());
        char *g[300]; int m = 0; g[m++] = "pemu"; for (int i = 0; i < k; i++) g[m++] = nv[i];
        clearenv(); pemu_o2 = -1; pemu_npb = 0; pemu_nb = 0;
        for (a = 1; a < m; a++) { int r = pemu_opt(m, g, &a); if (r != 1) { CHECK(0, "re-exec option %s", g[a]); break; } }
        CHECK(pemu_secure() == 0, "re-exec'd pemu with a scrubbed environment must keep openat2 OFF (got %d)", pemu_secure());
        /* explicit override still wins, and a plain Linux host (no ANDROID_ROOT) still defaults to ON */
        pemu_o2 = -1; setenv("PEMU_SECURE_OPEN", "1", 1); CHECK(pemu_secure() == 1, "PEMU_SECURE_OPEN=1");
        pemu_o2 = -1; unsetenv("PEMU_SECURE_OPEN"); CHECK(pemu_secure() == 1, "no ANDROID_ROOT: default on");
        fflush(stdout); _exit(fails != 0);
    }
    int st = 0; waitpid(p, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "secure-default child failed (status %d)", st);
}

/* ---- 2. mode only when a file can be created; RESOLVE_CACHED reaches the kernel ---- */
static void t_open_mode(void) {
    for (int mode = 1; mode >= 0; mode--) {
        pemu_o2 = mode;
        /* a raw `openat` caller (Go, hand-written asm) leaves junk in the mode register; the kernel rejects a mode without O_CREAT */
        long fd = pemu_open(AT_FDCWD, "/usr", O_RDONLY | O_DIRECTORY, 0x1ed, 0);
        CHECK(fd >= 0, "O_DIRECTORY open with a junk mode (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        fd = pemu_open(AT_FDCWD, "/usr/bin/sh", O_RDONLY, 0x1ed, 0);
        CHECK(fd >= 0, "plain open with a junk mode (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        fd = pemu_open(AT_FDCWD, "/tmp/created", O_CREAT | O_WRONLY, 0640, 0);
        struct stat st; mode_t um = umask(022); umask(um);
        CHECK(fd >= 0 && stat(T "/rf/tmp/created", &st) == 0 && (st.st_mode & 0777) == (0640 & ~um), "O_CREAT keeps its mode (secure=%d) fd=%ld", mode, fd);
        if (fd >= 0) close((int) fd);
        unlink(T "/rf/tmp/created");
        fd = pemu_open(AT_FDCWD, "/tmp", O_TMPFILE | O_WRONLY, 0600, 0);
        CHECK(fd >= 0 || fd == -EOPNOTSUPP || fd == -EISDIR, "O_TMPFILE must carry its mode (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        /* an explicit openat2 keeps the kernel's own rule */
        uint64_t how[3] = { O_RDONLY, 0644, 0 };
        CHECK(pemu_openat2(AT_FDCWD, "/usr/bin/sh", how, sizeof how) == -EINVAL, "openat2: mode without O_CREAT is EINVAL (secure=%d)", mode);
        how[1] = 0; how[2] = PEMU_RS_NO_MAGICLINK; fd = pemu_openat2(AT_FDCWD, "/usr/bin/sh", how, sizeof how);
        CHECK(fd >= 0, "openat2 NO_MAGICLINKS (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
    }
    /* RESOLVE_CACHED: forwarded (README says so).  pemu's own walk stat()s the path first, so on a live kernel it never answers
     * EAGAIN here; what must hold is that the flag arrives, and that a missing file is still ENOENT. */
    for (int mode = 1; mode >= 0; mode--) {
        pemu_o2 = mode; last_how_valid = 0;
        uint64_t how[3] = { O_RDONLY, 0, PEMU_RS_CACHED };
        long fd = pemu_openat2(AT_FDCWD, "/usr/bin/sh", how, sizeof how);
        CHECK(fd >= 0 || fd == -EAGAIN || fd == -EINVAL, "openat2 CACHED (secure=%d) fd=%ld", mode, fd); if (fd >= 0) close((int) fd);
        CHECK(last_how_valid && (last_how[2] & PEMU_RS_CACHED), "RESOLVE_CACHED must reach the kernel (secure=%d, how[2]=%#llx)", mode, (unsigned long long) last_how[2]);
        fd = pemu_openat2(AT_FDCWD, "/usr/bin/none", how, sizeof how);
        CHECK(fd == -ENOENT || fd == -EAGAIN, "CACHED on a missing file (secure=%d) fd=%ld", mode, fd);
    }
    pemu_o2 = -1;
}

/* ---- 3. fake root: temporary chmod ---- */
struct privarg { int ok, bad; };
static void *priv_worker(void *p) {
    struct privarg *a = p;
    for (int i = 0; i < 300; i++) {
        long fd = pemu_open(AT_FDCWD, "/tmp/ro", O_WRONLY | O_APPEND, 0, 0);
        if (fd >= 0) { a->ok++; close((int) fd); } else a->bad++;
    }
    return NULL;
}
static void t_open_priv(void) {
    pid_t p = fork();
    if (p == 0) {
        fails = 0;
        /* an unprivileged host user: a root-run test would never reach pemu_open_priv */
        if (geteuid() == 0) {                           /* as root, drop to "nobody"; as a normal user (CI) we already are one */
            if (system("chown -R 65534:65534 " T) != 0) { }
            if (setgid(65534) != 0 || setuid(65534) != 0) { printf("skip: cannot drop root\n"); _exit(0); }
        }
        char *a[] = { "pemu", "--no-default-binds", "-r", T "/rf", "--root-id" };
        CHECK(setup(a, 5) == 0 && PEMU_PRIV(), "fake root");
        int f = open(T "/rf/tmp/ro", O_CREAT | O_WRONLY, 0440); if (f >= 0) close(f); chmod(T "/rf/tmp/ro", 0440);
        long fd = pemu_open(AT_FDCWD, "/tmp/ro", O_WRONLY, 0, 0);
        struct stat st; stat(T "/rf/tmp/ro", &st);
        CHECK(fd >= 0, "root-like open of a 0440 file must succeed, fd=%ld", fd); if (fd >= 0) close((int) fd);
        CHECK((st.st_mode & 07777) == 0440, "mode must be restored, is %o", st.st_mode & 07777);
        /* several threads on the same file: none may fail spuriously (a thread that saw the temporary mode used to give up) and the
         * final mode must be the original one (it used to be possible to "restore" the temporary one for good) */
        pthread_t th[4]; struct privarg pa[4] = {{0, 0}};
        for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, priv_worker, &pa[i]);
        int ok = 0, bad = 0; for (int i = 0; i < 4; i++) { pthread_join(th[i], NULL); ok += pa[i].ok; bad += pa[i].bad; }
        stat(T "/rf/tmp/ro", &st);
        CHECK(bad == 0, "%d of %d concurrent root-like opens failed", bad, ok + bad);
        CHECK((st.st_mode & 07777) == 0440, "mode after concurrent opens is %o", st.st_mode & 07777);
        /* a read-only open of a 0200 file; and a file that is already open-able is untouched */
        chmod(T "/rf/tmp/ro", 0200); fd = pemu_open(AT_FDCWD, "/tmp/ro", O_RDONLY, 0, 0); stat(T "/rf/tmp/ro", &st);
        CHECK(fd >= 0 && (st.st_mode & 07777) == 0200, "O_RDONLY on 0200: fd=%ld mode %o", fd, st.st_mode & 07777); if (fd >= 0) close((int) fd);
        /* the path is swapped for a symlink to another file while no open is running: the chmod must not follow it later */
        fflush(stdout); _exit(fails != 0);
    }
    int st = 0; waitpid(p, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "open_priv child failed (status %d)", st);
}

/* ---- 4. the bind table ---- */
static void t_slots(void) {
    base_table();
    for (int i = 0; i < 300; i++) {                      /* the table has 64 slots: cycles must reuse them */
        long r = pemu_mount("/ha", "/m", PEMU_MS_BIND);
        if (r != 0) { CHECK(0, "mount #%d r=%ld (table exhausted?)", i, r); break; }
        char h[PEMU_HP]; int raw; if (pemu_g2h("/m/x", h, &raw) != 0 || strcmp(h, T "/hostA/x")) { CHECK(0, "mount #%d not in effect: %s", i, h); break; }
        r = pemu_umount("/m"); if (r != 0) { CHECK(0, "umount #%d r=%ld", i, r); break; }
    }
    want_h("/m/x", T "/rf/m/x");
    /* the table really is full when 63 mounts are live: ENOSPC, and one umount makes room again */
    int n = 0; long r = 0; while (n < 70 && (r = pemu_mount("/ha", "/m", PEMU_MS_BIND)) == 0) n++;
    CHECK(r == -ENOSPC && n >= 50 && n <= PEMU_MAXB, "full table: %d mounts, r=%ld", n, r);
    CHECK(pemu_umount("/m") == 0 && pemu_mount("/hb", "/m", PEMU_MS_BIND) == 0, "room again after one umount");
    want_h("/m/x", T "/hostB/x");
    while (pemu_umount("/m") == 0) { }
    /* "later wins" must hold when the later mount sits in a LOWER slot than the earlier one */
    base_table();
    CHECK(pemu_mount("/ha", "/p", PEMU_MS_BIND) == 0, "mount /p");          /* slot s */
    CHECK(pemu_mount("/ha", "/m", PEMU_MS_BIND) == 0, "mount /m = A");      /* slot s+1 */
    CHECK(pemu_umount("/p") == 0, "umount /p");                               /* slot s is free again */
    CHECK(pemu_mount("/hb", "/m", PEMU_MS_BIND) == 0, "mount /m = B");      /* reuses slot s: older slot, newer mount */
    want_h("/m/x", T "/hostB/x");
    CHECK(pemu_umount("/m") == 0, "umount top"); want_h("/m/x", T "/hostA/x");   /* the newest goes first, the older one shows again */
    CHECK(pemu_umount("/m") == 0, "umount bottom"); want_h("/m/x", T "/rf/m/x");
    /* a child started by re-exec must rebuild the same order */
    CHECK(pemu_mount("/ha", "/m", PEMU_MS_BIND) == 0 && pemu_mount("/hb", "/p", PEMU_MS_BIND) == 0 && pemu_umount("/m") == 0 && pemu_mount("/hb", "/m", PEMU_MS_BIND) == 0, "remount");
    char *nv[256]; int k = pemu_reexec_opts(nv, 0); char *g[300]; int m = 0; g[m++] = "pemu"; for (int i = 0; i < k; i++) g[m++] = nv[i];
    CHECK(setup(g, m) == 0, "rebuild from re-exec options");
    want_h("/m/x", T "/hostB/x"); want_h("/p/x", T "/hostB/x"); want_h("/ha/x", T "/hostA/x");
}

static volatile int stop_readers; static volatile int anomalies; static char anomaly_msg[PEMU_HP + 512];
struct rd { const char *g; const char *a, *b; };
static void *reader(void *p) {
    struct rd *q = p; char h[PEMU_HP]; int raw;
    while (!stop_readers) {
        if (pemu_g2h(q->g, h, &raw) != 0 || (strcmp(h, q->a) && strcmp(h, q->b))) { if (!anomalies++) snprintf(anomaly_msg, sizeof anomaly_msg, "%s -> '%s' (allowed '%s' / '%s')", q->g, h, q->a, q->b); }
        char g[PATH_MAX]; if (pemu_h2g(T "/hostA/x", g) != 0 && !stop_readers) { if (!anomalies++) snprintf(anomaly_msg, sizeof anomaly_msg, "h2g lost hostA"); }
    }
    return NULL;
}
static void *toggler(void *p) {
    (void) p;
    while (!stop_readers) {                              /* different guest-path lengths in the same slot: a torn gl / g pair would map the wrong host dir */
        pemu_mount("/ha", "/s", PEMU_MS_BIND); pemu_umount("/s");
        pemu_mount("/hb", "/longer-name", PEMU_MS_BIND); pemu_umount("/longer-name");
    }
    return NULL;
}
static void t_stress(void) {
    base_table();
    struct rd r[3] = { { "/s/x", T "/rf/s/x", T "/hostA/x" }, { "/longer-name/x", T "/rf/longer-name/x", T "/hostB/x" }, { "/ha/x", T "/hostA/x", T "/hostA/x" } };
    pthread_t rt[3], wt; stop_readers = 0; anomalies = 0;
    for (int i = 0; i < 3; i++) pthread_create(&rt[i], NULL, reader, &r[i]);
    pthread_create(&wt, NULL, toggler, NULL);
    usleep(1200 * 1000); stop_readers = 1;
    for (int i = 0; i < 3; i++) pthread_join(rt[i], NULL);
    pthread_join(wt, NULL);
    CHECK(anomalies == 0, "readers saw an inconsistent table %d times: %s", anomalies, anomaly_msg);
    /* chroot while readers walk: each lookup sees the old table or the new one, nothing in between */
    pid_t p = fork();
    if (p == 0) {
        fails = 0;
        base_table();
        CHECK(pemu_mount("/ha", "/jail/mnt/h", PEMU_MS_BIND) == 0, "bind into the jail");
        struct rd c[2] = { { "/mnt/h/x", T "/rf/mnt/h/x", T "/hostA/x" }, { "/jail/mnt/h/x", T "/hostA/x", T "/rf/jail/jail/mnt/h/x" } };
        pthread_t ct[2]; stop_readers = 0; anomalies = 0;
        for (int i = 0; i < 2; i++) pthread_create(&ct[i], NULL, reader, &c[i]);
        usleep(100 * 1000);
        CHECK(pemu_chroot("/jail") == 0, "chroot");
        usleep(300 * 1000); stop_readers = 1;
        for (int i = 0; i < 2; i++) pthread_join(ct[i], NULL);
        CHECK(anomalies == 0 || (anomalies == 1 && !strncmp(anomaly_msg, "h2g", 3)), "chroot: readers saw a mixed table: %s", anomaly_msg);
        want_h("/mnt/h/x", T "/hostA/x");
        fflush(stdout); _exit(fails != 0);
    }
    int st = 0; waitpid(p, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "chroot stress child failed (status %d)", st);
}
/* fork while a writer is mid-edit: the child must not hang on the seqlock or the lock */
static void t_fork_midedit(void) {
    base_table();
    pemu_seq |= 1u; pemu_lk = 1; pemu_privlk = 1;        /* as if another thread were in the middle of an edit at fork time */
    pemu_atfork_child();
    CHECK(!(pemu_seq & 1u) && pemu_lk == 0 && pemu_privlk == 0, "fork child: seqlock/locks must be released (seq=%u lk=%d)", pemu_seq, pemu_lk);
    want_h("/ha/x", T "/hostA/x");
}


/* ---- 5. (round 3) a mutating call on a direct child of the rootfs root: the name was cut off together with the parent, so
 *         mkdir("/d") hit the root itself (EEXIST) and lchown("/l") changed the owner of the rootfs root ---- */
static void t_top_level(void) {
    struct stat r0, r1, st;
    base_table();
    CHECK(stat(T "/rf", &r0) == 0, "stat rootfs");
    CHECK(pemu_callat(SYS_mkdirat, AT_FDCWD, "/newdir", 0, 0755, 0, 0, 0) == 0 && stat(T "/rf/newdir", &st) == 0, "mkdir /newdir in the rootfs root");
    CHECK(pemu_symlinkat("/x", AT_FDCWD, "/newlink") == 0 && lstat(T "/rf/newlink", &st) == 0, "symlink /newlink in the rootfs root");
    pemu_callat(SYS_fchownat, AT_FDCWD, "/newlink", 0, 5, 5, AT_SYMLINK_NOFOLLOW, 0);
    CHECK(stat(T "/rf", &r1) == 0 && r1.st_uid == r0.st_uid && r1.st_gid == r0.st_gid, "lchown of a top-level name must not touch the rootfs root");
    CHECK(pemu_callat(SYS_unlinkat, AT_FDCWD, "/newlink", 0, 0, 0, 0, 0) == 0 && lstat(T "/rf/newlink", &st) < 0, "unlink /newlink");
}
/* ---- 6. (round 3) fake owners: a chown by a fake root is remembered (xattr) and shown by stat ---- */
static long own_stat(const char *g, struct pemu_own_ov *ov) {
    struct stat st; memset(ov, 0, sizeof *ov);
    return pemu_callat_ov(SYS_newfstatat, AT_FDCWD, g, 1, (long) &st, 0, 0, 0, ov);
}
static void t_fake_owner(void) {
    struct pemu_own_ov ov; int fd;
    base_table(); pemu_own_off = -1; unsetenv("PEMU_FAKE_OWNER");
    fd = open(T "/rf/o1", O_CREAT | O_WRONLY, 0644); if (fd >= 0) close(fd);
    fd = open(T "/rf/o2", O_CREAT | O_WRONLY, 0644); if (fd >= 0) close(fd);
    if (setxattr(T "/rf/o1", PEMU_OWN_XA, "1:1", 3, 0) != 0 && (errno == ENOTSUP || errno == EPERM || errno == EACCES)) { printf("fake owner: no user xattrs here, skipped\n"); return; }
    removexattr(T "/rf/o1", PEMU_OWN_XA);
    CHECK(own_stat("/o1", &ov) == 0 && !ov.valid, "no record yet: the normal mapping applies");
    CHECK(pemu_callat(SYS_fchownat, AT_FDCWD, "/o1", 1, 1000, 100, 0, 0) == 0, "chown 1000:100 succeeds");
    CHECK(own_stat("/o1", &ov) == 0 && ov.valid && ov.uid == 1000 && ov.gid == 100, "stat shows 1000:100 (valid=%d %u:%u)", ov.valid, ov.uid, ov.gid);
    CHECK(pemu_callat(SYS_fchownat, AT_FDCWD, "/o1", 1, (long) (uint32_t) -1, 50, 0, 0) == 0, "chown -1:50");
    CHECK(own_stat("/o1", &ov) == 0 && ov.valid && ov.uid == 1000 && ov.gid == 50, "-1 leaves the uid alone (%u:%u)", ov.uid, ov.gid);
    CHECK(pemu_callat(SYS_fchownat, AT_FDCWD, "/o1", 1, (long) (uint32_t) -1, (long) (uint32_t) -1, 0, 0) == 0 && own_stat("/o1", &ov) == 0 && ov.uid == 1000 && ov.gid == 50, "chown -1:-1 changes nothing");
    fd = open(T "/rf/o2", O_RDONLY); pemu_own_chown(NULL, 0, fd, 7, 8); pemu_own_fstat(fd, &ov);
    CHECK(ov.valid && ov.uid == 7 && ov.gid == 8, "fchown by descriptor is remembered (%u:%u)", ov.uid, ov.gid); close(fd);
    CHECK(own_stat("/o2", &ov) == 0 && ov.valid && ov.uid == 7, "the record is on the inode, not on the open file");
    CHECK(rename(T "/rf/o2", T "/rf/o3") == 0 && own_stat("/o3", &ov) == 0 && ov.valid && ov.gid == 8, "the record survives a rename");
    setxattr(T "/rf/o3", PEMU_OWN_XA, "junk", 4, 0);
    CHECK(own_stat("/o3", &ov) == 0 && !ov.valid, "a malformed record is ignored");
    setxattr(T "/rf/o3", PEMU_OWN_XA, "1:2x", 4, 0);
    CHECK(own_stat("/o3", &ov) == 0 && !ov.valid, "trailing junk is ignored");
    setxattr(T "/rf/o3", PEMU_OWN_XA, "-1:2", 4, 0);
    CHECK(own_stat("/o3", &ov) == 0 && !ov.valid, "a sign is ignored");
    pemu_set_ids(5, 5);                                  /* not root any more: nothing may be recorded */
    pemu_callat(SYS_fchownat, AT_FDCWD, "/o3", 1, 9, 9, 0, 0);
    removexattr(T "/rf/o3", PEMU_OWN_XA);
    pemu_callat(SYS_fchownat, AT_FDCWD, "/o3", 1, 9, 9, 0, 0);
    CHECK(own_stat("/o3", &ov) == 0 && !ov.valid, "an unprivileged guest records nothing");
    pemu_set_ids(0, 0); pemu_own_off = 1;                /* PEMU_FAKE_OWNER=0 */
    pemu_callat(SYS_fchownat, AT_FDCWD, "/o3", 1, 4, 4, 0, 0);
    CHECK(own_stat("/o3", &ov) == 0 && !ov.valid, "switched off: nothing recorded, nothing shown");
    pemu_own_off = -1; setenv("PEMU_FAKE_OWNER", "0", 1);
    CHECK(!pemu_own_on(), "PEMU_FAKE_OWNER=0 switches the feature off"); unsetenv("PEMU_FAKE_OWNER");
    pemu_own_off = -1; pemu_on = 0;
    CHECK(!pemu_own_on(), "no rootfs: no fake owners"); pemu_on = 1;
}
/* ---- 6. set-uid file with a fake owner; partial chown from two threads ---- */
static const char *chown_path; static pthread_barrier_t chown_bar;
static void *chown_half(void *arg) {
    int uid_half = arg != NULL;
    pthread_barrier_wait(&chown_bar);
    if (uid_half) pemu_own_chown(chown_path, 1, -1, 1000, (uint32_t) -1);
    else          pemu_own_chown(chown_path, 1, -1, (uint32_t) -1, 50);
    return NULL;
}
static void t_owner_suid_lock(void) {
    struct pemu_idsave sv; int fd;
    base_table(); pemu_own_off = -1; unsetenv("PEMU_FAKE_OWNER"); pemu_set_ids(0, 0);
    fd = open(T "/rf/su", O_CREAT | O_WRONLY, 04755); if (fd >= 0) close(fd);
    chmod(T "/rf/su", 04755 | S_ISGID);
    if (setxattr(T "/rf/su", PEMU_OWN_XA, "1:1", 3, 0) != 0 && (errno == ENOTSUP || errno == EPERM || errno == EACCES)) { printf("suid / lock: no user xattrs here, skipped\n"); return; }
    removexattr(T "/rf/su", PEMU_OWN_XA);
    /* no record: effective and saved id become 0, as before */
    pemu_set_ids(0, 0); pemu_uid[PEMU_E] = pemu_uid[PEMU_S] = 77; pemu_gid[PEMU_E] = pemu_gid[PEMU_S] = 78;
    pemu_exec_ids(T "/rf/su", &sv);
    CHECK(pemu_uid[PEMU_E] == 0 && pemu_uid[PEMU_S] == 0 && pemu_gid[PEMU_E] == 0 && pemu_gid[PEMU_S] == 0, "no record: set-uid / set-gid give 0 (%u/%u %u/%u)", pemu_uid[PEMU_E], pemu_uid[PEMU_S], pemu_gid[PEMU_E], pemu_gid[PEMU_S]);
    pemu_exec_ids_undo(&sv);
    CHECK(pemu_uid[PEMU_E] == 77 && pemu_gid[PEMU_S] == 78, "a failed exec restores the ids");
    /* recorded owner 1000:100: that is the new id */
    pemu_set_ids(0, 0); pemu_own_chown(T "/rf/su", 1, -1, 1000, 100);
    pemu_exec_ids(T "/rf/su", &sv);
    CHECK(pemu_uid[PEMU_E] == 1000 && pemu_uid[PEMU_S] == 1000 && pemu_gid[PEMU_E] == 100 && pemu_gid[PEMU_S] == 100, "set-uid file owned by 1000:100 gives 1000:100 (%u/%u %u/%u)", pemu_uid[PEMU_E], pemu_uid[PEMU_S], pemu_gid[PEMU_E], pemu_gid[PEMU_S]);
    CHECK(pemu_uid[PEMU_R] == 0 && pemu_gid[PEMU_R] == 0, "the real ids stay");
    pemu_exec_ids_undo(&sv);
    /* feature off: the old answer */
    pemu_set_ids(0, 0); pemu_own_off = 1;
    pemu_exec_ids(T "/rf/su", &sv);
    CHECK(pemu_uid[PEMU_E] == 0 && pemu_gid[PEMU_E] == 0, "PEMU_FAKE_OWNER=0: 0 as before");
    pemu_exec_ids_undo(&sv); pemu_own_off = -1;
    /* a file without the set-uid bit is not touched */
    chmod(T "/rf/su", 0755); pemu_set_ids(0, 0); pemu_uid[PEMU_E] = 5;
    pemu_exec_ids(T "/rf/su", &sv);
    CHECK(pemu_uid[PEMU_E] == 5, "no set-uid bit: ids unchanged"); pemu_exec_ids_undo(&sv);
    pemu_set_ids(0, 0);

    /* two threads: chown(1000,-1) and chown(-1,50) on the same file must both stick */
    int lost = 0, n = 20000;
    chown_path = T "/rf/lk"; fd = open(chown_path, O_CREAT | O_WRONLY, 0644); if (fd >= 0) close(fd);
    for (int i = 0; i < n; i++) {
        struct pemu_own_ov ov; pthread_t a, b;
        removexattr(chown_path, PEMU_OWN_XA); pemu_own_chown(chown_path, 1, -1, 1, 1);
        pthread_barrier_init(&chown_bar, NULL, 2);
        pthread_create(&a, NULL, chown_half, (void *) 1); pthread_create(&b, NULL, chown_half, NULL);
        pthread_join(a, NULL); pthread_join(b, NULL); pthread_barrier_destroy(&chown_bar);
        if (!(pemu_own_get(chown_path, 1, -1, &ov.uid, &ov.gid) == 0 && ov.uid == 1000 && ov.gid == 50)) lost++;
    }
    CHECK(lost == 0, "partial chowns from two threads lost %d of %d updates", lost, n);
    /* a lock left set by another thread at fork time must not hang the child */
    pemu_ownlk = 1; pemu_atfork_child();
    CHECK(pemu_ownlk == 0, "the fork child releases the owner lock");
}
int main(void) {
    mk_tree();
    t_secure_default();
    mk_tree(); base_table();
    t_open_mode();
    mk_tree();
    t_open_priv();
    mk_tree();
    t_slots();
    t_stress();
    t_fork_midedit();
    mk_tree(); t_top_level();
    mk_tree(); t_fake_owner();
    mk_tree(); t_owner_suid_lock();
    printf(fails ? "%d FAILURES\n" : "all round-2 fix tests passed\n", fails);
    return fails != 0;
}
