/* linux-user/x86_64/signal.inc.h - signals. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ signals */
struct gsa { u64 handler, flags, restorer, mask; };
static struct gsa gsa[65];
static __thread siginfo_t pinfo[65];
/* sigaltstack state of the calling guest thread (a new thread starts without one) */
static __thread u64 g_ss_sp, g_ss_size;
static __thread sigjmp_buf g_jb;
static __thread volatile int g_fault; static __thread volatile u64 g_faddr;
static __thread volatile int g_fwrite;                /* the faulting access was a store (x86 page-fault error code bit 1) */
#if defined(__aarch64__)
#include <asm/sigcontext.h>
#endif
static int fault_was_write(void*u) {
#if defined(__aarch64__) && defined(ESR_MAGIC)
    ucontext_t*uc = u; u8*r = (u8*) uc->uc_mcontext.__reserved;
    for (int n = 0; n < 16; n++) {                        /* walk the reserved records until the ESR one */
        struct _aarch64_ctx*h = (struct _aarch64_ctx*) r;
        if (!h->magic || !h->size) break;
        if (h->magic == ESR_MAGIC) return (int)((((struct esr_context*) h)->esr >> 6) & 1);   /* data abort: WnR */
        r += h->size;
    }
    return 0;
#elif defined(__x86_64__)
    return (int)((((ucontext_t*) u)->uc_mcontext.gregs[REG_ERR] >> 1) & 1);
#else
    (void) u; return 0;
#endif
}

/* Android (and containers) kill a process with SIGSYS when it calls a syscall its seccomp filter does not allow.
 * The emulator forwards guest syscalls to the host, so turn such a kill into -ENOSYS: guests (glibc) then fall back. */
static void hsys(int s, siginfo_t*si, void*u) {
    ucontext_t*uc = u; (void) s;
#ifdef si_syscall
    if (g_trace && si) fprintf(stderr, "pemu: host seccomp blocked syscall %d -> ENOSYS\n", (int) si->si_syscall);
#else
    (void) si;
    if (g_trace) fprintf(stderr, "pemu: host seccomp blocked a syscall -> ENOSYS\n");
#endif
#if defined(__aarch64__)
    uc->uc_mcontext.regs[0] = (u64)(s64) -ENOSYS;
#elif defined(__x86_64__)
    uc->uc_mcontext.gregs[REG_RAX] = -ENOSYS;
#else
    (void) uc;
#endif
}
static void hsig(int s, siginfo_t*si, void*u) { (void) u; if (si) pinfo[s] = *si; __atomic_or_fetch(PENDP(), 1ULL << s, __ATOMIC_SEQ_CST); }
static void hsegv(int s, siginfo_t*si, void*u) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_fault(u, g_cur);                                     /* fault inside translated code: recover the guest rip */
#else
    (void) u;
#endif
    if (gsa[s].handler > 1) { g_fwrite = fault_was_write(u); g_fault = s; g_faddr = (u64) si->si_addr; pinfo[s] = *si; siglongjmp(g_jb, 1); }
    fprintf(stderr, "pemu: guest fault (signal %d) at addr=%p rip=%#llx\n", s, si->si_addr, (unsigned long long) g_cur->rip);
    signal(s, SIG_DFL); raise(s);
    _exit(128 + s);
}
static void fx_save(CPU*c, u8*p);                      /* FXSAVE image: fxsave, signal frame (defined with the x87 code) */
static void fx_restore(CPU*c, const u8*p);
static const int gidx[16] = {8, 9, 10, 11, 12, 13, 14, 15, 7, 6, 5, 3, 2, 0, 1, 4};
#define SFRAME 1088
static void trace_map(const char *tag, u64 a) {                  /* diagnostics: the mapping that holds address a, with the offset in its file */
    FILE *f = fopen("/proc/self/maps", "r"); char l[512];
    if (!f) return;
    while (fgets(l, sizeof l, f)) {
        unsigned long long lo, hi, off;
        if (sscanf(l, "%llx-%llx %*s %llx", &lo, &hi, &off) == 3 && a >= lo && a < hi) {
            l[strcspn(l, "\n")] = 0;
            fprintf(stderr, "pemu:   %s %#llx = file offset %#llx in: %s\n", tag, (unsigned long long) a, off + (a - lo), l); break;
        }
    }
    fclose(f);
}
static void deliver_sig(CPU*c, int s) {
    struct gsa*h = &gsa[s];
    if (g_trace && s == SIGSEGV && g_fault == s && g_faddr < 0x10000) {                     /* a jump to a bogus address: who called, and with what */
        fprintf(stderr, "pemu:   regs rax=%#llx rcx=%#llx rdx=%#llx rbx=%#llx rbp=%#llx rsi=%#llx rdi=%#llx r8=%#llx r9=%#llx r12=%#llx r13=%#llx r14=%#llx r15=%#llx\n",
                (unsigned long long) c->r[0], (unsigned long long) c->r[1], (unsigned long long) c->r[2], (unsigned long long) c->r[3], (unsigned long long) c->r[5],
                (unsigned long long) c->r[6], (unsigned long long) c->r[7], (unsigned long long) c->r[8], (unsigned long long) c->r[9], (unsigned long long) c->r[12],
                (unsigned long long) c->r[13], (unsigned long long) c->r[14], (unsigned long long) c->r[15]);
        for (int k = 0; k < 6; k++) {
            u64 w = ((u64*) c->r[RSP])[k]; char t[16]; snprintf(t, sizeof t, "[rsp+%d]", 8 * k);
            fprintf(stderr, "pemu:   %s = %#llx\n", t, (unsigned long long) w); trace_map(t, w);
        }
        if (g_ringi) for (unsigned k = g_ringi > 24 ? g_ringi - 24 : 0; k < g_ringi; k++) {            /* the last instructions, oldest first (interpreter only: PEMU_JIT=0) */
            const u8 *q = (const u8*) g_ring[k & 63];
            fprintf(stderr, "pemu:   exec %#llx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", (unsigned long long) g_ring[k & 63], q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9]);
            if (k == g_ringi - 1 || k == g_ringi - 24) trace_map("insn", g_ring[k & 63]);
        }
    }
    if (g_trace && (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE || s == SIGABRT || s == SIGTRAP))          /* diagnostics: what the guest's own handler is about to see */
        fprintf(stderr, "pemu: signal %d to guest handler: rip=%#llx fault addr=%#llx code=%d (rsp=%#llx)\n", s, (unsigned long long) c->rip,
                (unsigned long long)(g_fault == s ? g_faddr : 0), pinfo[s].si_code, (unsigned long long) c->r[RSP]);
    int on_alt = g_ss_size && c->r[RSP] >= g_ss_sp && c->r[RSP] - g_ss_sp < g_ss_size;     /* interrupted code already runs on the alternate stack */
    u64 top = ((h->flags & 0x08000000u /*SA_ONSTACK*/) && g_ss_size && !on_alt) ? g_ss_sp + g_ss_size : c->r[RSP] - 128;
    u64 sp = ((top - SFRAME) & ~15ULL) - 8;
    memset((void*) sp, 0, SFRAME);
    u64 uc = sp + 8, info = sp + 8 + 424, fp = sp + 568;
    memcpy((void*) info, &pinfo[s], 128);
    u64 old = 0;
    syscall(SYS_rt_sigprocmask, SIG_BLOCK, NULL, &old, 8);
    *(u64*)(uc + 296) = old;
    *(u64*)(uc + 16) = g_ss_sp; *(u32*)(uc + 24) = !g_ss_size ? 2 /*SS_DISABLE*/ : on_alt ? 1 /*SS_ONSTACK*/ : 0; *(u64*)(uc + 32) = g_ss_size;   /* uc_stack */
    u64 mc = uc + 40;
    for (int i = 0; i < 16; i++) *(u64*)(mc + 8 * (u64) i) = c->r[gidx[i]];
    *(u64*)(mc + 8 * 16) = c->rip; *(u64*)(mc + 8 * 17) = RF(c);
    *(u64*)(mc + 8 * 22) = g_fault == s ? g_faddr : 0;
    {   /* gregs[REG_ERR]=19, gregs[REG_TRAPNO]=20: Wine's segv_handler dispatches on the trap number */
        u64 trap = 0, err = 0;
        switch (s) {
        case SIGSEGV: case SIGBUS:
            if (pinfo[s].si_code == 0x80) trap = 13;                                    /* #GP from a privileged instruction */
            else if (g_fault == s) { trap = 14; err = 4 | (g_fwrite ? 2 : 0) | (pinfo[s].si_code == 2 ? 1 : 0); }   /* #PF: user, write?, protection? */
            break;
        case SIGILL: trap = 6; break;                                                   /* #UD */
        case SIGTRAP: trap = 3; break;                                                  /* #BP */
        case SIGFPE: trap = (pinfo[s].si_code == 1 || pinfo[s].si_code == 2) ? 0 : 19; break;   /* #DE, else #XM */
        default: break;
        }
        *(u64*)(mc + 8 * 19) = err; *(u64*)(mc + 8 * 20) = trap;
    }
    *(u64*)(mc + 8 * 23) = fp;
    fx_save(c, (u8*) fp);
    u64 nm = old | h->mask | ((h->flags & 0x40000000) ? 0 : (1ULL << (s - 1)));
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &nm, NULL, 8);
    *(u64*) sp = (h->flags & 0x04000000) ? h->restorer : g_tramp;
    c->r[RSP] = sp; c->r[RDI] = (u64) s; c->r[RSI] = info; c->r[RDX] = uc; c->r[RAX] = 0;
    c->rip = h->handler; RF(c) &= ~(u64)DF_;
    if (h->flags & 0x80000000u) h->handler = 0;
    g_fault = 0;
}
static void g_sigreturn(CPU*c) {
    u64 sp = c->r[RSP] - 8, uc = sp + 8, mc = uc + 40, fp = *(u64*)(mc + 8 * 23);
    for (int i = 0; i < 16; i++) c->r[gidx[i]] = *(u64*)(mc + 8 * (u64) i);
    c->rip = *(u64*)(mc + 8 * 16); RF(c) = *(u64*)(mc + 8 * 17);
    if (fp) fx_restore(c, (const u8*) fp);
    u64 m = *(u64*)(uc + 296);
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &m, NULL, 8);
}
static void check_pending(CPU*c) {
    volatile u64 *pp = PENDP();
    while (*pp) {
        u64 p = *pp; int s = __builtin_ctzll(p);
        __atomic_and_fetch(pp, ~(1ULL << s), __ATOMIC_SEQ_CST);
        if (gsa[s].handler > 1) deliver_sig(c, s);
    }
}

