/* linux-user/aarch64/signal.inc.h - signals. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ signals */
struct gsa { u64 handler, flags, mask; };
static struct gsa gsa[65];
static siginfo_t pinfo[65];
static volatile int g_fault; static volatile u64 g_faddr;

static void hsig(int s, siginfo_t*si, void*u) { (void) u; if (si) pinfo[s] = *si; __atomic_or_fetch(&pend, 1ULL << s, __ATOMIC_SEQ_CST); cpu.jflag = 1; }
static void hsegv(int s, siginfo_t*si, void*u) {
    if (g_jit && u) { u64 gp = jit_rip2pc((u64)((ucontext_t*) u)->uc_mcontext.gregs[REG_RIP]); if (gp) cpu.pc = gp; }
    if (gsa[s].handler > 1) { g_fault = s; g_faddr = (u64) si->si_addr; siglongjmp(g_jb, 1); }
    fprintf(stderr, "pemu: guest fault (signal %d) at addr=%p pc=%#llx\n", s, si->si_addr, (unsigned long long) cpu.pc);
    signal(s, SIG_DFL); raise(s);
    _exit(128 + s);
}
#define FRAME 1136
static void deliver_sig(CPU*c, int s) {
    struct gsa*h = &gsa[s];
    u64 sp = (c->sp - FRAME) & ~15ULL;
    memset((void*) sp, 0, FRAME);
    memcpy((void*) sp, &pinfo[s], 128);
    if (g_fault == s) { ((int*)sp)[0] = s; }
    u64 uc = sp + 128;
    u64 old = 0;
    syscall(SYS_rt_sigprocmask, SIG_BLOCK, NULL, &old, 8);
    *(u64*)(uc + 40) = old;
    u64 mc = uc + 176;
    *(u64*)(mc + 0) = g_fault == s ? g_faddr : 0;
    memcpy((void*)(mc + 8), c->x, 31 * 8);
    *(u64*)(mc + 256) = c->sp; *(u64*)(mc + 264) = c->pc; *(u64*)(mc + 272) = (u64) c->nzcv << 28;
    memcpy((void*)(mc + 288), c->v, 512);
    u64 nm = old | h->mask | ((h->flags & 0x40000000) ? 0 : (1ULL << (s - 1)));
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &nm, NULL, 8);
    c->x[0] = (u64) s; c->x[1] = sp; c->x[2] = uc;
    c->sp = sp; c->pc = h->handler; c->x[30] = g_tramp;
    if (h->flags & 0x80000000u) h->handler = 0;
    g_fault = 0;
}
static void g_sigreturn(CPU*c) {
    u64 sp = c->sp, uc = sp + 128, mc = uc + 176;
    memcpy(c->x, (void*)(mc + 8), 31 * 8);
    c->sp = *(u64*)(mc + 256); c->pc = *(u64*)(mc + 264); c->nzcv = (u32)(*(u64*)(mc + 272) >> 28) & 15;
    memcpy(c->v, (void*)(mc + 288), 512);
    u64 m = *(u64*)(uc + 40);
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &m, NULL, 8);
}
static void check_pending(CPU*c) {
    while (pend) {
        u64 p = pend; int s = __builtin_ctzll(p) ;
        __atomic_and_fetch(&pend, ~(1ULL << s), __ATOMIC_SEQ_CST);
        if (gsa[s].handler > 1) deliver_sig(c, s);
    }
}

