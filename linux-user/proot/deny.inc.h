/* linux-user/proot/deny.inc.h - ONE syscall refusal policy for both guest hooks (AArch64 and x86_64). Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0
 *
 * Calls that pemu cannot translate or provide, and that would otherwise act on the host or on the emulator process itself.  Each row
 * names the call once and gives its number in both guest ABIs; the two hooks look up their own column here (pemu_policy) instead of
 * keeping their own lists, so the architectures cannot drift apart.  Numbers are GUEST numbers (they are never passed to the host).
 * Not-applicable column: PEMU_NR_NONE.
 *
 *   PEMU_DN_ALWAYS     -EPERM always (as a container whose seccomp profile blocks the call).
 *   PEMU_DN_NEWNS      -EPERM when the flags in arg 0 ask for a new namespace (unshare); other flags pass through to the host.
 *   PEMU_DN_TRANSLATE  not refused: with a rootfs the path is translated (name_to_handle_at); without one it passes through unchanged.
 *   PEMU_DN_TIMEX0/1   adjtimex(buf) / clock_adjtime(clk, buf): only the READ-ONLY forms go to the host (modes == 0, or ADJ_OFFSET_SS_READ),
 *                      any request to change the clock is -EPERM.  The struct is copied once (a guest thread must not turn modes into a
 *                      write after the check) and the host gets the private copy; the result is copied back.
 *
 * The policy is applied with or without a rootfs, like the ptrace refusal it replaces: unshare / setns / the mount API would change the
 * emulator process or the host mount tree either way. */
#define PEMU_NR_NONE (-1)
#define PEMU_ARCH_A64 0
#define PEMU_ARCH_X64 1
enum { PEMU_DN_ALWAYS, PEMU_DN_NEWNS, PEMU_DN_TRANSLATE, PEMU_DN_TIMEX0, PEMU_DN_TIMEX1 };

/* unshare(2) knows CLONE_NEWTIME (0x80) as well; clone(2) has no such flag (that bit is the exit signal), see PEMU_CLONE_NEWMASK */
#define PEMU_UNSHARE_NEWMASK (PEMU_CLONE_NEWMASK | 0x80ULL)

struct pemu_deny { const char *name; int nr[2]; int how; };      /* nr[PEMU_ARCH_*] */
static const struct pemu_deny pemu_deny_tab[] = {
    /* name                     { AArch64, x86_64 }  */
    { "ptrace",                 { 117, 101 }, PEMU_DN_ALWAYS },     /* the host would trace the emulator (host registers, no guest view) */
    { "setns",                  { 268, 308 }, PEMU_DN_ALWAYS },     /* joining another namespace would leave the emulated view */
    { "unshare",                {  97, 272 }, PEMU_DN_NEWNS },      /* new namespaces cannot be offered (see clone) and would change the host process */
    { "open_by_handle_at",      { 265, 304 }, PEMU_DN_ALWAYS },     /* a handle names an inode, not a rootfs path (the "shocker" escape) */
    { "name_to_handle_at",      { 264, 303 }, PEMU_DN_TRANSLATE },  /* takes a path: resolved in the rootfs */
    /* new mount API: it would mount / move / reconfigure on the host, bypassing the bind table (identical numbers on every architecture) */
    { "open_tree",              { 428, 428 }, PEMU_DN_ALWAYS },
    { "move_mount",             { 429, 429 }, PEMU_DN_ALWAYS },
    { "fsopen",                 { 430, 430 }, PEMU_DN_ALWAYS },
    { "fsconfig",               { 431, 431 }, PEMU_DN_ALWAYS },
    { "fsmount",                { 432, 432 }, PEMU_DN_ALWAYS },
    { "fspick",                 { 433, 433 }, PEMU_DN_ALWAYS },
    { "mount_setattr",          { 442, 442 }, PEMU_DN_ALWAYS },
    { "open_tree_attr",         { 467, 467 }, PEMU_DN_ALWAYS },
    /* acting on other host processes, host kernel state or the host's mounts / keyrings: refused like a container's default seccomp profile does */
    { "process_vm_readv",       { 270, 310 }, PEMU_DN_ALWAYS },    /* reads another process's memory by host pid (the emulator's own, or any process of this user) */
    { "process_vm_writev",      { 271, 311 }, PEMU_DN_ALWAYS },    /* writes another process's memory by host pid (the emulator's own, or any process of this user) */
    { "kcmp",                   { 272, 312 }, PEMU_DN_ALWAYS },    /* compares kernel objects of host pids */
    { "pidfd_getfd",            { 438, 438 }, PEMU_DN_ALWAYS },    /* steals an fd from another host process, which may be outside the rootfs */
    { "bpf",                    { 280, 321 }, PEMU_DN_ALWAYS },    /* loads programs into the host kernel (a default container seccomp profile blocks it) */
    { "perf_event_open",        { 241, 298 }, PEMU_DN_ALWAYS },    /* counters / tracing of host tasks (blocked by a default container seccomp profile) */
    { "userfaultfd",            { 282, 323 }, PEMU_DN_ALWAYS },    /* as blocked by a default container seccomp profile */
    { "landlock_create_ruleset",{ 444, 444 }, PEMU_DN_ALWAYS },    /* Landlock would restrict the emulator itself, whose host paths are not the guest's; programs fall back to no sandbox */
    { "landlock_add_rule",      { 445, 445 }, PEMU_DN_ALWAYS },
    { "landlock_restrict_self", { 446, 446 }, PEMU_DN_ALWAYS },
    { "statmount",              { 457, 457 }, PEMU_DN_ALWAYS },    /* reports the HOST mount table, not pemu's bind table */
    { "listmount",              { 458, 458 }, PEMU_DN_ALWAYS },    /* lists the HOST mounts, not pemu's bind table */
    { "keyctl",                 { 219, 250 }, PEMU_DN_ALWAYS },    /* the host's session / user keyrings are shared with every process of this user */
    { "add_key",                { 217, 248 }, PEMU_DN_ALWAYS },    /* same keyrings as keyctl */
    { "request_key",            { 218, 249 }, PEMU_DN_ALWAYS },    /* same keyrings as keyctl */
    /* act on the host's clock, name, kernel log or on other host processes (only effective when pemu runs as real root, where they would hit the host) */
    { "sethostname",            { 161, 170 }, PEMU_DN_ALWAYS },    /* would rename the HOST when pemu runs as real root */
    { "setdomainname",          { 162, 171 }, PEMU_DN_ALWAYS },    /* same, the NIS domain name */
    { "settimeofday",           { 170, 164 }, PEMU_DN_ALWAYS },    /* would set the HOST clock when pemu runs as real root */
    { "clock_settime",          { 112, 227 }, PEMU_DN_ALWAYS },    /* same */
    { "clock_adjtime",          { 266, 305 }, PEMU_DN_TIMEX1 },    /* changing the HOST clock: refused; reading it (modes 0) is not */
    { "adjtimex",               { 171, 159 }, PEMU_DN_TIMEX0 },
    { "syslog",                 { 116, 103 }, PEMU_DN_ALWAYS },    /* reads / clears the HOST kernel log */
    { "vhangup",                { 58, 153 }, PEMU_DN_ALWAYS },    /* hangs up the emulator's controlling terminal */
    { "quotactl_fd",            { 443, 443 }, PEMU_DN_ALWAYS },    /* host filesystem quotas */
    { "lsm_set_self_attr",      { 460, 460 }, PEMU_DN_ALWAYS },    /* would change the emulator's own LSM context */
    { "migrate_pages",          { 238, 256 }, PEMU_DN_ALWAYS },    /* moves the pages of another host process */
    { "move_pages",             { 239, 279 }, PEMU_DN_ALWAYS },    /* moves the pages of another host process */
    { "process_madvise",        { 440, 440 }, PEMU_DN_ALWAYS },    /* advises the memory of another host process (pidfd) */
};

static const struct pemu_deny *pemu_deny_find(int arch, uint64_t n) {
    for (size_t i = 0; i < sizeof pemu_deny_tab / sizeof *pemu_deny_tab; i++)
        if (pemu_deny_tab[i].nr[arch] != PEMU_NR_NONE && (uint64_t) pemu_deny_tab[i].nr[arch] == n) return &pemu_deny_tab[i];   /* NONE (-1) must not match a guest that passes -1 */
    return NULL;
}

#define PEMU_TIMEX_SIZE 208          /* struct timex, the same 64-bit layout on AArch64 and x86_64 */
#define PEMU_TIMEX_RO   0xa001u      /* ADJ_OFFSET_SS_READ: the other read-only request */
/* Copy len bytes between guest memory and a private buffer WITHOUT faulting if the guest pointer is bad (a pipe write / read reports EFAULT). */
static int pemu_guest_copy(int to_guest, void *guest, void *priv, size_t len) {
    int p[2]; int rc = 0;
    if (pipe(p) < 0) return -EMFILE;
    if (to_guest) { if (write(p[1], priv, len) != (ssize_t) len || read(p[0], guest, len) != (ssize_t) len) rc = -EFAULT; }
    else          { if (write(p[1], guest, len) != (ssize_t) len || read(p[0], priv, len) != (ssize_t) len) rc = -EFAULT; }
    close(p[0]); close(p[1]);
    return rc;
}
/* adjtimex(buf) (clk_arg == 0) or clock_adjtime(clk, buf) (clk_arg == 1) */
static long pemu_timex(int clock_variant, uint64_t clk, uint64_t buf) {
    uint64_t tx[PEMU_TIMEX_SIZE / 8];
    long r = pemu_guest_copy(0, (void*) buf, tx, sizeof tx);
    if (r < 0) return r;
    uint32_t modes = (uint32_t) tx[0];
    if (modes != 0 && modes != PEMU_TIMEX_RO) return -EPERM;
#if defined(SYS_adjtimex) && defined(SYS_clock_adjtime) && __SIZEOF_LONG__ == 8      /* a 32-bit host's struct timex is smaller and laid out differently */
    r = clock_variant ? hs(SYS_clock_adjtime, (long) clk, (long) tx, 0, 0, 0, 0) : hs(SYS_adjtimex, (long) tx, 0, 0, 0, 0, 0);
    if (pemu_guest_copy(1, (void*) buf, tx, sizeof tx) < 0) return -EFAULT;
    return r;
#else
    (void) clk; (void) clock_variant;
    return -ENOSYS;
#endif
}

/* Pure decision for a row that was already found (d may be NULL), no side effects: 1 = refuse, 0 = let the call go on. */
static int pemu_deny_row_refuses(const struct pemu_deny *d, uint64_t a0) {
    if (!d) return 0;
    if (d->how == PEMU_DN_ALWAYS) return 1;
    if (d->how == PEMU_DN_NEWNS) return (a0 & PEMU_UNSHARE_NEWMASK) != 0;
    return 0;
}
/* Same by arch + number (the tests call it directly). */
static int pemu_deny_refuses(int arch, uint64_t n, uint64_t a0) { return pemu_deny_row_refuses(pemu_deny_find(arch, n), a0); }

/* name_to_handle_at(dirfd, path, handle, mount_id, flags): the path is a guest path.  AT_SYMLINK_FOLLOW (0x400) is the same on both ABIs. */
static long pemu_name_to_handle_at(uint64_t dirfd, const char *path, uint64_t handle, uint64_t mount_id, uint64_t flags) {
#ifdef SYS_name_to_handle_at
    return pemu_callat(SYS_name_to_handle_at, (long) dirfd, path, !!(flags & 0x400), (long) handle, (long) mount_id, (long) flags, 0);
#else
    (void) dirfd; (void) path; (void) handle; (void) mount_id; (void) flags;
    return -ENOSYS;
#endif
}

/* The hooks' single entry point.  Returns 1 and sets *res when the policy answered the call, 0 to let the normal translation run. */
static int pemu_policy(int arch, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, long *res) {
    const struct pemu_deny *d = pemu_deny_find(arch, n);
    if (!d) return 0;
    if (pemu_deny_row_refuses(d, a0)) { *res = -EPERM; return 1; }
    if (d->how == PEMU_DN_TIMEX0) { *res = pemu_timex(0, 0, a0); return 1; }
    if (d->how == PEMU_DN_TIMEX1) { *res = pemu_timex(1, a0, a1); return 1; }
    if (d->how == PEMU_DN_TRANSLATE && pemu_on) { *res = pemu_name_to_handle_at(a0, (const char*) a1, a2, a3, a4); return 1; }
    return 0;
}
