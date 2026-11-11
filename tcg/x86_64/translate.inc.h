/* tcg/x86_64/translate.inc.h - branches, instruction dispatch, block translation, run loop. SPDX-License-Identifier: Apache-2.0 */
/* ---- branches and system instructions; 1 = block ended, 0 = continue, -1 = not handled */
static int j_branch(u64 pc, u32 i, int fl) {
    int sf = i >> 31, rd = i & 31, rn = (i >> 5) & 31;
    if ((i & 0x7C000000) == 0x14000000) {                      /* B / BL */
        if (i >> 31) { mov_ri(RAX, pc + 4); stq(1, RAX, R15, OFF_X(30)); }
        jexit_direct(pc, pc + (u64)(sext(i & 0x3ffffff, 26) * 4));
        return 1;
    } else if ((i & 0x7E000000) == 0x34000000) {               /* CBZ / CBNZ */
        gld(sf, RAX, rd); test_rr(sf, RAX, RAX);
        u8 *t = jcc_fwd(((i >> 24) & 1) ? 5 : 4);
        jexit_direct(pc, pc + 4); here(t);
        jexit_direct(pc, pc + (u64)(sext((i >> 5) & 0x7ffff, 19) * 4));
        return 1;
    } else if ((i & 0x7E000000) == 0x36000000) {               /* TBZ / TBNZ */
        int bit = ((i >> 31) << 5) | ((i >> 19) & 31);
        gld(1, RAX, rd); op_r(0, 1, 0x0FBA, 4, RAX, 0); e1((u32) bit);
        u8 *t = jcc_fwd(((i >> 24) & 1) ? 2 : 3);
        jexit_direct(pc, pc + 4); here(t);
        jexit_direct(pc, pc + (u64)(sext((i >> 5) & 0x3fff, 14) * 4));
        return 1;
    } else if ((i & 0xFF000010) == 0x54000000) {               /* B.cond */
        int cc = i & 15; u64 tgt = pc + (u64)(sext((i >> 5) & 0x7ffff, 19) * 4);
        if ((cc >> 1) == 7) { jexit_direct(pc, tgt); return 1; }
        if (!fl) fl_restore();
        u8 *t = jcc_fwd(xcc(cc));
        jexit_direct(pc, pc + 4); here(t);
        jexit_direct(pc, tgt);
        return 1;
    } else if ((i & 0xFE000000) == 0xD6000000) {               /* BR / BLR / RET */
        int opc = (i >> 21) & 15;
        if (opc > 2) return -1;
        gld(1, RAX, rn);
        if (opc == 1) { mov_ri(RCX, pc + 4); stq(1, RCX, R15, OFF_X(30)); }
        jexit_indirect();
        return 1;
    } else if ((i & 0xFFE0001F) == 0xD4000001) {               /* SVC */
        set_pc(pc + 4); mov_rr(1, RDI, R15); call_abs((void*) jit_svc); jmp_to(jepi);
        return 1;
    } else if ((i & 0xFF000000) == 0xD4000000) {               /* BRK etc. */
        j_fallback(pc, 1); return 1;
    } else if ((i & 0xFFFFFFE0) == 0xD50B7420) {               /* DC ZVA */
        gld(1, RAX, rd); alu_ri(1, A_AND, RAX, -64);
        e1(0x0F); e1(0x57); e1(0xC0);                          /* xorps xmm0, xmm0 */
        for (int k = 0; k < 4; k++) op_m(0, 0, 0x0F11, 0, RAX, 16 * k, 0);
    } else if ((i & 0xFFF80000) == 0xD5080000) {               /* SYS (cache maintenance) */
        int crn = (i >> 12) & 15, crm = (i >> 8) & 15;
        if (crn == 7 && (crm == 5 || crm == 1)) {              /* IC: translated code may be stale */
            mov_rr(1, RDI, R15); call_abs((void*) jit_flush_helper);
            set_pc(pc + 4); jmp_to(jepi);
            return 1;
        }
    } else if ((i & 0xFFFFF000) == 0xD5033000 || (i & 0xFFFFF01F) == 0xD503201F || (i & 0xFFFFF000) == 0xD5032000) {
        /* barriers / hints */
    } else if ((i & 0xFFF00000) == 0xD5300000 && ((i >> 5) & 0x7fff) == 0x5E82) {   /* MRS TPIDR_EL0 */
        ldq(1, RAX, R15, OFF_TPIDR); gst(RAX, rd);
    } else if ((i & 0xFFF00000) == 0xD5100000 && ((i >> 5) & 0x7fff) == 0x5E82) {   /* MSR TPIDR_EL0 */
        gld(1, RAX, rd); stq(1, RAX, R15, OFF_TPIDR);
    } else if ((i & 0xFFF8F01F) == 0xD500401F) {               /* MSR (immediate) - ignored */
    } else if ((i & 0xFFF00000) == 0xD5300000 || (i & 0xFFF00000) == 0xD5100000) {
        j_fallback(pc, 0);
    } else return -1;
    return 0;
}

static int jit_insn(u64 pc, u32 i, int fl, int *flive) {
    int r;
    switch ((i >> 25) & 15) {
    case 8: case 9:
        r = j_dpimm(pc, i, flive); if (r < 0) { j_fallback(pc, 0); return 0; } return r;
    case 10: case 11:
        r = j_branch(pc, i, fl); if (r < 0) { j_fallback(pc, 1); return 1; } return r;
    case 4: case 6: case 12: case 14:
        r = j_ldst(pc, i); if (r < 0) j_fallback(pc, 0); return 0;
    case 5: case 13:
        r = j_dpreg(pc, i, fl, flive); if (r < 0) { j_fallback(pc, 0); return 0; } return r;
    default:
        j_fallback(pc, 0); return 0;       /* SIMD/FP and anything else: interpreter (undef exits the process) */
    }
}

static u8 *jtranslate(u64 pc0) {
    if ((size_t)(jlim - jcur) < (512u << 10) || jmapn + 4096 > JMAP_CAP || jhn > (1u << JH_BITS) / 2 || jpgn > (1u << JPG_BITS) / 2) jflush();
    u8 *start = jcur;
    jins(pc0, start); jstat_blocks++;
    u64 pc = pc0; int flive = 0, ended = 0, n = 0;
    while (!ended && n < JMAXI) {
        u32 ins = *(u32*) pc;
        jm_off[jmapn] = (u32)(jcur - jbase); jm_pc[jmapn] = pc; jmapn++;
        int fl = flive; flive = 0;
        ended = jit_insn(pc, ins, fl, &flive);
        n++; pc += 4;
    }
    if (!ended) jexit_direct(pc - 4, pc);
    for (u64 p = pc0 >> 12; p <= (pc - 1) >> 12; p++) jpg_add(p);
    return start;
}

static int jit_init(void) {
    size_t hs = sizeof(JEnt) << JH_BITS, is = sizeof(JEnt) << JIC_BITS, ps = sizeof(u64) << JPG_BITS;
    int pr = PROT_READ | PROT_WRITE, fg = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    jbase = mmap(NULL, JCACHE_SZ, pr | PROT_EXEC, fg, -1, 0);
    if (jbase == MAP_FAILED) return -1;
    jh = mmap(NULL, hs, pr, fg, -1, 0); jic = mmap(NULL, is, pr, fg, -1, 0);
    jm_off = mmap(NULL, JMAP_CAP * 4ull, pr, fg, -1, 0); jm_pc = mmap(NULL, JMAP_CAP * 8ull, pr, fg, -1, 0);
    jpg = mmap(NULL, ps, pr, fg, -1, 0);
    if (jh == MAP_FAILED || jic == MAP_FAILED || jm_off == MAP_FAILED || jm_pc == MAP_FAILED || jpg == MAP_FAILED) return -1;
    jlim = jbase + JCACHE_SZ;
    static const u8 enter[] = {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x08,
                               0x49, 0x89, 0xFF, 0x49, 0x89, 0xD6, 0xFF, 0xE6};
    static const u8 epi[] = {0x48, 0x83, 0xC4, 0x08, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5D, 0x5B, 0xC3};
    jenter = jbase; memcpy(jenter, enter, sizeof enter);
    jepi = jbase + 64; memcpy(jepi, epi, sizeof epi);
    jcur = jbase + JTRAMP_SZ;
    return 0;
}

static void run_jit(CPU *c) {
    jsync_out(c);
    if (sigsetjmp(g_jb, 1)) {                                  /* host fault with a guest handler */
        jsync_in(c); g_os->host_fault(c); jsync_out(c);
    }
    void (*enter)(CPU*, u8*, void*) = (void (*)(CPU*, u8*, void*)) (void*) jenter;
    for (;;) {
        if (c->jflag) { c->jflag = 0; if (pend) { jsync_in(c); g_os->pending(c); jsync_out(c); } }
        u64 pc = c->pc;
        u8 *code = jfind(pc);
        if (!code) code = jtranslate(pc);
        if (c->patch) { s32 d = (s32)(code - ((u8*) c->patch + 4)); memcpy((void*) c->patch, &d, 4); c->patch = 0; }
        u32 h = (u32)(pc >> 2) & ((1u << JIC_BITS) - 1);
        jic[h].pc = pc; jic[h].code = code;
        enter(c, code, jic);
    }
}
