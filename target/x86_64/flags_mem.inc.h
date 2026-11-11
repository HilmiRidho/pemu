/* target/x86_64/flags_mem.inc.h - lazy flags, ALU helpers, guest memory access. SPDX-License-Identifier: Apache-2.0 */
static inline u64 mskz(int sz) { return sz == 8 ? ~0ULL : (1ULL << (sz * 8)) - 1; }
static inline int sgn(u64 v, int sz) { return (int)((v >> (sz * 8 - 1)) & 1); }
static inline s64 sx(u64 v, int sz) { return sz == 8 ? (s64) v : (s64)(v << (64 - sz * 8)) >> (64 - sz * 8); }
/* Set once the guest has created a second thread. x86 is TSO, aarch64 is weakly ordered: from then on every interpreted
 * load is followed by an acquire barrier and every store is preceded by a release barrier, which gives the x86 ordering
 * (only store->load may be reordered). Free on x86 hosts (compiler barrier only). SSE/x87 accesses do not go through
 * mrd/mwr and are not covered. */
static int g_mt;
static u64 mrd(u64 a, int n) {
    u64 v;
    switch (n) { case 1: v = *(u8*) a; break; case 2: v = *(u16u*) a; break; case 4: v = *(u32u*) a; break; default: v = *(u64u*) a; }
    if (__builtin_expect(__atomic_load_n(&g_mt, __ATOMIC_RELAXED), 0)) __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return v;
}
static void mwr(u64 a, u64 v, int n) {
    if (__builtin_expect(__atomic_load_n(&g_mt, __ATOMIC_RELAXED), 0)) __atomic_thread_fence(__ATOMIC_RELEASE);
    switch (n) { case 1: *(u8*) a = (u8) v; break; case 2: *(u16u*) a = (u16) v; break; case 4: *(u32u*) a = (u32) v; break; default: *(u64u*) a = v; }
}

/* ---- lazy flags: add/sub/logic only record their operands; CF/OF/AF/ZF/SF/PF are computed when somebody reads them.
 * RF(c) is an lvalue that first materialises any pending flags, so every other use of the flags stays unchanged. */
enum { LZ_NONE, LZ_ADD, LZ_SUB, LZ_LOGIC, LZ_SHL, LZ_SHR, LZ_SAR, LZ_FCMP, LZ_INC, LZ_DEC };   /* LZ_INC/LZ_DEC (JIT only): la = operand, lb = the CF that inc/dec leave unchanged (0 or 1), lr = result; LZ_SH*: la = value, lb = count (1..bits-1), lr = result (only recorded by the JIT); LZ_FCMP: la, lb = the two operands of comiss/comisd (bit patterns, lsz 4 or 8), only recorded by the JIT */
static const u8 g_par[256] = {
#define PT2(n) n, n ^ 1, n ^ 1, n
#define PT4(n) PT2(n), PT2(n ^ 1), PT2(n ^ 1), PT2(n)
#define PT6(n) PT4(n), PT4(n ^ 1), PT4(n ^ 1), PT4(n)
    PT6(1), PT6(0), PT6(0), PT6(1)
};
#undef PT2
#undef PT4
#undef PT6
static void mat(CPU*c) {
    int sz = c->lsz, sb = sz * 8 - 1; u64 m = mskz(sz), a = c->la, b = c->lb, r = c->lr;
    u64 f = c->rf & ~(u64)(CF_ | OF_ | AF_ | ZF_ | SF_ | PF_);
    if (c->lop == LZ_FCMP) {                                    /* same flags as the interpreter's comisd: unordered 111, below CF, equal ZF */
        double x, y;
        if (sz == 8) { memcpy(&x, &a, 8); memcpy(&y, &b, 8); } else { float fx, fy; u32 ua = (u32) a, ub = (u32) b; memcpy(&fx, &ua, 4); memcpy(&fy, &ub, 4); x = fx; y = fy; }
        if (isnan(x) || isnan(y)) f |= ZF_ | PF_ | CF_; else if (x < y) f |= CF_; else if (x == y) f |= ZF_;
        c->rf = f; c->lop = LZ_NONE; return;
    }
    switch (c->lop) {
    case LZ_ADD:
        if (c->lcin ? r <= a : r < a) f |= CF_;
        if (((a ^ r) & (b ^ r)) >> sb & 1) f |= OF_;
        if ((a ^ b ^ r) & 0x10) f |= AF_;
        break;
    case LZ_SUB:
        if (c->lcin ? a <= b : a < b) f |= CF_;
        if (((a ^ b) & (a ^ r)) >> sb & 1) f |= OF_;
        if ((a ^ b ^ r) & 0x10) f |= AF_;
        break;
    case LZ_SHL:
        if ((a >> (sb + 1 - (int) b)) & 1) f |= CF_;
        if ((int)(r >> sb & 1) ^ (int)((a >> (sb + 1 - (int) b)) & 1)) f |= OF_;
        break;
    case LZ_SHR:
        if ((a >> (b - 1)) & 1) f |= CF_;
        if (a >> sb & 1) f |= OF_;
        break;
    case LZ_SAR:
        if ((u64)(sx(a, sz) >> (b - 1)) & 1) f |= CF_;
        break;
    case LZ_INC:
        if (b) f |= CF_;
        if (r == 1ULL << sb) f |= OF_;
        if ((a ^ 1 ^ r) & 0x10) f |= AF_;
        break;
    case LZ_DEC:
        if (b) f |= CF_;
        if (a == 1ULL << sb) f |= OF_;
        if ((a ^ 1 ^ r) & 0x10) f |= AF_;
        break;
    default: break;
    }
    (void) m;
    if (!r) f |= ZF_;
    if (r >> sb & 1) f |= SF_;
    if (g_par[(u8) r]) f |= PF_;
    c->rf = f; c->lop = LZ_NONE;
}
static inline u64*flagsp(CPU*c) { if (__builtin_expect(c->lop != LZ_NONE, 0)) mat(c); return &c->rf; }
#define RF(c) (*flagsp(c))

static void szp(CPU*c, u64 r, int sz) {
    u64*f = &RF(c);
    *f &= ~(u64)(ZF_ | SF_ | PF_);
    if (!(r & mskz(sz))) *f |= ZF_;
    if (sgn(r, sz)) *f |= SF_;
    if (g_par[(u8) r]) *f |= PF_;
}
static inline u64 f_add(CPU*c, u64 a, u64 b, u64 cin, int sz) {
    u64 m = mskz(sz); a &= m; b &= m;
    u64 r = (a + b + cin) & m;
    c->la = a; c->lb = b; c->lr = r; c->lcin = (u32) cin; c->lsz = sz; c->lop = LZ_ADD;
    return r;
}
static inline u64 f_sub(CPU*c, u64 a, u64 b, u64 bin, int sz) {
    u64 m = mskz(sz); a &= m; b &= m;
    u64 r = (a - b - bin) & m;
    c->la = a; c->lb = b; c->lr = r; c->lcin = (u32) bin; c->lsz = sz; c->lop = LZ_SUB;
    return r;
}
static inline u64 f_logic(CPU*c, u64 r, int sz) {
    r &= mskz(sz);
    c->la = c->lb = 0; c->lr = r; c->lcin = 0; c->lsz = sz; c->lop = LZ_LOGIC;
    return r;
}
/* type: 0 add 1 or 2 adc 3 sbb 4 and 5 sub 6 xor 7 cmp */
static u64 alu(CPU*c, int t, u64 a, u64 b, int sz) {
    switch (t) {
    case 0: return f_add(c, a, b, 0, sz);
    case 1: return f_logic(c, a | b, sz);
    case 2: return f_add(c, a, b, RF(c) & CF_, sz);
    case 3: return f_sub(c, a, b, RF(c) & CF_, sz);
    case 4: return f_logic(c, a & b, sz);
    case 5: case 7: return f_sub(c, a, b, 0, sz);
    default: return f_logic(c, a ^ b, sz);
    }
}
static inline int cond(CPU*c, int cc) {
    int r;
    int lop = c->lop;
    if (lop == LZ_LOGIC || (lop == LZ_SUB && !c->lcin)) {          /* decide directly from the operands, no flag computation */
        int sz = c->lsz, sb = sz * 8 - 1; u64 res = c->lr;
        if (lop == LZ_LOGIC) {
            switch (cc >> 1) {
            case 0: case 1: r = 0; break; case 2: case 3: r = !res; break;
            case 4: case 6: r = (int)(res >> sb & 1); break; case 5: r = g_par[(u8) res]; break;
            default: r = !res || (int)(res >> sb & 1); }
        } else {
            u64 a = c->la, b = c->lb;
            switch (cc >> 1) {
            case 0: r = (int)((((a ^ b) & (a ^ res)) >> sb) & 1); break;
            case 1: r = a < b; break; case 2: r = !res; break; case 3: r = a <= b; break;
            case 4: r = (int)(res >> sb & 1); break; case 5: r = g_par[(u8) res]; break;
            case 6: r = sx(a, sz) < sx(b, sz); break;
            default: r = sx(a, sz) <= sx(b, sz); }
        }
        return (cc & 1) ? !r : r;
    }
    u64 f = RF(c);
    int cf = !!(f & CF_), zf = !!(f & ZF_), sf = !!(f & SF_), of = !!(f & OF_), pf = !!(f & PF_);
    switch (cc >> 1) {
    case 0: r = of; break; case 1: r = cf; break; case 2: r = zf; break; case 3: r = cf || zf; break;
    case 4: r = sf; break; case 5: r = pf; break; case 6: r = sf != of; break; default: r = zf || sf != of; }
    return (cc & 1) ? !r : r;
}
static u64 shiftop(CPU*c, int t, u64 v, unsigned n, int sz) {
    int bits = sz * 8; u64 m = mskz(sz), r = v & m; int cf = 0, of = 0;
    v &= m;
    n &= sz == 8 ? 63 : 31;
    if (!n) return v;
    switch (t) {
    case 4: case 6:
        r = n >= (unsigned) bits ? 0 : (v << n) & m; cf = n > (unsigned) bits ? 0 : (int)((v >> (bits - n)) & 1);
        of = sgn(r, sz) ^ cf; szp(c, r, sz); RF(c) &= ~(u64)AF_; break;
    case 5:
        r = n >= (unsigned) bits ? 0 : v >> n; cf = n > (unsigned) bits ? 0 : (int)((v >> (n - 1)) & 1);
        of = sgn(v, sz); szp(c, r, sz); RF(c) &= ~(u64)AF_; break;
    case 7: {
        s64 sv = sx(v, sz); unsigned k = n >= (unsigned) bits ? (unsigned) bits - 1 : n;
        r = (u64)(sv >> k) & m; cf = (int)((sv >> (k ? k - 1 : 0)) & 1); of = 0; szp(c, r, sz); RF(c) &= ~(u64)AF_; break; }
    case 0: { unsigned k = n % bits; r = k ? ((v << k) | (v >> (bits - k))) & m : v; cf = (int)(r & 1); of = sgn(r, sz) ^ cf; break; }
    case 1: { unsigned k = n % bits; r = k ? ((v >> k) | (v << (bits - k))) & m : v; cf = sgn(r, sz); of = cf ^ (int)((r >> (bits - 2)) & 1); break; }
    case 2: case 3: {
        int cc = !!(RF(c) & CF_); r = v;
        for (unsigned i = 0; i < n % (bits + 1); i++) {
            if (t == 2) { int o = sgn(r, sz); r = ((r << 1) | (u64) cc) & m; cc = o; }
            else { int o = (int)(r & 1); r = (r >> 1) | ((u64) cc << (bits - 1)); cc = o; }
        }
        cf = cc; of = t == 2 ? sgn(r, sz) ^ cf : sgn(r, sz) ^ (int)((r >> (bits - 2)) & 1); break; }
    }
    RF(c) &= ~(u64)(CF_ | OF_);
    if (cf) RF(c) |= CF_;
    if (of) RF(c) |= OF_;
    return r;
}

