/* target/aarch64/cpu.inc.h - guest CPU state, types, host includes. SPDX-License-Identifier: Apache-2.0 */
/* pemu - small aarch64 user-mode emulator for x86_64 Linux hosts.
 *
 * A minimal, self-written user-mode emulator. It interprets AArch64 code,
 * maps the guest 1:1 into the host address space (so guest pointers are host
 * pointers) and translates guest Linux syscalls to host syscalls.
 *
 * Command line: pemu [-strace] [-nojit] prog [args...]   (see "pemu --help")
 * Environment: PEMU_SET_ENV, PEMU_STRACE.
 *
 * Supported: integer ISA, load/store (incl. exclusives, SIMD&FP register
 * forms, LD1/ST1), scalar FP, a small NEON subset, fork/exec, signals.
 * NOT supported: threads (CLONE_THREAD), LSE atomics, most of NEON, SVE.
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
#include <setjmp.h>
#include <math.h>
#include <elf.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/auxv.h>
#include <sys/random.h>
#include <stddef.h>
#include <ucontext.h>

#ifndef __x86_64__
#error "pemu currently targets x86_64 hosts (an aarch64 host does not need it)"
#endif

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int64_t s64; typedef int32_t s32;
typedef u16 __attribute__((may_alias, aligned(1))) u16u;
typedef u32 __attribute__((may_alias, aligned(1))) u32u;
typedef u64 __attribute__((may_alias, aligned(1))) u64u;

typedef struct {
    u64 x[32];          /* x[31] unused: sp kept separately */
    u64 sp, pc, tpidr, fpcr, fpsr;
    u32 nzcv;           /* N=8 Z=4 C=2 V=1 */
    u64 v[32][2];
    u64 xaddr, xval; int xsz;   /* exclusive monitor */
    u64 xfl, jflag, patch;      /* JIT: saved x86 flags, async-signal flag, chain patch site */
} CPU;

static CPU cpu;
static int g_trace;
static char g_self[4096];
static u64 g_tramp;
static int g_jit;                       /* JIT enabled */
static u64 jit_rip2pc(u64 rip);
static void jit_syscall_hook(u64 n, u64 a0, u64 a1, u64 a2, u64 a3);


/* Exit-request word, polled by the JIT; set by the OS layer's host signal handlers. */
static volatile u64 pend;
static sigjmp_buf g_jb;                 /* where a host fault in guest code lands (sigsetjmp in the run loops) */

/* ------------------------------------------------------------ interface to the OS layer
 * The CPU core (target/ and tcg/) never calls into linux-user/ by name: everything that needs an operating system goes through
 * this table, which linux-user/ installs before the first guest instruction runs.
 *   syscall(c)       the guest executed `svc`; c->pc is already the next instruction, the arguments are in c->x[]
 *   exception(c, e)  the CPU raised exception e (EXCP_*). It may return, and the core then carries on, or it may terminate
 *   pending(c)       the exit-request word is non-zero: deliver the queued guest signals
 *   host_fault(c)    a host fault in guest code came back through g_jb: turn it into a guest signal */
enum { EXCP_UDEF = 1, EXCP_BRK = 7 };
typedef struct PemuOS {
    void (*syscall)(CPU *c);
    void (*exception)(CPU *c, int excp);
    void (*pending)(CPU *c);
    void (*host_fault)(CPU *c);
} PemuOS;
static const PemuOS *g_os;
