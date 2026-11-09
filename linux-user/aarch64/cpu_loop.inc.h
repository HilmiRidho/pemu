/* linux-user/aarch64/cpu_loop.inc.h - run loop. SPDX-License-Identifier: Apache-2.0 */
/* ---- hook-up of the Linux layer to the CPU core (PemuOS in target/aarch64/cpu.inc.h) */
static void linux_host_fault(CPU *c) {                         /* host fault with a guest handler */
    int s = g_fault; sigset_t ss; sigemptyset(&ss); sigaddset(&ss, s); sigprocmask(SIG_UNBLOCK, &ss, NULL);
    siginfo_t si; memset(&si, 0, sizeof si); si.si_signo = s; pinfo[s] = si;
    deliver_sig(c, s);
}
static void linux_exception(CPU *c, int excp) {
    (void) c;
    switch (excp) {
    case EXCP_BRK: raise(SIGTRAP); break;
    case EXCP_UDEF: signal(SIGILL, SIG_DFL); raise(SIGILL); _exit(132);
    }
}
static const PemuOS linux_os = { do_svc, linux_exception, check_pending, linux_host_fault };
__attribute__((constructor)) static void linux_os_register(void) { g_os = &linux_os; }      /* before main(), so self-test builds that rename main() work too */

static void run(CPU*c) {
    if (sigsetjmp(g_jb, 1)) linux_host_fault(c);
    for (;;) { if (pend) check_pending(c); step(c); }
}

