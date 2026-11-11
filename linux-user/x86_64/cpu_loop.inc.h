/* linux-user/x86_64/cpu_loop.inc.h - run loop. SPDX-License-Identifier: Apache-2.0 */
/* ---- hook-up of the Linux layer to the CPU core (PemuOS in target/x86_64/cpu.inc.h) */
static void linux_exception(CPU *c, int excp) {
    switch (excp) {
    case EXCP_DE: raise(SIGFPE); break;
    case EXCP_BP: raise(SIGTRAP); break;
    case EXCP_UD: signal(SIGILL, SIG_DFL); raise(SIGILL); _exit(132);
    case EXCP_GP:                                      /* in/out/cli/sti/hlt: a real CPU raises #GP and Linux delivers SIGSEGV (si_code SI_KERNEL) */
        if (gsa[SIGSEGV].handler > 1) {
            memset(&pinfo[SIGSEGV], 0, sizeof pinfo[SIGSEGV]);
            pinfo[SIGSEGV].si_signo = SIGSEGV; pinfo[SIGSEGV].si_code = 0x80;
            g_fault = 0;
            deliver_sig(c, SIGSEGV);
            return;
        }
        signal(SIGSEGV, SIG_DFL); raise(SIGSEGV); _exit(128 + SIGSEGV);
    }
}
static const PemuOS linux_os = { do_syscall, linux_exception };
__attribute__((constructor)) static void linux_os_register(void) { g_os = &linux_os; }      /* before main(), so self-test builds that rename main() work too */


static void run(CPU*c) {
#ifdef HAVE_JIT
    if (g_jit_on) { jit_thread_init(); jit_rsb_clear(c); }
#endif
    if (sigsetjmp(g_jb, 1)) {
        int s = g_fault; sigset_t ss; sigemptyset(&ss); sigaddset(&ss, s); sigprocmask(SIG_UNBLOCK, &ss, NULL);
#ifdef HAVE_JIT
        c->patch = 0; jit_region_abort();
#endif
        deliver_sig(c, s);
    }
    for (;;) {
#ifdef HAVE_JIT
        if (g_jit_on && t_jt.ic) { if (*PENDP()) { c->patch = 0; check_pending(c); } jit_dispatch(c); continue; }
#endif
        if (*PENDP()) check_pending(c);
        step(c);
    }
}

