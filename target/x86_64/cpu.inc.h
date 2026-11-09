/* target/x86_64/cpu.inc.h - guest CPU state, types, host includes. SPDX-License-Identifier: Apache-2.0 */
/* pemu_x64 - small x86_64 user-mode emulator, a qemu-user alternative
 * for aarch64 hosts (Termux/Android, Linux ARM).
 *
 * Guest: x86_64 Linux. Host: aarch64 (the target) or x86_64 (used for testing).
 * The guest is mapped 1:1 into the host address space, so guest pointers are
 * host pointers. Guest syscalls are translated to the host generic syscall
 * ABI (openat, newfstatat, pipe2, ...), so the same code runs on both hosts.
 *
 * Command line (qemu-user compatible):
 *     pemu [-0 argv0] [-L prefix] [-E VAR=val] [-U VAR] [-cpu x] [-strace] prog [args...]
 * Environment: QEMU_SET_ENV, QEMU_UNSET_ENV, QEMU_STRACE.
 *
 * Arithmetic flags are evaluated lazily (see mat()/RF()), and jcc/setcc/cmov after cmp/test read the operands directly.
 * CPU model: x86-64-v2 (SSE2 to SSE4.2, CMPXCHG16B, POPCNT) with an x87 subset. cpuid reports only
 * what is implemented: every instruction of an advertised feature must exist, because glibc picks its code paths from it.
 * Threads (CLONE_THREAD) are supported: each guest thread runs on its own host pthread with its own CPU state.
 * NOT supported: AVX/AVX2/FMA/BMI/AES/XSAVE, parts of x87, 32-bit code.
 *
 * JIT (aarch64 hosts): guest basic blocks are translated to AArch64 code (see the "JIT" section below). Anything the
 * translator does not know runs through the interpreter from inside the translated block, so behaviour is identical.
 * Disable it with PEMU_JIT=0 or -nojit. The JIT works with guest threads (one shared cache, see JT).
 * PEMU_JIT_OPT=<mask> (default 127) toggles optional translations: 1 = lazy flags for shift-by-immediate, 2 = scalar SSE float,
 * 4 = guest rax..rdi pinned to host registers, 8 = superblocks, 16 = return stack buffer, 32 = comiss/comisd/ucomiss/ucomisd with the host
 * flags of fcmp, 64 = div/idiv through udiv/sdiv (only when the dividend fits and the divisor is not 0 or -1; the rest is interpreted),
 * 1024 = ldapur/stlur for aligned integer accesses of multi-threaded guests (needs FEAT_LRCPC2; see m_ld).
 * Not a security boundary. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <ucontext.h>
#include <setjmp.h>
#include <math.h>
#include <elf.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/auxv.h>
#include <linux/futex.h>
#include <stddef.h>

#if (defined(__aarch64__) || defined(PEMU_JIT_EMBED)) && !defined(PEMU_NO_JIT)
#define HAVE_JIT 1
#endif

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u16 __attribute__((may_alias, aligned(1))) u16u;
typedef u32 __attribute__((may_alias, aligned(1))) u32u;
typedef u64 __attribute__((may_alias, aligned(1))) u64u;

typedef union { u8 b[16]; s8 sb[16]; u16 w[8]; s16 sw[8]; u32 d[4]; s32 sd[4]; u64 q[2]; s64 sq[2]; float f[4]; double df[2]; } V;

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI };
#define CF_ 0x1
#define PF_ 0x4
#define AF_ 0x10
#define ZF_ 0x40
#define SF_ 0x80
#define DF_ 0x400
#define OF_ 0x800

#define RSB_N 64
typedef struct {
    u64 r[16];
    u64 rip, rf, fs, gs;
    u64 la, lb, lr;                                   /* lazy arithmetic flags: operands/result of the last add/sub/logic op */
    union { struct { u8 lop, lsz, lcin; }; u64 lmeta; };   /* lmeta = lop | lsz << 8 | lcin << 16 (the JIT stores it in one go) */
    u64 patch;                                        /* JIT: chain patch site left by an exit stub (bit 0: it is a bl); 1 = interpret one instruction */
    u64 rsbi; u64 rsb[RSB_N][2];                      /* JIT return stack buffer: ring of (guest return address, host continuation) pairs, filled by call, checked by ret */
    u64 mm[8];                                        /* MMX registers (kept apart from the x87 stack they alias on real hardware) */
    V x[16] __attribute__((aligned(16)));
    u32 mxcsr; u16 fcw, fsw;
    long double st[8]; int top;
} CPU;

static CPU cpu;
static __thread CPU *g_cur = &cpu;        /* CPU of the calling host thread (one host thread per guest thread) */
static __thread int g_is_thread;           /* set in threads created by CLONE_THREAD */
static __thread u64 g_clear_tid;           /* CLONE_CHILD_CLEARTID address of this thread */
static int g_trace;
static char g_self[4096];
static u64 g_tramp;
#ifdef HAVE_JIT
static volatile int g_jit_on;                 /* JIT active */
static void jit_fault(void*uctx, CPU*c);
static void jit_post_syscall(CPU*c, u64 n, long r);
static void jit_note_rwx(u64 start, u64 len);
static void jit_report(void);
static void jit_thread_init(void);
static void jit_thread_exit(void);
static void jit_region_abort(void);
static void jit_rsb_clear(CPU *c);
static void jit_mt_enable(void);
static void jit_atfork(int phase);
#endif

/* The emulated CPU is an Intel Core i7-920 (Bloomfield, Nehalem, D0 stepping): family 6 model 0x1a stepping 5, 2.66 GHz, 4 cores,
 * 32K+32K L1, 256K L2, 8M L3. It is the real CPU whose feature set matches what pemu implements (x86-64-v2: SSE4.2, POPCNT,
 * CMPXCHG16B, no AES/PCLMUL/AVX). cpuid() and /proc/cpuinfo both take their identity from here, so they cannot disagree. */
#define PEMU_BRAND     "Intel(R) Core(TM) i7 CPU         920  @ 2.67GHz"     /* 47 characters: leaves 0x80000002-4 hold 48 bytes */
#define PEMU_MHZ_STR   "2666.667"
#define PEMU_BOGO_STR  "5333.33"                                              /* Linux prints about 2 x MHz */

/* Exit-request words, polled by the JIT (like qemu's cpu->exit_request). They belong to the core; the OS layer sets them
 * (linux-user/: host signal handlers) and clears them when it has dealt with the request. */
static volatile u64 pend;                          /* events for the main thread (the JIT polls this word) */
static __thread volatile u64 tpend;                /* events for a CLONE_THREAD thread: delivered to that same thread, like the kernel does */
#define PENDP() (g_is_thread ? &tpend : &pend)
static __thread u64 g_ring[64]; static __thread unsigned g_ringi;      /* diagnostics (QEMU_STRACE): the last interpreted instruction addresses */

/* ------------------------------------------------------------ interface to the OS layer
 * The CPU core (target/ and tcg/) never calls into linux-user/ by name: everything that needs an operating system goes through
 * this table, which linux-user/ installs before the first guest instruction runs (the same hand-off qemu-user does when
 * cpu_exec() returns an exception to cpu_loop()).
 *   syscall(c)       the guest executed `syscall`; c->rip is already the next instruction, the arguments are in c->r[]
 *                    and the result goes to c->r[RAX]
 *   exception(c, e)  the CPU raised exception e (EXCP_*, x86 vector numbers). It may return, and the core then carries on
 *                    after the instruction, or it may terminate the process. */
enum { EXCP_DE = 0, EXCP_BP = 3, EXCP_UD = 6, EXCP_GP = 13 };
typedef struct PemuOS {
    void (*syscall)(CPU *c);
    void (*exception)(CPU *c, int excp);
} PemuOS;
static const PemuOS *g_os;
