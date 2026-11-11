/* tests/test_deny.c - the single syscall refusal table (linux-user/proot/deny.inc.h) shared by the AArch64 and x86_64 hooks.
 * Host-native, no guest binaries.  hs() is the test's own and NEVER reaches the kernel: it only counts calls, so a refused call is shown
 * to be refused before the host is touched (nothing here can run move_mount, unshare, ptrace ... on this machine). */
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
#include <time.h>
#include <sys/syscall.h>
static int hs_calls;
static long hs(long nr, long a, long b, long c_, long d, long e, long f) { (void) nr; (void) a; (void) b; (void) c_; (void) d; (void) e; (void) f; hs_calls++; return -ENOSYS; }
#include "pemu_root.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

static int policy(int arch, uint64_t n, uint64_t a0, long *r) { *r = 0; return pemu_policy(arch, n, a0, 0, 0, 0, 0, r); }
static const struct pemu_deny *row(const char *name) {
    for (size_t i = 0; i < sizeof pemu_deny_tab / sizeof *pemu_deny_tab; i++) if (!strcmp(pemu_deny_tab[i].name, name)) return &pemu_deny_tab[i];
    return NULL;
}
#define NROWS (sizeof pemu_deny_tab / sizeof *pemu_deny_tab)

int main(void) {
    long r;
    pemu_on = 0;
    /* 1. the numbers the task names, literally, for both ABIs */
    static const struct { const char *name; int a64, x64; } want[] = {
        {"ptrace", 117, 101}, {"setns", 268, 308}, {"unshare", 97, 272}, {"open_by_handle_at", 265, 304}, {"name_to_handle_at", 264, 303},
        {"open_tree", 428, 428}, {"move_mount", 429, 429}, {"fsopen", 430, 430}, {"fsconfig", 431, 431}, {"fsmount", 432, 432},
        {"fspick", 433, 433}, {"mount_setattr", 442, 442}, {"open_tree_attr", 467, 467},
        {"process_vm_readv", 270, 310}, {"process_vm_writev", 271, 311}, {"kcmp", 272, 312}, {"pidfd_getfd", 438, 438}, {"bpf", 280, 321}, {"perf_event_open", 241, 298}, {"userfaultfd", 282, 323}, {"landlock_create_ruleset", 444, 444}, {"landlock_add_rule", 445, 445}, {"landlock_restrict_self", 446, 446}, {"statmount", 457, 457}, {"listmount", 458, 458}, {"keyctl", 219, 250}, {"add_key", 217, 248}, {"request_key", 218, 249},
        {"sethostname", 161, 170}, {"setdomainname", 162, 171}, {"settimeofday", 170, 164}, {"clock_settime", 112, 227}, {"clock_adjtime", 266, 305}, {"adjtimex", 171, 159}, {"syslog", 116, 103}, {"vhangup", 58, 153}, {"quotactl_fd", 443, 443}, {"lsm_set_self_attr", 460, 460}, {"migrate_pages", 238, 256}, {"move_pages", 239, 279}, {"process_madvise", 440, 440},
    };
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        const struct pemu_deny *d = row(want[i].name);
        CHECK(d != NULL, "%s is missing from the table", want[i].name);
        if (d) CHECK(d->nr[PEMU_ARCH_A64] == want[i].a64 && d->nr[PEMU_ARCH_X64] == want[i].x64, "%s: numbers %d/%d, want %d/%d", want[i].name, d->nr[0], d->nr[1], want[i].a64, want[i].x64);
    }
    /* 2. the table agrees with the HOST's own numbers (a literal like the old 303 is wrong on an aarch64 host, where it is not name_to_handle_at) */
#if defined(__x86_64__)
#define HOSTCOL PEMU_ARCH_X64
#elif defined(__aarch64__)
#define HOSTCOL PEMU_ARCH_A64
#endif
#ifdef HOSTCOL
#define HOSTCHK(n) do { const struct pemu_deny *d = row(#n); if (d) CHECK(d->nr[HOSTCOL] == SYS_##n, "%s: table has %d for this host, SYS_%s is %d", #n, d->nr[HOSTCOL], #n, (int) SYS_##n); } while (0)
    HOSTCHK(ptrace); HOSTCHK(setns); HOSTCHK(unshare); HOSTCHK(open_by_handle_at); HOSTCHK(name_to_handle_at);
#ifdef SYS_open_tree
    HOSTCHK(open_tree);
#endif
#ifdef SYS_move_mount
    HOSTCHK(move_mount);
#endif
#ifdef SYS_fsopen
    HOSTCHK(fsopen);
#endif
#ifdef SYS_fsconfig
    HOSTCHK(fsconfig);
#endif
#ifdef SYS_fsmount
    HOSTCHK(fsmount);
#endif
#ifdef SYS_fspick
    HOSTCHK(fspick);
#endif
#ifdef SYS_mount_setattr
    HOSTCHK(mount_setattr);
#endif
#ifdef SYS_open_tree_attr
    HOSTCHK(open_tree_attr);
#endif
#ifdef SYS_process_vm_readv
    HOSTCHK(process_vm_readv);
#endif
#ifdef SYS_process_vm_writev
    HOSTCHK(process_vm_writev);
#endif
#ifdef SYS_kcmp
    HOSTCHK(kcmp);
#endif
#ifdef SYS_pidfd_getfd
    HOSTCHK(pidfd_getfd);
#endif
#ifdef SYS_bpf
    HOSTCHK(bpf);
#endif
#ifdef SYS_perf_event_open
    HOSTCHK(perf_event_open);
#endif
#ifdef SYS_userfaultfd
    HOSTCHK(userfaultfd);
#endif
#ifdef SYS_landlock_create_ruleset
    HOSTCHK(landlock_create_ruleset);
#endif
#ifdef SYS_landlock_add_rule
    HOSTCHK(landlock_add_rule);
#endif
#ifdef SYS_landlock_restrict_self
    HOSTCHK(landlock_restrict_self);
#endif
#ifdef SYS_statmount
    HOSTCHK(statmount);
#endif
#ifdef SYS_listmount
    HOSTCHK(listmount);
#endif
#ifdef SYS_keyctl
    HOSTCHK(keyctl);
#endif
#ifdef SYS_add_key
    HOSTCHK(add_key);
#endif
#ifdef SYS_request_key
    HOSTCHK(request_key);
#endif
#ifdef SYS_sethostname
    HOSTCHK(sethostname);
#endif
#ifdef SYS_setdomainname
    HOSTCHK(setdomainname);
#endif
#ifdef SYS_settimeofday
    HOSTCHK(settimeofday);
#endif
#ifdef SYS_clock_settime
    HOSTCHK(clock_settime);
#endif
#ifdef SYS_clock_adjtime
    HOSTCHK(clock_adjtime);
#endif
#ifdef SYS_adjtimex
    HOSTCHK(adjtimex);
#endif
#ifdef SYS_syslog
    HOSTCHK(syslog);
#endif
#ifdef SYS_vhangup
    HOSTCHK(vhangup);
#endif
#ifdef SYS_quotactl_fd
    HOSTCHK(quotactl_fd);
#endif
#ifdef SYS_lsm_set_self_attr
    HOSTCHK(lsm_set_self_attr);
#endif
#ifdef SYS_migrate_pages
    HOSTCHK(migrate_pages);
#endif
#ifdef SYS_move_pages
    HOSTCHK(move_pages);
#endif
#ifdef SYS_process_madvise
    HOSTCHK(process_madvise);
#endif
#endif
    /* 3. no duplicate number inside one ABI; every row has a number on both */
    for (size_t i = 0; i < NROWS; i++) for (int a = 0; a < 2; a++) {
        CHECK(pemu_deny_tab[i].nr[a] != PEMU_NR_NONE, "%s has no number for arch %d", pemu_deny_tab[i].name, a);
        for (size_t j = i + 1; j < NROWS; j++) CHECK(pemu_deny_tab[i].nr[a] != pemu_deny_tab[j].nr[a], "%s and %s share number %d", pemu_deny_tab[i].name, pemu_deny_tab[j].name, pemu_deny_tab[i].nr[a]);
    }
    /* 4. identical policy on both architectures, with and without a rootfs table: refused rows are EPERM and never reach the host */
    static const uint64_t flagsamples[] = {0, 1, 0x20000, 0x7e020000ULL, 0x80, 0x400, 0x200, 0x40000, 0xffffffffULL};
    for (size_t i = 0; i < NROWS; i++) {
        const struct pemu_deny *d = &pemu_deny_tab[i];
        for (size_t k = 0; k < sizeof flagsamples / sizeof *flagsamples; k++) {
            int ra[2]; long rr[2];
            hs_calls = 0;
            ra[0] = policy(PEMU_ARCH_A64, (uint64_t) d->nr[0], flagsamples[k], &rr[0]);
            ra[1] = policy(PEMU_ARCH_X64, (uint64_t) d->nr[1], flagsamples[k], &rr[1]);
            CHECK(ra[0] == ra[1] && (!ra[0] || rr[0] == rr[1]), "%s a0=%#llx: AArch64 and x86_64 answer differently (%d/%ld vs %d/%ld)", d->name, (unsigned long long) flagsamples[k], ra[0], rr[0], ra[1], rr[1]);
            CHECK(hs_calls == 0, "%s: the policy touched the host", d->name);
            if (d->how == PEMU_DN_ALWAYS) CHECK(ra[0] && rr[0] == -EPERM && ra[1] && rr[1] == -EPERM, "%s must be EPERM on both ABIs (got %d/%ld, %d/%ld)", d->name, ra[0], rr[0], ra[1], rr[1]);
        }
    }
    /* 5. unshare: every CLONE_NEW* flag alone is EPERM; flags without one go on to the host */
    static const uint64_t newflags[] = {0x20000 /*NEWNS*/, 0x2000000 /*NEWCGROUP*/, 0x4000000 /*NEWUTS*/, 0x8000000 /*NEWIPC*/, 0x10000000 /*NEWUSER*/, 0x20000000 /*NEWPID*/, 0x40000000 /*NEWNET*/, 0x80 /*NEWTIME*/};
    static const uint64_t okflags[] = {0, 0x200 /*FS*/, 0x400 /*FILES*/, 0x40000 /*SYSVSEM*/};
    for (int a = 0; a < 2; a++) {
        uint64_t nr = (uint64_t) row("unshare")->nr[a];
        for (size_t k = 0; k < sizeof newflags / sizeof *newflags; k++) CHECK(policy(a, nr, newflags[k], &r) && r == -EPERM, "arch %d: unshare(%#llx) must be EPERM", a, (unsigned long long) newflags[k]);
        for (size_t k = 0; k < sizeof okflags / sizeof *okflags; k++) CHECK(!policy(a, nr, okflags[k], &r), "arch %d: unshare(%#llx) must pass through", a, (unsigned long long) okflags[k]);
        CHECK(policy(a, nr, 0x200 | 0x4000000, &r) && r == -EPERM, "arch %d: unshare(FS|NEWUTS) must be EPERM", a);
    }
    /* 6. name_to_handle_at is not refused; without a rootfs it is left to the plain pass-through */
    CHECK(!policy(PEMU_ARCH_A64, 264, 0, &r) && !policy(PEMU_ARCH_X64, 303, 0, &r), "name_to_handle_at without a rootfs must pass through");
    /* 6b. with a rootfs (pemu_on) name_to_handle_at is answered by the policy (translated), never EPERM; the host stub is reached, not a raw guest path */
    pemu_on = 1; hs_calls = 0;
    CHECK(policy(PEMU_ARCH_A64, 264, 0, &r) && r != -EPERM, "name_to_handle_at with a rootfs must be answered by the translation, not refused");
    CHECK(policy(PEMU_ARCH_X64, 303, 0, &r) && r != -EPERM, "name_to_handle_at (x86_64) with a rootfs must be answered by the translation, not refused");
    pemu_on = 0; hs_calls = 0;
    /* 6c. a guest passing syscall number -1 must not match any row */
    CHECK(!policy(PEMU_ARCH_A64, (uint64_t) -1, 0, &r) && !policy(PEMU_ARCH_X64, (uint64_t) -1, 0, &r), "syscall -1 must not match a row");
    /* 6d. adjtimex / clock_adjtime: only the read-only forms reach the host, on a private copy; a bad pointer is EFAULT, never a crash */
    for (int a = 0; a < 2; a++) {
        uint64_t nra = (uint64_t) row("adjtimex")->nr[a], nrc = (uint64_t) row("clock_adjtime")->nr[a];
        static const struct { uint32_t modes; int ok; } cases[] = { {0, 1}, {0xa001, 1}, {0x100 /*ADJ_SETOFFSET*/, 0}, {1 /*ADJ_OFFSET*/, 0}, {0x8000 /*ADJ_TICK*/, 0}, {0x2001, 0}, {0xffffffffu, 0} };
        for (size_t k = 0; k < sizeof cases / sizeof *cases; k++) {
            uint64_t tx[26]; memset(tx, 0, sizeof tx); tx[0] = cases[k].modes;
            hs_calls = 0; r = 0;
            int ans = pemu_policy(a, nra, (uint64_t) (uintptr_t) tx, 0, 0, 0, 0, &r);
            CHECK(ans && (cases[k].ok ? (r == -ENOSYS && hs_calls == 1) : (r == -EPERM && hs_calls == 0)), "arch %d adjtimex modes=%#x: answer %d r=%ld hs=%d", a, cases[k].modes, ans, r, hs_calls);
            hs_calls = 0; r = 0;
            ans = pemu_policy(a, nrc, 0, (uint64_t) (uintptr_t) tx, 0, 0, 0, &r);
            CHECK(ans && (cases[k].ok ? (r == -ENOSYS && hs_calls == 1) : (r == -EPERM && hs_calls == 0)), "arch %d clock_adjtime modes=%#x: answer %d r=%ld hs=%d", a, cases[k].modes, ans, r, hs_calls);
        }
        hs_calls = 0; r = 0;
        CHECK(pemu_policy(a, nra, 1, 0, 0, 0, 0, &r) && r == -EFAULT && hs_calls == 0, "arch %d adjtimex(bad pointer) must be EFAULT without the host", a);
        CHECK(pemu_policy(a, nrc, 0, 0, 0, 0, 0, &r) && r == -EFAULT && hs_calls == 0, "arch %d clock_adjtime(NULL) must be EFAULT without the host", a);
    }
    hs_calls = 0;
    /* 7. numbers of the OTHER ABI never match: AArch64 117 is ptrace but x86_64 117 is setresgid; x86_64 101 is ptrace but AArch64 101 is nanosleep */
    CHECK(!policy(PEMU_ARCH_X64, 117, 0, &r), "x86_64 117 (setresgid) must not be refused");
    CHECK(!policy(PEMU_ARCH_A64, 101, 0, &r), "AArch64 101 (nanosleep) must not be refused");
    CHECK(!policy(PEMU_ARCH_A64, 303, 0, &r) && !policy(PEMU_ARCH_X64, 264, 0, &r), "numbers of the other ABI must not match");
    CHECK(hs_calls == 0, "the host was reached (%d calls)", hs_calls);
    if (fails) { printf("test_deny: %d FAILED\n", fails); return 1; }
    printf("test_deny: ok (%zu rows, both ABIs)\n", NROWS);
    return 0;
}
