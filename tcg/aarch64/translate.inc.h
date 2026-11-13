/* tcg/aarch64/translate.inc.h - JIT translation, dispatch, hooks. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ translation, dispatch, hooks */
static void jit_stw_locked(void) {                       /* flush the whole cache. jmu held, the caller is not active */
    JT *me = &t_jt;
    __atomic_store_n(&g_jstop, 1, __ATOMIC_SEQ_CST);
    for (JT *t = jreg; t; t = t->next) if (t != me) __atomic_or_fetch((u64*) t->pp, 1ULL, __ATOMIC_SEQ_CST);     /* bit 0: leave the cache (no signal has number 0) */
    for (JT *t = jreg; t; t = t->next) if (t != me) while (__atomic_load_n(&t->active, __ATOMIC_SEQ_CST)) sched_yield();
    jflush();
    __atomic_add_fetch(&g_jgen, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_jstop, 0, __ATOMIC_SEQ_CST);
}
static void jit_flush_req(void) {
    pthread_mutex_lock(&jmu);
    if (__atomic_exchange_n(&g_jflush_req, 0, __ATOMIC_ACQ_REL)) jit_stw_locked();
    pthread_mutex_unlock(&jmu);
}

/* Flags at a block exit. A block used to assume the flags live at every exit and so wrote the lazy flag state before each one. Looking at the code
 * the exit leads to shows that they are usually dead: the first instruction that touches them overwrites them without reading them (the next cmp, test,
 * add, ...). Returns 1 only when that is certain: instructions are decoded from pages this block has already read (so no new page is touched and the
 * pages are in jpg, which flushes the cache when they change), and anything unknown, indirect or flag-reading counts as "live". */
static int jflags_dead(u64 ip, const JI *ji, int n) {
    for (int k = 0; k < 12; k++) {
        if ((ip & 0xfff) > 4096 - 16) return 0;
        int seen = 0;
        for (int i = 0; i < n; i++) if ((ji[i].ip >> 12) == (ip >> 12)) { seen = 1; break; }
        if (!seen) return 0;
        JI t;
        if (!jdecode(&t, ip)) return 0;
        if (t.fr) return 0;
        if (t.fw) return 1;
        if (t.op == JO_JMP) { ip = t.target; continue; }
        if (t.op >= JO_JMP || t.op == JO_FBT) return 0;
        ip += t.len;
    }
    return 0;
}
/* Can the flags of an instruction of this kind be left in the host flags and be read by this jcc alone? (the same table jmapcc uses) */
static int jmap_ok(int kind, int x86cc) { int sv = fl.kind, cc; fl.kind = kind; int m = jmapcc(x86cc, &cc); fl.kind = sv; return m == 0; }

static void jtranslate(u64 pc) {                          /* the caller is not active; the block is found with jfind() afterwards */
    JI ji[JMAXI]; int n = 0; u64 ip = pc;
    int sb = !!(g_jopt & JOPT_SB), rsb = !!(g_jopt & JOPT_RSB);
    while (n < JMAXI) {                                   /* decoding reads guest code but touches no JIT state: a fault here is the guest's fault, and no lock is held */
        if (n && (ip & 0xfff) > 4096 - 16) break;               /* an instruction may cross into a page we have not looked at */
        JI *j = &ji[n];
        if (!jdecode(j, ip)) { memset(j, 0, sizeof *j); j->ip = ip; j->op = JO_FBT; j->fr = j->fw = 1; n++; break; }
        n++;
        if (j->op == JO_JCC) { j->dt = (u8) jflags_dead(j->target, ji, n); j->dn = (u8) jflags_dead(j->ip + j->len, ji, n); }
        else if (j->op == JO_JMP) j->dt = (u8) jflags_dead(j->target, ji, n);
        /* Superblock: keep translating along the likely path. Only addresses on pages this block already reads are followed, so that
         * decoding never touches a page the guest has not reached yet (it could be unmapped: the fault belongs to the guest, later). */
        if (j->op == JO_JMP && sb && n < JMAXI && (j->target & 0xfff) <= 4096 - 16) {
            int ok = 0;
            for (int k = 0; k < n; k++) { if (ji[k].ip == j->target) { ok = 0; break; } if ((ji[k].ip >> 12) == (j->target >> 12)) ok = 1; }
            if (ok) { j->op = JO_NOP; j->cnt = j->target <= j->ip; ip = j->target; continue; }
        }
        if (j->op == JO_JCC && sb && n < JMAXI && j->target > j->ip) { j->cnt = 1; ip = j->ip + j->len; continue; }       /* forward: assume not taken */
        if ((j->op == JO_CALL || j->op == JO_CALLI) && rsb && n < JMAXI) {                                                /* the callee returns here through the return stack buffer */
#ifdef PEMU_JIT_ABI_FLAGS
            j->fw = 1;
#else
            j->fr = 1;
#endif
            j->cnt = 1; ip = j->ip + j->len; continue;
        }
        ip += j->len;
        if (jterm(j)) break;
    }
    u64 endip = ip;
    if (n && !jterm(&ji[n - 1])) ji[n - 1].dn = (u8) jflags_dead(endip, ji, n);       /* the block ends by falling through */
    pthread_mutex_lock(&jmu);
    if (jfind(pc)) { pthread_mutex_unlock(&jmu); return; }       /* another thread was faster */
    if ((u64)(jlim - jcur) < 2 * JBLOCK_MAX || jmapn + 4 * JMAXI > JMAP_CAP || jhn * 2 > (1u << JH_BITS)) jit_stw_locked();
    if (g_jstats)
        for (int i = 0; i < n; i++)
            if (ji[i].op == JO_FB || ji[i].op == JO_FBT) {
                jfbh[jfb_idx((const u8*) ji[i].ip)]++; jfbv[jfb_key((const u8*) ji[i].ip)]++; JST3(jfb3, ji[i].ip); jst_fb++;
            }
    for (int i = 0; i < n; i++) {                                 /* pages that now hold translated code */
        jpg_add(ji[i].ip >> 12);
        if (ji[i].len) jpg_add((ji[i].ip + ji[i].len - 1) >> 12);
    }
    /* flag liveness, backwards. Flags are assumed live when the block ends. With -DPEMU_JIT_ABI_FLAGS they are assumed dead
     * across call/ret (the SysV ABI does not preserve them): faster, but wrong for assembly that returns a result in the flags. */
#ifdef PEMU_JIT_ABI_FLAGS
    int live = (ji[n - 1].op == JO_CALL || ji[n - 1].op == JO_RET || ji[n - 1].op == JO_CALLI) ? 0 : 1;
#else
    int live = 1;
#endif
    if (ji[n - 1].op == JO_JMP) live = !ji[n - 1].dt;                 /* the exits of the last instruction: dead when the code they lead to overwrites the flags first */
    else if (!jterm(&ji[n - 1])) live = !ji[n - 1].dn;
    for (int i = n - 1; i >= 0; i--) {
        ji[i].lva = (u8) live;
        if (ji[i].fr) live = 1; else if (ji[i].fw) live = 0;
        ji[i].lvb = (u8) live;
    }
    for (int i = 0; i + 1 < n; i++) {                                 /* a flag producer read only by the jcc right after it, whose exits all find the flags dead */
        const JI *c = &ji[i + 1];
        if (c->op != JO_JCC || !(g_jopt & JOPT_FUSE)) continue;
        int freeexit = c->cnt ? (!c->lva && c->dt) : (c->dt && c->dn);
        int kind = jfuse_form(&ji[i]);
        if (kind < 0 || !jmap_ok(kind, c->t)) continue;
        if (freeexit) ji[i].fuse = 1;
        else if (ji[i].sz >= 4) ji[i].fuse = 2;                       /* the exits that may read the flags rebuild them (fl_mat) */
    }
    u32 *start = jcur;
    jt_start = start; jt_pc = pc;
    memset(&fl, 0, sizeof fl);
    int ended = 0; jnstub = 0;
    for (int i = 0; i < n && !ended; i++) {
        u32 k = jmapn; jm_off[k] = (u32)((u8*) jcur - (u8*) jbase); jm_pc[k] = ji[i].ip;
        __atomic_store_n(&jmapn, k + 1, __ATOMIC_RELEASE);          /* the entry is complete before the fault handler of another thread can see it */
        ended = jemit(&ji[i]);
    }
    if (!ended) { fl_flush(ji[n - 1].lva); jx_direct(ji[n - 1].ip, endip); }
    jstubs_emit();
    jst_blocks++; jst_ins += (unsigned) n; jst_words += (unsigned long long)(jcur - start);
    j_sync_icache(start, jcur);
    jt_start = NULL; jt_pc = 0;
    jins(pc, start);                                              /* published: other threads may run it from now on */
    pthread_mutex_unlock(&jmu);
}

#define JSTOP 0x1000ULL
#ifdef PEMU_JIT_EMBED
static __thread u64 jemb_stack[8192];
#endif
static void jit_enter(CPU *c, u32 *code) {
    JT *t = &t_jt;
#ifdef PEMU_JIT_EMBED
    u64 x[32] = {0};
    x[0] = (u64) c; x[1] = (u64) code; x[2] = (u64)(void*) t->pp; x[3] = (u64) t->ic; x[30] = JSTOP; x[31] = (u64)(jemb_stack + 8192);
    pemu_embed_run(x, (u64) jenter, JSTOP);
#else
    ((void (*)(CPU*, u32*, volatile u64*, JEnt*)) (void*) jenter)(c, code, t->pp, t->ic);
#endif
}

/* A thread that has seen a flush starts again with an empty inline cache and no pending chain patch. */
static void jit_rsb_clear(CPU *c) { c->rsbi = 0; for (int i = 0; i < RSB_N; i++) { c->rsb[i][0] = ~0ULL; c->rsb[i][1] = 0; } }
static void jit_resync(JT *t, CPU *c) {
    memset(t->ic, 0, sizeof(JEnt) << JIC_BITS);
    c->patch = 0; jit_rsb_clear(c);
    t->gen = g_jgen;
#ifndef PEMU_JIT_EMBED
    __asm__ volatile("isb" ::: "memory");
#endif
}
static inline void jreg_enter(JT *t, CPU *c) {
    for (;;) {
        __atomic_store_n(&t->active, 1, __ATOMIC_SEQ_CST);
        if (!__atomic_load_n(&g_jstop, __ATOMIC_SEQ_CST)) break;
        __atomic_store_n(&t->active, 0, __ATOMIC_SEQ_CST);      /* a flush is running: wait for it */
        pthread_mutex_lock(&jmu); pthread_mutex_unlock(&jmu);
    }
    if (__builtin_expect(t->gen != __atomic_load_n(&g_jgen, __ATOMIC_ACQUIRE), 0)) jit_resync(t, c);
}
static inline void jreg_leave(JT *t) { __atomic_store_n(&t->active, 0, __ATOMIC_SEQ_CST); }

static void jit_dispatch(CPU *c) {
    JT *t = &t_jt;
    for (;;) {
        if (__builtin_expect(__atomic_load_n(&g_jflush_req, __ATOMIC_ACQUIRE), 0)) jit_flush_req();
        u64 pc = c->rip;
        if (c->patch == 1) {                                  /* the block ended with an instruction the translator does not handle: interpret it here */
            c->patch = 0;
            if (__builtin_expect(g_jstats, 0)) { jdyn[jfb_idx((const u8*) pc)]++; jdynv[jfb_key((const u8*) pc)]++; JST3(jdyn3, pc); jst_dyn++; }
            step(c); return;
        }
        jreg_enter(t, c);
        u32 *code = jfind(pc);
        if (!code) {
            jreg_leave(t);
            if ((pc & 0xfff) > 4096 - 16 || jit_is_rwx(pc)) { c->patch = 0; step(c); return; }     /* interpret: see header comment */
            jtranslate(pc);
            continue;
        }
        if (c->patch) {                                       /* chain: turn the exit's b (or bl, bit 0) into a branch to the new code */
            u64 pv = c->patch; u32 *site = (u32*)(pv & ~3ULL);
            *site = ((pv & 1) ? 0x94000000u : 0x14000000u) | ((u32)(code - site) & 0x3ffffff);
            j_sync_icache(site, site + 1); c->patch = 0; jst_chain++;
        }
        t->ic[icidx(pc)].pc = pc; t->ic[icidx(pc)].code = code;
        jit_enter(c, code);
        jreg_leave(t);
        return;
    }
}

static u64 jit_pc2rip(void *u) {
    u64 pc;
#ifdef PEMU_JIT_EMBED
    (void) u; pc = pemu_embed_pc(); pemu_embed_clear();
#else
    pc = (u64) ((ucontext_t*) u)->uc_mcontext.pc;
#endif
    if (!pc || !jbase || pc < (u64) jbase || pc >= (u64) jcur) return 0;
    u32 off = (u32)(pc - (u64) jbase), lo = 0, hi = __atomic_load_n(&jmapn, __ATOMIC_ACQUIRE);
    if (!hi) return 0;
    while (hi - lo > 1) { u32 mid = (lo + hi) / 2; if (jm_off[mid] <= off) lo = mid; else hi = mid; }
    return jm_off[lo] <= off ? jm_pc[lo] : 0;
}
static void jit_fault(void *u, CPU *c) {                          /* fault in translated code: guest rip and the pinned registers come from the signal context */
    u64 regs[8], r;
#ifdef PEMU_JIT_EMBED
    for (int i = 0; i < 8; i++) regs[i] = pemu_embed_x(jpin_reg[i]);
#else
    for (int i = 0; i < 8; i++) regs[i] = ((ucontext_t*) u)->uc_mcontext.regs[jpin_reg[i]];
#endif
    r = jit_pc2rip(u);                                            /* (the embedded interpreter's state is cleared here) */
    if (!r) return;
    c->rip = r;
    if (g_jpin) memcpy(c->r, regs, sizeof regs);
}
static void jit_region_abort(void) { __atomic_store_n(&t_jt.active, 0, __ATOMIC_SEQ_CST); }      /* after a fault longjmp'ed out of the cache */

static void jit_post_syscall(CPU *c, u64 n, long r) {
    u64 a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10];
    if (n != 9 && n != 10 && n != 11 && n != 25) return;
    pthread_mutex_lock(&jmu);                                     /* jpg / jrwx are shared with the translators */
    switch (n) {
    case 9:                                                       /* mmap */
        if ((u64) r >= 0xfffffffffffff000ULL) break;
        if ((a2 & PROT_EXEC) && (a2 & PROT_WRITE)) jit_note_rwx((u64) r, a1);
        if ((a3 & MAP_FIXED) && jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE);
        break;
    case 10:                                                      /* mprotect */
        if (r) break;
        if ((a2 & PROT_EXEC) && (a2 & PROT_WRITE)) jit_note_rwx(a0, a1);
        if (jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE);
        break;
    case 11: if (!r && jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE); break;        /* munmap */
    case 25: if ((u64) r < 0xfffffffffffff000ULL && jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE); break;   /* mremap */
    default: break;
    }
    pthread_mutex_unlock(&jmu);
}
#ifdef PEMU_JIT_PROF
static void jit_prof_report(void);
#endif
static void jit_report(void) {
#ifdef PEMU_JIT_PROF
    jit_prof_report();
#endif
    if (!g_jstats) return;
    g_jstats = 0;
    fprintf(stderr, "pemu jit: %llu blocks, %llu guest instructions translated (%.1f AArch64 instructions each), %llu chained exits, %llu cache flushes\n",
            jst_blocks, jst_ins, jst_ins ? (double) jst_words / (double) jst_ins : 0.0, jst_chain, jst_flush);
    fprintf(stderr, "pemu jit: %llu flag producers fused with their jcc, %llu jcc exits that skip the flag spill (flags dead there)\n", jst_fused, jst_xdead);
    fprintf(stderr, "pemu jit: %u translated instructions fall back to the interpreter; opcodes by number of sites:", jst_fb);
    for (int k = 0; k < 8; k++) {
        int b = -1; for (int i = 0; i < 512; i++) if (jfbh[i] && (b < 0 || jfbh[i] > jfbh[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " %s%02x x%u", b >= 256 ? "0f " : "", b & 255, jfbh[b]); jfbh[b] = 0;
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "pemu jit: %llu interpreter fallbacks executed; most executed opcodes (count):", jst_dyn);
    for (int k = 0; k < 12; k++) {
        int b = -1; for (int i = 0; i < 512; i++) if (jdyn[i] && (b < 0 || jdyn[i] > jdyn[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " %s%02x x%llu", b >= 256 ? "0f " : "", b & 255, jdyn[b]); jdyn[b] = 0;
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "pemu jit: fallback variants by sites (prefixes, /reg, r=register m=memory form):");
    for (int k = 0; k < 8; k++) {
        int b = -1; for (int i = 0; i < (1 << 18); i++) if (jfbv[i] && (b < 0 || jfbv[i] > jfbv[b])) b = i;
        if (b < 0) break;
        char nm[40]; jfb_keyname((unsigned) b, nm); fprintf(stderr, " [%s] x%u", nm, jfbv[b]); jfbv[b] = 0;
    }
    fprintf(stderr, "\n");
    for (int pass = 0; pass < 2; pass++) {                       /* 0F 38 / 0F 3A: the third opcode byte, which the variant lines above cannot show */
        int any = 0; for (int i = 0; i < 512; i++) if (pass ? jdyn3[i] : jfb3[i]) any = 1;
        if (!any) continue;
        fprintf(stderr, "pemu jit: 0f 38 / 0f 3a third opcode byte, %s:", pass ? "executed fallbacks (count)" : "translated sites");
        for (int k = 0; k < 12; k++) {
            int b = -1;
            for (int i = 0; i < 512; i++) if ((pass ? jdyn3[i] : jfb3[i]) && (b < 0 || (pass ? jdyn3[i] > jdyn3[b] : jfb3[i] > jfb3[b]))) b = i;
            if (b < 0) break;
            fprintf(stderr, " %s/%02x x%llu", b >= 256 ? "3a" : "38", b & 255, pass ? jdyn3[b] : (unsigned long long) jfb3[b]);
            if (pass) jdyn3[b] = 0; else jfb3[b] = 0;
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "pemu jit: executed fallback variants:");
    for (int k = 0; k < 12; k++) {
        int b = -1; for (int i = 0; i < (1 << 18); i++) if (jdynv[i] && (b < 0 || jdynv[i] > jdynv[b])) b = i;
        if (b < 0) break;
        char nm[40]; jfb_keyname((unsigned) b, nm); fprintf(stderr, " [%s] x%llu", nm, jdynv[b]); jdynv[b] = 0;
    }
    fprintf(stderr, "\n");
}

static void *jmap(size_t n) {
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static int jit_init(void) {
    void *m = mmap(NULL, JCACHE_SZ, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (m == MAP_FAILED) return -1;                               /* no executable memory: stay with the interpreter */
    jbase = m; jlim = jbase + JCACHE_SZ / 4;
    jh = jmap(sizeof(JEnt) << JH_BITS); jpg = jmap(sizeof(u64) << JPG_BITS);
    jm_off = jmap(4ul * JMAP_CAP); jm_pc = jmap(8ul * JMAP_CAP);
    if (!jh || !jpg || !jm_off || !jm_pc) return -1;
    { const char *eo = getenv("PEMU_JIT_OPT"); if (eo && *eo) g_jopt = (unsigned) strtoul(eo, NULL, 0); }
    g_jpin = !!(g_jopt & JOPT_PIN);
#ifdef PEMU_JIT_EMBED
    g_jrcpc = !!(g_jopt & JOPT_RCPC);                             /* the interpreter of pemu.c executes ldapur/stlur (and aborts on a misaligned one) */
#else
    g_jrcpc = (g_jopt & JOPT_RCPC) && (getauxval(AT_HWCAP) & (1ul << 26));      /* HWCAP_ILRCPC: FEAT_LRCPC2 (ldapur/stlur) */
#endif
    jcur = jbase;
    jenter = jcur;                                                /* void jenter(CPU *c, void *code, volatile u64 *pend, JEnt *ic) */
    E(0xA9800000u | (u32)(-12 & 0x7f) << 15 | 30u << 10 | 31u << 5 | 29u);           /* stp x29, x30, [sp, #-96]! */
    a_stp(27, 28, 31, 16); a_stp(25, 26, 31, 32); a_stp(19, 20, 31, 48); a_stp(21, 22, 31, 64); a_stp(23, 24, 31, 80);
    a_mov(28, 0); a_mov(27, 2); a_mov(26, 3);
    j_pin_fill();                                                 /* guest rax..rdi into their host registers */
    E(0xD5033FDFu);                                               /* isb: code other threads translated is visible */
    a_br(1);
    jepi = jcur;
    j_pin_spill();
    a_ldp(25, 26, 31, 32); a_ldp(27, 28, 31, 16); a_ldp(19, 20, 31, 48); a_ldp(21, 22, 31, 64); a_ldp(23, 24, 31, 80);
    E(0xA8C00000u | 12u << 15 | 30u << 10 | 31u << 5 | 29u);                         /* ldp x29, x30, [sp], #96 */
    E(0xD65F03C0u);                                               /* ret */
    jcur = jbase + JTRAMP;
    j_sync_icache(jbase, jcur);
    g_jstats = getenv("PEMU_JIT_STATS") != NULL;
    return 0;
}
/* Every guest thread registers once before it runs translated code; see the comment at JT. */
static void jit_thread_init(void) {
    JT *t = &t_jt;
    t->ic = jmap(sizeof(JEnt) << JIC_BITS);
    if (!t->ic) return;                                           /* this thread stays with the interpreter (run() checks t_jt.ic) */
    t->pp = PENDP(); t->active = 0;
    pthread_mutex_lock(&jmu);
    t->gen = g_jgen; t->next = jreg; jreg = t;
    pthread_mutex_unlock(&jmu);
}
static void jit_thread_exit(void) {
    JT *t = &t_jt;
    if (!t->ic) return;
    pthread_mutex_lock(&jmu);
    for (JT **pp = &jreg; *pp; pp = &(*pp)->next) if (*pp == t) { *pp = t->next; break; }
    pthread_mutex_unlock(&jmu);
    munmap(t->ic, sizeof(JEnt) << JIC_BITS); t->ic = NULL;
}
static void jit_mt_enable(void) {                                 /* the guest is about to get its second thread */
    if (g_jmt) return;
    pthread_mutex_lock(&jmu);
    g_jmt = 1; jit_stw_locked();                                  /* everything translated so far has no memory barriers */
    pthread_mutex_unlock(&jmu);
}
static void jit_atfork(int phase) {                               /* 0 before fork(), 1 in the parent after it, 2 in the child */
    if (phase == 0) { pthread_mutex_lock(&jmu); return; }
    if (phase == 2) { jreg = t_jt.ic ? &t_jt : NULL; t_jt.next = NULL; }      /* the other threads do not exist in the child */
    pthread_mutex_unlock(&jmu);
}
#ifdef PEMU_JIT_PROF
#include "tests/jit_prof.inc.h"
#endif
