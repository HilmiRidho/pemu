/* tests/embed_hook.inc.h - embedding hook (self-tests only). SPDX-License-Identifier: Apache-2.0 */
#ifdef PEMU_EMBED
/* ------------------------------------------------------------ embedding hook (self-tests only)
 * Built with -DPEMU_EMBED -Dmain=<unused>, this file is linked next to pemu_x64.c compiled with
 * -DPEMU_JIT_EMBED. pemu_x64's JIT then emits real AArch64 machine code, and that code is executed
 * by the interpreter above instead of by a CPU, which lets the AArch64 back end be tested on an
 * x86_64 machine. `svc #0x7001` is a host call: it calls the C function whose address is in x16
 * with x0..x5 as arguments and returns the result in x0 (what `blr x16` does on real hardware). */
static __thread int embed_in_host; static __thread CPU *embed_cur;      /* per thread: pemu_x64 runs one guest thread per host thread */
void (*pemu_embed_cb)(u64 pc, int host);                               /* optional profiler (tests/jit_prof.inc.h): called for every executed AArch64 instruction */
static void embed_hostcall(CPU *c) {
    typedef u64 (*fn6)(u64, u64, u64, u64, u64, u64);
    if (pemu_embed_cb) pemu_embed_cb(c->pc, 1);
    fn6 f = (fn6) c->x[16];
    embed_in_host++;
    c->x[0] = f(c->x[0], c->x[1], c->x[2], c->x[3], c->x[4], c->x[5]);
    embed_in_host--;
}
u64 pemu_embed_pc(void) { return (embed_cur && !embed_in_host) ? embed_cur->pc : 0; }
u64 pemu_embed_x(int n) { return embed_cur ? embed_cur->x[n] : 0; }      /* register n of the AArch64 code that is running (fault recovery) */
void pemu_embed_clear(void) { embed_cur = NULL; embed_in_host = 0; }
/* run AArch64 code at pc until it reaches `stop`; x[0..30] = registers, x[31] = sp (in/out) */
void pemu_embed_run(u64 *x, u64 pc, u64 stop) {
    static __thread CPU ec;
    memcpy(ec.x, x, 31 * 8); ec.sp = x[31]; ec.pc = pc; ec.nzcv = 0;
    embed_in_host = 0; embed_cur = &ec;
    while (ec.pc != stop) { if (pemu_embed_cb) pemu_embed_cb(ec.pc, 0); step(&ec); }
    memcpy(x, ec.x, 31 * 8); x[31] = ec.sp;
    embed_cur = NULL;
}
#endif
