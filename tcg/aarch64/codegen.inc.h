/* tcg/aarch64/codegen.inc.h - JIT flag state and code generation. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ flag state and code generation */
static struct { int nz, pend, kind, sz; } fl;        /* nz: host NZCV and x9-x11 hold the flags of kind/sz; pend: lazy fields not written yet */
static int jfb_idx(const u8 *q) {                               /* statistics: opcode of the instruction at q (0x100 + x for 0F xx) */
    while (*q == 0x66 || *q == 0xF3 || *q == 0xF2 || *q == 0x64 || *q == 0x65 || *q == 0x67 || *q == 0xF0 || (*q & 0xE7) == 0x26) q++;
    if ((*q & 0xF0) == 0x40) q++;
    return *q == 0x0F ? 256 + q[1] : *q;
}
/* statistics key: bits 0-8 opcode (0x100 + x for 0F xx), bit 9 66h, bit 10 lock, bit 11 67h, bit 12 REX.W, bits 13-15 modrm.reg and
 * bit 16 "memory operand" (both only for the opcodes whose modrm.reg selects the operation), bit 17 "has modrm.reg" */
static unsigned jfb_key(const u8 *q) {
    unsigned p66 = 0, lk = 0, a32 = 0, w = 0, ext = 0, mem = 0, hm = 0;
    for (;; q++) {
        u8 b = *q;
        if (b == 0x66) p66 = 1; else if (b == 0xF0) lk = 1; else if (b == 0x67) a32 = 1;
        else if (b == 0xF3 || b == 0xF2 || b == 0x64 || b == 0x65 || (b & 0xE7) == 0x26) { } else break;
    }
    if ((*q & 0xF0) == 0x40) { w = (*q >> 3) & 1; q++; }
    unsigned idx = *q == 0x0F ? 256u + q[1] : *q;
    const u8 *mp = q + (*q == 0x0F ? 2 : 1);
    switch (idx) {
    case 0x80: case 0x81: case 0x83: case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xF6: case 0xF7: case 0xFE: case 0xFF: case 0x1BA:
        hm = 1; ext = (*mp >> 3) & 7; mem = (*mp >> 6) != 3; break;
    default: break;
    }
    return idx | p66 << 9 | lk << 10 | a32 << 11 | w << 12 | ext << 13 | mem << 16 | hm << 17;
}
static void jfb_keyname(unsigned k, char *out) {
    unsigned idx = k & 511; char ext[8] = "";
    if ((k >> 17) & 1) snprintf(ext, sizeof ext, "/%u %c", (k >> 13) & 7, (k >> 16) & 1 ? 'm' : 'r');
    snprintf(out, 40, "%s%s%s%s%s%02x%s", (k >> 9) & 1 ? "66 " : "", (k >> 10) & 1 ? "lock " : "", (k >> 11) & 1 ? "67 " : "",
             (k >> 12) & 1 ? "w " : "", idx >= 256 ? "0f " : "", idx & 255, ext);
}
static void jh_step(CPU *c) {
    if (__builtin_expect(g_jstats, 0)) { jdyn[jfb_idx((const u8*) c->rip)]++; jdynv[jfb_key((const u8*) c->rip)]++; jst_dyn++; }
    step(c);
}

/* condition helper: after an interpreter fallback the flags live in memory, so jcc/setcc/cmov evaluate them with this
 * cheap call (it reads only the flags: no register spill/fill, no instruction decode) instead of interpreting. */
static u64 jh_cc(CPU *c, u64 cc) { return (u64) cond(c, (int) cc); }
static u64 jh_cf(CPU *c) { return (u64)(RF(c) & CF_ ? 1 : 0); }
/* exact flag updates for instructions whose flags the lazy model cannot express (bt: CF only, tzcnt/lzcnt: CF and ZF, popcnt: all six). The helper works on
 * the flags in memory (RF() materialises pending lazy flags); each returns the 0/1 value that the host flags then cache, see LZ_CFX / LZ_ZFX */
static u64 jh_btf(CPU *c, u64 bit) { u64 *f = &RF(c); *f = (*f & ~(u64) CF_) | (bit ? CF_ : 0); return bit ? 1 : 0; }
static u64 jh_f2(CPU *c, u64 cf, u64 zf) { u64 *f = &RF(c); *f = (*f & ~(u64)(CF_ | ZF_)) | (cf ? CF_ : 0) | (zf ? ZF_ : 0); return zf ? 1 : 0; }
static u64 jh_pcf(CPU *c, u64 cnt) { u64 *f = &RF(c); *f &= ~(u64)(CF_ | OF_ | AF_ | SF_ | PF_ | ZF_); if (!cnt) *f |= ZF_; return cnt ? 0 : 1; }
/* JIT-only flag states: the flags in memory are already exact (nothing pending) and the host flags additionally cache one condition:
 * LZ_CFX: host C = x86 CF (after bt), LZ_ZFX: host Z = x86 ZF (after tzcnt/lzcnt/popcnt). Both are set by "subs xzr, x0, #1" on the helper's 0/1 result. */
enum { LZ_CFX = 64, LZ_ZFX };
static void j_cc_helper(int cc) { a_mov(0, RCPU); a_movi(1, (u64) cc); j_callabs((void*) jh_cc); fl.nz = 0; fl.pend = 0; }   /* x0 = 0 or 1 */

static void fl_spill(void) {
    if (fl.kind == LZ_LOGIC) a_stp(31, 31, RCPU, (int) OFF_LA); else a_stp(9, 10, RCPU, (int) OFF_LA);
    c_st(11, OFF_LR);
    a_movi(17, (u64) fl.kind | (u64) fl.sz << 8);
    c_st(17, OFF_LMETA);
}
static void fl_flush(int live) { if (fl.pend) { if (live) fl_spill(); fl.pend = 0; } }

/* x86 condition code -> A64 condition for the host flags left by the last producer.
 * returns 0 normal (*cc set), 1 always true, 2 always false, -1 not expressible */
static int jmapcc(int x86cc, int *cc) {
    static const s8 sub_t[16] = { 6, 7, 3, 2, 0, 1, 9, 8, 4, 5, -1, -1, 11, 10, 13, 12 };
    static const s8 add_t[16] = { 6, 7, 2, 3, 0, 1, -1, -1, 4, 5, -1, -1, 11, 10, 13, 12 };
    static const s8 log_t[16] = { -2, -3, -2, -3, 0, 1, 0, 1, 4, 5, -1, -1, 11, 10, 13, 12 };
    static const s8 id_t[16] = { 6, 7, -1, -1, 0, 1, -1, -1, 4, 5, -1, -1, 11, 10, 13, 12 };   /* after inc/dec: CF is the old one, the rest come from adds/subs #1 */
    static const s8 sh_t[16] = { -1, -1, -1, -1, 0, 1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1 };   /* after shl/shr/sar only e/ne/s/ns come from the host flags */
    /* after comis*: CF = below or unordered, ZF = equal or unordered, PF = unordered, OF = SF = 0. fcmp leaves unordered V=1 N=0 Z=0 C=1, equal Z=1 C=1, less N=1,
     * greater C=1: below = lt, ae = ge, be = le, a = gt, p = vs, np = vc. e/ne/le/g also need the unordered case of ZF: not one A64 condition. */
    static const s8 fc_t[16] = { -2, -3, 11, 10, -1, -1, 13, 12, -2, -3, 6, 7, -2, -3, -1, -1 };
    static const s8 cfx_t[16] = { -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* after bt: only b/ae come from the host flags (C) */
    static const s8 zfx_t[16] = { -1, -1, -1, -1, 0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* after tzcnt/lzcnt/popcnt: e/ne (Z) */
    int v = fl.kind == LZ_CFX ? cfx_t[x86cc] : fl.kind == LZ_ZFX ? zfx_t[x86cc] : fl.kind == LZ_FCMP ? fc_t[x86cc] : fl.kind == LZ_SUB ? sub_t[x86cc] : fl.kind == LZ_ADD ? add_t[x86cc] : fl.kind >= LZ_INC ? id_t[x86cc] : fl.kind >= LZ_SHL ? sh_t[x86cc] : log_t[x86cc];
    if (v == -2) return 2;
    if (v == -3) return 1;
    if (v < 0) return -1;
    *cc = v; return 0;
}

static void j_ea(const JI *j, int *rb, int *roff) {   /* address = x[*rb] + *roff, *roff in -256..255; uses x12, x13 (*rb may be a pinned register) */
    s64 d = j->disp;
    if (j->base < 0 && j->index < 0) {
        a_movi(12, (u64) d);
        if (j->seg) { c_ld(13, j->seg == 1 ? OFF_FS : OFF_GS); a_addsh(12, 12, 13, 0); }
        *rb = 12; *roff = 0; return;
    }
    int rbase = -1, ridx = -1, cur;
    if (j->base >= 0) { int h = pin(j->base); if (h >= 0) rbase = h; else { c_ld(12, OFF_R(j->base)); rbase = 12; } }
    if (j->index >= 0) { int h = pin(j->index); if (h >= 0) ridx = h; else { c_ld(13, OFF_R(j->index)); ridx = 13; } }
    if (rbase >= 0 && ridx >= 0) { a_addsh(12, rbase, ridx, j->scale); cur = 12; }
    else if (rbase >= 0) cur = rbase;
    else { a_lsl(1, 12, ridx, j->scale); cur = 12; }
    if (j->seg) { c_ld(13, j->seg == 1 ? OFF_FS : OFF_GS); a_addsh(12, cur, 13, 0); cur = 12; }
    if (d >= -256 && d <= 255) { *rb = cur; *roff = (int) d; return; }
    if (d > 0 && d < (1 << 24)) {
        if (d & 0xfff) { a_addi(0, 12, cur, (u32) d & 0xfff, 0); cur = 12; }
        if (d >> 12) { a_addi(0, 12, cur, (u32)(d >> 12), 1); cur = 12; }
    }
    else if (d < 0 && d > -(1 << 24)) {
        u32 mm = (u32) -d;
        if (mm & 0xfff) { a_addi(1, 12, cur, mm & 0xfff, 0); cur = 12; }
        if (mm >> 12) { a_addi(1, 12, cur, mm >> 12, 1); cur = 12; }
    }
    else { a_movi(13, (u64) d); a_addsh(12, cur, 13, 0); cur = 12; }
    *rb = cur; *roff = 0;
}
static inline u64 szmask(int sz) { return sz == 8 ? ~0ULL : (1ULL << (sz * 8)) - 1; }
static void j_opld(int rt, const JI *j, int which, int sz, int rb, int off) {
    int ot = which ? j->bt : j->at, r = which ? j->br : j->ar;
    if (ot == OT_REG) g_ld(rt, r, sz, j->rex);
    else if (ot == OT_MEM) m_ld(sz, 0, 0, rt, rb, off);
    else a_movi(rt, j->imm & szmask(sz));
}
static void j_opst(int rt, const JI *j, int sz, int rb, int off) {
    if (j->at == OT_REG) g_st(rt, j->ar, sz, j->rex); else m_st(sz, rt, rb, off);
}
static void j_push(int rv) {                                    /* the store comes first: a fault leaves rsp unchanged */
    int hs = pin(RSP);
    if (hs >= 0) { m_st(8, rv, hs, -8); a_addi(1, hs, hs, 8, 0); return; }
    c_ld(1, OFF_R(RSP)); a_addi(1, 1, 1, 8, 0); m_st(8, rv, 1, 0); c_st(1, OFF_R(RSP));
}
static void j_pop(int rv) {
    int hs = pin(RSP);
    if (hs >= 0) { m_ld(8, 0, 0, rv, hs, 0); a_addi(0, hs, hs, 8, 0); return; }
    c_ld(1, OFF_R(RSP)); m_ld(8, 0, 0, rv, 1, 0); a_addi(0, 1, 1, 8, 0); c_st(1, OFF_R(RSP));
}

static void j_fb(const JI *j) {                       /* run one instruction through the interpreter */
    a_movi(0, j->ip); c_st(0, OFF_RIP); j_pin_spill(); a_mov(0, RCPU);
    j_callabs((void*) jh_step);
    j_pin_fill(); fl.nz = 0;
}

/* ---- block exits */
static void jx_direct(u64 from, u64 target) {
    if (target <= from) {                              /* backward branch: poll for pending signals */
        a_ldst_u(3, 1, 0, RPEND, 0);
        u32 *z = a_cbz_fwd(0, 0);
        a_movi(0, target); c_st(0, OFF_RIP); a_b(jepi);
        jpatch19(z);
    }
    u32 *tc = jfind_emit(target);
    if (tc) { a_b(tc); return; }
    u32 *site = jcur; E(0x14000001u);                  /* b .+4: patched to "b target" once the target is translated */
    a_adr(0, site); c_st(0, OFF_PATCH);
    a_movi(0, target); c_st(0, OFF_RIP); a_b(jepi);
}
static void jx_indirect(void) {                        /* guest target in x0 */
    a_ldst_u(3, 1, 1, RPEND, 0);
    u32 *s1 = a_cbz_fwd(1, 1);
    a_logsh(2, 2, 0, 0, 1, 13);                        /* eor x2, x0, x0, lsr #13 */
    a_andmask(2, 2, JIC_BITS);
    a_addsh(2, RIC, 2, 4);
    a_ldp(3, 4, 2, 0);
    a_addsub(1, 1, 1, 31, 3, 0);                       /* cmp x3, x0 */
    u32 *s2 = a_bcond_fwd(1);                          /* b.ne */
    a_br(4);
    jpatch19(s1); jpatch19(s2);
    c_st(0, OFF_RIP); a_b(jepi);
}

/* ---- deferred out-of-line exits of the block being translated (side exits of jcc, unresolved callees, indirect-call misses) */
enum { JS_SIDE, JS_CALL, JS_IND };
typedef struct { int kind; u32 *br[2]; u64 from, target; int fpend, fkind, fsz; } JStub;
static JStub jstub[2 * JMAXI]; static int jnstub;
static void jx_ret(void) {                              /* guest return address in x0: predicted by the return stack buffer, then the inline cache */
    if (!(g_jopt & JOPT_RSB)) { jx_indirect(); return; }
    a_ldst_u(3, 1, 1, RPEND, 0);
    u32 *s1 = a_cbz_fwd(1, 1);                         /* signal pending: slow path polls again and leaves */
    c_ld(2, OFF_RSBI); a_addi(1, 2, 2, 1, 0); a_andmask(2, 2, 6); c_st(2, OFF_RSBI);
    a_addsh(3, RCPU, 2, 4); a_ldp(4, 30, 3, OFF_RSB);
    a_addsub(1, 1, 1, 31, 4, 0);                       /* cmp x4, x0 */
    u32 *s2 = a_bcond_fwd(1);
    a_ret();                                           /* host ret: the CPU's own return predictor follows the bl of the call */
    jpatch19(s1); jpatch19(s2);
    jx_indirect();
}
static void jrsb_push(u64 retaddr, u32 **adrsite) {    /* uses x2-x5; *adrsite = adr of the host continuation, patched by the caller */
    c_ld(2, OFF_RSBI); a_addsh(3, RCPU, 2, 4);
    a_movi(4, retaddr); *adrsite = jcur; E(0);
    a_stp(4, 5, 3, OFF_RSB);
    a_addi(0, 2, 2, 1, 0); a_andmask(2, 2, 6); c_st(2, OFF_RSBI);
}
static void jstub_add(int kind, u32 *b0, u32 *b1, u64 from, u64 target) {
    JStub *t = &jstub[jnstub++]; t->kind = kind; t->br[0] = b0; t->br[1] = b1; t->from = from; t->target = target;
    t->fpend = fl.pend; t->fkind = fl.kind; t->fsz = fl.sz;
}
static void jstubs_emit(void) {
    for (int i = 0; i < jnstub; i++) {
        JStub *t = &jstub[i];
        if (t->kind == JS_CALL) {                       /* br[0] is a bl: becomes the chain patch site */
            u32 *site = t->br[0]; *site = 0x94000000u | ((u32)(jcur - site) & 0x3ffffff);
            a_adr(0, site); a_addi(0, 0, 0, 1, 0); c_st(0, OFF_PATCH);
            a_movi(0, t->target); c_st(0, OFF_RIP); a_b(jepi);
        } else if (t->kind == JS_IND) {
            for (int k = 0; k < 2; k++) if (t->br[k]) { u32 w = *t->br[k]; *t->br[k] = w | ((u32)(jcur - t->br[k]) & 0x7ffff) << 5; }
            c_st(0, OFF_RIP); a_b(jepi);
        } else {
            jpatch19(t->br[0]);
            if (t->fpend) { fl.kind = t->fkind; fl.sz = t->fsz; fl_spill(); }
            jx_direct(t->from, t->target);
        }
    }
    jnstub = 0;
}

/* ---- arithmetic: x9 = a, x10 = b (zero-extended to size) -> x11 = result; host flags + x9-x11 are the lazy state when nf */
static void alu_core(int t, int sz, int nf) {
    int sf = sz == 8, sh = sz == 1 ? 24 : sz == 2 ? 16 : 0;
    if (!nf) {
        switch (t) {
        case 0: a_addsub(sf, 0, 0, 11, 9, 10); break;
        case 1: a_log(sf, 1, 0, 11, 9, 10); break;
        case 4: a_log(sf, 0, 0, 11, 9, 10); break;
        case 6: a_log(sf, 2, 0, 11, 9, 10); break;
        default: a_addsub(sf, 1, 0, 11, 9, 10); break;      /* sub, cmp */
        }
        return;
    }
    switch (t) {
    case 0: case 5: case 7: {
        int sub = t != 0;
        if (sh) { a_lsl(0, 13, 9, sh); a_lsl(0, 14, 10, sh); a_addsub(0, sub, 1, 15, 13, 14); a_lsr(0, 11, 15, sh); }
        else a_addsub(sf, sub, 1, 11, 9, 10);
        fl.kind = sub ? LZ_SUB : LZ_ADD; break; }
    case 4:
        if (sh) { a_log(0, 0, 0, 11, 9, 10); a_lsl(0, 15, 11, sh); a_log(0, 3, 0, 31, 15, 15); } else a_log(sf, 3, 0, 11, 9, 10);
        fl.kind = LZ_LOGIC; break;
    default:
        a_log(sf, t == 1 ? 1 : 2, 0, 11, 9, 10);
        if (sh) { a_lsl(0, 15, 11, sh); a_log(0, 3, 0, 31, 15, 15); } else a_log(sf, 3, 0, 31, 11, 11);
        fl.kind = LZ_LOGIC; break;
    }
    fl.sz = sz; fl.nz = 1; fl.pend = 1;
}


/* ---- atomics, carry arithmetic. x86 locked read-modify-write operations are ldaxr/stlxr loops (a dmb ish after them once the
 * guest has several threads: a locked x86 instruction is a full barrier). The address is checked for natural alignment first:
 * exclusives fault on a misaligned address, so that case does the same operation with plain accesses, as the interpreter does.
 * Registers: x14 = address, x9 = old value, x10 = source operand, x11 = new value, x15 = store-exclusive status;
 * cmpxchg: x2 = rax, x3 = new value, x4 = value stored; xchg: x3 = value stored, x1 = old value. */
static inline int jszl(int sz) { return sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3; }
static void a_ldaxr(int sz, int rt, int rn) { E((u32) jszl(sz) << 30 | 0x085FFC00u | (u32) rn << 5 | (u32) rt); }
static void a_stlxr(int sz, int rs, int rt, int rn) { E((u32) jszl(sz) << 30 | 0x0800FC00u | (u32) rs << 16 | (u32) rn << 5 | (u32) rt); }
static void a_cbnz_back(int rt, u32 *target) { E(0x35000000u | ((u32)(target - jcur) & 0x7ffff) << 5 | (u32) rt); }   /* cbnz wt, target (backward) */
static void a_dmb_ish(void) { if (g_jmt) E(0xD5033BBFu); }
static void a_cmpx(int rn, int rm) { a_addsub(1, 1, 1, 31, rn, rm); }                                              /* cmp xn, xm */

static void j_cf_x8(void) {                           /* x8 = the x86 CF of the flags as they are now (0 or 1); may spill pending flags and clobber x0-x15 */
    if (fl.nz && (fl.kind == LZ_ADD || fl.kind == LZ_SUB || fl.kind == LZ_LOGIC || fl.kind == LZ_INC || fl.kind == LZ_DEC)) {
        if (fl.kind == LZ_ADD) a_cset(8, 2);                       /* carry set: hs */
        else if (fl.kind == LZ_SUB) a_cset(8, 3);                  /* x86 CF is the borrow: lo */
        else if (fl.kind == LZ_LOGIC) a_movi(8, 0);
        else a_mov(8, 10);
    } else if (fl.nz && fl.kind == LZ_CFX) a_cset(8, 2);           /* bt: host C is CF */
    else { fl_flush(1); a_mov(0, RCPU); j_callabs((void*) jh_cf); a_mov(8, 0); }
}

static void j_adc(const JI *j) {                      /* adc / sbb, 32 or 64 bit. Flags stay lazy; the carry-in is recorded in lmeta (bit 16) */
    int sz = j->sz, sf = sz == 8, rb = 0, off = 0, sbb = j->t == 3, hm = j->at == OT_MEM || j->bt == OT_MEM;
    j_cf_x8();
    fl_flush(1); fl.nz = 0;                           /* memory holds the exact old flags before anything can fault */
    if (hm) j_ea(j, &rb, &off);
    j_opld(9, j, 0, sz, rb, off); j_opld(10, j, 1, sz, rb, off);
    if (j->lva) {
        if (sbb) a_addsub(1, 1, 1, 31, 31, 8);        /* subs xzr, xzr, x8: host C = !CF */
        else E(0xF1000400u | 8u << 5 | 31u);          /* subs xzr, x8, #1: host C = CF */
        E((sbb ? 0x7A000000u : 0x3A000000u) | (sf ? 0x80000000u : 0u) | 10u << 16 | 9u << 5 | 11u);    /* adcs / sbcs x11, x9, x10 */
        j_opst(11, j, sz, rb, off);                   /* the store may fault: the flags are written after it */
        a_stp(9, 10, RCPU, (int) OFF_LA); c_st(11, OFF_LR);
        a_movi(17, (u64)(sbb ? LZ_SUB : LZ_ADD) | (u64) sz << 8); a_logsh(1, 17, 17, 8, 0, 16); c_st(17, OFF_LMETA);   /* lmeta |= carry-in << 16 */
        fl.kind = sbb ? LZ_SUB : LZ_ADD; fl.sz = sz; fl.nz = 1; fl.pend = 0;
    } else {
        a_addsub(sf, sbb, 0, 11, 9, 10); a_addsub(sf, sbb, 0, 11, 11, 8);
        j_opst(11, j, sz, rb, off);
    }
}

enum { AT_ALU, AT_XCHG, AT_CMPXCHG, AT_XADD };
static void j_atom_body(const JI *j, int mode, int ex) {
    int sz = j->sz, sl = jszl(sz), vin = mode == AT_CMPXCHG ? 4 : mode == AT_XCHG ? 3 : 11, old = mode == AT_XCHG ? 1 : 9;
    u32 *loop = jcur;
    if (ex) a_ldaxr(sz, old, 14); else a_ldst_s(sl, 1, old, 14, 0);
    switch (mode) {
    case AT_ALU: alu_core(j->t, sz, 0); break;                    /* x11 = x9 op x10 */
    case AT_XADD: a_addsub(1, 0, 0, 11, 9, 10); break;
    case AT_CMPXCHG: a_cmpx(9, 2); a_csel(1, 0, 4, 3, 9); break;  /* store the new value when equal, else write the old one back (as the hardware does) */
    default: break;
    }
    if (ex) { a_stlxr(sz, 15, vin, 14); a_cbnz_back(15, loop); } else a_ldst_s(sl, 0, vin, 14, 0);
}
static void j_atomic(const JI *j, int mode) {
    int sz = j->sz, rb = 0, off = 0, incdec = mode == AT_ALU && j->incdec && j->lva;
    if (incdec) { j_cf_x8(); fl_flush(1); fl.nz = 0; }            /* inc/dec leave CF alone: it is carried over in x8 */
    else if (mode == AT_XCHG) fl_flush(j->lvb);                   /* no flags involved: the pending state survives (nothing below touches NZCV or x9-x11) */
    else { fl_flush(j->lvb); fl.nz = 0; }
    switch (mode) {
    case AT_ALU: j_opld(10, j, 1, sz, rb, off); break;            /* register or immediate source */
    case AT_XCHG: g_ld(3, j->br, sz, j->rex); break;
    case AT_CMPXCHG: g_ld(2, RAX, sz, 0); g_ld(3, j->br, sz, j->rex); break;
    default: g_ld(10, j->br, sz, j->rex); break;
    }
    j_ea(j, &rb, &off);
    if (off >= 0) a_addi(0, 14, rb, (u32) off, 0); else a_addi(1, 14, rb, (u32) -off, 0);
    u32 *slow = NULL, *done = NULL;
    if (sz > 1) { a_andmask(13, 14, sz == 2 ? 1 : sz == 4 ? 2 : 3); slow = a_cbz_fwd(1, 13); }
    j_atom_body(j, mode, 1);
    if (slow) { done = a_b_fwd(); jpatch19(slow); j_atom_body(j, mode, 0); jpatch26(done); }
    a_dmb_ish();
    switch (mode) {
    case AT_XCHG: g_st(1, j->br, sz, j->rex); break;
    case AT_XADD: g_st(9, j->br, sz, j->rex); if (j->lva) alu_core(0, sz, 1); break;
    case AT_CMPXCHG: {
        a_cmpx(9, 2); u32 *e = a_bcond_fwd(0); g_st(9, RAX, sz, 0); jpatch19(e);     /* rax = old value, only when the compare failed */
        if (j->lva) { a_mov(10, 9); a_mov(9, 2); alu_core(7, sz, 1); }               /* flags of cmp rax, old */
        break; }
    default:
        if (incdec) { alu_core(j->t ? 5 : 0, sz, 1); a_mov(10, 8); fl.kind = j->t ? LZ_DEC : LZ_INC; }
        else if (j->lva) alu_core(j->t, sz, 1);
    }
}

static int jemit(const JI *j) {          /* returns 1 when it ended the block (the exit is emitted) */
    int sz = j->sz, rb = 0, off = 0, hm = j->at == OT_MEM || j->bt == OT_MEM, sf = sz == 8;
    switch (j->op) {
    case JO_NOP:
        if (j->cnt) {                                    /* followed backward jmp: poll for signals here, since no exit stub will */
            a_ldst_u(3, 1, 0, RPEND, 0);
            u32 *z = a_cbz_fwd(0, 0);
            if (fl.pend) fl_spill();
            a_movi(0, j->target); c_st(0, OFF_RIP); a_b(jepi);
            jpatch19(z);
        }
        break;
    case JO_FB: fl_flush(j->lvb); j_fb(j); break;
    case JO_FBT:                                         /* not translated, ends the block: the dispatcher interprets it (outside the cache, so a blocking system call never holds up a flush) */
        fl_flush(1); a_movi(0, j->ip); c_st(0, OFF_RIP); a_movi(0, 1); c_st(0, OFF_PATCH); a_b(jepi); return 1;
    case JO_MOV:
        if (hm) { fl_flush(j->lvb); j_ea(j, &rb, &off); }
        j_opld(0, j, 1, sz, rb, off); j_opst(0, j, sz, rb, off); break;
    case JO_MOVX: {
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(j->sz2, j->sx, j->sz == 8, 0, rb, off); }
        else g_ldx(0, j->br, j->sz2, j->sx, j->sz, j->rex);
        g_st(0, j->ar, j->sz, j->rex); break; }
    case JO_LEA: {
        j_ea(j, &rb, &off);
        if (off >= 0) a_addi(0, 0, rb, (u32) off, 0); else a_addi(1, 0, rb, (u32) -off, 0);
        if (j->sz == 4) a_movw(0, 0);
        g_st(0, j->ar, j->sz, j->rex); break; }
    case JO_PUSH:
        fl_flush(j->lvb);
        if (j->at == OT_REG) g_ld(0, j->ar, 8, 0);
        else if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(8, 0, 0, 0, rb, off); }      /* push m64: the address is computed before rsp moves */
        else a_movi(0, j->imm);
        j_push(0); break;
    case JO_XCHGA: g_ld(0, RAX, sz, 0); g_ld(1, j->ar, sz, 0); g_st(1, RAX, sz, 0); g_st(0, j->ar, sz, 0); break;
    case JO_XCHG: j_atomic(j, AT_XCHG); break;
    case JO_CMPXCHG: j_atomic(j, AT_CMPXCHG); break;
    case JO_XADD: j_atomic(j, AT_XADD); break;
    case JO_XMOVQ:                                       /* movq xmm, xmm/m64: the upper half becomes zero */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(8, 0, rb, off); } else f_ldc(8, 0, OFF_X(j->br));
        q_stc(0, OFF_X(j->ar)); break;
    case JO_POP: fl_flush(j->lvb); j_pop(0); g_st(0, j->ar, 8, 0); break;
    case JO_LEAVE:
        fl_flush(j->lvb);
        if (pin(RBP) >= 0) { m_ld(8, 0, 0, 2, pin(RBP), 0); a_addi(0, pin(RSP), pin(RBP), 8, 0); a_mov(pin(RBP), 2); break; }
        c_ld(0, OFF_R(RBP)); m_ld(8, 0, 0, 2, 0, 0); a_addi(0, 0, 0, 8, 0); c_st(0, OFF_R(RSP)); c_st(2, OFF_R(RBP)); break;
    case JO_CWDE:
        if (sz == 8) { g_ld(0, RAX, 4, 0); a_sxtw(0, 0); g_st(0, RAX, 8, 0); }
        else { g_ld(0, RAX, 2, 0); a_sxth(0, 0, 0); g_st(0, RAX, 4, 0); }
        break;
    case JO_CDQ:
        if (sz == 8) { g_ld(0, RAX, 8, 0); a_asr(1, 0, 0, 63); g_st(0, RDX, 8, 0); }
        else { g_ld(0, RAX, 4, 0); a_asr(0, 0, 0, 31); g_st(0, RDX, 4, 0); }
        break;
    case JO_ALU: {
        int nf = j->lva;
        if (j->lk) { j_atomic(j, AT_ALU); break; }
        if (j->t == 2 || j->t == 3) { j_adc(j); break; }
        if (j->incdec && nf) {                                       /* inc/dec with live flags: CF passes through. x8 = the CF of the flags so far; the new state is kind INC/DEC with lb = that CF */
            if (fl.nz && (fl.kind == LZ_ADD || fl.kind == LZ_SUB || fl.kind == LZ_LOGIC || fl.kind == LZ_INC || fl.kind == LZ_DEC)) {
                if (fl.kind == LZ_ADD) a_cset(8, 2);                 /* carry set: hs */
                else if (fl.kind == LZ_SUB) a_cset(8, 3);            /* x86 CF is the borrow: lo */
                else if (fl.kind == LZ_LOGIC) a_movi(8, 0);
                else a_mov(8, 10);
            } else { fl_flush(1); a_mov(0, RCPU); j_callabs((void*) jh_cf); a_mov(8, 0); }      /* flags in memory (or shift/comis state): helper */
            if (hm) fl_flush(1);                                     /* the access below can fault: the old flags must be in memory then */
            fl.nz = 0; fl.pend = 0;
            if (hm) j_ea(j, &rb, &off);
            j_opld(9, j, 0, sz, rb, off); a_movi(10, 1);
            alu_core(j->t ? 5 : 0, sz, 1);
            a_mov(10, 8);
            fl.kind = j->t ? LZ_DEC : LZ_INC;
            j_opst(11, j, sz, rb, off);
            break;
        }
        fl_flush(j->lvb); fl.nz = 0;
        if (hm) j_ea(j, &rb, &off);
        j_opld(9, j, 0, sz, rb, off); j_opld(10, j, 1, sz, rb, off);
        alu_core(j->t, sz, nf);
        if (!j->nost) j_opst(11, j, sz, rb, off);
        break; }
    case JO_NEG: {
        int nf = j->lva;
        fl_flush(j->lvb); fl.nz = 0;
        if (j->at == OT_MEM) j_ea(j, &rb, &off);
        j_opld(10, j, 0, sz, rb, off); a_movz(0, 9, 0, 0);
        alu_core(5, sz, nf);
        j_opst(11, j, sz, rb, off); break; }
    case JO_NOT:
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); }
        j_opld(0, j, 0, sz, rb, off); a_log(sf, 1, 1, 0, 31, 0); j_opst(0, j, sz, rb, off); break;
    case JO_SHIFT: {
        if (sz < 4 && j->t < 2) {                        /* rol/ror r/m8|16, imm8 (only reaches here with flags dead: otherwise the interpreter) */
            if (j->lva) { fl_flush(j->lvb); j_fb(j); break; }
            fl_flush(j->lvb); fl.nz = 0;
            int bits = sz * 8, n = (int) j->imm % bits; if (j->t == 1 && n) n = bits - n;       /* n = left-rotate amount */
            if (n == 0) break;
            if (j->at == OT_MEM) j_ea(j, &rb, &off);
            j_opld(0, j, 0, sz, rb, off);
            a_lsl(0, 1, 0, n); a_lsr(0, 2, 0, bits - n); a_log(0, 1, 0, 1, 1, 2);
            j_opst(1, j, sz, rb, off); break;
        }
        if (j->lva && j->cnt == 2 && j->t >= 4 && sz >= 4 && (g_jopt & JOPT_SHFL)) {    /* shl/shr/sar r/m, cl with the flags wanted: a zero count leaves them alone, so the lazy state is written only when the count is not zero */
            fl_flush(1); fl.nz = 0;
            if (j->at == OT_MEM) j_ea(j, &rb, &off);
            j_opld(9, j, 0, sz, rb, off);
            g_ld(10, RCX, 8, 0); a_andmask(10, 10, sf ? 6 : 5);
            u32 *z = a_cbz_fwd(0, 10);
            a_shv(sf, j->t == 5 ? 1 : j->t == 7 ? 2 : 0, 11, 9, 10);
            j_opst(11, j, sz, rb, off);                  /* the store may fault: the flags are written after it */
            fl.kind = j->t == 5 ? LZ_SHR : j->t == 7 ? LZ_SAR : LZ_SHL; fl.sz = sz; fl_spill();
            jpatch19(z); fl.pend = 0; fl.nz = 0;         /* both paths: the flags are in memory */
            break;
        }
        if (j->lva && sz < 4 && j->t >= 4 && j->cnt != 2 && j->imm >= 1 && (int) j->imm < sz * 8 && (g_jopt & JOPT_SHFL)) {   /* 8/16-bit shl/shr/sar by 1..bits-1, flags wanted: lazy as well (x9 = value, x10 = count, x11 = result, both zero-extended) */
            fl_flush(j->lvb); fl.nz = 0;
            if (j->at == OT_MEM) j_ea(j, &rb, &off);
            j_opld(9, j, 0, sz, rb, off);
            int n = (int) j->imm, bits = sz * 8, kind;
            if (j->t == 5) { a_lsr(1, 11, 9, n); kind = LZ_SHR; }
            else if (j->t == 7) { a_bfm(1, 1, 14, 9, 0, bits - 1); a_asr(1, 11, 14, n); a_andmask(11, 11, bits); kind = LZ_SAR; }
            else { a_lsl(1, 11, 9, n); a_andmask(11, 11, bits); kind = LZ_SHL; }
            a_movi(10, (u64) n);
            a_lsl(1, 14, 11, 64 - bits); a_log(1, 3, 0, 31, 14, 14);    /* tst: host Z and N now describe the sz-bit result */
            j_opst(11, j, sz, rb, off);
            fl.kind = kind; fl.sz = sz; fl.nz = 1; fl.pend = 1;
            break;
        }
        if (j->lva && (!(g_jopt & JOPT_SHFL) || j->cnt == 2 || !j->imm || j->t < 4 || sz < 4)) { fl_flush(j->lvb); j_fb(j); break; }   /* flags wanted: interpreter */
        if (j->lva) {                                            /* shl/shr/sar by a non-zero immediate, flags wanted: keep them lazy (x9 = value, x10 = count, x11 = result) */
            fl_flush(j->lvb); fl.nz = 0;
            if (j->at == OT_MEM) j_ea(j, &rb, &off);
            j_opld(9, j, 0, sz, rb, off);
            int n = (int) j->imm, kind;
            if (j->t == 5) { a_lsr(sf, 11, 9, n); kind = LZ_SHR; }
            else if (j->t == 7) { a_asr(sf, 11, 9, n); kind = LZ_SAR; }
            else { a_lsl(sf, 11, 9, n); kind = LZ_SHL; }
            a_movi(10, (u64) n);
            a_log(sf, 3, 0, 31, 11, 11);                         /* tst x11, x11: host Z and N now describe the result */
            j_opst(11, j, sz, rb, off);
            fl.kind = kind; fl.sz = sz; fl.nz = 1; fl.pend = 1;
            break;
        }
        fl_flush(j->lvb); fl.nz = 0;
        if (j->at == OT_MEM) j_ea(j, &rb, &off);
        if (sz < 4 && j->t == 7) {                       /* 8/16-bit sar: sign-extend the operand first (flags dead; shl/shr work on the zero-extended value) */
            if (j->at == OT_MEM) m_ld(sz, 1, 0, 0, rb, off); else g_ldx(0, j->ar, sz, 1, 4, j->rex);
        } else j_opld(0, j, 0, sz, rb, off);
        if (j->cnt == 2) {
            g_ld(1, RCX, 8, 0);
            switch (j->t) {
            case 4: case 6: a_shv(sf, 0, 0, 0, 1); break;
            case 5: a_shv(sf, 1, 0, 0, 1); break;
            case 7: a_shv(sf, 2, 0, 0, 1); break;
            case 1: a_shv(sf, 3, 0, 0, 1); break;
            default: a_addsub(1, 1, 0, 1, 31, 1); a_shv(sf, 3, 0, 0, 1); break;     /* rol: ror by -cl */
            }
        } else if (j->imm) {
            int n = (int) j->imm;
            switch (j->t) {
            case 4: case 6: a_lsl(sf, 0, 0, n); break;
            case 5: a_lsr(sf, 0, 0, n); break;
            case 7: a_asr(sf, 0, 0, n); break;
            case 1: a_movi(1, (u64) n); a_shv(sf, 3, 0, 0, 1); break;
            default: a_movi(1, (u64)((sf ? 64 : 32) - n)); a_shv(sf, 3, 0, 0, 1); break;
            }
        }
        j_opst(0, j, sz, rb, off); break; }
    case JO_IMUL: {
        if (j->lva) { fl_flush(j->lvb); j_fb(j); break; }
        fl_flush(j->lvb); fl.nz = 0;
        if (j->bt == OT_MEM) j_ea(j, &rb, &off);
        j_opld(0, j, 1, sz, rb, off);
        if (j->sx) a_movi(1, j->imm); else g_ld(1, j->ar, sz, 0);
        a_mul(sf, 0, 0, 1); g_st(0, j->ar, sz, 0); break; }
    case JO_SETCC: {
        fl_flush(j->lvb);
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        if (m < 0 && !(g_jopt & JOPT_CC)) { j_fb(j); break; }
        if (m < 0) {
            fl_flush(1); j_cc_helper(j->t);
            if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_st(1, 0, rb, off); } else g_st(0, j->ar, 1, j->rex);
            break;
        }
        if (m == 0) a_cset(0, cc); else a_movz(0, 0, m == 1, 0);
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_st(1, 0, rb, off); } else g_st(0, j->ar, 1, j->rex);
        break; }
    case JO_CMOV: {
        fl_flush(j->lvb);
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        if (m < 0 && !(g_jopt & JOPT_CC)) { j_fb(j); break; }
        if (m < 0) {
            fl_flush(1); j_cc_helper(j->t); a_mov(2, 0);                  /* x2 = condition; the source is read either way */
            if (j->bt == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->br, sz, 0);
            g_ld(0, j->ar, sz, 0);
            a_addsub(1, 1, 1, 31, 2, 31); a_csel(sf, 1, 0, 1, 0);        /* subs xzr, x2, xzr; csel ne */
            g_st(0, j->ar, sz, 0); break;
        }
        if (j->bt == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->br, sz, 0);
        g_ld(0, j->ar, sz, 0);
        if (m == 0) a_csel(sf, cc, 0, 1, 0); else if (m == 1) a_mov(0, 1);
        g_st(0, j->ar, sz, 0); break; }
    case JO_XLD:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); vm_ldm(j->mmx, 0, rb, off); } else vm_ldc(j->mmx, 0, j->br);
        vm_stc(j->mmx, 0, j->ar); break;
    case JO_XST:
        vm_ldc(j->mmx, 0, j->br);
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); vm_stm(j->mmx, 0, rb, off); } else vm_stc(j->mmx, 0, j->ar);
        break;
    case JO_XLOG:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); vm_ldm(j->mmx, 1, rb, off); } else vm_ldc(j->mmx, 1, j->br);
        vm_ldc(j->mmx, 0, j->ar);
        if (j->t == 3) q_log(3, 0, 1, 0); else q_log(j->t, 0, 0, 1);       /* pandn: ~dst & src */
        vm_stc(j->mmx, 0, j->ar); break;
    case JO_XVEC:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); vm_ldm(j->mmx, 1, rb, off); } else vm_ldc(j->mmx, 1, j->br);
        vm_ldc(j->mmx, 0, j->ar);
        if (j->t == 16) { E(0x2E212800u | 0u << 5 | 0u); E(0x6E212800u | 1u << 5 | 0u); }       /* packuswb: sqxtun v0.8b, v0.8h; sqxtun2 v0.16b, v1.8h */
        else q_v3(j->t, j->sz, 0, 0, 1);
        vm_stc(j->mmx, 0, j->ar); break;
    case JO_MMSHUF:                                      /* pshufw: the four 16-bit lanes of the low 64 bits */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(8, 1, rb, off); } else f_ldc(8, 1, OFF_MM(j->br));
        for (int k = 0; k < 4; k++) q_ins8h(0, k, 1, (int)((j->imm >> (2 * k)) & 3));
        f_stc(8, 0, OFF_MM(j->ar)); break;
    case JO_XSHN: {                                      /* psrl/psra/psll w|d by an immediate */
        unsigned n = (unsigned) j->imm; int esz = j->sz;
        if (n >= (unsigned) esz) { if (j->t == 4) n = (unsigned) esz - 1; else { c_st(31, OFF_X(j->br)); c_st(31, OFF_X(j->br) + 8); break; } }
        if (n == 0) break;
        q_ldc(0, OFF_X(j->br)); q_shi(j->t, esz, 0, 0, (int) n); q_stc(0, OFF_X(j->br)); break; }
    case JO_XSHUF:                                       /* pshufd xmm, xmm/m128, imm8 */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(1, rb, off); } else q_ldc(1, OFF_X(j->br));
        for (int k = 0; k < 4; k++) q_ins4s(0, k, 1, (int)((j->imm >> (2 * k)) & 3));
        q_stc(0, OFF_X(j->ar)); break;
    case JO_XDUP:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(1, rb, off); } else q_ldc(1, OFF_X(j->br));
        q_dup4s(0, 1, (int) j->imm);
        q_stc(0, OFF_X(j->ar)); break;
    case JO_MUL1: {                                      /* mul / imul r/m (32 or 64 bit), flags dead: rdx:rax = rax * r/m */
        if (j->lva) { fl_flush(j->lvb); j_fb(j); break; }
        fl_flush(j->lvb); fl.nz = 0;
        int sg = j->t == 5;
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->ar, sz, j->rex);
        g_ld(0, RAX, sz, 0);
        if (sz == 8) { a_mul(1, 2, 0, 1); a_mulh(sg, 3, 0, 1); }
        else {
            if (sg) { a_sxtw(0, 0); a_sxtw(1, 1); }
            a_mul(1, 2, 0, 1); a_lsr(1, 3, 2, 32); a_movw(2, 2);
        }
        g_st(2, RAX, sz, 0); g_st(3, RDX, sz, 0); break; }
    case JO_BSWAP:
        g_ld(0, j->ar, sz, 0); a_rev(sf, 0, 0); g_st(0, j->ar, sz, 0); break;
    case JO_BSF: {
        fl_flush(j->lvb); fl.nz = 0;                                         /* the compare below clobbers the host's flags; bsf defines ZF afresh */
        if (j->bt == OT_MEM) j_ea(j, &rb, &off);
        j_opld(0, j, 1, sz, rb, off);                                        /* x0 = source, zero-extended */
        if (j->t == 0) { a_rbit(sf, 2, 0); a_clz(sf, 2, 2); }                /* bsf / tzcnt: clz(rbit(x)), the operand width for 0 */
        else if (j->sz2) a_clz(sf, 2, 0);                                    /* lzcnt */
        else { a_clz(sf, 2, 0); a_movi(3, (u64)(sz * 8 - 1)); a_addsub(sf, 1, 0, 2, 3, 2); }   /* bsr: width - 1 - clz */
        if (j->sz2) {
            g_st(2, j->ar, sz, 0);
            if (j->lva) {                                                    /* tzcnt/lzcnt: CF = (source == 0), ZF = (result == 0); the rest is kept: exact update in memory, host Z caches ZF */
                a_addsub(sf, 1, 1, 31, 0, 31); a_cset(1, 0);                 /* x1 = source == 0 */
                a_addsub(sf, 1, 1, 31, 2, 31); a_cset(2, 0);                 /* x2 = result == 0 */
                a_mov(0, RCPU); j_callabs((void*) jh_f2);
                E(0xF1000400u | 0u << 5 | 31u);                              /* subs xzr, x0, #1: host Z = ZF */
                fl.kind = LZ_ZFX; fl.sz = sz; fl.nz = 1; fl.pend = 0;
            }
            break;
        } else {                                                               /* plain bsf/bsr leave the destination alone for a zero source */
            a_addsub(sf, 1, 1, 31, 0, 31);                                   /* cmp x0, #0 */
            g_ld(3, j->ar, 8, 0); a_csel(1, 0, 2, 3, 2); g_st(2, j->ar, 8, 0);
        }
        if (j->lva) {                                                        /* ZF = (source == 0) is what tst x,x gives; the other flags are undefined after bsf */
            a_mov(11, 0); a_log(sf, 3, 0, 31, 11, 11);
            fl.kind = LZ_LOGIC; fl.sz = sz; fl.nz = 1; fl.pend = 1;
        }
        break; }
    case JO_PMOVMSKB:                                    /* bit i of the result = top bit of byte i: (x & 0x80..80) * 0x0002040810204081 >> 56 */
        c_ld(0, OFF_X(j->br)); c_ld(1, OFF_X(j->br) + 8);
        a_movi(2, 0x8080808080808080ULL); a_movi(3, 0x0002040810204081ULL);
        a_log(1, 0, 0, 0, 0, 2); a_log(1, 0, 0, 1, 1, 2);
        a_mul(1, 0, 0, 3); a_mul(1, 1, 1, 3);
        a_lsr(1, 0, 0, 56); a_lsr(1, 1, 1, 56);
        a_logsh(1, 0, 0, 1, 0, 8);                       /* orr x0, x0, x1, lsl #8 */
        g_st(0, j->ar, 4, 0); break;
    case JO_XSHI: {                                      /* the count is known: shift the two 64-bit halves with integer code (x0 = low, x1 = high) */
        u32 o = OFF_X(j->br); unsigned n = (unsigned) j->imm;
        if (j->t == 2 || j->t == 6) {                    /* psrlq / psllq */
            if (n > 63) { c_st(31, o); c_st(31, o + 8); break; }
            if (n == 0) break;
            c_ld(0, o); c_ld(1, o + 8);
            if (j->t == 2) { a_lsr(1, 0, 0, (int) n); a_lsr(1, 1, 1, (int) n); } else { a_lsl(1, 0, 0, (int) n); a_lsl(1, 1, 1, (int) n); }
            c_st(0, o); c_st(1, o + 8); break;
        }
        if (n > 15) { c_st(31, o); c_st(31, o + 8); break; }       /* psrldq / pslldq: n bytes */
        if (n == 0) break;
        int b = (int) n * 8; c_ld(0, o); c_ld(1, o + 8);
        if (j->t == 3) {                                 /* psrldq: whole xmm >> b */
            if (n < 8) { a_lsr(1, 2, 0, b); a_logsh(1, 0, 2, 1, 0, 64 - b); a_lsr(1, 1, 1, b); c_st(0, o); c_st(1, o + 8); }
            else { if (n == 8) a_mov(0, 1); else a_lsr(1, 0, 1, b - 64); c_st(0, o); c_st(31, o + 8); }
        } else {                                         /* pslldq: whole xmm << b */
            if (n < 8) { a_lsl(1, 2, 1, b); a_logsh(1, 1, 2, 0, 1, 64 - b); a_lsl(1, 0, 0, b); c_st(0, o); c_st(1, o + 8); }
            else { if (n != 8) a_lsl(1, 0, 0, b - 64); c_st(31, o); c_st(0, o + 8); }
        }
        break; }
    case JO_XMOVDLD:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(sz, 0, 0, 0, rb, off); } else g_ld(0, j->br, sz, 0);
        if (j->mmx) { c_st(0, OFF_MM(j->ar)); break; }
        c_st(0, OFF_X(j->ar)); c_st(31, OFF_X(j->ar) + 8); break;
    case JO_XMOVDST:
        if (j->mmx) c_ld(0, OFF_MM(j->br)); else c_ld(0, OFF_X(j->br));
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_st(sz, 0, rb, off); }
        else { if (sz == 4) a_movw(0, 0); g_st(0, j->ar, sz, 0); }
        break;
    case JO_FMOV:                                        /* movss / movsd */
        if (!j->t) {
            if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(j->sz2, 0, rb, off); q_stc(0, OFF_X(j->ar)); }   /* from memory: upper bits become zero */
            else { f_ldc(j->sz2, 0, OFF_X(j->br)); f_stc(j->sz2, 0, OFF_X(j->ar)); }                                          /* reg to reg: only the low element moves */
        } else {
            f_ldc(j->sz2, 0, OFF_X(j->br));
            if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_stm(j->sz2, 0, rb, off); } else f_stc(j->sz2, 0, OFF_X(j->ar));
        }
        break;
    case JO_FARITH:
        if (j->t >= 5) fl_flush(j->lvb);                               /* min/max compare with fcmp: the host flags are overwritten */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(j->sz2, 1, rb, off); } else f_ldc(j->sz2, 1, OFF_X(j->br));
        if (j->t != 4) f_ldc(j->sz2, 0, OFF_X(j->ar));
        if (j->t >= 5) {                                               /* minsd/maxsd: dest = (dest < src) ? dest : src (max: >), so a NaN or equal operands (+0/-0) give the source, as on x86 */
            fl.nz = 0;
            f_cmp(j->sz2, 0, 1);
            E(0x1E200C00u | (j->sz2 == 8 ? 1u << 22 : 0u) | 1u << 16 | (u32)(j->t == 5 ? 4 : 12) << 12 | 0u << 5 | 0u);   /* fcsel d0, d0, d1, mi (min) / gt (max) */
        } else
        f_arith(j->t, j->sz2, 0, 0, 1);
        f_stc(j->sz2, 0, OFF_X(j->ar)); break;
    case JO_FCVTF: {                                                   /* cvtss2sd (sz2 4): low double of the destination; cvtsd2ss (sz2 8): low float */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(j->sz2, 1, rb, off); } else f_ldc(j->sz2, 1, OFF_X(j->br));
        E(j->sz2 == 4 ? 0x1E22C000u | 1u << 5 : 0x1E624000u | 1u << 5);            /* fcvt d0, s1 / fcvt s0, d1 */
        f_stc(j->sz2 == 4 ? 8 : 4, 0, OFF_X(j->ar)); break; }
    case JO_CVTSI2F:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(sz, 0, 0, 0, rb, off); } else g_ld(0, j->br, sz, 0);
        f_scvtf(j->sz2, sz == 8, 0, 0);
        f_stc(j->sz2, 0, OFF_X(j->ar)); break;
    case JO_FCMP:
        fl_flush(j->lvb); fl.nz = 0;
        if (j->bt == OT_MEM) { j_ea(j, &rb, &off); f_ldm(j->sz2, 1, rb, off); } else f_ldc(j->sz2, 1, OFF_X(j->br));
        if (!j->lva) break;                                              /* flags dead: nothing but the (possibly faulting) load */
        f_ldc(j->sz2, 0, OFF_X(j->ar));
        f_cmp(j->sz2, 0, 1); f_tox(j->sz2, 9, 0); f_tox(j->sz2, 10, 1);   /* host flags for jcc/setcc/cmov; the operands for the lazy spill */
        fl.kind = LZ_FCMP; fl.sz = j->sz2; fl.nz = 1; fl.pend = 1;
        break;
    case JO_DIV: {                                                       /* rdx:rax / r/m, only when it cannot trap: the interpreter does the rest */
        fl_flush(j->lvb); fl.nz = 0;
        int sg = j->t == 7;
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->ar, sz, j->rex);
        u32 *sl[4]; int ns = 0;
        sl[ns++] = a_cbz_fwd(0, 1);                                      /* divisor 0 */
        g_ld(0, RAX, sz, 0); g_ld(2, RDX, sz, 0);
        if (!sg) sl[ns++] = a_cbz_fwd(1, 2);                             /* rdx != 0: wide dividend */
        else {
            a_asr(sf, 5, 0, sf ? 63 : 31); a_log(1, 2, 0, 6, 2, 5);        /* rdx must be the sign of rax */
            sl[ns++] = a_cbz_fwd(1, 6);
            a_addi(0, 5, 1, 1, 0); if (!sf) a_lsr(1, 5, 5, 32);          /* divisor -1 (INT_MIN / -1 traps) */
            sl[ns++] = a_cbz_fwd(sf ? 0 : 1, 5);
        }
        a_div(sf, sg, 3, 0, 1); a_msub(sf, 4, 3, 1, 0);
        g_st(3, RAX, sz, 0); g_st(4, RDX, sz, 0);
        u32 *done = a_b_fwd();
        for (int k = 0; k < ns; k++) jpatch19(sl[k]);
        j_fb(j);
        jpatch26(done);
        fl.nz = 0;
        break; }
    case JO_BT: {                                        /* bt/bts/btr/btc r/m, r|imm8. x0 = value, x1 = bit, x3 = the selected bit, x4 = its mask */
        int bits = sz * 8, lg = sz == 2 ? 4 : sz == 4 ? 5 : 6, lgb = sz == 2 ? 1 : sz == 4 ? 2 : 3;
        int memf = j->at == OT_MEM, imm = j->bt == OT_IMM, rmw = j->t != 0, nb = (int) j->imm & (bits - 1);
        if (memf || j->lva) fl_flush(1);                 /* the flags other than CF are kept: memory must hold them exactly before anything can fault */
        if (memf) {
            j_ea(j, &rb, &off);
            if (!imm) {                                  /* register bit offset: signed, it also selects the word: address += (off >> lg) * size */
                if (sz == 8) g_ld(1, j->br, 8, j->rex); else g_ldx(1, j->br, sz, 1, 8, j->rex);
                a_asr(1, 3, 1, lg); a_andmask(1, 1, lg);
                if (off >= 0) a_addi(0, 14, rb, (u32) off, 0); else a_addi(1, 14, rb, (u32) -off, 0);
                a_addsh(14, 14, 3, lgb); rb = 14; off = 0;
            }
            m_ld(sz, 0, 0, 0, rb, off);
        } else g_ld(0, j->ar, sz, j->rex);
        if (!imm && !memf) { g_ld(1, j->br, sz, j->rex); a_andmask(1, 1, lg); }
        if (imm) a_bfm(1, 0, 3, 0, nb, nb);              /* ubfx x3, x0, #nb, #1 */
        else { a_shv(1, 1, 3, 0, 1); a_andmask(3, 3, 1); }
        if (rmw) {
            if (imm) a_movi(4, 1ULL << nb); else { a_movi(4, 1); a_shv(1, 0, 4, 4, 1); }
            if (j->t == 1) a_log(1, 1, 0, 2, 0, 4);      /* bts: orr */
            else if (j->t == 2) a_log(1, 0, 1, 2, 0, 4); /* btr: bic */
            else a_log(1, 2, 0, 2, 0, 4);                /* btc: eor */
            if (memf) m_st(sz, 2, rb, off); else g_st(2, j->ar, sz, j->rex);     /* the store may fault: the flags are written after it */
        }
        if (j->lva) {
            a_mov(1, 3); a_mov(0, RCPU); j_callabs((void*) jh_btf);
            E(0xF1000400u | 0u << 5 | 31u);              /* subs xzr, x0, #1: host C = CF */
            fl.kind = LZ_CFX; fl.sz = sz; fl.nz = 1; fl.pend = 0;
        }
        break; }
    case JO_XRR:                                         /* xchg r, r */
        g_ld(0, j->ar, sz, j->rex); g_ld(1, j->br, sz, j->rex); g_st(1, j->ar, sz, j->rex); g_st(0, j->br, sz, j->rex); break;
    case JO_DF:                                          /* cld / std */
        c_ld(0, OFF_RFL); a_movi(1, DF_);
        if (j->t) a_log(1, 1, 0, 0, 0, 1); else a_log(1, 0, 1, 0, 0, 1);
        c_st(0, OFF_RFL); break;
    case JO_XLH:                                         /* movlps/movhps/movlpd/movhpd (64 bits to or from memory), movhlps, movlhps */
        switch (j->t) {
        case 0: case 1: fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(8, 0, 0, 0, rb, off); c_st(0, OFF_X(j->ar) + (j->t ? 8u : 0u)); break;
        case 2: case 3: c_ld(0, OFF_X(j->br) + (j->t == 3 ? 8u : 0u)); fl_flush(j->lvb); j_ea(j, &rb, &off); m_st(8, 0, rb, off); break;
        case 4: c_ld(0, OFF_X(j->br) + 8); c_st(0, OFF_X(j->ar)); break;
        default: c_ld(0, OFF_X(j->br)); c_st(0, OFF_X(j->ar) + 8); break;
        }
        break;
    case JO_POPCNT:                                      /* x0 = source, SWAR bit count (no NEON dependency) */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); }
        else fl_flush(j->lvb);
        j_opld(0, j, 1, sz, rb, off);
        a_lsr(1, 1, 0, 1); a_movi(2, 0x5555555555555555ULL); a_log(1, 0, 0, 1, 1, 2); a_addsub(1, 1, 0, 0, 0, 1);
        a_movi(2, 0x3333333333333333ULL); a_log(1, 0, 0, 1, 0, 2); a_lsr(1, 3, 0, 2); a_log(1, 0, 0, 3, 3, 2); a_addsub(1, 0, 0, 0, 1, 3);
        a_lsr(1, 1, 0, 4); a_addsub(1, 0, 0, 0, 0, 1); a_movi(2, 0x0F0F0F0F0F0F0F0FULL); a_log(1, 0, 0, 0, 0, 2);
        a_movi(2, 0x0101010101010101ULL); a_mul(1, 0, 0, 2); a_lsr(1, 0, 0, 56);
        g_st(0, j->ar, sz, j->rex);
        if (j->lva) {                                    /* CF = OF = AF = SF = PF = 0, ZF = (source == 0) */
            a_mov(1, 0); a_mov(0, RCPU); j_callabs((void*) jh_pcf);
            E(0xF1000400u | 0u << 5 | 31u);              /* subs xzr, x0, #1: host Z = ZF */
            fl.kind = LZ_ZFX; fl.sz = sz; fl.nz = 1; fl.pend = 0;
        }
        break;
    case JO_PSHUFB:                                      /* pshufb xmm, xmm/m128: tbl with the index byte masked to bit 7 and bits 0-3 (an index >= 16 gives 0, like bit 7 on x86) */
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(1, rb, off); } else q_ldc(1, OFF_X(j->br));
        q_ldc(0, OFF_X(j->ar));
        E(0x4F04E5E2u);                                  /* movi v2.16b, #0x8f */
        q_log(2, 1, 1, 2);                               /* and v1, v1, v2 */
        E(0x4E010000u);                                  /* tbl v0.16b, {v0.16b}, v1.16b */
        q_stc(0, OFF_X(j->ar)); break;
    case JO_JMP: fl_flush(j->lvb); jx_direct(j->ip, j->target); return 1;
    case JO_JCC: {
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        u64 next = j->ip + j->len;
        if (j->cnt) {                                    /* superblock: the fall-through path continues, the taken path is an out-of-line side exit */
            if (m < 0 && !(g_jopt & JOPT_CC)) { fl_flush(1); j_fb(j); c_ld(0, OFF_RIP); jx_indirect(); return 1; }
            if (m < 0) { fl_flush(1); j_cc_helper(j->t); jstub_add(JS_SIDE, a_cbz_fwd(1, 0), NULL, j->ip, j->target); break; }   /* flags in memory: helper decides, cbnz to the side exit */
            if (m == 1) { fl_flush(1); jx_direct(j->ip, j->target); return 1; }
            if (m == 2) break;
            jstub_add(JS_SIDE, a_bcond_fwd(cc), NULL, j->ip, j->target);       /* the stub is entered by the branch itself: x9-x11 still hold the lazy flags */
            break;
        }
        fl_flush(1);
        if (m < 0 && !(g_jopt & JOPT_CC)) { j_fb(j); c_ld(0, OFF_RIP); jx_indirect(); }
        else if (m < 0) { j_cc_helper(j->t); u32 *bt = a_cbz_fwd(1, 0); jx_direct(j->ip, next); jpatch19(bt); jx_direct(j->ip, j->target); }   /* flags in memory: helper decides, both exits chain */
        else if (m == 1) jx_direct(j->ip, j->target);
        else if (m == 2) jx_direct(j->ip, next);
        else { u32 *bt = a_bcond_fwd(cc); jx_direct(j->ip, next); jpatch19(bt); jx_direct(j->ip, j->target); }
        return 1; }
    case JO_CALL:
        fl_flush(j->lvb);
        a_movi(0, j->imm); j_push(0);
        if (!j->cnt) { jx_direct(j->ip, j->target); return 1; }
        else {                                           /* bl callee; the code after it is the continuation, reached by the callee's ret */
            u32 *adr; jrsb_push(j->imm, &adr);
            u32 *tc = jfind_emit(j->target);
            if (tc) a_bl(tc); else { E(0x94000000u); jstub_add(JS_CALL, jcur - 1, NULL, j->ip, j->target); }
            a_adr_at(adr, 5, jcur);
            fl.nz = 0; fl.pend = 0;
        }
        break;
    case JO_RET:
        fl_flush(j->lvb);
        if (pin(RSP) >= 0) {
            int hs = pin(RSP);
            m_ld(8, 0, 0, 0, hs, 0); a_addi(0, hs, hs, 8, 0);
            if (j->imm) { a_movi(2, j->imm); a_addsh(hs, hs, 2, 0); }
            jx_ret(); return 1;
        }
        c_ld(1, OFF_R(RSP)); m_ld(8, 0, 0, 0, 1, 0); a_addi(0, 1, 1, 8, 0);
        if (j->imm) { a_movi(2, j->imm); a_addsh(1, 1, 2, 0); }
        c_st(1, OFF_R(RSP)); jx_ret(); return 1;
    case JO_JMPI: case JO_CALLI:
        fl_flush(j->lvb);
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(8, 0, 0, 0, rb, off); } else g_ld(0, j->ar, 8, 0);
        if (j->op == JO_CALLI) { a_movi(2, j->imm); j_push(2); }
        if (!j->cnt) { jx_indirect(); return 1; }
        {                                                /* indirect call: inline cache hit = blr, miss = leave through an out-of-line exit */
            u32 *adr; jrsb_push(j->imm, &adr);
            a_ldst_u(3, 1, 1, RPEND, 0);
            u32 *s1 = a_cbz_fwd(1, 1);
            a_logsh(2, 2, 0, 0, 1, 13); a_andmask(2, 2, JIC_BITS); a_addsh(2, RIC, 2, 4);
            a_ldp(3, 4, 2, 0); a_addsub(1, 1, 1, 31, 3, 0);
            u32 *s2 = a_bcond_fwd(1);
            E(0xD63F0000u | 4u << 5);                    /* blr x4 */
            a_adr_at(adr, 5, jcur);
            jstub_add(JS_IND, s1, s2, j->ip, 0);
            fl.nz = 0; fl.pend = 0;
        }
        break;
    }
    return 0;
}

