/* target/x86_64/x87.inc.h - x87 subset. SPDX-License-Identifier: Apache-2.0 */
/* ------------------------------------------------------------ instruction step */
/* ------------------------------------------------------------ x87 (subset) */
/* Values are held as host long double: exact 80-bit on x86_64 hosts; IEEE quad
 * (slightly more precise) on aarch64 hosts. Memory format is always 80-bit. */
#define ST(i) (c->st[(c->top + (i)) & 7])
static long double ld_from80(const u8*p) {
    u64 m = *(u64u*) p; u16 se = *(u16u*)(p + 8); int e = se & 0x7fff; long double v;
    if (e == 0x7fff) v = (m << 1) ? (long double) NAN : (long double) INFINITY;
    else if (!m) v = 0;
    else v = ldexpl((long double) m, (e ? e : 1) - 16383 - 63);
    return (se >> 15) ? -v : v;
}
static void ld_to80(long double v, u8*p) {
    u64 m = 0; u16 se = 0;
    if (signbit(v)) { se = 0x8000; v = -v; }
    if (isnan(v)) { se |= 0x7fff; m = 0xC000000000000000ULL; }
    else if (isinf(v)) { se |= 0x7fff; m = 0x8000000000000000ULL; }
    else if (v != 0) {
        int e; long double f = frexpl(v, &e);
        long double sc = nearbyintl(ldexpl(f, 64));
        if (sc >= 18446744073709551616.0L) { m = 0x8000000000000000ULL; e++; } else m = (u64) sc;
        int be = e - 1 + 16383;
        if (be <= 0) { m = 0; be = 0; } else if (be >= 0x7fff) { be = 0x7fff; m = 0x8000000000000000ULL; }
        se |= (u16) be;
    }
    *(u64u*) p = m; *(u16u*)(p + 8) = se;
}
/* FXSAVE image (512 bytes), shared by fxsave/fxrstor, the signal frame and sigreturn. Layout: 0 fcw, 2 fsw (TOP in bits 11-13),
 * 4 abridged tag word (tags are not tracked: 0), 24 mxcsr, 28 mxcsr_mask, 32 ST(0..7) at 16 bytes each in stack order, 160 xmm0-15. */
static void fx_save(CPU*c, u8*p) {
    memset(p, 0, 512);
    *(u16u*) p = c->fcw; *(u16u*)(p + 2) = (u16)((c->fsw & ~0x3800) | (c->top << 11));
    *(u32u*)(p + 24) = c->mxcsr; *(u32u*)(p + 28) = 0xffff;
    for (int i = 0; i < 8; i++) ld_to80(c->st[(c->top + i) & 7], p + 32 + 16 * i);
    memcpy(p + 160, c->x, 256);
}
static void fx_restore(CPU*c, const u8*p) {
    u16 sw = *(const u16u*)(p + 2);
    c->fcw = *(const u16u*) p; c->fsw = sw & 0x4700; c->top = (sw >> 11) & 7;
    c->mxcsr = *(const u32u*)(p + 24);
    for (int i = 0; i < 8; i++) c->st[(c->top + i) & 7] = ld_from80(p + 32 + 16 * i);
    memcpy(c->x, p + 160, 256);
}
static void xpush(CPU*c, long double v) { c->top = (c->top - 1) & 7; c->st[c->top] = v; }
static void xpop(CPU*c) { c->top = (c->top + 1) & 7; }
static long double xround(CPU*c, long double v) {
    switch ((c->fcw >> 10) & 3) { case 1: return floorl(v); case 2: return ceill(v); case 3: return truncl(v); default: return nearbyintl(v); }
}
static long double xop(int r, long double a, long double b) {
    switch (r) { case 0: return a + b; case 1: return a * b; case 4: return a - b; case 5: return b - a; case 6: return a / b; default: return b / a; }
}
static void x_cc(CPU*c, long double a, long double b) {
    c->fsw &= ~0x4500;
    if (isnan(a) || isnan(b)) c->fsw |= 0x4500; else if (a < b) c->fsw |= 0x0100; else if (a == b) c->fsw |= 0x4000;
}
static void x_flags(CPU*c, long double a, long double b) {
    RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
    if (isnan(a) || isnan(b)) RF(c) |= ZF_ | PF_ | CF_; else if (a < b) RF(c) |= CF_; else if (a == b) RF(c) |= ZF_;
}
static u64 x_toint(CPU*c, long double v, int sz) {
    long double r = xround(c, v), lim = sz == 2 ? 32768.0L : sz == 4 ? 2147483648.0L : 9223372036854775808.0L;
    if (isnan(r) || r >= lim || r < -lim) return sz == 2 ? 0x8000 : sz == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    return (u64)(s64) r;
}
static u64 x_tointt(long double v, int sz) {            /* fisttp: truncate toward zero, whatever the rounding mode says */
    long double r = truncl(v), lim = sz == 2 ? 32768.0L : sz == 4 ? 2147483648.0L : 9223372036854775808.0L;
    if (isnan(r) || r >= lim || r < -lim) return sz == 2 ? 0x8000 : sz == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    return (u64)(s64) r;
}
static u64 x_ld_int(u64 raw, int sz) { return (u64) sx(raw, sz); }
static int x87(CPU*c, I*in, u8 op) {
    M m; modrm(c, in, &m, 0);
    int r = m.reg & 7, i = m.rm & 7;
    if (m.ismem) {
        u64 ea = m.ea; long double v;
        switch (op) {
        case 0xD8: case 0xDC: case 0xDA: case 0xDE: {
            if (op == 0xD8) { float f; memcpy(&f, (void*) ea, 4); v = f; }
            else if (op == 0xDC) { double d; memcpy(&d, (void*) ea, 8); v = d; }
            else if (op == 0xDA) v = (long double)(s32) mrd(ea, 4);
            else v = (long double)(s16) mrd(ea, 2);
            if (r == 2 || r == 3) { x_cc(c, ST(0), v); if (r == 3) xpop(c); }
            else ST(0) = xop(r, ST(0), v);
            return 0; }
        case 0xD9:
            switch (r) {
            case 0: { float f; memcpy(&f, (void*) ea, 4); xpush(c, f); return 0; }
            case 2: case 3: { float f = (float) ST(0); memcpy((void*) ea, &f, 4); if (r == 3) xpop(c); return 0; }
            case 4: c->fcw = (u16) mrd(ea, 2); c->fsw = (u16)(mrd(ea + 4, 2) & 0x4700); c->top = (int)((mrd(ea + 4, 2) >> 11) & 7); return 0;
            case 5: c->fcw = (u16) mrd(ea, 2); return 0;
            case 6: memset((void*) ea, 0, 28); mwr(ea, c->fcw, 2); mwr(ea + 4, (u64)((c->fsw & ~0x3800) | (c->top << 11)), 2); mwr(ea + 8, 0xffff, 2); return 0;
            case 7: mwr(ea, c->fcw, 2); return 0;
            }
            return -1;
        case 0xDD:
            switch (r) {
            case 0: { double d; memcpy(&d, (void*) ea, 8); xpush(c, d); return 0; }
            case 2: case 3: { double d = (double) ST(0); memcpy((void*) ea, &d, 8); if (r == 3) xpop(c); return 0; }
            case 7: mwr(ea, (u64)((c->fsw & ~0x3800) | (c->top << 11)), 2); return 0;
            case 1: mwr(ea, x_tointt(ST(0), 8), 8); xpop(c); return 0;                          /* fisttp m64int (SSE3) */
            case 4: {                                                                          /* frstor: 28-byte environment, then ST(0..7) as 80-bit */
                u16 sw = (u16) mrd(ea + 4, 2);
                c->fcw = (u16) mrd(ea, 2); c->fsw = sw & 0x4700; c->top = (sw >> 11) & 7;
                for (int i = 0; i < 8; i++) ST(i) = ld_from80((u8*)(ea + 28 + 10 * (u64) i));
                return 0; }
            case 6:                                                                            /* fnsave: environment + ST(0..7), then fninit */
                memset((void*) ea, 0, 28); mwr(ea, c->fcw, 2); mwr(ea + 4, (u64)((c->fsw & ~0x3800) | (c->top << 11)), 2); mwr(ea + 8, 0xffff, 2);
                for (int i = 0; i < 8; i++) ld_to80(ST(i), (u8*)(ea + 28 + 10 * (u64) i));
                c->fcw = 0x37f; c->fsw = 0; c->top = 0;
                return 0;
            }
            return -1;
        case 0xDB:
            switch (r) {
            case 0: xpush(c, (long double)(s32) mrd(ea, 4)); return 0;
            case 2: case 3: mwr(ea, x_toint(c, ST(0), 4), 4); if (r == 3) xpop(c); return 0;
            case 5: xpush(c, ld_from80((u8*) ea)); return 0;
            case 7: ld_to80(ST(0), (u8*) ea); xpop(c); return 0;
            case 1: mwr(ea, x_tointt(ST(0), 4), 4); xpop(c); return 0;                          /* fisttp m32int (SSE3) */
            }
            return -1;
        case 0xDF:
            switch (r) {
            case 0: xpush(c, (long double)(s16) mrd(ea, 2)); return 0;
            case 2: case 3: mwr(ea, x_toint(c, ST(0), 2), 2); if (r == 3) xpop(c); return 0;
            case 5: xpush(c, (long double)(s64) mrd(ea, 8)); return 0;
            case 7: mwr(ea, x_toint(c, ST(0), 8), 8); xpop(c); return 0;
            case 1: mwr(ea, x_tointt(ST(0), 2), 2); xpop(c); return 0;                          /* fisttp m16int (SSE3) */
            case 4: {                                                                          /* fbld: 18 packed BCD digits (low nibble first), sign in bit 7 of byte 9 */
                const u8*b = (const u8*) ea; long double v = 0;
                for (int i = 8; i >= 0; i--) v = v * 100 + (b[i] >> 4) * 10 + (b[i] & 15);
                xpush(c, (b[9] & 0x80) ? -v : v); return 0; }
            case 6: {                                                                          /* fbstp */
                u8*b = (u8*) ea; long double v = xround(c, ST(0)); int neg = signbit(v) != 0;
                if (neg) v = -v;
                if (isnan(v) || v >= 1e18L) { memset(b, 0, 10); b[7] = 0xC0; b[8] = 0xFF; b[9] = 0xFF; }   /* packed-BCD indefinite */
                else { u64 n = (u64) v; for (int i = 0; i < 9; i++) { b[i] = (u8)((n % 10) | ((n / 10 % 10) << 4)); n /= 100; } b[9] = neg ? 0x80 : 0; }
                xpop(c); return 0; }
            }
            return -1;
        }
        (void) x_ld_int;
        return -1;
    }
    switch (op) {
    case 0xD8: if (r == 2 || r == 3) { x_cc(c, ST(0), ST(i)); if (r == 3) xpop(c); } else ST(0) = xop(r, ST(0), ST(i)); return 0;
    case 0xDC: case 0xDE: {
        if (op == 0xDE && r == 3 && i == 1) { x_cc(c, ST(0), ST(1)); xpop(c); xpop(c); return 0; }          /* fcompp */
        if (r == 2 || r == 3) return -1;
        int rr = r >= 4 ? (r ^ 1) : r;
        ST(i) = xop(rr, ST(i), ST(0));
        if (op == 0xDE) xpop(c);
        return 0; }
    case 0xD9:
        switch (r) {
        case 0: xpush(c, ST(i)); return 0;
        case 1: { long double t = ST(0); ST(0) = ST(i); ST(i) = t; return 0; }
        case 2: return i == 0 ? 0 : -1;
        case 4:
            switch (i) { case 0: ST(0) = -ST(0); return 0; case 1: ST(0) = fabsl(ST(0)); return 0; case 4: x_cc(c, ST(0), 0.0L); return 0;
            case 5: { long double v = ST(0); c->fsw &= ~0x4700; if (signbit(v)) c->fsw |= 0x200;
                      switch (fpclassify(v)) { case FP_NAN: c->fsw |= 0x100; break; case FP_INFINITE: c->fsw |= 0x500; break; case FP_ZERO: c->fsw |= 0x4000; break; case FP_SUBNORMAL: c->fsw |= 0x4400; break; default: c->fsw |= 0x400; } return 0; } }
            return -1;
        case 5:
            switch (i) { case 0: xpush(c, 1.0L); return 0; case 1: xpush(c, 3.321928094887362347870319429489390175864831393L); return 0;
            case 2: xpush(c, 1.442695040888963407359924681001892137L); return 0; case 3: xpush(c, 3.141592653589793238462643383279502884L); return 0;
            case 4: xpush(c, 0.301029995663981195213738894724493026768189881L); return 0; case 5: xpush(c, 0.693147180559945309417232121458176568L); return 0;
            case 6: xpush(c, 0.0L); return 0; }
            return -1;
        case 6:
            switch (i) {
            case 0: ST(0) = expm1l(ST(0) * 0.693147180559945309417232121458176568L); return 0;
            case 1: { long double x = ST(0), y = ST(1); xpop(c); ST(0) = y * log2l(x); return 0; }
            case 2: { long double v = ST(0); ST(0) = tanl(v); xpush(c, 1.0L); c->fsw &= ~0x400; return 0; }
            case 3: { long double x = ST(0), y = ST(1); xpop(c); ST(0) = atan2l(y, x); return 0; }
            case 4: { long double v = ST(0); int e; if (v == 0) { ST(0) = -INFINITY; xpush(c, v); return 0; } long double f = frexpl(v, &e); ST(0) = (long double)(e - 1); xpush(c, f * 2); return 0; }
            case 5: { long double a = ST(0), b = ST(1); ST(0) = remainderl(a, b); c->fsw &= ~0x400; return 0; }
            case 6: c->top = (c->top - 1) & 7; return 0;
            case 7: c->top = (c->top + 1) & 7; return 0;
            }
            return -1;
        case 7:
            switch (i) {
            case 0: { long double a = ST(0), b = ST(1); ST(0) = fmodl(a, b); c->fsw &= ~0x400; return 0; }
            case 1: { long double x = ST(0), y = ST(1); xpop(c); ST(0) = y * log2l(x + 1.0L); return 0; }
            case 2: ST(0) = sqrtl(ST(0)); return 0;
            case 3: { long double v = ST(0); ST(0) = sinl(v); xpush(c, cosl(v)); c->fsw &= ~0x400; return 0; }
            case 4: ST(0) = xround(c, ST(0)); return 0;
            case 5: ST(0) = ldexpl(ST(0), (int) truncl(ST(1))); return 0;
            case 6: ST(0) = sinl(ST(0)); c->fsw &= ~0x400; return 0;
            case 7: ST(0) = cosl(ST(0)); c->fsw &= ~0x400; return 0;
            }
            return -1;
        }
        return -1;
    case 0xDA:
        if (r == 5 && i == 1) { x_cc(c, ST(0), ST(1)); xpop(c); xpop(c); return 0; }                 /* fucompp */
        if (r < 4) { int cc = r == 0 ? (RF(c) & CF_) : r == 1 ? (RF(c) & ZF_) : r == 2 ? (RF(c) & (CF_ | ZF_)) : (RF(c) & PF_); if (cc) ST(0) = ST(i); return 0; }
        return -1;
    case 0xDB:
        if (r < 4) { int cc = r == 0 ? (RF(c) & CF_) : r == 1 ? (RF(c) & ZF_) : r == 2 ? (RF(c) & (CF_ | ZF_)) : (RF(c) & PF_); if (!cc) ST(0) = ST(i); return 0; }
        if (r == 4 && i == 2) { c->fsw = 0; return 0; }
        if (r == 4 && i == 3) { c->fcw = 0x37f; c->fsw = 0; c->top = 0; return 0; }
        if (r == 5 || r == 6) { x_flags(c, ST(0), ST(i)); return 0; }
        return -1;
    case 0xDD:
        if (r == 0) return 0;                                                                         /* ffree */
        if (r == 2 || r == 3) { ST(i) = ST(0); if (r == 3) xpop(c); return 0; }
        if (r == 4 || r == 5) { x_cc(c, ST(0), ST(i)); if (r == 5) xpop(c); return 0; }
        return -1;
    case 0xDF:
        if (r == 4 && i == 0) { setr(c, RAX, 2, 0, (u64)((c->fsw & ~0x3800) | (c->top << 11))); return 0; }
        if (r == 5 || r == 6) { x_flags(c, ST(0), ST(i)); xpop(c); return 0; }
        return -1;
    }
    return -1;
}

