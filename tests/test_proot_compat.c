/* Behaviour that pemu's built-in proot takes from proot 5.5.0: bindings, placeholders, fake ids, cwd, exit, and the optional
 * features (-i -k -v -b -w --kill-on-exit, -l link2symlink).  The options are pemu's own, only what they do follows proot.
 * The resolver state is global, so every scenario runs in its own forked child. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/stat.h>
static long hs(long nr, long a, long b, long c_, long d, long e, long f) { long r = syscall(nr,a,b,c_,d,e,f); return r == -1 ? -errno : r; }
#include "pemu_root.h"

static int fails, cur_fails;
#define CHECK(c, ...) do { if (!(c)) { cur_fails++; printf("FAIL %s line %d: ", cur_name, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char *cur_name;
#define T "/tmp/pemu-proot-test"
static void mk(const char *cmd) { if (system(cmd)) { } }
static void fresh(void) {
    mk("rm -rf " T "; mkdir -p " T "/rf/usr/bin " T "/rf/etc " T "/rf/tmp " T "/rf/root " T "/host/a/b " T "/host2"
       " && echo one > " T "/host/f1 && echo two > " T "/host2/f2 && ln -s /usr/bin " T "/rf/bin && printf x > " T "/rf/usr/bin/sh && chmod +x " T "/rf/usr/bin/sh"
       " && ln -s /real " T "/rf/link && mkdir -p " T "/rf/real");
}
static int run(const char *name, void (*fn)(void)) {
    cur_name = name; fflush(stdout);
    pid_t p = fork();
    if (p == 0) { cur_fails = 0; fn(); fflush(stdout); _exit(cur_fails ? 1 : 0); }
    int st; waitpid(p, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st)) { fails++; if (!WIFEXITED(st)) printf("FAIL %s: crashed\n", name); }
    return 0;
}
/* run pemu's own option parser over av[1..n-1] (as main() does), then pemu_finish() */
static int setup(char **av, int n) {
    pemu_npb = 0;
    for (int a = 1; a < n; a++) { int r = pemu_opt(n, av, &a); if (r <= 0) return -1; }
    return pemu_finish();
}
#define NDB "--no-default-binds"

static void t_ids_user(void) {                       /* -i 123:456: an unprivileged identity */
    pemu_set_ids(123, 456);
    CHECK(pemu_idcall(PEMU_GETUID, 0, 0, 0) == 123 && pemu_idcall(PEMU_GETGID, 0, 0, 0) == 456, "ids");
    CHECK(pemu_idcall(PEMU_SETUID, 0, 0, 0) == -EPERM, "setuid(0) must fail for a normal user");
    CHECK(pemu_idcall(PEMU_SETUID, 123, 0, 0) == 0, "setuid(own)");
    CHECK(pemu_idcall(PEMU_SETRESUID, 7, 0, 0) == -EPERM, "setresuid(7)");
    CHECK(pemu_idcall(PEMU_SETREUID, (uint64_t) -1, 123, 0) == 0, "setreuid(-1, own)");
    CHECK(!PEMU_PRIV(), "not privileged");
    CHECK(pemu_force(-EPERM) == -EPERM, "no forced success without root");
}
static void t_ids_root(void) {                       /* -0: root drops privilege like the kernel: ruid, euid, suid and fsuid all change */
    pemu_set_ids(0, 0);
    CHECK(PEMU_PRIV() && pemu_force(-EPERM) == 0 && pemu_force(-ENOENT) == -ENOENT, "root forces EPERM only");
    CHECK(pemu_idcall(PEMU_SETUID, 1000, 0, 0) == 0, "setuid(1000)");
    CHECK(pemu_idcall(PEMU_GETUID, 0, 0, 0) == 1000 && pemu_idcall(PEMU_GETEUID, 0, 0, 0) == 1000, "r/e = 1000");
    uint32_t r, e, s;
    CHECK(pemu_idcall(PEMU_GETRESUID, (uint64_t) (uintptr_t) &r, (uint64_t) (uintptr_t) &e, (uint64_t) (uintptr_t) &s) == 0 && r == 1000 && e == 1000 && s == 1000, "res %u %u %u", r, e, s);
    CHECK(pemu_idcall(PEMU_SETUID, 0, 0, 0) == -EPERM, "cannot become root again");
    CHECK(!PEMU_PRIV(), "no longer privileged");
}
static void t_ids_saved(void) {                      /* setresuid keeps a saved id so a program can come back (like su / apt's _apt) */
    pemu_set_ids(0, 0);
    CHECK(pemu_idcall(PEMU_SETRESUID, 100, 100, 0) == 0, "setresuid(100,100,0)");
    CHECK(pemu_idcall(PEMU_SETUID, 0, 0, 0) == 0, "saved id 0 allows setuid(0)");
    CHECK(pemu_idcall(PEMU_SETFSUID, 55, 0, 0) == 0, "setfsuid returns the old value (0)");
    CHECK(pemu_idcall(PEMU_SETFSUID, 66, 0, 0) == 55, "setfsuid returns the previous fsuid");
    pemu_set_ids(5, 5);
    CHECK(pemu_idcall(PEMU_SETFSUID, 9, 0, 0) == 5 && pemu_uid[PEMU_F] == 5, "setfsuid refused: old value returned, unchanged");
    CHECK(pemu_idcall(PEMU_SETGID, 0, 0, 0) == -EPERM, "setgid(0) refused for gid 5");
}
static void t_ids_kernel(void) {                     /* the rules of the kernel's set*id calls (found by auditing fake_id) */
    uint32_t r, e, sv;
    pemu_set_ids(0, 0); pemu_idcall(PEMU_SETRESGID, 5, 5, 5);
    CHECK(pemu_idcall(PEMU_SETGID, 9, 0, 0) == 0 && pemu_gid[PEMU_R] == 9 && pemu_gid[PEMU_E] == 9 && pemu_gid[PEMU_S] == 9 && pemu_gid[PEMU_F] == 9,
          "setgid as euid 0 sets real, effective, saved and fs gid");
    pemu_set_ids(0, 0); pemu_idcall(PEMU_SETRESGID, 7, 0, 8); pemu_idcall(PEMU_SETRESUID, 100, 100, 100);
    CHECK(pemu_idcall(PEMU_SETGID, 7, 0, 0) == 0 && pemu_gid[PEMU_S] == 8 && pemu_gid[PEMU_E] == 7, "setgid by an unprivileged user (egid 0): saved stays 8, is %u", pemu_gid[PEMU_S]);
    pemu_set_ids(0, 0);
    CHECK(pemu_idcall(PEMU_SETUID, (uint64_t) -1, 0, 0) == -EINVAL && pemu_idcall(PEMU_SETGID, (uint64_t) -1, 0, 0) == -EINVAL, "setuid / setgid(-1) are EINVAL");
    CHECK(pemu_uid[PEMU_E] == 0, "and change nothing");
    pemu_idcall(PEMU_SETRESUID, 1, 2, 3);                                    /* real 1, effective 2, saved 3 */
    CHECK(pemu_idcall(PEMU_SETUID, 2, 0, 0) == -EPERM, "unprivileged setuid(effective id) is EPERM unless it is also real or saved");
    CHECK(pemu_idcall(PEMU_SETREUID, 2, 3, 0) == 0 && pemu_uid[PEMU_R] == 2 && pemu_uid[PEMU_E] == 3 && pemu_uid[PEMU_S] == 3 && pemu_uid[PEMU_F] == 3,
          "setreuid(effective, saved) is allowed: %u %u %u %u", pemu_uid[0], pemu_uid[1], pemu_uid[2], pemu_uid[3]);
    CHECK(pemu_idcall(PEMU_SETREUID, 9, (uint64_t) -1, 0) == -EPERM, "setreuid(other, -1) is EPERM");
    pemu_set_ids(0, 0); pemu_idcall(PEMU_SETRESUID, 1, 2, 3);
    CHECK(pemu_idcall(PEMU_SETREUID, 2, (uint64_t) -1, 0) == 0 && pemu_uid[PEMU_S] == 2, "setreuid(r, -1) sets saved = effective (saved is %u)", pemu_uid[PEMU_S]);
    pemu_set_ids(0, 0); pemu_idcall(PEMU_SETRESUID, 1, 2, 3); pemu_idcall(PEMU_SETFSUID, 9, 0, 0);
    CHECK(pemu_idcall(PEMU_SETRESUID, 1, (uint64_t) -1, (uint64_t) -1) == 0 && pemu_uid[PEMU_F] == pemu_uid[PEMU_E], "setresuid resets the fs id to the effective id");
    pemu_set_ids(5, 5);
    CHECK(pemu_idcall(PEMU_SETFSUID, (uint64_t) -1, 0, 0) == 5 && pemu_uid[PEMU_F] == 5, "setfsuid(-1) changes nothing");
    CHECK(pemu_idcall(PEMU_GETRESUID, 1, 0, 0) == -EFAULT, "getresuid with a bad pointer is EFAULT, not a crash");
    CHECK(pemu_idcall(PEMU_GETRESUID, (uint64_t) (uintptr_t) &r, (uint64_t) (uintptr_t) &e, (uint64_t) (uintptr_t) &sv) == 0 && r == 5 && e == 5 && sv == 5, "getresuid still works");
}
static void t_stat_ids(void) {                       /* files of the host user show as the fake id */
    pemu_huid = getuid(); pemu_hgid = getgid();
    pemu_set_ids(123, 456);
    CHECK(PEMU_UID(pemu_huid) == 123 && PEMU_GID(pemu_hgid) == 456 && PEMU_UID(pemu_huid + 1) == pemu_huid + 1, "stat ids");
    CHECK(pemu_at_uid() == 123 && pemu_at_egid() == 456, "auxv ids");
}
static void t_bind_forms(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host/a", "-b", T "/host2:/mnt/two", "-b", T "/host/f1:/link" };
    CHECK(setup(a, 10) == 0, "finish");
    char h[PEMU_HP];
    CHECK(pemu_path(AT_FDCWD, T "/host/a/b", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host/a/b"), "symmetric binding keeps the path: %s", h);
    CHECK(pemu_path(AT_FDCWD, "/mnt/two/f2", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host2/f2"), "asymmetric binding: %s", h);
    /* "/link" is a symlink to /real in the guest: the guest path is dereferenced, so /real is what gets bound */
    CHECK(pemu_path(AT_FDCWD, "/real", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host/f1"), "guest symlink dereferenced: %s", h);
}
static void t_bind_nofollow(void) {                  /* a trailing '!' keeps the guest symlink itself */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host/f1:/link!" };
    CHECK(setup(a, 6) == 0, "finish");
    char h[PEMU_HP];
    CHECK(pemu_path(AT_FDCWD, "/link", 0, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host/f1"), "link itself bound: %s", h);
    CHECK(pemu_path(AT_FDCWD, "/real", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/rf/real"), "target untouched: %s", h);
}
static void t_missing_bind(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/nope", "-b", T "/host2:/mnt/two" };
    CHECK(setup(a, 8) == 0, "a missing host path is a warning, not an error");
    char h[PEMU_HP];
    CHECK(pemu_path(AT_FDCWD, "/mnt/two/f2", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host2/f2"), "the other bindings still work");
}
static void t_order(void) {                          /* a deeper binding listed first must still work (proot sorts them) */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host/f1:/m/a/b/c", "-b", T "/host/a:/m/a", "-b", T "/host2:/m" };
    CHECK(setup(a, 10) == 0, "finish");
    char h[PEMU_HP];
    CHECK(pemu_path(AT_FDCWD, "/m/a/b/c", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host/f1"), "deepest binding wins: %s", h);
    CHECK(pemu_path(AT_FDCWD, "/m/f2", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host2/f2"), "outer binding: %s", h);
    CHECK(pemu_path(AT_FDCWD, "/m/a/b", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host/a/b"), "middle binding: %s", h);
}
static void t_same_guest(void) {                     /* two bindings of one guest path: the last one wins */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host/f1:/etc/x", "-b", T "/host2/f2:/etc/x" };
    CHECK(setup(a, 8) == 0, "finish");
    char h[PEMU_HP]; pemu_path(AT_FDCWD, "/etc/x", 1, h, sizeof h, NULL);
    CHECK(!strcmp(h, T "/host2/f2"), "last binding is active: %s", h);
}
static void t_last_root(void) {                      /* a second -r replaces the first */
    char *a[] = { "pemu", NDB, "-r", T "/host2", "-r", T "/rf" };
    CHECK(setup(a, 6) == 0 && !strcmp(pemu_root, T "/rf"), "last -r wins: %s", pemu_root);
}
static void t_bad_root(void) {
    char *a[] = { "pemu", "-r", T "/nonexistent" };
    CHECK(setup(a, 3) < 0, "an unusable rootfs is an error");
}
static void t_placeholder(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host:/black/holes/revelations", "-b", T "/host/f1:/etc/newfile" };
    CHECK(setup(a, 8) == 0, "finish");
    struct stat st;
    CHECK(stat(T "/rf/black/holes/revelations", &st) == 0 && S_ISDIR(st.st_mode), "mount point created in the rootfs");
    CHECK(stat(T "/rf/etc/newfile", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0, "file mount point created");
    pemu_ph_cleanup();
    CHECK(access(T "/rf/black", F_OK) != 0 && access(T "/rf/etc/newfile", F_OK) != 0, "removed again on exit");
    CHECK(access(T "/rf/etc", F_OK) == 0, "pre-existing directories stay");
}
static void t_placeholder_default(void) {            /* the default /dev /proc /sys binds get placeholders too */
    char *a[] = { "pemu", "-r", T "/rf" };
    CHECK(setup(a, 3) == 0, "finish");
    CHECK(access(T "/rf/proc", F_OK) == 0 && access(T "/rf/dev", F_OK) == 0, "default mount points exist");
    pemu_ph_cleanup();
    CHECK(access(T "/rf/proc", F_OK) != 0 && access(T "/rf/sys", F_OK) != 0, "and are removed again");
}
static void t_placeholder_nonempty(void) {           /* a placeholder file that was written to is not removed */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host/f1:/etc/newfile" };
    setup(a, 6);
    FILE *f = fopen(T "/rf/etc/newfile", "w"); if (f) { fputs("data", f); fclose(f); }
    pemu_ph_cleanup();
    CHECK(access(T "/rf/etc/newfile", F_OK) == 0, "non-empty placeholder kept");
}
static void t_dont_pollute(void) {
    setenv("PROOT_DONT_POLLUTE_ROOTFS", "1", 1);
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host2:/dont/create" };
    CHECK(setup(a, 6) == 0, "finish");
    CHECK(access(T "/rf/dont", F_OK) != 0, "nothing created in the rootfs");
    char h[PEMU_HP];
    CHECK(pemu_path(AT_FDCWD, "/dont/create/f2", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host2/f2"), "the binding still resolves: %s", h);
}
static void t_cwd(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-w", "/nonexistent" };
    CHECK(setup(a, 6) == 0 && !strcmp(pemu_cwd, "/"), "unusable -w falls back to /: %s", pemu_cwd);
}
static void t_cwd_symlink(void) {
    char *b[] = { "pemu", NDB, "-r", T "/rf", "-w", "/bin" };
    CHECK(setup(b, 6) == 0 && !strcmp(pemu_cwd, "/usr/bin"), "-w through a guest symlink: %s", pemu_cwd);
    CHECK(!strcmp(getenv("PWD"), "/usr/bin"), "PWD kept consistent");
}
static void t_cwd_default(void) {
    char *b[] = { "pemu", NDB, "-r", T "/rf" };
    CHECK(setup(b, 4) == 0 && !strcmp(pemu_cwd, "/"), "default cwd is /: %s", pemu_cwd);
}
static void t_root_id(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "--root-id" };
    CHECK(setup(a, 5) == 0 && pemu_fakeroot && PEMU_PRIV(), "--root-id: fake root");
    CHECK(pemu_idcall(PEMU_GETUID, 0, 0, 0) == 0 && pemu_idcall(PEMU_GETEGID, 0, 0, 0) == 0, "ids are 0");
}
static void t_mount_needs_root(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf" };
    setup(a, 4);
    CHECK(pemu_chroot("/usr") == -EPERM && pemu_mount("/etc", "/tmp", PEMU_MS_BIND) == -EPERM && pemu_umount("/tmp") == -EPERM, "no chroot/mount without root");
    char *b[] = { "pemu", NDB, "-r", T "/rf", "--root-id" };
    setup(b, 5);
    CHECK(pemu_chroot("/usr") == 0, "chroot works for the fake root");
}
static void t_reexec(void) {                          /* state handed to a re-exec'd pemu rebuilds the same environment */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host2:/mnt/two", "--root-id", "-w", "/root" };
    CHECK(setup(a, 9) == 0, "finish");
    pemu_uid[PEMU_E] = 9;                              /* a changed effective id must survive */
    char *nv[64]; int k = pemu_reexec_opts(nv, 0);
    CHECK(k <= pemu_reexec_count(), "count %d", k);
    int n = 0, ids = 0, top = 0; for (int i = 0; i < k; i++) { n += !strcmp(nv[i], "-b"); if (!strcmp(nv[i], "--ids")) ids = !strcmp(nv[i + 1], "0,9,0,0,0,0,0,0"); top += !strcmp(nv[i], "--top"); }
    CHECK(n == 1 && ids && top == 1, "binds %d ids %d top %d", n, ids, top);
    /* a pemu started from these options ends up with the same table and identity */
    pemu_nb = 0; pemu_fakeroot = 0; memset(pemu_uid, 0, sizeof pemu_uid);
    char *av[70]; int m = 0; av[m++] = "pemu"; for (int i = 0; i < k; i++) av[m++] = nv[i];
    CHECK(setup(av, m) == 0 && pemu_nb == 2 && pemu_uid[PEMU_E] == 9 && pemu_fakeroot, "rebuilt: nb %d euid %u", pemu_nb, pemu_uid[PEMU_E]);
}
static void t_top_wait(void) {                        /* the first process waits for the others before it exits */
    char *a[] = { "pemu", NDB, "-r", T "/rf" };
    setup(a, 4);
    CHECK(pemu_wait && pemu_top == getpid(), "a guest with a rootfs waits for its children");
    pid_t ch = fork();
    if (ch == 0) { usleep(300000); _exit(0); }
    pemu_exit_hook();
    CHECK(kill(ch, 0) != 0, "the child had ended when the hook returned");
}

static void t_kernel(void) {
    struct utsname u; uname(&u);
    CHECK(pemu_opt_kernel("5.0", 0) == 0, "simple");
    pemu_uname_fix(&u, "aarch64");
    CHECK(!strcmp(u.release, "5.0") && !strcmp(u.machine, "aarch64"), "release %s machine %s", u.release, u.machine);
    char lng[100]; memset(lng, '0', 70); lng[70] = 0;
    CHECK(pemu_opt_kernel(lng, 0) == 0, "long");
    uname(&u); pemu_uname_fix(&u, "x86_64"); CHECK(strlen(u.release) == 64, "release truncated to 64: %zu", strlen(u.release));
    CHECK(pemu_opt_kernel("\\sysname\\nodename\\9.9\\version\\machine\\domainname\\0\\", 0) == 0, "complex");
    uname(&u); pemu_uname_fix(&u, "x86_64");
    CHECK(!strcmp(u.sysname, "sysname") && !strcmp(u.nodename, "nodename") && !strcmp(u.release, "9.9") && !strcmp(u.version, "version") && !strcmp(u.machine, "machine") && !strcmp(u.domainname, "domainname"), "complex fields");
    long res; CHECK(pemu_set_uts_name(0, "newhost", 7, &res) == 1 && res == 0, "sethostname");
    uname(&u); pemu_uname_fix(&u, "x86_64"); CHECK(!strcmp(u.nodename, "newhost"), "nodename %s", u.nodename);
    CHECK(pemu_set_uts_name(1, "x", 65, &res) == 1 && res == -EINVAL, "too long");
    char s[512]; pemu_uts_string(s, sizeof s);
    pemu_kset = 0; CHECK(pemu_opt_kernel(s, 1) == 0, "round trip");
    uname(&u); pemu_uname_fix(&u, "x86_64"); CHECK(!strcmp(u.nodename, "newhost") && !strcmp(u.release, "9.9"), "round trip values");
    CHECK(pemu_opt_kernel("\\a\\b\\c\\", 0) == -1, "incomplete complex form");
}

static void t_opts(void) {                           /* the optional proot options, spelled as pemu's option parser takes them */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-i", "7:8", "-v", "-1", "-b", T "/host2:/mnt/two", "-w", "/root", "--kill-on-exit" };
    CHECK(setup(a, 13) == 0, "finish");
    CHECK(pemu_fakeroot && pemu_uid[PEMU_R] == 7 && pemu_gid[PEMU_R] == 8 && !PEMU_PRIV(), "-i 7:8: %u:%u", pemu_uid[0], pemu_gid[0]);
    CHECK(pemu_verbose == -1 && pemu_killall, "-v -1, --kill-on-exit");
    CHECK(!strcmp(pemu_cwd, "/root"), "-w: %s", pemu_cwd);
    char h[PEMU_HP]; CHECK(pemu_path(AT_FDCWD, "/mnt/two/f2", 1, h, sizeof h, NULL) == 0 && !strcmp(h, T "/host2/f2"), "-m is -b: %s", h);
    char *b[] = { "pemu", NDB, "-r", T "/rf", "--change-id=3:4", "--verbose=2", "--bind=" T "/host2:/m2", "--cwd=/root", "--kernel-release=4.19" };
    pemu_fakeroot = 0; pemu_npb = 0; pemu_nb = 0;
    CHECK(setup(b, 9) == 0 && pemu_uid[PEMU_R] == 3 && pemu_gid[PEMU_R] == 4 && pemu_verbose == 2 && pemu_kset, "long forms");
    pemu_note(PEMU_INFO, "level 2: verbose messages are printed");
    char *c[] = { "pemu", "-v", "x" };  pemu_npb = 0; CHECK(setup(c, 3) < 0, "-v needs an integer");
    char *d[] = { "pemu", "-r", T "/rf", "-i" }; pemu_npb = 0; CHECK(setup(d, 4) < 0, "-i needs a value");
    char *e[] = { "pemu", NDB, "-r", T "/rf", "-l" }; pemu_npb = 0; pemu_l2s = 0;
    CHECK(setup(e, 5) == 0 && pemu_l2s, "-l");
    pemu_l2s = 0; char *f[] = { "pemu", NDB, "-r", T "/rf", "--link2symlink" }; pemu_npb = 0;
    CHECK(setup(f, 5) == 0 && pemu_l2s, "--link2symlink");
}
static void t_kill_on_exit(void) {                   /* --kill-on-exit: leftover processes are killed, not waited for */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "--kill-on-exit" };
    CHECK(setup(a, 5) == 0, "finish");
    pid_t ch = fork();
    if (ch == 0) { sleep(30); _exit(0); }
    usleep(100000);
    time_t t0 = time(NULL); pemu_exit_hook();
    CHECK(time(NULL) - t0 < 5 && kill(ch, 0) != 0, "the child was killed, not waited for");
}
static void t_verbose_table(void) {                  /* -v 1 lists the configuration (stderr) */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-b", T "/host2:/mnt/two", "-v", "1" };
    CHECK(setup(a, 8) == 0 && pemu_verbose == 1, "-v 1");
}
static void t_reexec_opts(void) {                    /* -k, -v, -l, --kill-on-exit and the identity are handed to a re-exec'd pemu */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-i", "5:6", "-k", "4.4.0", "-v", "-1", "-l", "--kill-on-exit" };
    CHECK(setup(a, 12) == 0, "finish");
    char *nv[64]; int k = pemu_reexec_opts(nv, 0);
    int l2s = 0, uts = 0, v = 0, ko = 0, ids = 0;
    for (int i = 0; i < k; i++) { l2s += !strcmp(nv[i], "--link2symlink"); uts += !strcmp(nv[i], "--uts"); v += !strcmp(nv[i], "-v"); ko += !strcmp(nv[i], "--kill-on-exit"); ids += !strcmp(nv[i], "--ids"); }
    CHECK(l2s == 1 && uts == 1 && v == 1 && ko == 1 && ids == 1, "l2s %d uts %d v %d kill %d ids %d", l2s, uts, v, ko, ids);
    pemu_nb = 0; pemu_fakeroot = 0; pemu_kset = 0; pemu_l2s = 0; pemu_killall = 0; pemu_verbose = 0; pemu_npb = 0;
    char *av[70]; int m = 0; av[m++] = "pemu"; for (int i = 0; i < k; i++) av[m++] = nv[i];
    CHECK(setup(av, m) == 0 && pemu_l2s && pemu_kset && pemu_killall && pemu_verbose == -1 && pemu_uid[PEMU_R] == 5, "rebuilt");
    struct utsname u; uname(&u); pemu_uname_fix(&u, "x86_64"); CHECK(!strcmp(u.release, "4.4.0"), "release %s", u.release);
}

/* ---- link2symlink */
static long gstat(const char *p, int follow, struct stat *st) { return pemu_callat(SYS_newfstatat, AT_FDCWD, p, follow, (long) st, follow ? 0 : AT_SYMLINK_NOFOLLOW, 0, 0); }
static long glink(const char *o, const char *n) { return pemu_call2at(SYS_linkat, AT_FDCWD, o, 0, AT_FDCWD, n, 0, 0); }
static long gunlink(const char *p) { return pemu_callat(SYS_unlinkat, AT_FDCWD, p, 0, 0, 0, 0, 0); }
static int l2s_files(const char *dir) {              /* number of .l2s. entries in a host directory */
    int n = 0; DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d))) n += !strncmp(e->d_name, ".l2s.", 5);
    if (d) closedir(d);
    return n;
}
static void t_l2s(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-l" };
    CHECK(setup(a, 5) == 0 && pemu_l2s, "finish");
    mk("echo hello > " T "/rf/tmp/a");
    struct stat st, lst; char buf[64];
    CHECK(gstat("/tmp/a", 1, &st) == 0 && st.st_nlink == 1, "plain file: nlink 1");
    CHECK(glink("/tmp/a", "/tmp/b") == 0, "link");
    struct stat hl;
    CHECK(lstat(T "/rf/tmp/a", &hl) == 0 && S_ISLNK(hl.st_mode) && lstat(T "/rf/tmp/b", &hl) == 0 && S_ISLNK(hl.st_mode), "both names are symlinks on the host");
    CHECK(lstat(T "/rf/tmp/.l2s.a0001", &hl) == 0 && S_ISLNK(hl.st_mode) && lstat(T "/rf/tmp/.l2s.a0001.0002", &hl) == 0 && S_ISREG(hl.st_mode), "intermediate symlink + file, count 0002");
    CHECK(gstat("/tmp/b", 1, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 2 && st.st_size == 6, "stat: nlink %lu size %ld", (unsigned long) st.st_nlink, (long) st.st_size);
    CHECK(gstat("/tmp/b", 0, &lst) == 0 && S_ISREG(lst.st_mode) && lst.st_nlink == 2 && lst.st_ino == st.st_ino, "lstat shows the file, not the symlink");
    char h[PEMU_HP]; int tfd; long r = pemu_path_s(1, AT_FDCWD, "/tmp/b", 1, h, sizeof h, NULL, &tfd);
    int fd = r == 0 ? open(h, O_RDONLY) : -1; ssize_t n = fd >= 0 ? read(fd, buf, sizeof buf - 1) : -1; if (n >= 0) buf[n] = 0;
    CHECK(fd >= 0 && n == 6 && !strcmp(buf, "hello\n"), "open through the link reaches the data (r=%ld)", r);
    struct stat fs; if (fd >= 0) { fstat(fd, &fs); l2s_fix_fd(fd, &fs); CHECK(fs.st_nlink == 2, "fstat: nlink %lu", (unsigned long) fs.st_nlink); close(fd); }
    if (tfd >= 0) close(tfd);
    unsigned char sx[256]; memset(sx, 0, sizeof sx);
    CHECK(pemu_callat(SYS_statx, AT_FDCWD, "/tmp/a", 1, 0, 0x7ff, (long) sx, 0) == 0 && *(uint32_t*) (sx + 16) == 2, "statx nlink %u", *(uint32_t*) (sx + 16));
    char rl[64]; CHECK(pemu_readlinkat(AT_FDCWD, "/tmp/b", rl, sizeof rl) == -EINVAL, "readlink of a hard link: EINVAL");
    CHECK(glink("/tmp/a", "/tmp/c") == 0 && gstat("/tmp/c", 1, &st) == 0 && st.st_nlink == 3, "a third name: nlink %lu", (unsigned long) st.st_nlink);
    CHECK(glink("/tmp/a", "/tmp/c") == -EEXIST, "existing name: EEXIST");
    CHECK(gstat("/tmp/a", 1, &st) == 0 && st.st_nlink == 3, "count not raised by the failed link");
    CHECK(gunlink("/tmp/b") == 0 && gstat("/tmp/a", 1, &st) == 0 && st.st_nlink == 2 && access(T "/rf/tmp/b", F_OK) != 0 && lstat(T "/rf/tmp/b", &hl) < 0, "unlink: nlink 2, name gone");
    CHECK(gunlink("/tmp/a") == 0 && gstat("/tmp/c", 1, &st) == 0 && st.st_nlink == 1 && st.st_size == 6, "unlink: nlink 1, data kept");
    CHECK(gunlink("/tmp/c") == 0 && l2s_files(T "/rf/tmp") == 0, "last unlink removes the intermediate and the file");
}
static void t_l2s_rename(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-l" };
    CHECK(setup(a, 5) == 0, "finish");
    mk("echo one > " T "/rf/tmp/a; echo two > " T "/rf/tmp/x");
    struct stat st;
    CHECK(glink("/tmp/a", "/tmp/b") == 0, "link");
    CHECK(pemu_call2at(SYS_renameat, AT_FDCWD, "/tmp/x", 0, AT_FDCWD, "/tmp/b", 0, 0) == 0, "rename over a hard link");
    CHECK(gstat("/tmp/a", 1, &st) == 0 && st.st_nlink == 1 && st.st_size == 4, "the other name: nlink %lu", (unsigned long) st.st_nlink);
    CHECK(gstat("/tmp/b", 1, &st) == 0 && st.st_size == 4 && st.st_nlink == 1, "new content in b");
    CHECK(pemu_call2at(SYS_renameat, AT_FDCWD, "/tmp/a", 0, AT_FDCWD, "/tmp/z", 0, 0) == 0 && gstat("/tmp/z", 1, &st) == 0 && st.st_size == 4, "renaming a fake link keeps it working");
}
static void t_l2s_errors(void) {
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-l" };
    CHECK(setup(a, 5) == 0, "finish");
    CHECK(glink("/tmp", "/tmp2") == -EPERM, "a directory cannot be linked");
    CHECK(glink("/tmp/missing", "/tmp/b") == -ENOENT, "missing source");
    mk("echo x > " T "/rf/tmp/a");
    CHECK(glink("/tmp/a", "/nodir/b") == -ENOENT || glink("/tmp/a", "/nodir/b") < 0, "missing target directory fails");
    struct stat st; CHECK(gstat("/tmp/a", 1, &st) == 0 && S_ISREG(st.st_mode), "the source is untouched by failed links");
}
static void t_l2s_off(void) {                        /* without -l, link(2) is the host's own */
    char *a[] = { "pemu", NDB, "-r", T "/rf" };
    CHECK(setup(a, 4) == 0 && !pemu_l2s, "finish");
    mk("echo x > " T "/rf/tmp/a");
    struct stat st; long r = glink("/tmp/a", "/tmp/b");
    CHECK(r == 0 && lstat(T "/rf/tmp/b", &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 2 && l2s_files(T "/rf/tmp") == 0, "a real hard link: %ld", r);
}
static void t_l2s_proot_made(void) {                 /* a tree made by real proot (absolute host targets) works too */
    char *a[] = { "pemu", NDB, "-r", T "/rf", "-l" };
    CHECK(setup(a, 5) == 0, "finish");
    mk("cd " T "/rf/tmp && echo data > .l2s.p0001.0003 && ln -s " T "/rf/tmp/.l2s.p0001.0003 .l2s.p0001 && ln -s " T "/rf/tmp/.l2s.p0001 p && ln -s " T "/rf/tmp/.l2s.p0001 q && ln -s " T "/rf/tmp/.l2s.p0001 r");
    struct stat st; CHECK(gstat("/tmp/q", 1, &st) == 0 && st.st_nlink == 3 && st.st_size == 5, "nlink from the name: %lu", (unsigned long) st.st_nlink);
    char h[PEMU_HP]; CHECK(pemu_path(AT_FDCWD, "/tmp/p", 1, h, sizeof h, NULL) == 0 && strstr(h, ".l2s.p0001.0003"), "path walk reaches the file: %s", h);
    CHECK(gunlink("/tmp/p") == 0 && gstat("/tmp/q", 1, &st) == 0 && st.st_nlink == 2, "unlink lowers the count");
}

int main(void) {
    fresh();
    run("ids_user", t_ids_user); run("ids_root", t_ids_root); run("ids_saved", t_ids_saved); run("ids_kernel", t_ids_kernel); run("stat_ids", t_stat_ids);
    fresh(); run("bind_forms", t_bind_forms); run("bind_nofollow", t_bind_nofollow); run("missing_bind", t_missing_bind);
    run("order", t_order); run("same_guest", t_same_guest); run("last_root", t_last_root); run("bad_root", t_bad_root);
    fresh(); run("placeholder", t_placeholder); fresh(); run("placeholder_default", t_placeholder_default);
    fresh(); run("placeholder_nonempty", t_placeholder_nonempty);
    fresh(); run("dont_pollute", t_dont_pollute);
    fresh(); run("cwd", t_cwd); run("cwd_symlink", t_cwd_symlink); run("cwd_default", t_cwd_default);
    fresh(); run("root_id", t_root_id); run("mount_needs_root", t_mount_needs_root);
    fresh(); run("reexec", t_reexec); fresh(); run("top_wait", t_top_wait);
    fresh(); run("kernel", t_kernel); run("opts", t_opts); run("kill_on_exit", t_kill_on_exit); run("verbose_table", t_verbose_table); run("reexec_opts", t_reexec_opts);
    fresh(); run("l2s", t_l2s); fresh(); run("l2s_rename", t_l2s_rename); fresh(); run("l2s_errors", t_l2s_errors);
    fresh(); run("l2s_off", t_l2s_off); fresh(); run("l2s_proot_made", t_l2s_proot_made);
    mk("chmod -R u+rwx " T " 2>/dev/null; rm -rf " T);
    printf(fails ? "%d proot-compat scenarios FAILED\n" : "all proot-compat tests passed\n", fails);
    return fails != 0;
}
