/* linux-user/proot/state.inc.h - global state, logging, fake-id helpers. Fragment of pemu_root.h (not self-contained). SPDX-License-Identifier: Apache-2.0 */
/* openat2 / openat take a mode only when a file can be created.  Use the HOST's flag values (O_TMPFILE differs per architecture:
 * 020200000 on x86_64 is 020040000 on aarch64, where 0200000 is O_DIRECT), never a hard-coded octal constant. */
#ifndef O_TMPFILE
#define O_TMPFILE (020000000 | O_DIRECTORY)
#endif
#define PEMU_OPEN_HAS_MODE(f) (((f) & O_CREAT) || (((f) & O_TMPFILE) == O_TMPFILE))
/* clone(2) / unshare(2) flags that ask for a new namespace: CLONE_NEW{NS,CGROUP,UTS,IPC,USER,PID,NET}.  pemu cannot provide them (a guest
 * "fork" is a plain host fork), so they are refused with EPERM, as a container whose seccomp profile blocks them would: a clone that
 * "succeeded" without the namespace would let a sandbox tool (bwrap, chrome) believe it is isolated when it is not. */
#define PEMU_CLONE_NEWMASK 0x7e020000ULL
#define PEMU_HP   8192
#define PEMU_MAXB 64
#define PEMU_VERSION "5.5.0"                /* the PRoot release whose behaviour is followed */

/* One entry of the bind table.  g / h are fixed buffers, not heap pointers: an entry is edited in place (mount reuses the slot of an
 * umounted one, chroot shortens g) while other threads may be walking the table, so nothing it points to may move or be freed.
 * Readers use the seqlock below (pemu_rd_begin / pemu_rd_retry); writers hold pemu_lock() and bracket the edit with
 * pemu_tab_begin / pemu_tab_end.  ord: creation order, so "later binds win ties" still holds when a slot is reused. */
struct pemu_bind { char g[PATH_MAX], h[PATH_MAX]; size_t gl, hl; int raw; volatile int off; unsigned long ord; };   /* off: removed by umount */
static struct pemu_bind pemu_b[PEMU_MAXB];
static volatile unsigned pemu_seq;        /* seqlock over pemu_b[], pemu_nb and pemu_root: odd while an edit is in progress */
static unsigned long pemu_ord;            /* last ord handed out */
static int pemu_nb, pemu_on, pemu_fakeroot, pemu_nodefb;
static int pemu_verbose;                  /* -v: <0 quiet, >0 print the configuration */
static int pemu_execfd = -1;              /* hidden --exec-fd: the ELF to load is this inherited fd (execveat of a memfd / deleted file); closed once loaded */
static int pemu_reexec;                   /* 1: this is a re-exec'd pemu rebuilding a table that already exists */
static int pemu_killall;                  /* --kill-on-exit */
static int pemu_l2s;                      /* -l / --link2symlink: hard links emulated with symlinks (proot's link2symlink extension) */
static int pemu_wait;                     /* the first guest process waits for the others before it exits (proot mode) */
static int pemu_kset;                     /* -k given */
static pid_t pemu_top;                    /* pid of the first guest process */
static const char *pemu_tool = "pemu";    /* name used in warnings and errors */
#define PEMU_MAXPH 256
static char *pemu_ph[PEMU_MAXPH]; static int pemu_nph; static pid_t pemu_ph_owner;    /* placeholders pemu created */
static unsigned pemu_huid, pemu_hgid;
/* faked identity: real, effective, saved, fs.  Like proot's fake_id0 extension (-0 = 0:0, -i UID:GID). */
#define PEMU_R 0
#define PEMU_E 1
#define PEMU_S 2
#define PEMU_F 3
static unsigned pemu_uid[4], pemu_gid[4];
static char pemu_root[PATH_MAX], pemu_cwd[PATH_MAX] = "/", pemu_exe[PATH_MAX], pemu_wd[PATH_MAX] = "/";
static int pemu_wd_set;
/* command-line bindings, applied in order by pemu_finish(); -r / -R / -S come through here too (guest "/") */
struct pemu_pb { const char *h, *g; int must; };
static struct pemu_pb pemu_pb[PEMU_MAXB * 2]; static int pemu_npb;
static const char *pemu_rootarg;
static volatile int pemu_lk;
static volatile int pemu_ownlk;           /* serialises the read-modify-write of a fake owner record between threads */
static volatile int pemu_privlk;          /* serialises the temporary chmod of pemu_open_priv inside this process */
static void pemu_lock(void)   { while (__sync_lock_test_and_set(&pemu_lk, 1)) sched_yield(); }
static void pemu_unlock(void) { __sync_lock_release(&pemu_lk); }
/* In a fork child only the forking thread exists: a lock or an odd (edit in progress) seqlock that another thread held at fork time
 * would never be released.  Both guest-fork paths (x86_64 and AArch64 hooks) call this in the child. */
static void pemu_atfork_child(void) { pemu_lk = 0; pemu_privlk = 0; pemu_ownlk = 0; pemu_seq += pemu_seq & 1u; }
static void pemu_uts_string(char *o, size_t sz);

/* proot's note(): "<tool> warning: ..." / "<tool> error: ..." / "<tool> info: ..."; -v -1 keeps only errors */
enum { PEMU_INFO, PEMU_WARNING, PEMU_ERROR };
static void pemu_note(int sev, const char *fmt, ...) {
    if (pemu_verbose < 0 && sev != PEMU_ERROR) return;
    fprintf(stderr, "%s %s: ", pemu_tool, sev == PEMU_ERROR ? "error" : sev == PEMU_WARNING ? "warning" : "info");
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

/* uid/gid shown to the guest.  Files owned by the host user appear as owned by the (saved) fake id, as proot does. */
#define PEMU_UID(u) ((pemu_fakeroot && (u) == pemu_huid) ? pemu_uid[PEMU_S] : (u))
#define PEMU_GID(g) ((pemu_fakeroot && (g) == pemu_hgid) ? pemu_gid[PEMU_S] : (g))
/* ids put in the auxiliary vector of a new program */
static unsigned pemu_at_uid(void)  { return pemu_fakeroot ? pemu_uid[PEMU_R] : (unsigned) getuid(); }
static unsigned pemu_at_euid(void) { return pemu_fakeroot ? pemu_uid[PEMU_E] : (unsigned) geteuid(); }
static unsigned pemu_at_gid(void)  { return pemu_fakeroot ? pemu_gid[PEMU_R] : (unsigned) getgid(); }
static unsigned pemu_at_egid(void) { return pemu_fakeroot ? pemu_gid[PEMU_E] : (unsigned) getegid(); }
/* "the tracee is supposed to have the capability": fake root (effective uid 0) */
#define PEMU_PRIV() (pemu_fakeroot && pemu_uid[PEMU_E] == 0)
/* a call that fails with EPERM succeeds when the guest is supposed to hold the capability */
static long pemu_force(long r) { return (r == -EPERM && PEMU_PRIV()) ? 0 : r; }
/* as pemu_force, but also for calls the host (Android seccomp) answers with ENOSYS: setgroups, capset */
static long pemu_force_ns(long r) { return ((r == -EPERM || r == -ENOSYS) && PEMU_PRIV()) ? 0 : r; }
/* chown: a real root is never refused for access reasons, but Android answers EACCES for e.g. chown(/dev/pts/N) from grantpt() */
static long pemu_force_acc(long r) { return ((r == -EPERM || r == -EACCES) && PEMU_PRIV()) ? 0 : r; }
/* chown(2): the fake real id is swapped for the true one, so the host has a chance to accept it */
static unsigned long pemu_chown_id(unsigned long v, const unsigned *fake, unsigned real) {
    return (pemu_fakeroot && (unsigned) v == fake[PEMU_R]) ? (unsigned long) real : v;
}
static void pemu_set_ids(unsigned u, unsigned g) { for (int i = 0; i < 4; i++) { pemu_uid[i] = u; pemu_gid[i] = g; } pemu_fakeroot = 1; }
