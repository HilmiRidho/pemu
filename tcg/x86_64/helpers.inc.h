/* tcg/x86_64/helpers.inc.h - helpers called from translated code, block exits. SPDX-License-Identifier: Apache-2.0 */
/* ---- helpers called from translated code */
static void jit_interp(CPU *c) { jsync_in(c); step(c); jsync_out(c); }
static void jit_svc(CPU *c) { jsync_in(c); g_os->syscall(c); jsync_out(c); }
static void jit_flush_helper(CPU *c) { (void) c; jflush(); }

/* ---- control flow exits */
static void jpoll_exit(u64 target) {
    op_m(0, 1, 0x83, 7, R15, OFF_JFLAG, 0); e1(0);
    u8 *ok = jcc_fwd(4);
    mov_ri(RAX, target); stq(1, RAX, R15, OFF_PC); jmp_to(jepi);
    here(ok);
}
static void jexit_direct(u64 from, u64 target) {
    if (target <= from) jpoll_exit(target);
    u8 *tc = jfind(target);
    if (tc) { jmp_to(tc); return; }
    u8 *site = jcur; e1(0xE9); e4(0);
    mov_ri(RAX, (u64)(site + 1)); stq(1, RAX, R15, OFF_PATCH);   /* address of the rel32 */
    mov_ri(RAX, target); stq(1, RAX, R15, OFF_PC);
    jmp_to(jepi);
}
static void jexit_indirect(void) {                  /* target in RAX */
    op_m(0, 1, 0x83, 7, R15, OFF_JFLAG, 0); e1(0);
    u8 *m1 = jcc_fwd(5);
    mov_rr(1, RCX, RAX); sh_i(1, S_SHR, RCX, 2); alu_ri(0, A_AND, RCX, (1 << JIC_BITS) - 1); sh_i(0, S_SHL, RCX, 4);
    alu_rr(1, A_ADD, RCX, R14);
    op_m(0, 1, 0x39, RAX, RCX, 0, 0);               /* cmp [rcx], rax */
    u8 *m2 = jcc_fwd(5);
    op_m(0, 0, 0xFF, 4, RCX, 8, 0);                 /* jmp [rcx+8] */
    here(m1); here(m2);
    stq(1, RAX, R15, OFF_PC); jmp_to(jepi);
}
static void j_fallback(u64 pc, int term) {
    set_pc(pc);
    mov_rr(1, RDI, R15);
    call_abs((void*) jit_interp);
    if (term) jmp_to(jepi);
}
